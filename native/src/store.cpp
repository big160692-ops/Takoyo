#include "store.h"

#include "theme.h"
#include "util.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

QString cleanTitle(const QString &text, int limit = 48)
{
    QString oneLine = text.simplified();
    if (oneLine.startsWith(QLatin1String("```")))
        return QStringLiteral("New chat");
    if (oneLine.size() > limit)
        oneLine = oneLine.left(limit - 1).trimmed() + QStringLiteral("\u2026");
    return oneLine.isEmpty() ? QStringLiteral("New chat") : oneLine;
}

QString newConversationId()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const quint32 suffix = QRandomGenerator::global()->generate() & 0xFFFFFF;
    return stamp + QStringLiteral("-") + QString::number(suffix, 16).rightJustified(6, QChar('0'));
}

QString hexSuffix(int length)
{
    const quint32 value = QRandomGenerator::global()->generate();
    return QString::number(value, 16).rightJustified(length, QChar('0')).right(length);
}

const QSet<QString> &skipDirs()
{
    static const QSet<QString> dirs = {
        QStringLiteral(".git"), QStringLiteral(".hg"), QStringLiteral(".svn"), QStringLiteral("node_modules"),
        QStringLiteral("__pycache__"), QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral("env"),
        QStringLiteral("dist"), QStringLiteral("build"), QStringLiteral(".next"), QStringLiteral(".cache"),
        QStringLiteral(".idea"), QStringLiteral(".vscode"), QStringLiteral("target"), QStringLiteral("bin"),
        QStringLiteral("obj"),
    };
    return dirs;
}

const QSet<QString> &textExtensions()
{
    static const QSet<QString> exts = {
        QStringLiteral(".py"), QStringLiteral(".pyw"), QStringLiteral(".js"), QStringLiteral(".jsx"),
        QStringLiteral(".ts"), QStringLiteral(".tsx"), QStringLiteral(".json"), QStringLiteral(".html"),
        QStringLiteral(".htm"), QStringLiteral(".css"), QStringLiteral(".scss"), QStringLiteral(".sass"),
        QStringLiteral(".md"), QStringLiteral(".txt"), QStringLiteral(".java"), QStringLiteral(".c"),
        QStringLiteral(".h"), QStringLiteral(".cpp"), QStringLiteral(".hpp"), QStringLiteral(".cs"),
        QStringLiteral(".go"), QStringLiteral(".rs"), QStringLiteral(".rb"), QStringLiteral(".php"),
        QStringLiteral(".sh"), QStringLiteral(".bat"), QStringLiteral(".ps1"), QStringLiteral(".sql"),
        QStringLiteral(".yaml"), QStringLiteral(".yml"), QStringLiteral(".toml"), QStringLiteral(".ini"),
        QStringLiteral(".cfg"), QStringLiteral(".xml"), QStringLiteral(".svg"), QStringLiteral(".env"),
        QStringLiteral(".lua"), QStringLiteral(".kt"), QStringLiteral(".swift"), QStringLiteral(".dart"),
        QStringLiteral(".r"), QStringLiteral(".m"), QStringLiteral(".vue"), QStringLiteral(".svelte"),
    };
    return exts;
}

