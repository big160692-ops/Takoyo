#include "backend.h"

#include "config.h"
#include "util.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
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
} // namespace

// ---------------------------------------------------------------------------
// ChatStream
// ---------------------------------------------------------------------------
ChatStream::ChatStream(QNetworkAccessManager *network, Endpoint endpoint, const QJsonObject &payload,
                       const QString &token, Recover recover, QObject *parent)
    : QObject(parent)
    , network_(network)
    , token_(token)
    , endpoint_(std::move(endpoint))
    , payload_(payload)
    , recover_(std::move(recover))
{
    send();
}

void ChatStream::send()
{
    receivedDone_ = false;
    QNetworkRequest request(endpoint_());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token_.toUtf8());
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
        processEvent(event);
        split = buffer_.indexOf("\n\n");
    }
}

void ChatStream::processEvent(const QByteArray &event)
{
    for (QByteArray line : event.split('\n')) {
        line = line.trimmed();
        if (!line.startsWith("data:"))
            continue;
        const QByteArray data = line.mid(5).trimmed();
        if (data == "[DONE]") {
            receivedDone_ = true;
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
            continue;
        const QJsonObject object = doc.object();
        if (object.contains(QStringLiteral("error"))) {
            const QJsonValue error = object.value(QStringLiteral("error"));
            if (error.isObject())
                streamError_ = error.toObject().value(QStringLiteral("message")).toString();
            else if (error.isString())
                streamError_ = error.toString();
            if (streamError_.isEmpty())
                streamError_ = QStringLiteral("The response stream returned an error.");
            continue;
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
}

void ChatStream::onFinished()
{
    if (done_)
        return;
    done_ = true;
    QNetworkReply *reply = reply_;
    reply_ = nullptr;
    buffer_ += reply->readAll();
    if (!buffer_.isEmpty())
        processEvent(buffer_);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!cancelled_ && !streamError_.isEmpty()) {
        reply->deleteLater();
        emit failed(streamError_, full_);
        return;
    }
    if (!cancelled_ && reply->error() == QNetworkReply::NoError && status < 400 && !receivedDone_) {
        reply->deleteLater();
        emit failed(QStringLiteral("The response stream ended before completion."), full_);
        return;
    }
    if ((reply->error() != QNetworkReply::NoError || status >= 400) && !cancelled_) {
        const QNetworkReply::NetworkError error = reply->error();
        const bool connectionProblem = error == QNetworkReply::ConnectionRefusedError
            || error == QNetworkReply::RemoteHostClosedError
            || error == QNetworkReply::HostNotFoundError;
        // The engine can be stopped while the app is open. Wake it back up and
        // resend instead of leaving the chat with a dead-end error.
        if (connectionProblem && full_.isEmpty() && streamError_.isEmpty() && recover_ && retries_ < 12) {
            ++retries_;
            const bool restarting = recover_();
            reply->deleteLater();
            buffer_.clear();
            done_ = false;
            QTimer::singleShot(restarting ? 900 : 1200, this, &ChatStream::retry);
            return;
        }
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
        emit failed(message, full_);
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
    probeTimer_->setSingleShot(true);
    connect(probeTimer_, &QTimer::timeout, this, [this]() { probeHealth(++attempts_); });
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

void Backend::authorize(QNetworkRequest &request) const
{
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + apiToken_.toUtf8());
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
    QNetworkRequest request(QUrl(baseUrl_ + QStringLiteral("/health")));
    authorize(request);
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
    ownsBackend_ = true;
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning("Takoyo sidecar error %d: %s", int(error), qPrintable(process_->errorString()));
    });
    connect(process_, &QProcess::finished, this, &Backend::onSidecarFinished);
#ifdef Q_OS_WIN
    connect(process_, &QProcess::started, this, &Backend::adoptEngineProcess);
#endif
    process_->start(command, arguments);
    return true;
}

bool Backend::recoverEngine()
{
    if (!ownsBackend_ || shuttingDown_)
        return false;
    if (process_ && process_->state() != QProcess::NotRunning)
        return false;
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
    QNetworkRequest request(QUrl(baseUrl_ + QStringLiteral("/health")));
    authorize(request);
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
    if (!ready_) {
        apiToken_ = config_->str(QStringLiteral("api_token"));
        return;
    }
    QNetworkRequest request(QUrl(baseUrl_ + QStringLiteral("/config/reload")));
    authorize(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(3000);
    QNetworkReply *reply = network_->post(request, QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status < 400;
        reply->deleteLater();
        if (ok)
            apiToken_ = config_->str(QStringLiteral("api_token"));
    });
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
    return new ChatStream(network_,
                          [this]() { return QUrl(baseUrl_ + QStringLiteral("/v1/chat/completions")); },
                          payload, apiToken_, [this]() { return recoverEngine(); }, this);
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
    QNetworkRequest request(QUrl(baseUrl_ + QStringLiteral("/images/generations")));
    authorize(request);
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
        QNetworkRequest request(QUrl(baseUrl_ + QStringLiteral("/local/stop")));
        authorize(request);
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
