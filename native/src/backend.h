#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class Config;
class QNetworkAccessManager;
class QNetworkRequest;
class QNetworkReply;
class QProcess;
class QTimer;

class ChatStream : public QObject {
    Q_OBJECT
public:
    using Endpoint = std::function<QUrl()>;
    using Recover = std::function<bool()>;
    ChatStream(QNetworkAccessManager *network, Endpoint endpoint, const QJsonObject &payload,
               const QString &token, Recover recover = {}, QObject *parent = nullptr);
    void cancel();

signals:
    void chunk(const QString &text);
    void finished(const QString &fullText, bool cancelled);
    void failed(const QString &message, const QString &partial);

private:
    void send();
    void retry();
    void onReadyRead();
    void onFinished();
    void processEvent(const QByteArray &event);

    QNetworkAccessManager *network_ = nullptr;
    QString token_;
    Endpoint endpoint_;
    QJsonObject payload_;
    Recover recover_;
    QNetworkReply *reply_ = nullptr;
    QByteArray buffer_;
    QString full_;
    QString streamError_;
    int retries_ = 0;
    bool cancelled_ = false;
    bool done_ = false;
    bool receivedDone_ = false;
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
    void shutdown();

signals:
    void readyChanged(bool ready, const QString &message);
    void imageReady(const QJsonObject &meta, const QJsonObject &context);
    void imageFailed(const QString &message, const QJsonObject &context);
    void status(const QJsonObject &health);

private:
    void authorize(QNetworkRequest &request) const;
    void probeHealth(int attempt);
    void onHealth(QNetworkReply *reply);
    bool launchSidecar();
    bool recoverEngine();
    void restartSidecar();
    void onSidecarFinished();
    int freePort() const;
#ifdef Q_OS_WIN
    // Keeps the engine (and the child process it unpacks) tied to this app's
    // lifetime so no locked TakoyoEngine.exe is left behind.
    void adoptEngineProcess();
    void closeEngineJob();
#endif

    Config *config_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QString apiToken_;
    QProcess *process_ = nullptr;
    QTimer *probeTimer_ = nullptr;
    QString baseUrl_;
    bool ready_ = false;
    bool started_ = false;
    bool ownsBackend_ = false;
    bool shuttingDown_ = false;
    int attempts_ = 0;
    int restartAttempts_ = 0;
#ifdef Q_OS_WIN
    void *job_ = nullptr;
#endif
};