bool isSecret(const QString &relative)
{
    static const QRegularExpression re(
        QStringLiteral("(^\\.env|^\\.netrc$|^\\.pgpass$|^id_(rsa|dsa|ecdsa|ed25519)|credential|secret|"
                       "password|passwd|(^|[._-])api[_-]?key|(^|[._-])access[_-]?key|private[_-]?key)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QSet<QString> suffixes = {
        QStringLiteral(".pem"), QStringLiteral(".key"), QStringLiteral(".p12"), QStringLiteral(".pfx"),
        QStringLiteral(".jks"), QStringLiteral(".keystore"), QStringLiteral(".ppk"),
    };
    const QFileInfo info(relative);
    if (suffixes.contains(QStringLiteral(".") + info.suffix().toLower()))
        return true;
    return re.match(info.fileName()).hasMatch();
}

bool looksText(const QByteArray &data)
{
    if (data.isEmpty())
        return true;
    if (data.left(4096).contains('\0'))
        return false;
    return true;
}

struct ProjectFile {
    QString path;
    QString name;
    qint64 size = 0;
    double modified = 0;
    bool text = false;
};

QList<ProjectFile> projectFiles(const QString &rootPath, int maxFiles = 800)
{
    QList<ProjectFile> files;
    QDir root(rootPath);
    if (!root.exists())
        return files;
    QDirIterator it(rootPath, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext() && files.size() < maxFiles) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = root.relativeFilePath(info.absoluteFilePath());
        const QStringList parts = relative.split(QLatin1Char('/'));
        bool skip = false;
        for (int i = 0; i < parts.size() - 1; ++i) {
            if (skipDirs().contains(parts[i]) || parts[i].startsWith(QLatin1Char('.'))) {
                skip = true;
                break;
            }
        }
        if (skip || info.fileName() == QLatin1String(".takoyo.json") || isSecret(relative))
            continue;
        ProjectFile file;
        file.path = relative;
        file.name = info.fileName();
        file.size = info.size();
        file.modified = static_cast<double>(info.lastModified().toSecsSinceEpoch());
        file.text = textExtensions().contains(QStringLiteral(".") + info.suffix().toLower()) || info.size() == 0;
        files << file;
    }
    std::sort(files.begin(), files.end(), [](const ProjectFile &a, const ProjectFile &b) {
        return a.modified > b.modified;
    });
    return files;
}

} // namespace

// ---------------------------------------------------------------------------
// ConversationStore
// ---------------------------------------------------------------------------
ConversationStore::ConversationStore(const QString &directory)
    : directory_(directory)
{
    QDir().mkpath(directory_);
}

QString ConversationStore::pathFor(const QString &id) const
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._-]{3,80}$"));
    if (!valid.match(id).hasMatch())
        return QString();
    return QDir(directory_).filePath(id + QStringLiteral(".json"));
}

QJsonObject ConversationStore::create(const QString &title, const QString &projectId, const QString &firstMessage)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QJsonObject conversation;
    conversation.insert(QStringLiteral("id"), newConversationId());
    conversation.insert(QStringLiteral("title"), cleanTitle(firstMessage.isEmpty() ? title : firstMessage));
    conversation.insert(QStringLiteral("project_id"), projectId);
    conversation.insert(QStringLiteral("created"), static_cast<double>(now));
    conversation.insert(QStringLiteral("updated"), static_cast<double>(now));
    conversation.insert(QStringLiteral("messages"), QJsonArray());
    write(conversation);
    return conversation;
}

QJsonObject ConversationStore::get(const QString &id) const
{
    const QString path = pathFor(id);
    if (path.isEmpty())
        return {};
    return util::readJson(path);
}

void ConversationStore::write(QJsonObject &conversation) const
{
    conversation.insert(QStringLiteral("updated"), static_cast<double>(QDateTime::currentSecsSinceEpoch()));
    const QString path = pathFor(conversation.value(QStringLiteral("id")).toString());
    if (!path.isEmpty())
        util::writeJsonAtomic(path, conversation);
}

QJsonObject ConversationStore::append(const QString &id, const QString &role, const QString &content,
                                      const QJsonObject &extra)
{
    QJsonObject conversation = get(id);
    if (conversation.isEmpty())
        return {};
    QJsonArray messages = conversation.value(QStringLiteral("messages")).toArray();
    QJsonObject message;
    message.insert(QStringLiteral("role"), role);
    message.insert(QStringLiteral("content"), content);
    message.insert(QStringLiteral("ts"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0);
    for (auto it = extra.begin(); it != extra.end(); ++it)
        message.insert(it.key(), it.value());
    messages.append(message);
    conversation.insert(QStringLiteral("messages"), messages);
    if (role == QLatin1String("user")
        && (conversation.value(QStringLiteral("title")).toString().isEmpty()
            || conversation.value(QStringLiteral("title")).toString() == QLatin1String("New chat")))
        conversation.insert(QStringLiteral("title"), cleanTitle(content));
    write(conversation);
    return conversation;
}

QJsonObject ConversationStore::updateMessage(const QString &id, int index, const QJsonObject &fields)
{
    QJsonObject conversation = get(id);
    if (conversation.isEmpty())
        return {};
    QJsonArray messages = conversation.value(QStringLiteral("messages")).toArray();
    if (index < 0 || index >= messages.size())
        return conversation;
    QJsonObject message = messages.at(index).toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it)
        message.insert(it.key(), it.value());
    messages.replace(index, message);
    conversation.insert(QStringLiteral("messages"), messages);
    write(conversation);
    return conversation;
}

