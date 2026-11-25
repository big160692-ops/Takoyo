#include "syntaxhighlighter.h"

#include "theme.h"

#include <QColor>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>

namespace {

enum Role { Keyword, Type, String, Number, Comment };

QColor roleColor(Role role)
{
    const bool dark = theme::darkMode();
    switch (role) {
    case Keyword: return dark ? QColor("#C792EA") : QColor("#7C3AED");
    case Type:    return dark ? QColor("#82AAFF") : QColor("#1B4FA0");
    case String:  return dark ? QColor("#C3E88D") : QColor("#0F7B3E");
    case Number:  return dark ? QColor("#F78C6C") : QColor("#C2410C");
    case Comment: return dark ? QColor("#6E6E78") : QColor("#8B8B8F");
    }
    return QColor();
}

QTextCharFormat formatFor(Role role)
{
    QTextCharFormat format;
    format.setForeground(roleColor(role));
    if (role == Comment)
        format.setFontItalic(true);
    return format;
}

struct Spec {
    QSet<QString> keywords;
    QSet<QString> types;
    QString lineComment; // e.g. "//" or "#"
    QString blockStart;  // e.g. "/*"
    QString blockEnd;    // e.g. "*/"
    bool tripleQuotes = false;
};

Spec specFor(const QString &language)
{
    const QString lang = language.toLower();
    Spec spec;
    if (lang == QLatin1String("python") || lang == QLatin1String("py")) {
        spec.lineComment = QStringLiteral("#");
        spec.tripleQuotes = true;
        spec.keywords = {"def", "class", "return", "if", "elif", "else", "for", "while", "in", "not",
                         "and", "or", "import", "from", "as", "with", "try", "except", "finally",
                         "raise", "lambda", "yield", "global", "nonlocal", "pass", "break", "continue",
                         "is", "None", "True", "False", "async", "await", "del", "assert"};
        spec.types = {"int", "float", "str", "bool", "list", "dict", "set", "tuple", "bytes", "self"};
    } else if (lang == QLatin1String("javascript") || lang == QLatin1String("js")
               || lang == QLatin1String("typescript") || lang == QLatin1String("ts")
               || lang == QLatin1String("java") || lang == QLatin1String("c")
               || lang == QLatin1String("cpp") || lang == QLatin1String("c++") || lang == QLatin1String("cs")
               || lang == QLatin1String("csharp") || lang == QLatin1String("go") || lang == QLatin1String("rust")
               || lang == QLatin1String("php") || lang == QLatin1String("swift") || lang == QLatin1String("kotlin")
               || lang == QLatin1String("objective-c")) {
        spec.lineComment = QStringLiteral("//");
        spec.blockStart = QStringLiteral("/*");
        spec.blockEnd = QStringLiteral("*/");
        spec.keywords = {"function", "return", "if", "else", "for", "while", "do", "switch", "case",
                         "break", "continue", "class", "new", "delete", "typeof", "instanceof", "var",
                         "let", "const", "public", "private", "protected", "static", "void", "int",
                         "float", "double", "char", "bool", "struct", "enum", "namespace", "using",
                         "template", "typename", "try", "catch", "finally", "throw", "import", "export",
                         "default", "extends", "implements", "interface", "package", "func", "type",
                         "map", "fn", "mut", "pub", "impl", "trait", "async", "await", "yield", "this",
                         "super", "null", "nullptr", "true", "false", "def"};
        spec.types = {"QString", "QObject", "QWidget", "std", "string", "vector", "QStringList"};
    } else if (lang == QLatin1String("css") || lang == QLatin1String("scss")) {
        spec.blockStart = QStringLiteral("/*");
        spec.blockEnd = QStringLiteral("*/");
        spec.keywords = {"important", "media", "keyframes", "import", "font-face"};
    } else if (lang == QLatin1String("bash") || lang == QLatin1String("sh") || lang == QLatin1String("shell")
               || lang == QLatin1String("powershell") || lang == QLatin1String("ps1") || lang == QLatin1String("zsh")) {
        spec.lineComment = QStringLiteral("#");
        spec.keywords = {"if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case",
                         "esac", "function", "echo", "export", "local", "return", "exit", "in"};
    } else if (lang == QLatin1String("sql")) {
        spec.lineComment = QStringLiteral("--");
        spec.blockStart = QStringLiteral("/*");
        spec.blockEnd = QStringLiteral("*/");
        spec.keywords = {"select", "from", "where", "insert", "update", "delete", "join", "on", "group",
                         "by", "order", "limit", "create", "table", "drop", "alter", "and", "or", "not",
                         "null", "as", "distinct", "values", "into", "set"};
    } else if (lang == QLatin1String("json")) {
        // Strings and numbers only.
    } else if (lang.isEmpty() || lang == QLatin1String("text") || lang == QLatin1String("txt")
               || lang == QLatin1String("code") || lang == QLatin1String("plaintext")) {
        // No language rules; leave code uncolored rather than guessing.
        spec.keywords.clear();
        spec.lineComment.clear();
        return spec;
    } else {
        // Unknown language: fall back to the C-like comment/string rules.
        spec.lineComment = QStringLiteral("//");
        spec.blockStart = QStringLiteral("/*");
        spec.blockEnd = QStringLiteral("*/");
    }
    return spec;
}

class CodeHighlighter : public QSyntaxHighlighter {
public:
    CodeHighlighter(QTextDocument *document, const Spec &spec)
        : QSyntaxHighlighter(document)
        , spec_(spec)
        , keywordFormat_(formatFor(Keyword))
        , typeFormat_(formatFor(Type))
        , stringFormat_(formatFor(String))
        , numberFormat_(formatFor(Number))
        , commentFormat_(formatFor(Comment))
    {
        if (!spec_.keywords.isEmpty()) {
            QStringList words;
            for (const QString &word : spec_.keywords)
                words << QRegularExpression::escape(word);
            keywordRegex_ = QRegularExpression(QStringLiteral("\\b(?:%1)\\b").arg(words.join(QLatin1Char('|'))));
        }
        if (!spec_.types.isEmpty()) {
            QStringList words;
            for (const QString &word : spec_.types)
                words << QRegularExpression::escape(word);
            typeRegex_ = QRegularExpression(QStringLiteral("\\b(?:%1)\\b").arg(words.join(QLatin1Char('|'))));
        }
        numberRegex_ = QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"));
    }

protected:
    void highlightBlock(const QString &text) override
    {
        // 1. Base layer: numbers, types, keywords. These are overridden below by
        //    comments and strings so those always win where they overlap.
        applyMatches(numberRegex_, text, numberFormat_);
        applyMatches(typeRegex_, text, typeFormat_);
        applyMatches(keywordRegex_, text, keywordFormat_);

        // 2. Comments and strings, scanned left-to-right so whichever comes first
        //    wins and the rest of a comment/string line is consumed.
        int i = 0;
        // Continue a multi-line block comment from the previous line.
        if (previousBlockState() == 1 && !spec_.blockEnd.isEmpty()) {
            const int end = text.indexOf(spec_.blockEnd);
            if (end < 0) {
                setFormat(0, text.size(), commentFormat_);
                setCurrentBlockState(1);
                return;
            }
            setFormat(0, end + spec_.blockEnd.size(), commentFormat_);
            i = end + spec_.blockEnd.size();
        } else if (previousBlockState() == 2 && spec_.tripleQuotes) {
            // Continue a multi-line triple-quoted string from the previous line.
            const int dq = text.indexOf(QStringLiteral("\"\"\""));
            const int sq = text.indexOf(QStringLiteral("'''"));
            int close;
            if (dq >= 0 && (sq < 0 || dq < sq))
                close = dq;
            else
                close = sq;
            if (close < 0) {
                setFormat(0, text.size(), stringFormat_);
                setCurrentBlockState(2);
                return;
            }
            setFormat(0, close + 3, stringFormat_);
            i = close + 3;
        }

        while (i < text.size()) {
            // Line comment.
            if (!spec_.lineComment.isEmpty() && text.mid(i).startsWith(spec_.lineComment)) {
                setFormat(i, text.size() - i, commentFormat_);
                return;
            }
            // Block comment start.
            if (!spec_.blockStart.isEmpty() && text.mid(i).startsWith(spec_.blockStart)) {
                const int end = text.indexOf(spec_.blockEnd, i + spec_.blockStart.size());
                if (end < 0) {
                    setFormat(i, text.size() - i, commentFormat_);
                    setCurrentBlockState(1);
                    return;
                }
                setFormat(i, end + spec_.blockEnd.size() - i, commentFormat_);
                i = end + spec_.blockEnd.size();
                continue;
            }
            // Triple-quoted string.
            if (spec_.tripleQuotes && (text.mid(i).startsWith(QStringLiteral("\"\"\""))
                                       || text.mid(i).startsWith(QStringLiteral("'''")))) {
                const QString quote = text.mid(i, 3);
                const int end = text.indexOf(quote, i + 3);
                if (end < 0) {
                    setFormat(i, text.size() - i, stringFormat_);
                    setCurrentBlockState(2);
                    return;
                }
                setFormat(i, end + 3 - i, stringFormat_);
                i = end + 3;
                continue;
            }
            // Single-line string.
            const QChar ch = text.at(i);
            if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
                int j = i + 1;
                while (j < text.size()) {
                    if (text.at(j) == QLatin1Char('\\')) {
                        j += 2;
                        continue;
                    }
                    if (text.at(j) == ch) {
                        ++j;
                        break;
                    }
                    ++j;
                }
                setFormat(i, j - i, stringFormat_);
                i = j;
                continue;
            }
            ++i;
        }
    }

private:
    void applyMatches(const QRegularExpression &regex, const QString &text, const QTextCharFormat &format)
    {
        if (regex.pattern().isEmpty() || !regex.isValid())
            return;
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    }

    Spec spec_;
    QTextCharFormat keywordFormat_;
    QTextCharFormat typeFormat_;
    QTextCharFormat stringFormat_;
    QTextCharFormat numberFormat_;
    QTextCharFormat commentFormat_;
    QRegularExpression keywordRegex_;
    QRegularExpression typeRegex_;
    QRegularExpression numberRegex_;
};

} // namespace

namespace syntax {

void install(QTextDocument *document, const QString &language)
{
    if (!document)
        return;
    const Spec spec = specFor(language);
    // Nothing to colourise for plain text.
    if (spec.keywords.isEmpty() && spec.lineComment.isEmpty() && spec.blockStart.isEmpty()
        && !spec.tripleQuotes)
        return;
    // The highlighter is owned by the document.
    new CodeHighlighter(document, spec);
}

} // namespace syntax