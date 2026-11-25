#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class Config;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QProcess;
class QTimer;

class ChatStream : public QObject {
    Q_OBJECT
public:
    using Endpoint = std::function<QUrl()>;
    using Recover = std::function<bool()>;
    ChatStream(QNetworkAccessManager *network, Endpoint endpoint, const QJsonObject &payload,
               Recover recover = {}, const QString &token = QString(), QObject *parent = nullptr);
    void cancel();

signals:
    void chunk(const QString &text);
    void finished(const QString &fullText, bool cancelled);
    void failed(const QString &message);

private:
    void send();
    void retry();
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager *network_ = nullptr;
    Endpoint endpoint_;
    QJsonObject payload_;
    Recover recover_;
    QString token_;
    QNetworkReply *reply_ = nullptr;
    QByteArray buffer_;
    QString full_;
    int retries_ = 0;
    bool cancelled_ = false;
    bool done_ = false;
};

class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(Config *config, QObject *parent = nullptr);
    ~Backend() override;

    void start();
    bool ready() const { return ready_; }
    QString baseUrl() const;
    QNetworkAccessManager *network() const { return network_; }

    ChatStream *chat(const QJsonArray &messages, const QString &provider, const QString &model,
                     double temperature, int maxTokens);
    void generateImage(const QString &prompt, const QString &size, const QString &provider,
                       const QJsonObject &context = QJsonObject());

    void refreshStatus();
    void reloadConfig();
    void installLocalModel(const QString &key);
    // Tells the engine which folder is open, so its own prompt does not tell the
    // model "no project is open" while the app is offering file edits.
    void setWorkspace(const QString &folder);
    void shutdown();

signals:
    void readyChanged(bool ready, const QString &message);
    void imageReady(const QJsonObject &meta, const QJsonObject &context);
    void imageFailed(const QString &message, const QJsonObject &context);
    void status(const QJsonObject &health);

private:
    void probeHealth(int attempt);
    void onHealth(QNetworkReply *reply);
    bool launchSidecar();
    bool recoverEngine();
    void restartSidecar();
    void onSidecarFinished();
    int freePort() const;
    QNetworkRequest authRequest(const QUrl &url) const;
#ifdef Q_OS_WIN
    // Keeps the engine (and the child process it unpacks) tied to this app's
    // lifetime so no locked TakoyoEngine.exe is left behind.
    void adoptEngineProcess();
    void closeEngineJob();
#endif

    Config *config_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QProcess *process_ = nullptr;
    QTimer *probeTimer_ = nullptr;
    QString baseUrl_;
    bool ready_ = false;
    bool started_ = false;
    bool ownsBackend_ = false;
    bool shuttingDown_ = false;
    int attempts_ = 0;
    int restartAttempts_ = 0;
    QString apiToken_;
    QString workspace_;
#ifdef Q_OS_WIN
    void *job_ = nullptr;
#endif
};