QJsonObject ConversationStore::replaceMessages(const QString &id, const QJsonArray &messages)
{
    QJsonObject conversation = get(id);
    if (conversation.isEmpty())
        return {};
    conversation.insert(QStringLiteral("messages"), messages);
    write(conversation);
    return conversation;
}

void ConversationStore::rename(const QString &id, const QString &title)
{
    QJsonObject conversation = get(id);
    if (conversation.isEmpty())
        return;
    conversation.insert(QStringLiteral("title"), cleanTitle(title, 80));
    write(conversation);
}

void ConversationStore::remove(const QString &id)
{
    const QString path = pathFor(id);
    if (path.isEmpty() || !QFile::exists(path))
        return;
    const QString binDir = QDir(util::trashDir()).filePath(QStringLiteral("conversations"));
    QDir().mkpath(binDir);
    const QString target = QDir(binDir).filePath(
        id + QStringLiteral("-") + QString::number(QDateTime::currentSecsSinceEpoch()) + QStringLiteral(".json"));
    QFile::rename(path, target);
}

QJsonObject ConversationStore::latestForProject(const QString &projectId)
{
    const QList<ConversationSummary> items = summaries(projectId);
    return items.isEmpty() ? QJsonObject() : get(items.first().id);
}

QList<ConversationSummary> ConversationStore::summaries(const QString &projectId, const QString &query) const
{
    QList<ConversationSummary> items;
    const QString needle = query.trimmed().toLower();
    const QDir dir(directory_);
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &file : files) {
        const QJsonObject data = util::readJson(dir.filePath(file));
        if (data.isEmpty())
            continue;
        const QString pid = data.value(QStringLiteral("project_id")).toString();
        if (!projectId.isEmpty() && projectId != QLatin1String("__all__") && pid != projectId)
            continue;
        const QString title = data.value(QStringLiteral("title")).toString(QStringLiteral("New chat"));
        const QJsonArray messages = data.value(QStringLiteral("messages")).toArray();
        QString preview;
        bool hasImage = false;
        bool contentMatch = false;
        for (int i = messages.size() - 1; i >= 0; --i) {
            const QJsonObject message = messages.at(i).toObject();
            if (!needle.isEmpty()) {
                const QJsonObject attachment = message.value(QStringLiteral("attachment")).toObject();
                if (message.value(QStringLiteral("content")).toString().toLower().contains(needle)
                    || attachment.value(QStringLiteral("name")).toString().toLower().contains(needle))
                    contentMatch = true;
            }
            if (message.value(QStringLiteral("hidden")).toBool())
                continue;
            if (message.value(QStringLiteral("images")).toArray().size() > 0
                || !message.value(QStringLiteral("image")).toString().isEmpty())
                hasImage = true;
            if (preview.isEmpty()) {
                const QString role = message.value(QStringLiteral("role")).toString();
                const QString content = message.value(QStringLiteral("content")).toString();
                if ((role == QLatin1String("user") || role == QLatin1String("assistant")) && !content.isEmpty())
                    preview = content.simplified().left(90);
            }
        }
        if (!needle.isEmpty() && !title.toLower().contains(needle) && !preview.toLower().contains(needle)
            && !contentMatch)
            continue;
        ConversationSummary summary;
        summary.id = data.value(QStringLiteral("id")).toString(QFileInfo(file).completeBaseName());
        summary.title = title;
        summary.projectId = pid;
        summary.preview = preview;
        summary.updated = data.value(QStringLiteral("updated")).toDouble();
        summary.messageCount = messages.size();
        summary.hasImage = hasImage;
        items << summary;
    }
    std::sort(items.begin(), items.end(), [](const ConversationSummary &a, const ConversationSummary &b) {
        return a.updated > b.updated;
    });
    return items;
}

