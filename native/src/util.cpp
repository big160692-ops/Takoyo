#include "util.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QWidget>
#include <QWindow>

namespace util {

QString appRoot()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;

    const QString override = qEnvironmentVariable("TAKOYO_ROOT");
    if (!override.isEmpty() && QDir(override).exists()) {
        cached = QDir(override).absolutePath();
        return cached;
    }

    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 5; ++depth) {
        if (dir.exists(QStringLiteral("takoyo_agent")) && dir.exists(QStringLiteral("assets"))) {
            cached = dir.absolutePath();
            return cached;
        }
        if (!dir.cdUp())
            break;
    }
    cached = QCoreApplication::applicationDirPath();
    return cached;
}

QString assetsDir()
{
    return QDir(appRoot()).filePath(QStringLiteral("assets"));
}

QString takoyoHome()
{
    const QString override = qEnvironmentVariable("TAKOYO_HOME");
    if (!override.isEmpty())
        return QDir(override).absolutePath();
    const QString legacy = qEnvironmentVariable("TAKAYO_HOME");
    if (!legacy.isEmpty())
        return QDir(legacy).absolutePath();
    return QDir(QDir::homePath()).filePath(QStringLiteral(".takoyo"));
}

static QString subdir(const QString &name)
{
    QDir dir(takoyoHome());
    dir.mkpath(name);
    return dir.filePath(name);
}

QString conversationsDir()
{
    return subdir(QStringLiteral("conversations"));
}

QString projectsDir()
{
    return subdir(QStringLiteral("projects"));
}

QString imagesDir()
{
    return subdir(QStringLiteral("images"));
}

QString trashDir()
{
    return subdir(QStringLiteral("trash"));
}

QString configPath()
{
    QDir().mkpath(takoyoHome());
    return QDir(takoyoHome()).filePath(QStringLiteral("config.json"));
}

QString pythonExecutable()
{
    const QString override = qEnvironmentVariable("TAKOYO_PYTHON");
    if (!override.isEmpty())
        return override;
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (!python.isEmpty())
        return python;
    return QStandardPaths::findExecutable(QStringLiteral("python3"));
}

QJsonObject readJson(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok)
            *ok = false;
        return {};
    }
    if (ok)
        *ok = true;
    return doc.object();
}

bool writeJsonAtomic(const QString &path, const QJsonObject &object)
{
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    const QString temporary = path + QStringLiteral(".tmp");
    QFile file(temporary);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.close();
    QFile::remove(path);
    return QFile::rename(temporary, path);
}

QString ago(double stamp)
{
    if (stamp <= 0)
        return QString();
    const double now = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    const double delta = qMax(0.0, now - stamp);
    if (delta < 60)
        return QStringLiteral("now");
    if (delta < 3600)
        return QString::number(static_cast<int>(delta / 60)) + QStringLiteral("m");
    if (delta < 86400)
        return QString::number(static_cast<int>(delta / 3600)) + QStringLiteral("h");
    if (delta < 86400 * 7)
        return QString::number(static_cast<int>(delta / 86400)) + QStringLiteral("d");
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(stamp)).toString(QStringLiteral("MMM dd"));
}

QString shorten(const QString &text, int limit, bool ellipsis)
{
    QString oneLine = text.simplified();
    if (oneLine.size() <= limit)
        return oneLine;
    oneLine = oneLine.left(limit - 1).trimmed();
    return ellipsis ? oneLine + QStringLiteral("\u2026") : oneLine;
}

