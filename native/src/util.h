#pragma once

#include <QCoreApplication>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace util {

QString appRoot();
QString assetsDir();
QString takoyoHome();
QString conversationsDir();
QString projectsDir();
QString imagesDir();
QString trashDir();
QString configPath();
QString pythonExecutable();

QJsonObject readJson(const QString &path, bool *ok = nullptr);
bool writeJsonAtomic(const QString &path, const QJsonObject &object);

QString ago(double epochSeconds);
QString shorten(const QString &text, int limit, bool ellipsis = true);
void openPath(const QString &path);
QString formatGeometry(const QWidget *window);
void applyGeometry(QWidget *window, const QString &geometry);

QString imageRequestPrompt(const QString &text);

// Rough token accounting so long prompts and file contents can be fitted to the
// model's context window instead of being cut at an arbitrary character count.
int estimateTokens(const QString &text);
QString fitToTokens(const QString &text, int tokens);
int promptBudgetTokens(int contextTokens, int requestedTokens, int systemTokens);

struct ToolEdit {
    QString path;
    QString content;
    bool applied = false;
};

struct FileRequest {
    QString path;
    bool listing = false;
};

struct ParsedReply {
    QString cleaned;
    QList<ToolEdit> edits;
    QList<FileRequest> fileRequests;
    QStringList imagePrompts;
};

ParsedReply parseReply(const QString &text);

struct Segment {
    bool code = false;
    QString text;
    QString info;
};

QList<Segment> splitFences(const QString &text);
QPair<QString, QString> splitInfo(const QString &info);

} // namespace util