// ---------------------------------------------------------------------------
// ProjectStore
// ---------------------------------------------------------------------------
ProjectStore::ProjectStore(const QString &directory)
    : directory_(directory)
{
    QDir().mkpath(directory_);
}

QString ProjectStore::metaPath(const QString &id) const
{
    return QDir(directory_).filePath(id + QStringLiteral("/.takoyo.json"));
}

QJsonObject ProjectStore::readMeta(const QString &id) const
{
    QJsonObject meta = util::readJson(metaPath(id));
    if (meta.isEmpty())
        return meta;
    if (meta.value(QStringLiteral("id")).toString().isEmpty())
        meta.insert(QStringLiteral("id"), id);
    if (meta.value(QStringLiteral("name")).toString().isEmpty())
        meta.insert(QStringLiteral("name"), id);
    return meta;
}

void ProjectStore::writeMeta(const QString &id, QJsonObject meta) const
{
    meta.insert(QStringLiteral("updated"), static_cast<double>(QDateTime::currentSecsSinceEpoch()));
    util::writeJsonAtomic(metaPath(id), meta);
}

QList<ProjectSummary> ProjectStore::list(const QString &query) const
{
    QList<ProjectSummary> items;
    const QString needle = query.trimmed().toLower();
    const QDir dir(directory_);
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : entries) {
        const QFileInfo info(dir.filePath(name));
        QJsonObject meta = readMeta(name);
        ProjectSummary summary;
        summary.id = name;
        summary.name = meta.value(QStringLiteral("name")).toString(name);
        summary.description = meta.value(QStringLiteral("description")).toString();
        summary.updated = meta.value(QStringLiteral("updated")).toDouble(info.lastModified().toSecsSinceEpoch());
        summary.created = meta.value(QStringLiteral("created")).toDouble(info.birthTime().toSecsSinceEpoch());
        if (!needle.isEmpty() && !summary.name.toLower().contains(needle)
            && !summary.description.toLower().contains(needle))
            continue;
        summary.fileCount = projectFiles(info.absoluteFilePath(), 100000).size();
        items << summary;
    }
    std::sort(items.begin(), items.end(), [](const ProjectSummary &a, const ProjectSummary &b) {
        return a.updated > b.updated;
    });
    return items;
}

bool ProjectStore::get(const QString &id, ProjectSummary *out) const
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._-]{1,80}$"));
    if (!valid.match(id).hasMatch())
        return false;
    const QDir root(directory_);
    if (!QFileInfo(root.filePath(id)).isDir())
        return false;
    const QJsonObject meta = readMeta(id);
    ProjectSummary summary;
    summary.id = id;
    summary.name = meta.value(QStringLiteral("name")).toString(id);
    summary.description = meta.value(QStringLiteral("description")).toString();
    summary.updated = meta.value(QStringLiteral("updated")).toDouble();
    summary.created = meta.value(QStringLiteral("created")).toDouble();
    summary.fileCount = projectFiles(root.filePath(id), 100000).size();
    if (out)
        *out = summary;
    return true;
}

ProjectSummary ProjectStore::create(const QString &name, const QString &description) const
{
    QString base = name.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (base.startsWith(QLatin1Char('-')))
        base.remove(0, 1);
    while (base.endsWith(QLatin1Char('-')))
        base.chop(1);
    if (base.isEmpty())
        base = QStringLiteral("project");
    base = base.left(36);
    const QString id = base + QStringLiteral("-") + hexSuffix(5);
    QDir().mkpath(QDir(directory_).filePath(id));
    const double now = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    QJsonObject meta;
    meta.insert(QStringLiteral("id"), id);
    meta.insert(QStringLiteral("name"), name.trimmed().left(80));
    meta.insert(QStringLiteral("description"), description.trimmed().left(240));
    meta.insert(QStringLiteral("created"), now);
    meta.insert(QStringLiteral("updated"), now);
    util::writeJsonAtomic(metaPath(id), meta);
    ProjectSummary summary;
    summary.id = id;
    summary.name = meta.value(QStringLiteral("name")).toString();
    summary.description = meta.value(QStringLiteral("description")).toString();
    summary.created = now;
    summary.updated = now;
    return summary;
}