void openPath(const QString &path)
{
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString formatGeometry(const QWidget *window)
{
    if (!window)
        return QStringLiteral("1280x800");
    const QRect rect = window->geometry();
    QString out = QStringLiteral("%1x%2").arg(rect.width()).arg(rect.height());
    const QPoint topLeft = window->pos();
    out += QStringLiteral("%1%2").arg(topLeft.x() >= 0 ? QStringLiteral("+") : QString()).arg(topLeft.x());
    out += QStringLiteral("%1%2").arg(topLeft.y() >= 0 ? QStringLiteral("+") : QString()).arg(topLeft.y());
    return out;
}

void applyGeometry(QWidget *window, const QString &geometry)
{
    static const QRegularExpression re(QStringLiteral("^(\\d+)x(\\d+)([+-]\\d+[+-]\\d+)?$"));
    const QRegularExpressionMatch match = re.match(geometry);
    if (!window || !match.hasMatch()) {
        window->resize(1280, 800);
        return;
    }
    const int width = qMax(1020, match.captured(1).toInt());
    const int height = qMax(640, match.captured(2).toInt());
    window->resize(width, height);
    const QString tail = match.captured(3);
    if (!tail.isEmpty()) {
        static const QRegularExpression pos(QStringLiteral("^([+-]\\d+)([+-]\\d+)$"));
        const QRegularExpressionMatch pm = pos.match(tail);
        if (pm.hasMatch())
            window->move(pm.captured(1).toInt(), pm.captured(2).toInt());
    }
}

QString imageRequestPrompt(const QString &input)
{
    const QString text = input.trimmed();
    if (text.isEmpty())
        return QString();
    static const QRegularExpression command(
        QStringLiteral("^\\s*/(?:image|img|art|draw|paint)\\b[:,]?\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch cm = command.match(text);
    if (cm.hasMatch() && cm.capturedStart() == 0) {
        const QString rest = text.mid(cm.capturedEnd()).trimmed();
        return rest.isEmpty() ? QStringLiteral("a calm Takoyo landscape") : rest;
    }
    const QString low = text.toLower();
    static const QRegularExpression paint(QStringLiteral("\\b(draw|paint|illustrate|sketch|render)\\b"));
    if (paint.match(low).hasMatch())
        return text;
    static const QRegularExpression make(
        QStringLiteral("\\b(generate|create|make|design)\\b[^.!?\\n]{0,34}\\b(image|picture|illustration|artwork|art|poster|wallpaper|logo)\\b"));
    if (make.match(low).hasMatch())
        return text;
    static const QRegularExpression lead(
        QStringLiteral("^(an?\\s+)?(image|picture|illustration|photo|poster|wallpaper)\\s+(of|showing)\\b"));
    if (lead.match(low).hasMatch())
        return text;
    return QString();
}

namespace {

double tokenWeight(const QChar ch)
{
    const ushort u = ch.unicode();
    const bool wide = (u >= 0x1100 && u <= 0x11FF) || (u >= 0x2E80 && u <= 0xA4CF)
        || (u >= 0xAC00 && u <= 0xD7AF) || (u >= 0xF900 && u <= 0xFAFF)
        || (u >= 0xFE30 && u <= 0xFE4F) || (u >= 0xFF00 && u <= 0xFFEF);
    return wide ? 0.75 : 0.32;
}

} // namespace

int estimateTokens(const QString &text)
{
    double total = 0;
    for (const QChar ch : text)
        total += tokenWeight(ch);
    return static_cast<int>(total + 0.999);
}

QString fitToTokens(const QString &text, int tokens)
{
    if (tokens <= 0 || estimateTokens(text) <= tokens)
        return text;
    // Keep the end of the text: pasted documents usually finish with the
    // question or instruction the model must actually act on.
    const int headTokens = qMax(1, tokens * 2 / 3);
    const int tailTokens = qMax(0, tokens - headTokens);
    double used = 0;
    int headEnd = 0;
    for (; headEnd < text.size(); ++headEnd) {
        used += tokenWeight(text.at(headEnd));
        if (used > headTokens)
            break;
    }
    used = 0;
    int tailStart = text.size();
    while (tailStart > headEnd) {
        used += tokenWeight(text.at(tailStart - 1));
        if (used > tailTokens)
            break;
        --tailStart;
    }
    return text.left(headEnd)
        + QStringLiteral("\n… (Takoyo shortened this text to fit the model's context) …\n")
        + text.mid(tailStart);
}

int promptBudgetTokens(int contextTokens, int requestedTokens, int systemTokens)
{
    const int context = contextTokens > 0 ? contextTokens : 8192;
    const int reply = qBound(512, requestedTokens > 0 ? requestedTokens : 2048, 1024);
    return qMax(768, context - reply - qMax(0, systemTokens) - 64);
}

namespace {

// Small models often emit Windows paths with single backslashes, which is not
// valid JSON. Double any escape that JSON does not define so the object parses.
QString relaxJson(const QString &raw)
{
    QString out;
    out.reserve(raw.size() + 16);
    for (int i = 0; i < raw.size(); ++i) {
        const QChar ch = raw.at(i);
        if (ch != QLatin1Char('\\')) {
            out.append(ch);
            continue;
        }
        if (i + 1 >= raw.size()) {
            out.append(QLatin1String("\\\\"));
            break;
        }
        const QChar next = raw.at(i + 1);
        if (next == QLatin1Char('\\') || next == QLatin1Char('"') || next == QLatin1Char('/')
            || next == QLatin1Char('b') || next == QLatin1Char('f') || next == QLatin1Char('n')
            || next == QLatin1Char('r') || next == QLatin1Char('t') || next == QLatin1Char('u')) {
            out.append(ch);
            out.append(next);
            ++i;
            continue;
        }
        out.append(QLatin1String("\\\\"));
    }
    return out;
}

} // namespace

namespace {

// Finds the end of the JSON object that starts at "open". Small models emit code
// inside strings, so a plain non-greedy regex would stop at the first brace.
int jsonObjectEnd(const QString &text, int open)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = open; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (ch == QLatin1Char('"'))
            inString = true;
        else if (ch == QLatin1Char('{'))
            ++depth;
        else if (ch == QLatin1Char('}') && --depth == 0)
            return i;
    }
    return -1;
}

} // namespace

ParsedReply parseReply(const QString &text)
{
    ParsedReply parsed;
    QString cleaned = text;

    // Models sometimes drop the angle brackets ("takoyo-tool { ... }"), so locate
    // the tool name itself and read the JSON object that follows it.
    int search = 0;
    while (search < cleaned.size()) {
        const int tag = cleaned.indexOf(QStringLiteral("takoyo-tool"), search, Qt::CaseInsensitive);
        if (tag < 0)
            break;
        int open = -1;
        for (int i = tag + 11; i < cleaned.size() && i < tag + 400; ++i) {
            const QChar ch = cleaned.at(i);
            if (ch == QLatin1Char('{')) {
                open = i;
                break;
            }
            if (ch == QLatin1Char('<') || ch == QLatin1Char('}'))
                break;
        }
        if (open < 0) {
            search = tag + 11;
            continue;
        }
        const int close = jsonObjectEnd(cleaned, open);
        if (close < 0) {
            search = open + 1;
            continue;
        }

        int begin = tag;
        while (begin > 0 && (cleaned.at(begin - 1) == QLatin1Char('<')
                             || cleaned.at(begin - 1) == QLatin1Char('/')
                             || cleaned.at(begin - 1).isSpace()))
            --begin;
        int end = close + 1;
        int probe = end;
        while (probe < cleaned.size() && cleaned.at(probe).isSpace())
            ++probe;
        if (probe < cleaned.size() && cleaned.at(probe) == QLatin1Char('<'))
            ++probe;
        if (probe < cleaned.size() && cleaned.at(probe) == QLatin1Char('/'))
            ++probe;
        if (cleaned.mid(probe, 11).compare(QStringLiteral("takoyo-tool"), Qt::CaseInsensitive) == 0) {
            end = probe + 11;
            if (end < cleaned.size() && cleaned.at(end) == QLatin1Char('>'))
                ++end;
        }

        const QString raw = relaxJson(cleaned.mid(open, close - open + 1));
        cleaned.remove(begin, end - begin);
        search = begin;

        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        const QJsonObject obj = doc.object();
        const QString tool = obj.value(QStringLiteral("tool")).toString();
        if (tool == QLatin1String("read_file") || tool == QLatin1String("list_dir")) {
            const QString path = obj.value(QStringLiteral("path")).toString().trimmed();
            if (!path.isEmpty())
                parsed.fileRequests.append(FileRequest{path, tool == QLatin1String("list_dir")});
            continue;
        }
        if (tool != QLatin1String("write_file"))
            continue;
        const QString path = obj.value(QStringLiteral("path")).toString().trimmed().replace('\\', '/');
        const QString content = obj.value(QStringLiteral("content")).toString();
        if (path.isEmpty() || !obj.value(QStringLiteral("content")).isString())
            continue;
        if (path.startsWith('/') || QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(path).hasMatch()
            || path.split('/').contains(QStringLiteral("..")))
            continue;
        ToolEdit edit;
        edit.path = path;
        edit.content = content;
        parsed.edits.append(edit);
    }

    // Image tags get the same treatment: opening and closing markers can arrive
    // as "<takoyo-image>", "</takoyo-image>" or bare "takoyo-image>", and a
    // closing marker is often missing entirely.
    static const QString kImageTag = QStringLiteral("takoyo-image");
    QList<int> tags;
    int at = 0;
    while ((at = cleaned.indexOf(kImageTag, at, Qt::CaseInsensitive)) >= 0) {
        tags << at;
        at += kImageTag.size();
    }
    QList<QPair<int, int>> ranges;
    for (int i = 0; i < tags.size(); i += 2) {
        const int openTag = tags.at(i);
        int contentStart = openTag + kImageTag.size();
        if (contentStart < cleaned.size() && cleaned.at(contentStart) == QLatin1Char('>'))
            ++contentStart;
        const bool hasClose = i + 1 < tags.size();
        const int closeTag = hasClose ? tags.at(i + 1) : cleaned.size();

        QString prompt = cleaned.mid(contentStart, closeTag - contentStart).trimmed();
        while (prompt.endsWith(QLatin1Char('<')) || prompt.endsWith(QLatin1Char('/'))
               || prompt.endsWith(QLatin1Char('>')))
            prompt.chop(1);
        prompt = prompt.trimmed();
        if (!prompt.isEmpty() && !parsed.imagePrompts.contains(prompt))
            parsed.imagePrompts << prompt;

        int begin = openTag;
        while (begin > 0 && (cleaned.at(begin - 1) == QLatin1Char('<')
                             || cleaned.at(begin - 1) == QLatin1Char('/')))
            --begin;
        int end = closeTag;
        if (hasClose) {
            end = closeTag + kImageTag.size();
            if (end < cleaned.size() && cleaned.at(end) == QLatin1Char('>'))
                ++end;
        }
        ranges.append(qMakePair(begin, end));
    }
    for (int i = ranges.size() - 1; i >= 0; --i)
        cleaned.remove(ranges.at(i).first, ranges.at(i).second - ranges.at(i).first);

    parsed.cleaned = cleaned.trimmed();
    return parsed;
}

QList<Segment> splitFences(const QString &text)
{
    static const QRegularExpression fence(QStringLiteral("```([^\\n`]*)\\n(.*?)```"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QList<Segment> segments;
    int pos = 0;
    QRegularExpressionMatchIterator it = fence.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString before = text.mid(pos, match.capturedStart() - pos);
        if (!before.trimmed().isEmpty()) {
            Segment s;
            s.text = before;
            segments << s;
        }
        Segment s;
        s.code = true;
        s.info = match.captured(1).trimmed();
        s.text = match.captured(2);
        while (s.text.endsWith('\n'))
            s.text.chop(1);
        segments << s;
        pos = match.capturedEnd();
    }
    const QString after = text.mid(pos);
    if (!after.trimmed().isEmpty()) {
        Segment s;
        s.text = after;
        segments << s;
    }
    return segments;
}

QPair<QString, QString> splitInfo(const QString &info)
{
    QString language;
    QString filename;
    const QStringList tokens = info.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        if (token.startsWith(QLatin1String("file="))) {
            filename = token.mid(5);
            filename.remove('"');
            filename.remove('\'');
        } else if (language.isEmpty()) {
            language = token;
        }
    }
    if (language.isEmpty() && !filename.isEmpty())
        language = QFileInfo(filename).suffix().toLower();
    return {language, filename};
}

} // namespace util
