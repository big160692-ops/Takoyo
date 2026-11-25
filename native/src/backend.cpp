#include "backend.h"

#include "config.h"
#include "util.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
QString errorMessage(const QJsonObject &object)
{
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? QStringLiteral("The local engine did not answer") : message;
}

// A URL-safe token shared by the UI and the engine. The 2.2.0 engine requires
// "Authorization: Bearer <token>" on every request; we mint one, persist it in
// config.json (which the engine reads) and hand it to the engine via the
// TAKOYO_API_TOKEN environment variable so both sides always agree.
QString randomToken()
{
    static const char kChars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    QString token;
    token.reserve(40);
    for (int i = 0; i < 40; ++i)
        token.append(QLatin1Char(kChars[QRandomGenerator::global()->bounded(int(sizeof(kChars) - 1))]));
    return token;
}
} // namespace

// ---------------------------------------------------------------------------
// ChatStream
// ---------------------------------------------------------------------------
ChatStream::ChatStream(QNetworkAccessManager *network, Endpoint endpoint, const QJsonObject &payload,
                       Recover recover, const QString &token, QObject *parent)
    : QObject(parent)
    , network_(network)
    , endpoint_(std::move(endpoint))
    , payload_(payload)
    , recover_(std::move(recover))
    , token_(token)
{
    send();
}

void ChatStream::send()
{
    QNetworkRequest request(endpoint_());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token_.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + token_.toUtf8());
    reply_ = network_->post(request, QJsonDocument(payload_).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::readyRead, this, &ChatStream::onReadyRead);
    connect(reply_, &QNetworkReply::finished, this, &ChatStream::onFinished);
}

void ChatStream::retry()
{
    if (cancelled_) {
        emit finished(full_, true);
        return;
    }
    if (done_)
        return;
    send();
}

void ChatStream::cancel()
{
    cancelled_ = true;
    if (reply_ && reply_->isRunning())
        reply_->abort();
}

void ChatStream::onReadyRead()
{
    buffer_ += reply_->readAll();
    int split = buffer_.indexOf("\n\n");
    while (split >= 0) {
        const QByteArray event = buffer_.left(split);
        buffer_.remove(0, split + 2);
        for (const QByteArray &line : event.split('\n')) {
            if (!line.startsWith("data:"))
                continue;
            QByteArray data = line.mid(5).trimmed();
            if (data == "[DONE]") {
                if (!done_) {
                    done_ = true;
                    emit finished(full_, cancelled_);
                }
                continue;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
                continue;
            const QJsonObject object = doc.object();
            if (object.contains(QStringLiteral("error"))) {
                const QString message = errorMessage(object);
                done_ = true;
                QNetworkReply *reply = reply_;
                reply_ = nullptr;
                if (reply) {
                    QObject::disconnect(reply, nullptr, this, nullptr);
                    reply->abort();
                    reply->deleteLater();
                }
                emit failed(message);
                return;
            }
            const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty())
                continue;
            const QString piece = choices.first().toObject()
                                      .value(QStringLiteral("delta")).toObject()
                                      .value(QStringLiteral("content")).toString();
            if (!piece.isEmpty()) {
                full_ += piece;
                emit chunk(piece);
            }
        }
        split = buffer_.indexOf("\n\n");
    }
}