bool ProjectStore::rename(const QString &id, const QString &name) const
{
    QJsonObject meta = readMeta(id);
    if (meta.isEmpty())
        return false;
    meta.insert(QStringLiteral("name"), name.trimmed().left(80));
    writeMeta(id, meta);
    return true;
}

void ProjectStore::remove(const QString &id) const
{
    const QString root = QDir(directory_).filePath(id);
    if (!QFileInfo(root).isDir())
        return;
    const QString binRoot = QDir(util::trashDir()).filePath(QStringLiteral("projects"));
    QDir().mkpath(binRoot);
    const QString target = QDir(binRoot).filePath(
        id + QStringLiteral("-") + QString::number(QDateTime::currentSecsSinceEpoch()));
    QDir().rename(root, target);
}

namespace {

bool validateRelative(const QString &relative, QString *clean, QString *error)
{
    QString rel = relative.trimmed();
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    static const QRegularExpression drive(QStringLiteral("^[A-Za-z]:"));
    if (rel.isEmpty()) {
        if (error)
            *error = QStringLiteral("Choose a file name");
        return false;
    }
    if (rel.startsWith(QLatin1Char('/')) || drive.match(rel).hasMatch()) {
        if (error)
            *error = QStringLiteral("Use a path relative to the project");
        return false;
    }
    rel = QDir::cleanPath(rel);
    if (rel == QLatin1String(".") || rel == QLatin1String("..") || rel.startsWith(QLatin1String("../"))) {
        if (error)
            *error = QStringLiteral("Path escapes the project");
        return false;
    }
    if (rel == QLatin1String(".takoyo.json") || rel.endsWith(QLatin1String("/.takoyo.json"))) {
        if (error)
            *error = QStringLiteral("That is Takoyo's project file");
        return false;
    }
    if (clean)
        *clean = rel;
    return true;
}

} // namespace

QStringList ProjectStore::filePaths(const QString &id) const
{
    QStringList paths;
    const QString root = QDir(directory_).filePath(id);
    if (!QFileInfo(root).isDir())
        return paths;
    for (const ProjectFile &file : projectFiles(root, 100000))
        paths << file.path;
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

bool ProjectStore::fileExists(const QString &id, const QString &relative) const
{
    QString clean;
    if (!validateRelative(relative, &clean, nullptr))
        return false;
    return QFileInfo(QDir(directory_).filePath(id + QStringLiteral("/") + clean)).isFile();
}

QString ProjectStore::readText(const QString &id, const QString &relative, QString *error) const
{
    const QString root = QDir(directory_).filePath(id);
    if (!QFileInfo(root).isDir()) {
        if (error)
            *error = QStringLiteral("Project not found");
        return QString();
    }
    QString clean;
    if (!validateRelative(relative, &clean, error))
        return QString();
    QFile file(root + QStringLiteral("/") + clean);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not read %1").arg(clean);
        return QString();
    }
    const QByteArray data = file.read(400001);
    file.close();
    if (data.left(4096).contains('\0')) {
        if (error)
            *error = QStringLiteral("This file looks binary");
        return QString();
    }
    QString text = QString::fromUtf8(data.left(400000));
    if (data.size() > 400000)
        text += QStringLiteral("\n… (truncated)");
    return text;
}

bool ProjectStore::writeText(const QString &id, const QString &relative, const QString &content,
                             QString *error) const
{
    const QString root = QDir(directory_).filePath(id);
    if (!QFileInfo(root).isDir()) {
        if (error)
            *error = QStringLiteral("Project not found");
        return false;
    }
    QString clean;
    if (!validateRelative(relative, &clean, error))
        return false;
    QString text = content;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (text.toUtf8().size() > 2000000) {
        if (error)
            *error = QStringLiteral("File is too large to save");
        return false;
    }
    const QFileInfo info(root + QStringLiteral("/") + clean);
    QDir().mkpath(info.absolutePath());
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Could not write %1").arg(clean);
        return false;
    }
    file.write(text.toUtf8());
    file.close();
    QJsonObject meta = readMeta(id);
    if (!meta.isEmpty())
        writeMeta(id, meta);
    return true;
}

