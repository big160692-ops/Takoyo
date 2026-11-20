#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

struct ConversationSummary {
    QString id;
    QString title;
    QString projectId;
    QString preview;
    double updated = 0;
    int messageCount = 0;
    bool hasImage = false;
};

struct ProjectSummary {
    QString id;
    QString name;
    QString description;
    double updated = 0;
    double created = 0;
    int fileCount = 0;
};

class ConversationStore {
public:
    explicit ConversationStore(const QString &directory);

    QJsonObject create(const QString &title, const QString &projectId, const QString &firstMessage);
    QJsonObject get(const QString &id) const;
    QJsonObject append(const QString &id, const QString &role, const QString &content,
                       const QJsonObject &extra = QJsonObject());
    QJsonObject updateMessage(const QString &id, int index, const QJsonObject &fields);
    void rename(const QString &id, const QString &title);
    void remove(const QString &id);
    bool exportAll(const QString &path, QString *error = nullptr) const;
    int restoreFrom(const QString &path, QString *error = nullptr);
    int recoveryCount() const { return recoveryCount_; }
    QJsonObject latestForProject(const QString &projectId);
    QList<ConversationSummary> summaries(const QString &projectId = QString(), const QString &query = QString()) const;

private:
    QString directory_;
    mutable int recoveryCount_ = 0;
    QString pathFor(const QString &id) const;
    void quarantine(const QString &path) const;
    void write(QJsonObject &conversation) const;
};

class ProjectStore {
public:
    explicit ProjectStore(const QString &directory);

    QList<ProjectSummary> list(const QString &query = QString()) const;
    bool get(const QString &id, ProjectSummary *out) const;
    QString contextForAi(const QString &id, const QString &prompt, int maxChars = 22000) const;
    ProjectSummary create(const QString &name, const QString &description) const;
    bool rename(const QString &id, const QString &name) const;
    void remove(const QString &id) const;

    QStringList filePaths(const QString &id) const;
    bool fileExists(const QString &id, const QString &relative) const;
    QString readText(const QString &id, const QString &relative, QString *error = nullptr) const;
    bool writeText(const QString &id, const QString &relative, const QString &content,
                   QString *error = nullptr) const;
    bool deleteFile(const QString &id, const QString &relative, QString *error = nullptr) const;

private:
    QString directory_;
    QString metaPath(const QString &id) const;
    QJsonObject readMeta(const QString &id) const;
    void writeMeta(const QString &id, QJsonObject meta) const;
};