void ChatStream::onFinished()
{
    if (done_)
        return;
    done_ = true;
    QNetworkReply *reply = reply_;
    reply_ = nullptr;
    buffer_ += reply->readAll();
    if (!buffer_.isEmpty()) {
        for (const QByteArray &line : buffer_.split('\n')) {
            if (!line.startsWith("data:"))
                continue;
            const QByteArray data = line.mid(5).trimmed();
            if (data == "[DONE]")
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
                continue;
            const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty())
                continue;
            const QString piece = choices.first().toObject()
                                      .value(QStringLiteral("delta")).toObject()
                                      .value(QStringLiteral("content")).toString();
            if (!piece.isEmpty()) {
                full_ += piece;
                emit chunk(piece);
            }
        }
    }
    if (reply->error() != QNetworkReply::NoError && !cancelled_) {
        const QNetworkReply::NetworkError error = reply->error();
        const bool connectionProblem = error == QNetworkReply::ConnectionRefusedError
            || error == QNetworkReply::RemoteHostClosedError
            || error == QNetworkReply::HostNotFoundError;
        // The engine can be stopped while the app is open. Wake it back up and
        // resend instead of leaving the chat with a dead-end error. Model load
        // takes ~15s, so keep waiting well past that before giving up.
        if (connectionProblem && full_.isEmpty() && recover_ && retries_ < 40) {
            ++retries_;
            const bool restarting = recover_();
            reply->deleteLater();
            buffer_.clear();
            done_ = false;
            QTimer::singleShot(restarting ? 900 : 1200, this, &ChatStream::retry);
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(buffer_);
        QString message;
        if (doc.isObject())
            message = errorMessage(doc.object());
        if (message.isEmpty()) {
            if (connectionProblem && recover_)
                message = QStringLiteral(
                    "The built-in engine is not responding. Open Settings → Built-in AI, or restart Takoyo.");
            else if (status >= 400)
                message = QStringLiteral("The local engine answered %1").arg(status);
            else
                message = reply->errorString();
        }
        reply->deleteLater();
        // Keep whatever text already streamed rather than replacing a partial
        // answer with an error card.
        if (!full_.isEmpty()) {
            emit finished(full_, cancelled_);
            return;
        }
        emit failed(message);
        return;
    }
    reply->deleteLater();
    emit finished(full_, cancelled_);
}

// ---------------------------------------------------------------------------
// Backend
// ---------------------------------------------------------------------------
Backend::Backend(Config *config, QObject *parent)
    : QObject(parent)
    , config_(config)
    , network_(new QNetworkAccessManager(this))
    , probeTimer_(new QTimer(this))
{
    apiToken_ = config_->str(QStringLiteral("api_token"));
    if (apiToken_.isEmpty()) {
        apiToken_ = randomToken();
        config_->set(QStringLiteral("api_token"), apiToken_);
    }
    probeTimer_->setSingleShot(true);
    connect(probeTimer_, &QTimer::timeout, this, [this]() { probeHealth(++attempts_); });
}

QNetworkRequest Backend::authRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + apiToken_.toUtf8());
    return request;
}

Backend::~Backend()
{
    if (process_)
        process_->disconnect(this);
#ifdef Q_OS_WIN
    closeEngineJob();
#endif
}

QString Backend::baseUrl() const
{
    if (!baseUrl_.isEmpty())
        return baseUrl_;
    const QString host = config_->str(QStringLiteral("api_host"));
    int port = config_->integer(QStringLiteral("api_port"));
    if (port <= 0)
        port = 8765;
    return QStringLiteral("http://%1:%2").arg(host.isEmpty() ? QStringLiteral("127.0.0.1") : host).arg(port);
}

int Backend::freePort() const
{
    QTcpServer server;
    if (server.listen(QHostAddress::LocalHost, 0))
        return server.serverPort();
    return 8765;
}

void Backend::start()
{
    if (started_)
        return;
    started_ = true;
    baseUrl_ = baseUrl();
    attempts_ = 0;
    probeHealth(0);
}

void Backend::probeHealth(int attempt)
{
    QNetworkRequest request = authRequest(QUrl(baseUrl_ + QStringLiteral("/health")));
    request.setTransferTimeout(1200);
    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, attempt]() {
        reply->deleteLater();
        if (ready_)
            return;
        if (reply->error() == QNetworkReply::NoError) {
            ready_ = true;
            restartAttempts_ = 0;
            emit readyChanged(true, QString());
            refreshStatus();
            return;
        }
        if (attempt == 0) {
            if (!launchSidecar()) {
                emit readyChanged(false, QStringLiteral("Takoyo's local engine could not start"));
                return;
            }
            if (baseUrl_.isEmpty())
                baseUrl_ = baseUrl();
        }
        if (attempt >= 60) {
            emit readyChanged(false, QStringLiteral("The local engine took too long to start"));
            return;
        }
        probeTimer_->start(250);
    });
}