bool ProjectStore::deleteFile(const QString &id, const QString &relative, QString *error) const
{
    QString clean;
    if (!validateRelative(relative, &clean, error))
        return false;
    const QFileInfo info(QDir(directory_).filePath(id + QStringLiteral("/") + clean));
    if (!info.isFile())
        return true;
    const QString binDir = QDir(util::trashDir()).filePath(QStringLiteral("files/") + id);
    QDir().mkpath(binDir);
    const QString target = QDir(binDir).filePath(
        QStringLiteral("%1-%2").arg(QDateTime::currentSecsSinceEpoch()).arg(info.fileName()));
    if (!QFile::rename(info.absoluteFilePath(), target)) {
        if (error)
            *error = QStringLiteral("Could not remove %1").arg(clean);
        return false;
    }
    QJsonObject meta = readMeta(id);
    if (!meta.isEmpty())
        writeMeta(id, meta);
    return true;
}

QString ProjectStore::contextForAi(const QString &id, const QString &prompt, int maxChars) const
{
    ProjectSummary meta;
    if (!get(id, &meta))
        return QString();

    QList<ProjectFile> all;
    for (const ProjectFile &file : projectFiles(QDir(directory_).filePath(id), 300)) {
        if (!isSecret(file.path))
            all << file;
    }
    const QString promptLower = prompt.toLower();
    QList<ProjectFile> mentioned;
    for (const ProjectFile &file : all) {
        if (promptLower.contains(file.name.toLower()) || promptLower.contains(file.path.toLower()))
            mentioned << file;
    }
    QList<ProjectFile> candidates = mentioned;
    QSet<QString> candidatePaths;
    for (const ProjectFile &file : mentioned)
        candidatePaths.insert(file.path);
    for (const ProjectFile &file : all) {
        if (candidatePaths.contains(file.path))
            continue;
        if (file.text && file.size <= 200000)
            candidates << file;
    }

    QStringList chunks;
    chunks << QStringLiteral("Open project: \"%1\" (%2)").arg(meta.name, id);
    if (!meta.description.isEmpty())
        chunks << QStringLiteral("Description: ") + meta.description;

    QStringList treeLines;
    for (const ProjectFile &file : all.mid(0, 160))
        treeLines << QStringLiteral("%1  (%2 B)").arg(file.path).arg(file.size);
    chunks << QStringLiteral("File tree:\n") + (treeLines.isEmpty() ? QStringLiteral("(empty project)") : treeLines.join('\n'));

    int budget = maxChars - 400;
    for (const QString &chunk : chunks)
        budget -= chunk.size();
    int included = 0;
    QSet<QString> seen;
    for (const ProjectFile &file : candidates) {
        if (included >= 7 || budget <= 0)
            break;
        if (!file.text || file.size > 200000 || seen.contains(file.path))
            continue;
        QFile handle(QDir(directory_).filePath(id + QStringLiteral("/") + file.path));
        if (!handle.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = handle.read(6000);
        handle.close();
        if (!looksText(data))
            continue;
        QString content = QString::fromUtf8(data);
        if (file.size > 6000)
            content += QStringLiteral("\n… (truncated)");
        if (budget - content.size() - file.path.size() - 20 < 0)
            content = content.left(qMax(0, budget - 300));
        if (content.isEmpty())
            break;
        chunks << QStringLiteral("\n--- file: %1 ---\n%2\n--- end: %1 ---").arg(file.path, content);
        budget -= content.size() + file.path.size() + 40;
        ++included;
        seen.insert(file.path);
    }
    if (included == 0)
        chunks << QStringLiteral("(No text files have been added yet.)");
    return chunks.join(QStringLiteral("\n\n"));
}