bool Backend::launchSidecar()
{
    const QString root = util::appRoot();
    QString command;
    QStringList arguments;
    const QString custom = qEnvironmentVariable("TAKOYO_BACKEND_CMD");
    const bool isCustom = !custom.isEmpty();
    if (isCustom) {
        const QStringList parts = QProcess::splitCommand(custom);
        if (parts.isEmpty())
            return false;
        command = parts.first();
        arguments = parts.mid(1);
    } else if (!util::pythonExecutable().isEmpty() && QFileInfo::exists(root + QStringLiteral("/main.py"))) {
        command = util::pythonExecutable();
        arguments << QStringLiteral("main.py");
    } else if (QFileInfo::exists(root + QStringLiteral("/TakoyoEngine.exe"))) {
        command = root + QStringLiteral("/TakoyoEngine.exe");
    } else if (QFileInfo::exists(root + QStringLiteral("/Takoyo.exe"))) {
        command = root + QStringLiteral("/Takoyo.exe");
    } else {
        return false;
    }

    if (!isCustom) {
        const int port = freePort();
        baseUrl_ = QStringLiteral("http://127.0.0.1:%1").arg(port);
        arguments << QStringLiteral("--api-only") << QStringLiteral("--host") << QStringLiteral("127.0.0.1")
                  << QStringLiteral("--port") << QString::number(port);
    }

    process_ = new QProcess(this);
    process_->setWorkingDirectory(root);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    // Drain the sidecar's output: an unread pipe can block it, and its stderr
    // is the only trace when the Python engine dies.
    connect(process_, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!process_)
            return;
        const QByteArray chunk = process_->readAllStandardOutput();
        if (chunk.isEmpty())
            return;
        const QString logs = QDir(util::takoyoHome()).filePath(QStringLiteral("logs"));
        QDir().mkpath(logs);
        QFile file(QDir(logs).filePath(QStringLiteral("engine.log")));
        if (file.open(QIODevice::Append))
            file.write(chunk);
    });
    ownsBackend_ = true;
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning("Takoyo sidecar error %d: %s", int(error), qPrintable(process_->errorString()));
    });
    connect(process_, &QProcess::finished, this, &Backend::onSidecarFinished);
#ifdef Q_OS_WIN
    connect(process_, &QProcess::started, this, &Backend::adoptEngineProcess);
#endif
    QProcessEnvironment engineEnv = QProcessEnvironment::systemEnvironment();
    engineEnv.insert(QStringLiteral("TAKOYO_API_TOKEN"), apiToken_);
    process_->setProcessEnvironment(engineEnv);
    process_->start(command, arguments);
    return true;
}

bool Backend::recoverEngine()
{
    if (!ownsBackend_ || shuttingDown_)
        return false;
    if (process_ && process_->state() != QProcess::NotRunning) {
        // A sidecar that was healthy but stopped answering is wedged; restart
        // it instead of waiting for a process that will never reply.
        if (!ready_)
            return true;
        ready_ = false;
        restartSidecar();
        return true;
    }
    restartSidecar();
    return true;
}

void Backend::restartSidecar()
{
    if (shuttingDown_)
        return;
    if (restartAttempts_ >= 3) {
        emit readyChanged(false, QStringLiteral("Takoyo's local engine keeps stopping. Open Settings → Built-in AI."));
        return;
    }
    ++restartAttempts_;
    ready_ = false;
#ifdef Q_OS_WIN
    closeEngineJob();
#endif
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->deleteLater();
        process_ = nullptr;
    }
    ownsBackend_ = false;
    if (!launchSidecar()) {
        emit readyChanged(false, QStringLiteral("Takoyo's local engine could not start"));
        return;
    }
    attempts_ = 0;
    // Probe from attempt 1 so this does not launch a second engine.
    probeHealth(1);
}

void Backend::onSidecarFinished()
{
    if (shuttingDown_)
        return;
    ready_ = false;
#ifdef Q_OS_WIN
    closeEngineJob();
#endif
    if (process_) {
        process_->deleteLater();
        process_ = nullptr;
    }
    emit readyChanged(false, QString());
    QTimer::singleShot(500, this, [this]() {
        if (shuttingDown_ || (process_ && process_->state() != QProcess::NotRunning))
            return;
        restartSidecar();
    });
}

#ifdef Q_OS_WIN
void Backend::adoptEngineProcess()
{
    if (job_ || !process_ || process_->state() != QProcess::Running)
        return;
    const qint64 pid = process_->processId();
    if (pid <= 0)
        return;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    HANDLE engine = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
        || !engine || !AssignProcessToJobObject(job, engine)) {
        if (engine)
            CloseHandle(engine);
        CloseHandle(job);
        return;
    }
    CloseHandle(engine);
    job_ = job;
}

void Backend::closeEngineJob()
{
    if (!job_)
        return;
    CloseHandle(static_cast<HANDLE>(job_));
    job_ = nullptr;
}
#endif

void Backend::refreshStatus()
{
    if (!ready_)
        return;
    QNetworkRequest request = authRequest(QUrl(baseUrl_ + QStringLiteral("/health")));
    request.setTransferTimeout(1500);
    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject())
            emit status(doc.object());
    });
}

void Backend::reloadConfig()
{
    if (!ready_)
        return;
    QNetworkRequest request = authRequest(QUrl(baseUrl_ + QStringLiteral("/config/reload")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(3000);
    QNetworkReply *reply = network_->post(request, QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void Backend::setWorkspace(const QString &folder)
{
    workspace_ = folder;
}

void Backend::installLocalModel(const QString &key)
{
    if (!ready_ || key.isEmpty())
        return;
    QNetworkRequest request = authRequest(QUrl(baseUrl_ + QStringLiteral("/local/setup")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(5000);
    QJsonObject payload;
    payload.insert(QStringLiteral("model"), key);
    QNetworkReply *reply = network_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

ChatStream *Backend::chat(const QJsonArray &messages, const QString &provider, const QString &model,
                          double temperature, int maxTokens)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("provider"), provider);
    payload.insert(QStringLiteral("model"), model);
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("stream"), true);
    payload.insert(QStringLiteral("temperature"), temperature);
    payload.insert(QStringLiteral("max_tokens"), maxTokens);
    if (!workspace_.isEmpty())
        payload.insert(QStringLiteral("workspace"), workspace_);
    return new ChatStream(network_,
                          [this]() { return QUrl(baseUrl_ + QStringLiteral("/v1/chat/completions")); },
                          payload, [this]() { return recoverEngine(); }, apiToken_, this);
}

void Backend::generateImage(const QString &prompt, const QString &size, const QString &provider,
                            const QJsonObject &context)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("prompt"), prompt);
    if (!size.isEmpty())
        payload.insert(QStringLiteral("size"), size);
    if (!provider.isEmpty())
        payload.insert(QStringLiteral("provider"), provider);
    QNetworkRequest request = authRequest(QUrl(baseUrl_ + QStringLiteral("/images/generations")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(180000);
    QNetworkReply *reply = network_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, context]() {
        reply->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit imageFailed(reply->errorString(), context);
            return;
        }
        const QJsonObject object = doc.object();
        if (reply->error() != QNetworkReply::NoError || !object.contains(QStringLiteral("data"))) {
            emit imageFailed(errorMessage(object), context);
            return;
        }
        const QJsonArray data = object.value(QStringLiteral("data")).toArray();
        if (data.isEmpty()) {
            emit imageFailed(QStringLiteral("No image came back"), context);
            return;
        }
        emit imageReady(data.first().toObject(), context);
    });
}

void Backend::shutdown()
{
    shuttingDown_ = true;
    if (!ownsBackend_ || !process_ || process_->state() == QProcess::NotRunning)
        return;
    if (ready_ && ownsBackend_) {
        QNetworkRequest request = authRequest(QUrl(baseUrl_ + QStringLiteral("/local/stop")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *reply = network_->post(request, QByteArray("{}"));
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(1200);
        loop.exec();
        reply->deleteLater();
    }
    process_->terminate();
    if (!process_->waitForFinished(700))
        process_->kill();
#ifdef Q_OS_WIN
    closeEngineJob();
#endif
}
