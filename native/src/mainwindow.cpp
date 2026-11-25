#include "mainwindow.h"

#include "artpanel.h"
#include "backend.h"
#include "chatview.h"
#include "composer.h"
#include "dialogs.h"
#include "homeview.h"
#include "icons.h"
#include "sidebar.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QVector>

#include <functional>
#include <algorithm>

// A host strip that slides a fixed-width panel in and out of view. The panel
// is right-anchored for the sidebar and left-anchored for the art panel, which
// keeps its edge riding the moving boundary instead of being squashed.
class SlideHost : public QWidget {
public:
    SlideHost(bool anchorRight, int panelWidth, QWidget *panel, QWidget *parent = nullptr)
        : QWidget(parent)
        , anchorRight_(anchorRight)
        , panelWidth_(panelWidth)
        , panel_(panel)
    {
        panel_->setParent(this);
        panel_->setFixedWidth(panelWidth_);
        panel_->hide();
        setFixedWidth(0);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }

    void setSlideWidth(int width)
    {
        width = qBound(0, width, panelWidth_);
        if (width != this->width())
            setFixedWidth(width);
        const bool visible = width > 0;
        if (panel_->isVisible() != visible)
            panel_->setVisible(visible);
        const int x = anchorRight_ ? width - panelWidth_ : 0;
        panel_->setGeometry(x, 0, panelWidth_, height());
        if (visible)
            panel_->raise();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        const int x = anchorRight_ ? width() - panelWidth_ : 0;
        panel_->setGeometry(x, 0, panelWidth_, height());
    }

private:
    bool anchorRight_;
    int panelWidth_;
    QWidget *panel_;
};

namespace {

class Clickable : public QWidget {
public:
    explicit Clickable(QWidget *parent = nullptr) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick)
            onClick();
        QWidget::mouseReleaseEvent(event);
    }
};

const QString kBasePrompt = QStringLiteral(
    "You are Takoyo, a curious, friendly octopus who lives inside the Takoyo desktop app. "
    "You are warm and a little playful, never gushy, and you take the user's work seriously.\n"
    "You help people build software, make images, explore ideas and simply chat.\n\n"
    "How to answer:\n"
    "- Lead with the answer or the result. Skip preambles like \"Great question\" and do not restate the question.\n"
    "- Keep replies tight: usually 2-4 short paragraphs, or a few bullets when listing things. "
    "If the user asks for more detail or steps, give it.\n"
    "- Use Markdown when it helps: short paragraphs, bullets, fenced code with a language tag. "
    "Put code first and explain it after, briefly.\n"
    "- Be concrete. Prefer a specific recommendation over a survey of every option.\n\n"
    "Honesty:\n"
    "- If you do not know something, say \"I'm not sure\" and name the check that would settle it. "
    "Never invent facts, APIs, file contents, quotes or citations.\n"
    "- Never claim to have run something you did not run. If a result depends on the user's machine, say what to run.\n"
    "- You are a small model running on this device. For long files, heavy math or reasoning, or very recent "
    "facts, say plainly when you might struggle and suggest a bigger model in Settings.\n"
    "- For math, show the steps and sanity-check the result.\n\n"
    "Answer the user's question in this reply. If you already know the answer, give it now instead of "
    "describing what you would do. Never end with a promise to fetch, look up or do something "
    "\"in the next step\" or \"in the next turn\": either use one of the tools below in this reply, "
    "or answer directly with what you know.\n\n"
    "You can create images, but ONLY when the user explicitly asks for a picture, drawing, painting, "
    "sketch, logo, poster, wallpaper, icon or similar visual. Never emit an image tag for code, files, "
    "writing, explanations, or any other text request. If the user asks you to build, create, write, "
    "fix or explain code or a file, answer with text (and a write_file tool call when a project is open) "
    "— never an image.\n"
    "To create an image, emit exactly:\n"
    "<takoyo-image>a detailed image prompt</takoyo-image>\n"
    "Takoyo renders it in the art panel on the right.");

QString styleInstruction(const QString &style)
{
    if (style == QLatin1String("short"))
        return QStringLiteral("Reply style: Short. One to three sentences or a compact list. "
                              "No preamble, no recap, no follow-up offer.");
    if (style == QLatin1String("detailed"))
        return QStringLiteral("Reply style: Detailed. Give a thorough answer with context, examples and "
                              "trade-offs, but keep it organized with short sections or bullets.");
    if (style == QLatin1String("steps"))
        return QStringLiteral("Reply style: Step-by-step. Structure the answer as numbered steps, one "
                              "action per step, each step short. Add a brief note after the list if needed.");
    return QStringLiteral("Reply style: Balanced. A concise, complete answer: a short paragraph plus a few "
                          "bullets when they help.");
}

const QString kProjectTools = QStringLiteral(
    "A project is open, so you can read and edit real files in it.\n"
    "The file contents under \"Project snapshot\" are real files the user has shared with you. "
    "Read them directly and never claim you cannot access the user's files. If the contents are not "
    "in your context yet, do not apologize and never say you cannot read files. Reply with exactly "
    "this block and nothing else:\n"
    "<takoyo-tool>\n"
    "{\"tool\": \"read_file\", \"path\": \"relative/path.ext\"}\n"
    "</takoyo-tool>\n"
    "Takoyo fetches the file and sends it to you right away. Use {\"tool\": \"list_dir\", \"path\": \"folder\"} "
    "to list a folder. Paths may be absolute (for example C:/Users/you/notes.txt) or relative to "
    "the project root.\n"
    "When you need to replace or create a file, emit exactly this block (valid JSON, full file content):\n"
    "<takoyo-tool>\n"
    "{\"tool\": \"write_file\", \"path\": \"relative/path.ext\", \"content\": \"the complete new file content\"}\n"
    "</takoyo-tool>\n"
    "Rules for edits:\n"
    "- Use paths relative to the project root. Never use absolute paths or `..`.\n"
    "- Always send the complete file content, not a patch.\n"
    "- Keep prose around edits short. Explain what changed after the block.\n"
    "- One write_file block per file. Do not invent files that the user did not ask for.\n"
    "- If you put code in a normal fenced block, start the fence with the language and file, like:\n"
    "  ```python file=src/main.py\n"
    "  ...code...\n"
    "  ```\n"
    "  Takoyo will offer that block as an editable file too.");

// A folder the user pointed Takoyo at is a workspace, not a stored project, so
// it needs its own instructions: without this the model was told "no project is
// open" and never emitted a write_file, which is why edits silently did nothing.
const QString kWorkspaceTools = QStringLiteral(
    "A folder is open at %1, so you can read and edit real files in it.\n"
    "Paths in your <takoyo-tool> blocks are relative to that folder (for example "
    "\"notes/todo.txt\"), or absolute. Never use \"..\" to leave the folder.\n"
    "When the user asks you to create, write, change or fix a file, emit the write_file "
    "block below. Do not only describe the file in a fenced code block: a fenced block "
    "is only a preview, and the user wants the file actually written to disk.\n"
    "If the file already exists and you do not have its contents, read it first with "
    "read_file, then send the complete updated version.\n"
    "<takoyo-tool>\n"
    "{\"tool\": \"read_file\", \"path\": \"relative/path.ext\"}\n"
    "</takoyo-tool>\n"
    "Takoyo fetches the file and sends it to you right away. Use {\"tool\": \"list_dir\", "
    "\"path\": \"folder\"} to list a folder.\n"
    "When you need to replace or create a file, emit exactly this block (valid JSON, "
    "full file content):\n"
    "<takoyo-tool>\n"
    "{\"tool\": \"write_file\", \"path\": \"relative/path.ext\", \"content\": \"the complete new file content\"}\n"
    "</takoyo-tool>\n"
    "Rules for edits:\n"
    "- Use paths relative to the folder. Never use absolute paths or `..`.\n"
    "- Always send the complete file content, not a patch.\n"
    "- Keep prose around edits short. Explain what changed after the block.\n"
    "- One write_file block per file. Do not invent files that the user did not ask for.");

const QString kNoProjectPrompt = QStringLiteral(
    "No project is open. If the user wants to build or edit a project, suggest starting one "
    "from the sidebar so you can touch the files directly.\n"
    "You can still read files from this computer, but only when the user asks about a specific "
    "file or folder. If its contents are missing from your context, do not apologize and never "
    "say you cannot read files. Reply with exactly this block and nothing else:\n"
    "<takoyo-tool>\n"
    "{\"tool\": \"read_file\", \"path\": \"C:/Users/you/file.txt\"}\n"
    "</takoyo-tool>\n"
    "Takoyo fetches the file and sends it to you right away. Use {\"tool\": \"list_dir\", \"path\": "
    "\"folder\"} to list a folder. For general questions about facts, words, definitions, history, "
    "science or code, answer from what you know and do not fetch or search for anything.");

const QString kImageTools = QStringLiteral(
    "Only use the <takoyo-image> tool when the user explicitly asks for a picture or visual "
    "(draw, paint, sketch, image, logo, poster, wallpaper, icon). Never use it for code, file, "
    "build or writing requests — those must be answered with text, not an image.\n"
    "When the user attaches an image, Takoyo has already read its text on this device and placed that text "
    "in the user's message. Use that text directly: never call read_file on an image, never ask for its "
    "path, and never say the image is still being processed.");

const QString kAttachmentTools = QStringLiteral(
    "When the user attaches a file (Word, PowerPoint, spreadsheet, plain text or code), Takoyo has already "
    "opened it on this device and placed its text in the user's message under \"The user attached a file "
    "named…\". Work from that text. Never ask for the file's path or name, never ask the user to paste or "
    "re-upload its contents, and never say the file is unreadable while that text is present.");

const QString kLocalBasePrompt = QStringLiteral(
    "You are Takoyo, a friendly, concise assistant running on this device. Answer the user's actual "
    "question now; do not promise future work. Be accurate; never invent facts or claim actions you did "
    "not perform. Lead with the answer, usually in 2-4 short paragraphs. Use Markdown or code when useful. "
    "For image requests, emit <takoyo-image>a detailed image prompt</takoyo-image>. Only do this when "
    "the user explicitly asks for a picture or visual; never for code, file or text requests.");

const QString kLocalNoProjectPrompt = QStringLiteral(
    "No project is open. Suggest opening one for project edits. Read local files only when asked. If "
    "requested file contents are missing, emit exactly <takoyo-tool>{\"tool\":\"read_file\","
    "\"path\":\"path/to/file\"}</takoyo-tool>. Use list_dir for folders. Answer general questions "
    "directly from what you know.");

const QString kLocalImageTools = QStringLiteral(
    "Attached images were read on this device; use the OCR text in the user's message. Do not ask for "
    "the image path or say it is still processing.");

const QString kLocalAttachmentTools = QStringLiteral(
    "Attached document text is included in the user's message. Use it directly; do not ask for its path "
    "or ask the user to paste it again.");

// Small local models sometimes answer a direct question with a plan ("I'll fetch
// a corpus… in the next step") instead of the answer. Such replies are detected
// here so they can be retried once with an explicit instruction to answer now.
bool isDeferredPromise(const QString &text)
{
    if (text.size() > 700)
        return false;
    static const QRegularExpression re(
        QStringLiteral("\\b(?:in the next (?:step|turn)|next (?:step|turn) I will|"
                       "I(?:'ll| will| am going to) (?:fetch|look|search|check|list|"
                       "gather|retrieve|download|pull|read)|"
                       "I(?:'ll| will| am going to) (?:need to|have to) (?:read|open|render|process|parse|"
                       "extract|convert|examine|inspect|analy[sz]e)|"
                       "I (?:need|have) to (?:render|extract|parse|convert|process) (?:it|this|the file|that) first|"
                       "let me (?:fetch|look|search|check|find|retrieve|download|render|extract)|"
                       "let(?:'s| us) (?:fetch|look up|look for|search|check|review|read|use|proceed)|"
                       "once the (?:image|file|document) is (?:processed|ready|loaded|rendered)|"
                       "the (?:image|file|document) is (?:being )?(?:processed|opened|read|rendered)|"
                       "I(?:'ll| will) (?:analy[sz]e|examine|open|view|process|use) (?:the|this|that) "
                       "(?:attached )?(?:image|file|document)|"
                       "I will proceed from there)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

bool isImagePath(const QString &path)
{
    static const QStringList extensions = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("bmp"),
        QStringLiteral("tiff"),
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

// An attached image or document is read on-device before the reply, so these
// lines mean the model ignored the extracted text and asked for a file it
// already has.
bool isImageConfusion(const QString &text)
{
    if (text.size() > 700)
        return false;
    static const QRegularExpression re(
        QStringLiteral("\\b(?:(?:could not|couldn't|cannot|can't|unable to) (?:open|read|access|view|see|process) "
                       "(?:the|this|that|your) (?:attached )?(?:image|file|document|presentation|slide deck|"
                       "powerpoint|spreadsheet|word document)|"
                       "(?:issue|problem|error) (?:with )?(?:(?:opening|reading|accessing|processing) )?"
                       "(?:the|this|that|your) (?:attached )?(?:image|file|document)|"
                       "(?:provide|send|share|give|upload|attach) (?:me )?(?:the|its|your) (?:file )?"
                       "(?:path|name|location|contents?)|"
                       "(?:need|require) (?:the|a) (?:file|document) (?:path|name|content)|"
                       "I (?:do not|don't) (?:have|see) (?:access to )?(?:the|that) (?:attached )?(?:image|file|document)|"
                       "(?:image|file|document) (?:file )?(?:could not|couldn't|cannot|can't) be (?:opened|read|accessed|processed))\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

QString attachmentNote(const QString &name, const QString &content)
{
    const int limit = 60000;
    const QString trimmed = content.left(limit);
    const QString suffix = content.size() > limit ? QStringLiteral("\n… (truncated)") : QString();
    return QStringLiteral("\n\nThe user attached a file named %1:\n```\n%2\n```%3").arg(name, trimmed, suffix);
}

// A short top-level listing so the model knows what is already in the folder
// before it writes. Deliberately shallow: a full tree would blow the local
// context budget for no benefit.
QString workspaceSnapshot(const QString &folder)
{
    const QDir dir(folder);
    if (!dir.exists())
        return QStringLiteral("(folder unavailable)");

    QStringList lines;
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                    QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (lines.size() >= 80)
            break;
        QString line = entry.fileName();
        if (entry.isDir())
            line += QLatin1Char('/');
        else if (entry.size() > 0)
            line += QStringLiteral(" (%1)").arg(entry.size());
        lines << line;
    }
    return lines.isEmpty() ? QStringLiteral("(empty folder)") : lines.join(QChar('\n'));
}

QString systemPrompt(const ProjectStore &projects, bool hasProject, const ProjectSummary &project,
                     const QString &workspaceFolder, const QString &prompt, int snapshotChars,
                     const QString &style, bool localModel)
{
    QStringList parts;
    parts << (localModel ? kLocalBasePrompt : kBasePrompt);
    parts << styleInstruction(style);
    if (!localModel) {
        const QString today =
            QLocale(QLocale::English).toString(QDate::currentDate(), QStringLiteral("MMMM d, yyyy"));
        parts << QStringLiteral("Today is %1.").arg(today);
    }
    if (hasProject) {
        parts << kProjectTools;
        const QString context = projects.contextForAi(project.id, prompt, snapshotChars);
        parts << QStringLiteral("Current project: %1 (id %2).\nProject snapshot:\n%3")
                     .arg(project.name, project.id, context.isEmpty() ? QStringLiteral("(empty project)") : context);
    } else if (!workspaceFolder.isEmpty()) {
        parts << kWorkspaceTools.arg(QDir::toNativeSeparators(workspaceFolder));
        parts << QStringLiteral("Current folder: %1\nFolder contents:\n%2")
                     .arg(QDir::toNativeSeparators(workspaceFolder), workspaceSnapshot(workspaceFolder));
    } else {
        parts << (localModel ? kLocalNoProjectPrompt : kNoProjectPrompt);
    }
    parts << (localModel ? kLocalImageTools : kImageTools);
    parts << (localModel ? kLocalAttachmentTools : kAttachmentTools);
    if (!localModel)
        parts << QStringLiteral("Answer now: if you know the answer, say it in this reply and do not "
                                "promise to look it up later.");
    return parts.join(QStringLiteral("\n\n"));
}

QJsonObject imageContentPart(const QString &path, const QString &mime)
{
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile() || info.size() <= 0 || info.size() > 8 * 1024 * 1024)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty())
        return {};
    QJsonObject imageUrl;
    imageUrl.insert(QStringLiteral("url"),
                    QStringLiteral("data:%1;base64,%2")
                        .arg(mime.isEmpty() ? QStringLiteral("image/png") : mime,
                             QString::fromLatin1(data.toBase64())));
    QJsonObject part;
    part.insert(QStringLiteral("type"), QStringLiteral("image_url"));
    part.insert(QStringLiteral("image_url"), imageUrl);
    return part;
}

QJsonArray buildChatMessages(const QString &system, const QJsonArray &history, int historyTokens = 4000,
                             int turnTokens = 16000)
{
    struct Turn {
        QString role;
        QString content;
        QString imagePath;
        QString imageMime;
        QString imageName;
        bool hidden = false;
    };

    QVector<Turn> cleaned;
    for (const QJsonValue value : history) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role != QLatin1String("user") && role != QLatin1String("assistant"))
            continue;
        QString content = message.value(QStringLiteral("content")).toString();
        const QJsonObject attachment = message.value(QStringLiteral("attachment")).toObject();
        const QString name = attachment.value(QStringLiteral("name")).toString();
        Turn turn;
        turn.role = role;
        turn.hidden = message.value(QStringLiteral("hidden")).toBool();
        if (attachment.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
            turn.imagePath = attachment.value(QStringLiteral("path")).toString();
            turn.imageMime = attachment.value(QStringLiteral("mime")).toString();
            turn.imageName = name;
        } else if (!name.isEmpty()) {
            content += attachmentNote(name, attachment.value(QStringLiteral("content")).toString());
        }
        content = util::fitToTokens(content, turnTokens);
        if (content.trimmed().isEmpty() && turn.imagePath.isEmpty())
            continue;
        turn.content = content;
        cleaned.append(turn);
    }

    QVector<Turn> kept;
    int total = 0;
    for (int i = cleaned.size() - 1; i >= 0; --i) {
        Turn turn = cleaned.at(i);
        int cost = util::estimateTokens(turn.content) + 24;
        if (!kept.isEmpty() && total + cost > historyTokens) {
            const int remaining = historyTokens - total;
            // File contents are attached as a hidden turn; squeeze them in
            // rather than dropping them, so the model can still read the file.
            if (!turn.hidden || remaining < 256)
                break;
            turn.content = util::fitToTokens(turn.content, remaining - 24);
            cost = util::estimateTokens(turn.content) + 24;
        }
        kept.prepend(turn);
        total += cost;
    }
    while (!kept.isEmpty() && kept.first().role != QLatin1String("user"))
        kept.removeFirst();

    int currentImage = -1;
    for (int i = 0; i < kept.size(); ++i) {
        if (kept.at(i).role == QLatin1String("user") && !kept.at(i).imagePath.isEmpty())
            currentImage = i;
    }

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMessage.insert(QStringLiteral("content"), system);
    messages.append(systemMessage);
    for (int i = 0; i < kept.size(); ++i) {
        Turn &turn = kept[i];
        QJsonObject message;
        message.insert(QStringLiteral("role"), turn.role);
        if (!turn.imagePath.isEmpty()) {
            if (i != currentImage) {
                turn.content += QStringLiteral("\n\n[An image attachment from earlier in this chat is not visible in this turn.]");
            } else {
                const QJsonObject part = imageContentPart(turn.imagePath, turn.imageMime);
                if (!part.isEmpty()) {
                    QJsonArray content;
                    QJsonObject textPart;
                    textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
                    textPart.insert(QStringLiteral("text"),
                                    turn.content.trimmed().isEmpty()
                                        ? QStringLiteral("Please look at the attached image and help with my request.")
                                        : turn.content);
                    content.append(textPart);
                    content.append(part);
                    message.insert(QStringLiteral("content"), content);
                    messages.append(message);
                    continue;
                }
                if (turn.content.trimmed().isEmpty())
                    turn.content = QStringLiteral("[I attached %1, but Takoyo could not read that image.]").arg(turn.imageName);
            }
        }
        if (turn.content.trimmed().isEmpty())
            continue;
        message.insert(QStringLiteral("content"), turn.content);
        messages.append(message);
    }
    return messages;
}

QString localModelName(const QString &key)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("qwen2.5-0.5b"), QStringLiteral("Qwen2.5 0.5B")},
        {QStringLiteral("qwen2.5-1.5b"), QStringLiteral("Qwen2.5 1.5B")},
        {QStringLiteral("qwen2.5-coder-1.5b"), QStringLiteral("Qwen2.5 Coder 1.5B")},
        {QStringLiteral("qwen2.5-3b"), QStringLiteral("Qwen2.5 3B")},
    };
    return names.value(key, key.isEmpty() ? QStringLiteral("Qwen2.5 1.5B") : key);
}

QString menuStyle()
{
    return QStringLiteral(
        "QMenu{background:#FFFFFF;border:1px solid #E7E7E9;border-radius:10px;padding:6px;}"
        "QMenu::item{padding:7px 18px;border-radius:6px;color:#0B0B0D;}"
        "QMenu::item:selected{background:#EDEDEF;color:#0B0B0D;}"
        "QMenu::separator{height:1px;background:#E7E7E9;margin:5px 8px;}");
}

} // namespace

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , projects_(util::projectsDir())
    , conversations_(util::conversationsDir())
{
    theme::setDarkMode(config_.flag(QStringLiteral("dark_mode")));
    setWindowTitle(QStringLiteral("Takoyo"));
    setMinimumSize(1020, 640);
    theme::setSurface(this);

    buildUi();
    wireSidebar();
    wireViews();
    wireComposers();
    wireBackend();
    wireArt();

    const QString lastProject = config_.str(QStringLiteral("last_project"));
    if (!lastProject.isEmpty()) {
        ProjectSummary stored;
        if (projects_.get(lastProject, &stored)) {
            hasProject_ = true;
            project_ = stored;
        }
    }
    workspaceFolder_ = config_.str(QStringLiteral("workspace_folder"));
    if (!workspaceFolder_.isEmpty() && !QDir(workspaceFolder_).exists())
        workspaceFolder_.clear();
    if (backend_)
        backend_->setWorkspace(workspaceFolder_);
    syncHomeProject();

    sidebarOpen_ = config_.flag(QStringLiteral("sidebar_open"));
    logo_->setVisible(!sidebarOpen_);
    sidebarHost_->setSlideWidth(sidebarOpen_ ? 396 : 0);
    artHost_->setSlideWidth(0);

    util::applyGeometry(this, config_.str(QStringLiteral("window_geometry")));

    auto *newChatShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+N")), this);
    connect(newChatShortcut, &QShortcut::activated, this, &MainWindow::newChat);
    auto *settingsShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+,")), this);
    connect(settingsShortcut, &QShortcut::activated, this, &MainWindow::openSettings);
    auto *sidebarShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+B")), this);
    connect(sidebarShortcut, &QShortcut::activated, this, &MainWindow::toggleSidebar);
    auto *modelShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+M")), this);
    connect(modelShortcut, &QShortcut::activated, this, &MainWindow::openModelPicker);
    auto *themeShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")), this);
    connect(themeShortcut, &QShortcut::activated, this, &MainWindow::toggleTheme);
    auto *searchShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        if (!sidebarOpen_)
            setSidebarOpen(true, true);
        sidebar_->focusSearch();
    });

    refreshSidebar();
    showHome();
    updateStatus();
    backend_->start();

    QTimer::singleShot(0, this, [this]() {
        if (stack_->currentWidget() == home_)
            home_->composer()->focusInput();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    sidebar_ = new Sidebar(this);
    sidebarHost_ = new SlideHost(true, 396, sidebar_, this);
    root->addWidget(sidebarHost_);

    auto *main = new QWidget(this);
    theme::style(main, QStringLiteral("background:#FFFFFF;"));
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *header = new QWidget(main);
    header->setFixedHeight(66);
    theme::style(header, QStringLiteral("background:#FFFFFF;"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 10, 18, 10);
    headerLayout->setSpacing(0);

    logo_ = new QWidget(header);
    auto *logoLayout = new QHBoxLayout(logo_);
    logoLayout->setContentsMargins(0, 0, 0, 0);
    logoLayout->setSpacing(9);
    auto *toggle = new IconButton(QStringLiteral("sidebar"), 25, logo_);
    toggle->setColor(theme::INK);
    toggle->setHoverColor(theme::INK);
    toggle->setToolTip(QStringLiteral("Toggle sidebar"));
    connect(toggle, &QAbstractButton::clicked, this, &MainWindow::toggleSidebar);
    logoLayout->addWidget(toggle);
    auto *brand = new Clickable(logo_);
    auto *brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->addWidget(new Wordmark(20, brand));
    brand->onClick = [this]() { showHome(); };
    logoLayout->addWidget(brand);
    headerLayout->addWidget(logo_);
    headerLayout->addStretch(1);

    themeButton_ = new IconButton(theme::darkMode() ? QStringLiteral("sun") : QStringLiteral("moon"), 17, header);
    themeButton_->setColor(theme::MUTED);
    themeButton_->setHoverColor(theme::INK);
    themeButton_->setToolTip(theme::darkMode() ? QStringLiteral("Switch to light mode (Ctrl+Shift+D)")
                                               : QStringLiteral("Switch to dark mode (Ctrl+Shift+D)"));
    connect(themeButton_, &QAbstractButton::clicked, this, &MainWindow::toggleTheme);
    headerLayout->addWidget(themeButton_);
    headerLayout->addSpacing(4);

    auto *gear = new IconButton(QStringLiteral("gear"), 18, header);
    gear->setColor(theme::MUTED);
    gear->setHoverColor(theme::INK);
    gear->setToolTip(QStringLiteral("Settings (Ctrl+,)"));
    connect(gear, &QAbstractButton::clicked, this, &MainWindow::openSettings);
    headerLayout->addWidget(gear);
    headerLayout->addSpacing(8);

    artButton_ = new IconButton(QStringLiteral("image"), 18, header);
    artButton_->setColor(theme::MUTED);
    artButton_->setHoverColor(theme::INK);
    artButton_->setToolTip(QStringLiteral("Takoyo Art panel — your latest image"));
    connect(artButton_, &QAbstractButton::clicked, this, &MainWindow::toggleArt);
    headerLayout->addWidget(artButton_);

    auto *body = new QWidget(main);
    theme::style(body, QStringLiteral("background:#FFFFFF;"));
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    stack_ = new QStackedWidget(body);
    theme::style(stack_, QStringLiteral("QStackedWidget{background:#FFFFFF;}"));
    home_ = new HomeView(stack_);
    chat_ = new ChatView(stack_);
    stack_->addWidget(home_);
    stack_->addWidget(chat_);
    bodyLayout->addWidget(stack_, 1);

    art_ = new ArtPanel(body);
    artHost_ = new SlideHost(false, 360, art_, body);
    bodyLayout->addWidget(artHost_);

    mainLayout->addWidget(header);
    mainLayout->addWidget(body, 1);
    root->addWidget(main, 1);
}

void MainWindow::wireSidebar()
{
    connect(sidebar_, &Sidebar::openProjectRequested, this, &MainWindow::openProject);
    connect(sidebar_, &Sidebar::openConversationRequested, this, &MainWindow::loadConversation);
    connect(sidebar_, &Sidebar::newProjectRequested, this, &MainWindow::newProjectDialog);
    connect(sidebar_, &Sidebar::projectMenuRequested, this, &MainWindow::showProjectMenu);
    connect(sidebar_, &Sidebar::conversationMenuRequested, this, &MainWindow::showConversationMenu);
    connect(sidebar_, &Sidebar::settingsRequested, this, &MainWindow::openSettings);
    connect(sidebar_, &Sidebar::modelPickerRequested, this, &MainWindow::openModelPicker);
    connect(sidebar_, &Sidebar::homeRequested, this, &MainWindow::showHome);
    connect(sidebar_, &Sidebar::paletteRequested, this, &MainWindow::openPalettePicker);
    connect(sidebar_, &Sidebar::newChatRequested, this, &MainWindow::newChat);
    connect(sidebar_, &Sidebar::hideRequested, this, [this]() {
        setSidebarOpen(false, true);
        config_.set(QStringLiteral("sidebar_open"), false);
    });
}

void MainWindow::wireViews()
{
    connect(chat_, &ChatView::applyEditRequested, this, &MainWindow::applyProposal);
    connect(chat_, &ChatView::projectFilesRequested, this, &MainWindow::openProjectFiles);
    connect(chat_, &ChatView::closeProjectRequested, this, &MainWindow::closeProject);
    connect(chat_, &ChatView::newChatRequested, this, &MainWindow::newChat);
    connect(chat_, &ChatView::settingsRequested, this, &MainWindow::openSettings);
    connect(chat_, &ChatView::imageRequested, this, &MainWindow::showArtifact);
    connect(chat_, &ChatView::saveImageRequested, this, &MainWindow::saveArtifact);
    connect(chat_, &ChatView::regenerateRequested, this, &MainWindow::regenerateReply);
    connect(home_, &HomeView::projectFilesRequested, this, &MainWindow::openProjectFiles);
}

void MainWindow::wireComposers()
{
    const QString style = config_.str(QStringLiteral("response_style"));
    for (Composer *composer : {home_->composer(), chat_->composer()}) {
        composer->setReplyStyle(style);
        connect(composer, &Composer::submitted, this, [this, composer]() { sendFromComposer(composer); });
        connect(composer, &Composer::stopRequested, this, &MainWindow::stopStreaming);
        connect(composer, &Composer::voiceRequested, this, &MainWindow::startVoice);
        connect(composer, &Composer::replyStyleChanged, this, [this](const QString &changed) {
            config_.set(QStringLiteral("response_style"), changed, false);
            config_.save();
            for (Composer *other : {home_->composer(), chat_->composer()})
                other->setReplyStyle(changed);
        });
        connect(composer, &Composer::folderChanged, this, [this](const QString &path) {
            if (!path.isEmpty())
                setWorkspaceFolder(path);
        });
    }
}

void MainWindow::wireBackend()
{
    backend_ = new Backend(&config_, this);
    connect(backend_, &Backend::readyChanged, this, [this](bool ready, const QString &message) {
        updateStatus();
        if (!ready && !message.isEmpty())
            toast(message, QStringLiteral("close"));
    });
    connect(backend_, &Backend::status, this, [this](const QJsonObject &health) { updateStatus(health); });
    connect(backend_, &Backend::imageReady, this, &MainWindow::finishImage);
    connect(backend_, &Backend::imageFailed, this, &MainWindow::failImage);
}

void MainWindow::wireArt()
{
    connect(art_, &ArtPanel::openRequested, this, [this]() {
        const QString path = art_->artifact().value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            return;
        util::openPath(path);
    });
    connect(art_, &ArtPanel::saveRequested, this, [this]() { saveArtifact(art_->artifact()); });
    connect(art_, &ArtPanel::regenerateRequested, this,
            [this]() { regenerateArtifact(art_->artifact()); });
    connect(art_, &ArtPanel::closeRequested, this, [this]() { showArt(false); });
}

// ---------------------------------------------------------------------------
// Panels and views
// ---------------------------------------------------------------------------
void MainWindow::animateSlide(SlideHost *host, QVariantAnimation **animation, int end, int duration)
{
    if (*animation) {
        (*animation)->stop();
        (*animation)->deleteLater();
        *animation = nullptr;
    }
    auto *anim = new QVariantAnimation(this);
    anim->setStartValue(host->width());
    anim->setEndValue(end);
    anim->setDuration(duration);
    anim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(anim, &QVariantAnimation::valueChanged, host,
            [host](const QVariant &value) { host->setSlideWidth(value.toInt()); });
    connect(anim, &QVariantAnimation::finished, this, [animation, anim]() {
        if (*animation == anim)
            *animation = nullptr;
        anim->deleteLater();
    });
    *animation = anim;
    anim->start();
}

void MainWindow::setSidebarOpen(bool open, bool animate)
{
    sidebarOpen_ = open;
    logo_->setVisible(!open);
    if (animate)
        animateSlide(sidebarHost_, &sidebarAnim_, open ? 396 : 0, 240);
    else
        sidebarHost_->setSlideWidth(open ? 396 : 0);
}

void MainWindow::toggleSidebar()
{
    setSidebarOpen(!sidebarOpen_, true);
    config_.set(QStringLiteral("sidebar_open"), sidebarOpen_);
}

void MainWindow::showArt(bool visible, bool silent)
{
    if (visible == artVisible_)
        return;
    if (visible) {
        if (width() < 1120 && !silent) {
            toast(QStringLiteral("Widen the window to keep the art panel open"), QStringLiteral("image"));
            return;
        }
        artVisible_ = true;
        animateSlide(artHost_, &artAnim_, 360, 260);
    } else {
        artVisible_ = false;
        animateSlide(artHost_, &artAnim_, 0, 260);
    }
    if (artButton_)
        artButton_->setColor(artVisible_ ? theme::INK : theme::MUTED);
}

void MainWindow::toggleArt()
{
    if (!artVisible_) {
        const QJsonObject latest = latestImage();
        if (!latest.isEmpty())
            art_->setArtifact(latest);
        else
            art_->setEmpty();
    }
    showArt(!artVisible_);
}

void MainWindow::showHome()
{
    if (artVisible_)
        showArt(false);
    stack_->setCurrentWidget(home_);
    syncHomeProject();
    home_->startFloat();
    home_->composer()->focusInput();
}

void MainWindow::showChat()
{
    home_->stopFloat();
    stack_->setCurrentWidget(chat_);
    syncChatProject();
    chat_->composer()->focusInput();
}

void MainWindow::syncChatProject()
{
    chat_->setProject(hasProject_ ? &project_ : nullptr);
}

void MainWindow::syncHomeProject()
{
    if (hasProject_) {
        home_->setProject(&project_);
    } else if (!workspaceFolder_.isEmpty()) {
        home_->setWorkspaceFolder(workspaceFolder_);
    } else {
        home_->setProject(nullptr);
    }
}

void MainWindow::refreshSidebar()
{
    sidebar_->refresh(sidebar_->query(), hasProject_ ? project_.id : QString(),
                      hasConversation_ ? conversation_.value(QStringLiteral("id")).toString() : QString());
}

void MainWindow::syncBusy()
{
    const bool busyNow = busy();
    const QString label = streaming_ ? QStringLiteral("Thinking") : QStringLiteral("Painting");
    for (Composer *composer : {home_->composer(), chat_->composer()}) {
        composer->setThinking(busyNow, label);
        composer->setStopVisible(streaming_);
    }
    home_->setMascotThinking(streaming_);
}

void MainWindow::updateStatus(const QJsonObject &health)
{
    Q_UNUSED(health);
    const QString provider = config_.str(QStringLiteral("provider")).isEmpty()
                                 ? QStringLiteral("local")
                                 : config_.str(QStringLiteral("provider"));
    if (!backend_->ready()) {
        sidebar_->setStatus(QStringLiteral("Local engine · starting"), theme::FAINT, true);
        return;
    }
    if (provider == QLatin1String("local")) {
        sidebar_->setStatus(QStringLiteral("Built-in AI · ") + localModelName(config_.modelFor(provider)),
                            theme::SUCCESS, true);
        return;
    }
    const QColor color = config_.apiKey(provider).isEmpty() ? theme::MUTED : theme::SUCCESS;
    sidebar_->setStatus(config_.providerLabel(provider) + QStringLiteral(" · ")
                            + util::shorten(config_.modelFor(provider), 22),
                        color, color == theme::SUCCESS);
}

void MainWindow::toast(const QString &text, const QString &icon)
{
    auto *widget = new Toast(this, text, icon);
    widget->popup(QPoint(width() / 2, 78));
}

void MainWindow::startVoice()
{
    toast(QStringLiteral("Voice input isn't in the native build yet"), QStringLiteral("mic"));
}

QString MainWindow::conversationId() const
{
    return conversation_.value(QStringLiteral("id")).toString();
}

// ---------------------------------------------------------------------------
// Conversations
// ---------------------------------------------------------------------------
void MainWindow::newChat()
{
    if (streaming_) {
        toast(QStringLiteral("Stop the current reply first"), QStringLiteral("stop"));
        return;
    }
    hasConversation_ = false;
    conversation_ = QJsonObject();
    config_.set(QStringLiteral("last_conversation"), QString());
    chat_->clear();
    showHome();
    refreshSidebar();
}

void MainWindow::openProject(const QString &id)
{
    if (streaming_) {
        toast(QStringLiteral("Stop the current reply first"), QStringLiteral("stop"));
        return;
    }
    ProjectSummary meta;
    if (!projects_.get(id, &meta)) {
        refreshSidebar();
        return;
    }
    hasProject_ = true;
    project_ = meta;
    config_.set(QStringLiteral("last_project"), id);

    const QJsonObject latest = conversations_.latestForProject(id);
    if (!latest.isEmpty() && !latest.value(QStringLiteral("messages")).toArray().isEmpty()) {
        hasConversation_ = true;
        conversation_ = latest;
        config_.set(QStringLiteral("last_conversation"), conversationId());
        showChat();
        chat_->refresh(conversation_);
        syncChatProject();
        chat_->scrollToBottom();
    } else {
        hasConversation_ = false;
        conversation_ = QJsonObject();
        config_.set(QStringLiteral("last_conversation"), QString());
        chat_->clear();
        showHome();
    }
    syncHomeProject();
    refreshSidebar();
}

void MainWindow::loadConversation(const QString &id)
{
    if (streaming_) {
        toast(QStringLiteral("Stop the current reply first"), QStringLiteral("stop"));
        return;
    }
    const QJsonObject conv = conversations_.get(id);
    if (conv.isEmpty()) {
        toast(QStringLiteral("That chat is no longer here"), QStringLiteral("close"));
        return;
    }
    hasConversation_ = true;
    conversation_ = conv;
    config_.set(QStringLiteral("last_conversation"), id);

    const QString pid = conv.value(QStringLiteral("project_id")).toString();
    ProjectSummary stored;
    if (!pid.isEmpty() && projects_.get(pid, &stored)) {
        hasProject_ = true;
        project_ = stored;
        config_.set(QStringLiteral("last_project"), pid);
    } else {
        hasProject_ = false;
        project_ = ProjectSummary();
    }
    if (!conv.value(QStringLiteral("messages")).toArray().isEmpty()) {
        showChat();
        chat_->refresh(conversation_);
        syncChatProject();
        chat_->scrollToBottom();
    } else {
        showHome();
    }
    syncHomeProject();
    refreshSidebar();
}

void MainWindow::closeProject()
{
    hasProject_ = false;
    project_ = ProjectSummary();
    config_.set(QStringLiteral("last_project"), QString());
    hasConversation_ = false;
    conversation_ = QJsonObject();
    config_.set(QStringLiteral("last_conversation"), QString());
    chat_->clear();
    showHome();
    syncHomeProject();
    refreshSidebar();
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------
void MainWindow::sendFromComposer(Composer *composer)
{
    if (busy()) {
        toast(QStringLiteral("Takoyo is still working — stop it first"), QStringLiteral("stop"));
        return;
    }
    const QString text = composer->text().trimmed();
    const QJsonObject attachment = composer->attachment();
    if (text.isEmpty() && attachment.isEmpty())
        return;
    if (attachment.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
        const QString provider = config_.str(QStringLiteral("provider")).isEmpty()
                                     ? QStringLiteral("local")
                                     : config_.str(QStringLiteral("provider"));
        // The built-in engine and Ollama read images through on-device OCR on the
        // Python side, so only hosted providers need a key here.
        const bool hosted = provider == QLatin1String("openai") || provider == QLatin1String("anthropic")
                            || provider == QLatin1String("google");
        if (hosted && config_.apiKey(provider).isEmpty()) {
            toast(QStringLiteral("Add your %1 API key in Settings so Takoyo can read images")
                      .arg(config_.providerLabel(provider)),
                  QStringLiteral("settings"));
            return;
        }
    }
    composer->clear();

    QString display = text;
    if (display.isEmpty() && !attachment.isEmpty()) {
        const QString name = attachment.value(QStringLiteral("name")).toString();
        display = attachment.value(QStringLiteral("kind")).toString() == QLatin1String("image")
                      ? QStringLiteral("Please look at this image: %1").arg(name)
                      : QStringLiteral("Attached ") + name;
    }

    if (!hasConversation_) {
        QJsonObject conv = conversations_.create(display.isEmpty() ? QStringLiteral("New chat") : display,
                                                 hasProject_ ? project_.id : QString(), display);
        if (conv.isEmpty()) {
            toast(QStringLiteral("Could not save this chat"), QStringLiteral("close"));
            return;
        }
        conversation_ = conv;
        hasConversation_ = true;
        config_.set(QStringLiteral("last_conversation"), conversationId());
    }

    QJsonObject extra;
    if (!attachment.isEmpty())
        extra.insert(QStringLiteral("attachment"), attachment);
    QJsonObject conv = conversations_.append(conversationId(), QStringLiteral("user"), display, extra);
    if (conv.isEmpty()) {
        toast(QStringLiteral("Could not save this chat"), QStringLiteral("close"));
        return;
    }
    conversation_ = conv;

    const QString projectId = conv.value(QStringLiteral("project_id")).toString();
    if (!hasProject_ && !projectId.isEmpty()) {
        ProjectSummary stored;
        if (projects_.get(projectId, &stored)) {
            hasProject_ = true;
            project_ = stored;
        }
    }

    showChat();
    chat_->refresh(conversation_);
    syncChatProject();
    chat_->scrollToBottom();
    syncHomeProject();
    refreshSidebar();

    const QString imagePrompt = util::imageRequestPrompt(text);
    if (!imagePrompt.isEmpty()) {
        startDirectImage(conversationId(), imagePrompt);
        return;
    }

    QList<util::FileRequest> mentioned;
    for (const QString &path : mentionedFilePaths(text)) {
        bool seen = false;
        for (const QJsonValue &value : conversation_.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject message = value.toObject();
            if (message.value(QStringLiteral("hidden")).toBool()
                && message.value(QStringLiteral("content")).toString().contains(path)) {
                seen = true;
                break;
            }
        }
        if (!seen)
            mentioned.append(util::FileRequest{path, false});
    }
    if (!mentioned.isEmpty()) {
        const QString fileContext = readRequestedFiles(mentioned);
        if (!fileContext.isEmpty()) {
            QJsonObject hiddenExtra;
            hiddenExtra.insert(QStringLiteral("hidden"), true);
            const QJsonObject withFiles =
                conversations_.append(conversationId(), QStringLiteral("user"), fileContext, hiddenExtra);
            if (!withFiles.isEmpty())
                conversation_ = withFiles;
        }
    }

    const int index = conversation_.value(QStringLiteral("messages")).toArray().size();
    const QJsonArray messages = chatRequest(text, conversation_);

    QJsonObject assistant = conversations_.append(conversationId(), QStringLiteral("assistant"), QString());
    if (assistant.isEmpty()) {
        toast(QStringLiteral("Could not save this chat"), QStringLiteral("close"));
        return;
    }
    conversation_ = assistant;

    readRounds_ = 0;
    promiseRetries_ = 0;
    beginReply(conversationId(), index, messages);
}

void MainWindow::beginReply(const QString &cid, int index, const QJsonArray &messages)
{
    streamCid_ = cid;
    streamIndex_ = index;
    streaming_ = true;
    chat_->startStream();
    syncBusy();

    const QString provider = config_.str(QStringLiteral("provider")).isEmpty()
                                 ? QStringLiteral("local")
                                 : config_.str(QStringLiteral("provider"));
    int maxTokens = config_.integer(QStringLiteral("max_tokens"));
    if (provider == QLatin1String("local")) {
        // llama-server rejects a request when prompt tokens plus the requested
        // reply exceed the context, so leave room for the prompt estimate.
        const int context = config_.integer(QStringLiteral("local_context")) > 0
                                ? config_.integer(QStringLiteral("local_context"))
                                : 8192;
        int promptTokens = 0;
        for (const QJsonValue &value : messages) {
            const QJsonObject message = value.toObject();
            const QJsonValue content = message.value(QStringLiteral("content"));
            if (content.isArray()) {
                for (const QJsonValue &partValue : content.toArray()) {
                    const QJsonObject part = partValue.toObject();
                    if (part.value(QStringLiteral("type")).toString() == QLatin1String("image_url"))
                        promptTokens += 800;
                    else
                        promptTokens += util::estimateTokens(part.value(QStringLiteral("text")).toString()) + 8;
                }
            } else {
                promptTokens += util::estimateTokens(content.toString()) + 8;
            }
        }
        promptTokens = static_cast<int>(promptTokens * 1.15) + 64;
        maxTokens = qMax(128, qMin(maxTokens, context - promptTokens));
    }
    stream_ = backend_->chat(messages, provider, config_.modelFor(provider),
                             config_.number(QStringLiteral("temperature")),
                             maxTokens);

    connect(stream_, &ChatStream::chunk, this, [this, cid](const QString &piece) {
        if (hasConversation_ && conversationId() == cid)
            chat_->appendStream(piece);
    });
    connect(stream_, &ChatStream::finished, this, [this, cid, index](const QString &full, bool cancelled) {
        if (stream_) {
            stream_->deleteLater();
            stream_ = nullptr;
        }
        finishReply(cid, index, full, cancelled);
    });
    connect(stream_, &ChatStream::failed, this, [this, cid, index](const QString &message) {
        if (stream_) {
            stream_->deleteLater();
            stream_ = nullptr;
        }
        finishError(cid, index, message);
    });
}

QJsonArray MainWindow::chatRequest(const QString &prompt, const QJsonObject &conversation) const
{
    const QJsonObject convo = conversation.isEmpty() ? conversation_ : conversation;
    const QString provider = config_.str(QStringLiteral("provider")).isEmpty()
                                 ? QStringLiteral("local")
                                 : config_.str(QStringLiteral("provider"));
    const bool smallContext = provider == QLatin1String("local");
    const QString system = systemPrompt(projects_, hasProject_, project_, workspaceFolder_, prompt,
                                        smallContext ? 6000 : 22000,
                                        config_.str(QStringLiteral("response_style")), smallContext);
    const QJsonArray history = convo.value(QStringLiteral("messages")).toArray();
    if (smallContext) {
        const int context = config_.integer(QStringLiteral("local_context"));
        const int requested = config_.integer(QStringLiteral("max_tokens"));
        const int budget = util::promptBudgetTokens(context, requested, util::estimateTokens(system));
        return buildChatMessages(system, history, budget, budget);
    }
    return buildChatMessages(system, history, 4000, 16000);
}

void MainWindow::stopStreaming()
{
    if (streaming_ && stream_) {
        stream_->cancel();
        toast(QStringLiteral("Stopping…"), QStringLiteral("stop"));
    }
}

void MainWindow::regenerateReply()
{
    if (streaming_ || imageJobs_ > 0 || !hasConversation_)
        return;
    const QString cid = conversationId();
    QJsonObject conv = conversations_.get(cid);
    QJsonArray messages = conv.value(QStringLiteral("messages")).toArray();

    int target = -1;
    for (int i = messages.size() - 1; i >= 0; --i) {
        const QJsonObject message = messages.at(i).toObject();
        if (message.value(QStringLiteral("hidden")).toBool())
            continue;
        if (message.value(QStringLiteral("role")).toString() == QLatin1String("assistant")) {
            target = i;
            break;
        }
    }
    if (target < 0)
        return;

    // Only the newest reply can be regenerated; a later user turn would be lost.
    bool hasUserAfter = false;
    for (int i = target + 1; i < messages.size(); ++i) {
        const QJsonObject message = messages.at(i).toObject();
        if (!message.value(QStringLiteral("hidden")).toBool()
            && message.value(QStringLiteral("role")).toString() == QLatin1String("user")) {
            hasUserAfter = true;
            break;
        }
    }
    if (hasUserAfter)
        return;

    QJsonArray kept;
    bool hasUserBefore = false;
    for (int i = 0; i < target; ++i) {
        const QJsonObject message = messages.at(i).toObject();
        if (!message.value(QStringLiteral("hidden")).toBool()
            && message.value(QStringLiteral("role")).toString() == QLatin1String("user"))
            hasUserBefore = true;
        kept.append(message);
    }
    if (!hasUserBefore)
        return;

    QJsonObject requestConv = conv;
    requestConv.insert(QStringLiteral("messages"), kept);
    const QJsonArray request = chatRequest(lastUserText(cid), requestConv);

    conv = conversations_.replaceMessages(cid, kept);
    if (conv.isEmpty())
        return;
    const int index = kept.size();
    QJsonObject assistant = conversations_.append(cid, QStringLiteral("assistant"), QString());
    if (assistant.isEmpty())
        return;

    conversation_ = assistant;
    readRounds_ = 0;
    promiseRetries_ = 0;
    chat_->refresh(conversation_);
    syncChatProject();
    chat_->scrollToBottom();
    beginReply(cid, index, request);
}

QString MainWindow::lastUserText(const QString &cid) const
{
    const QJsonArray messages = conversations_.get(cid).value(QStringLiteral("messages")).toArray();
    for (int i = messages.size() - 1; i >= 0; --i) {
        const QJsonObject message = messages.at(i).toObject();
        if (message.value(QStringLiteral("role")).toString() != QLatin1String("user"))
            continue;
        if (message.value(QStringLiteral("hidden")).toBool())
            continue;
        QString text = message.value(QStringLiteral("content")).toString();
        const QJsonObject attachment = message.value(QStringLiteral("attachment")).toObject();
        if (!attachment.isEmpty()) {
            const QString name = attachment.value(QStringLiteral("name")).toString();
            const QString body = attachment.value(QStringLiteral("content")).toString();
            if (!body.isEmpty())
                text += attachmentNote(name, body);
            else if (!name.isEmpty())
                text += QStringLiteral("\n\nThe user attached a file at %1.")
                            .arg(attachment.value(QStringLiteral("path")).toString());
        }
        return text;
    }
    return QString();
}

QString MainWindow::readRequestedFiles(const QList<util::FileRequest> &requests) const
{
    const QString root = hasProject_ ? QDir(util::projectsDir()).filePath(project_.id)
                                     : workspaceFolder_;
    QStringList chunks;
    int budget = 48000;
    for (const util::FileRequest &request : requests) {
        if (chunks.size() >= 4 || budget <= 0)
            break;
        const QString raw = request.path.trimmed();
        if (raw.isEmpty())
            continue;
        QFileInfo info(raw);
        if (!info.isAbsolute() && !root.isEmpty())
            info = QFileInfo(QDir(root).filePath(raw));
        const QString absolute = info.absoluteFilePath();
        if (request.listing) {
            const QDir dir(absolute);
            if (!dir.exists()) {
                chunks << QStringLiteral("[Takoyo could not list %1: no such folder]").arg(absolute);
                continue;
            }
            QStringList lines;
            const QFileInfoList entries =
                dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &entry : entries) {
                if (lines.size() >= 200)
                    break;
                lines << entry.fileName() + (entry.isDir() ? QStringLiteral("/") : QString());
            }
            const QString listing = lines.isEmpty() ? QStringLiteral("(empty folder)") : lines.join(QChar('\n'));
            chunks << QStringLiteral("[Takoyo listed %1]\n%2").arg(absolute, listing);
            budget -= listing.size() + 80;
            continue;
        }
        if (!info.isFile()) {
            chunks << QStringLiteral("[Takoyo could not open %1: no such file. "
                                     "Check the path and try again, or ask the user where it lives.]")
                          .arg(absolute);
            continue;
        }
        if (info.size() > 512 * 1024) {
            chunks << QStringLiteral("[%1 is too large for Takoyo to read (%2 MB)]")
                          .arg(absolute)
                          .arg(QString::number(info.size() / (1024.0 * 1024.0), 'f', 1));
            continue;
        }
        QFile file(absolute);
        if (!file.open(QIODevice::ReadOnly)) {
            chunks << QStringLiteral("[Takoyo could not open %1: permission denied]").arg(absolute);
            continue;
        }
        const QByteArray data = file.read(200000);
        file.close();
        QString text = QString::fromUtf8(data);
        if (text.contains(QChar::ReplacementCharacter) || text.contains(QChar('\0'))) {
            chunks << QStringLiteral("[%1 is a binary file, so Takoyo cannot read it as text]").arg(absolute);
            continue;
        }
        if (text.size() > budget) {
            text = text.left(budget);
            text += QStringLiteral("\n… (truncated to fit the model's context)");
        }
        chunks << QStringLiteral("[Contents of %1]\n```\n%2\n```").arg(absolute, text);
        budget -= text.size() + 80;
    }
    if (chunks.isEmpty())
        return QString();
    const QString header = QStringLiteral("Takoyo read these files for you. Use them to answer the user's "
                                          "request and do not claim you cannot access files.\n\n");
    return header + chunks.join(QStringLiteral("\n\n"));
}

QStringList MainWindow::mentionedFilePaths(const QString &text) const
{
    QString root;
    QStringList projectFiles;
    if (hasProject_) {
        root = QDir(util::projectsDir()).filePath(project_.id);
        projectFiles = projects_.filePaths(project_.id);
    }

    const auto clean = [](QString value) {
        value = value.trimmed();
        while (!value.isEmpty() && QStringLiteral(".,;:!?…»”’").contains(value.back()))
            value.chop(1);
        return value;
    };
    const auto resolve = [&root](const QString &candidate, QFileInfo *info) {
        if (candidate.isEmpty())
            return false;
        QFileInfo file(candidate);
        if (!file.isAbsolute() && !root.isEmpty())
            file = QFileInfo(QDir(root).filePath(candidate));
        if (!file.isFile())
            return false;
        *info = file;
        return true;
    };

    QStringList found;
    const auto add = [&found](const QFileInfo &info) {
        const QString absolute = info.absoluteFilePath();
        if (!found.contains(absolute) && found.size() < 3)
            found << absolute;
    };

    static const QRegularExpression quoted(QStringLiteral("\"([^\"]{2,300})\"|'([^']{2,300})'"));
    for (auto it = quoted.globalMatch(text); it.hasNext() && found.size() < 3;) {
        const QRegularExpressionMatch match = it.next();
        QFileInfo info;
        if (resolve(clean(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)), &info))
            add(info);
    }

    static const QRegularExpression separators(QStringLiteral("[\\s,;()\\[\\]{}<>|]+"));
    const QStringList tokens = text.split(separators, Qt::SkipEmptyParts);
    for (int i = 0; i < tokens.size() && found.size() < 3; ++i) {
        QString candidate = clean(tokens.at(i));
        if (candidate.size() < 3)
            continue;
        const bool looksLikePath =
            candidate.contains(QLatin1Char('/')) || candidate.contains(QLatin1Char('\\'))
            || projectFiles.contains(candidate);
        if (!looksLikePath)
            continue;
        QFileInfo info;
        if (resolve(candidate, &info)) {
            add(info);
            continue;
        }
        // The path may contain spaces, so keep gluing on the following words.
        for (int extra = 1; extra <= 6 && i + extra < tokens.size(); ++extra) {
            candidate += QLatin1Char(' ') + tokens.at(i + extra);
            if (resolve(clean(candidate), &info)) {
                add(info);
                break;
            }
        }
    }
    return found;
}

void MainWindow::followUpWithFiles(const QString &cid, const QList<util::FileRequest> &requests)
{
    const QString context = readRequestedFiles(requests);
    if (context.isEmpty())
        return;
    ++readRounds_;

    QJsonObject extra;
    extra.insert(QStringLiteral("hidden"), true);
    QJsonObject conv = conversations_.append(cid, QStringLiteral("user"), context, extra);
    if (conv.isEmpty())
        return;

    QString prompt = lastUserText(cid);
    if (prompt.trimmed().isEmpty())
        prompt = QStringLiteral("Continue");
    const QJsonArray messages = chatRequest(prompt, conv);

    conv = conversations_.append(cid, QStringLiteral("assistant"), QString());
    if (conv.isEmpty())
        return;
    const int index = conv.value(QStringLiteral("messages")).toArray().size() - 1;
    if (hasConversation_ && conversationId() == cid) {
        conversation_ = conv;
        chat_->refresh(conversation_);
        syncChatProject();
        chat_->scrollToBottom();
    }
    beginReply(cid, index, messages);
}

void MainWindow::finishReply(const QString &cid, int index, const QString &full, bool cancelled)
{
    QJsonObject conv = conversations_.get(cid);
    const QJsonArray before = conv.value(QStringLiteral("messages")).toArray();
    if (conv.isEmpty() || index < 0 || index >= before.size()) {
        streaming_ = false;
        syncBusy();
        return;
    }

    util::ParsedReply parsed = util::parseReply(full);

    // Inspect the user's request so a stray image tag on a code/file request
    // (e.g. "create this code") can be suppressed.
    bool imageTurn = false;
    bool documentTurn = false;
    QString attachedName;
    QString userText;
    for (int i = index - 1; i >= 0; --i) {
        const QJsonObject message = before.at(i).toObject();
        if (message.value(QStringLiteral("role")).toString() != QLatin1String("user")
            || message.value(QStringLiteral("hidden")).toBool())
            continue;
        const QJsonObject attachment = message.value(QStringLiteral("attachment")).toObject();
        imageTurn = attachment.value(QStringLiteral("kind")).toString() == QLatin1String("image");
        documentTurn = attachment.value(QStringLiteral("kind")).toString() == QLatin1String("text");
        attachedName = attachment.value(QStringLiteral("name")).toString();
        userText = message.value(QStringLiteral("content")).toString();
        break;
    }
    const QString userLower = userText.toLower();
    static const QRegularExpression visualNoun(
        QStringLiteral("\\b(image|images|picture|pictures|photo|photos|drawing|drawings|draw|draws|"
                       "painting|paintings|paint|paints|illustration|illustrations|illustrate|"
                       "logo|logos|poster|posters|wallpaper|wallpapers|icon|icons|"
                       "artwork|art|sketch|sketches|render|renders|visual|visuals|diagram|diagrams)\\b"));
    static const QRegularExpression buildVerb(
        QStringLiteral("\\b(code|script|program|function|class|file|files|app|application|website|web page|"
                       "build|create|write|implement|refactor|fix|debug|compile|generate)\\b"));
    const bool asksVisual = visualNoun.match(userLower).hasMatch();
    const bool asksBuild = buildVerb.match(userLower).hasMatch();
    const bool wantsVisualOutput = asksVisual || (imageTurn && !asksBuild);
    if (!wantsVisualOutput)
        parsed.imagePrompts.clear();

    QString cleaned = parsed.cleaned;
    if (cleaned.trimmed().isEmpty()) {
        if (!parsed.imagePrompts.isEmpty())
            cleaned = QStringLiteral("Made you an image of **%1**.").arg(parsed.imagePrompts.first());
        else if (!parsed.edits.isEmpty()) {
            const bool autoApplied =
                config_.flag(QStringLiteral("auto_apply_edits"))
                && (!conv.value(QStringLiteral("project_id")).toString().isEmpty()
                    || !workspaceFolder_.isEmpty());
            cleaned = autoApplied
                          ? QStringLiteral("Done — I wrote the file to disk.")
                          : QStringLiteral("I've prepared the change — apply it when you're ready.");
        }
        else if (cancelled)
            cleaned = QStringLiteral("_Stopped._");
    }

    QJsonArray editArray;
    for (const util::ToolEdit &edit : parsed.edits) {
        QJsonObject object;
        object.insert(QStringLiteral("path"), edit.path);
        object.insert(QStringLiteral("content"), edit.content);
        object.insert(QStringLiteral("applied"), edit.applied);
        editArray.append(object);
    }

    QJsonObject fields;
    fields.insert(QStringLiteral("content"), cleaned);
    fields.insert(QStringLiteral("edits"), editArray);
    fields.insert(QStringLiteral("images"),
                  before.at(index).toObject().value(QStringLiteral("images")).toArray());
    conv = conversations_.updateMessage(cid, index, fields);

    const QString pid = conv.value(QStringLiteral("project_id")).toString();
    const bool autoApply = config_.flag(QStringLiteral("auto_apply_edits"));
    // A selected folder counts as a project here: with auto-apply on, edits land
    // on disk immediately instead of waiting for the Apply button.
    if (!parsed.edits.isEmpty() && (!pid.isEmpty() || !workspaceFolder_.isEmpty()) && autoApply) {
        QJsonArray applied;
        for (const util::ToolEdit &edit : parsed.edits) {
            const bool wrote = pid.isEmpty() ? writeWorkspaceFile(edit.path, edit.content)
                                             : writeProjectFile(edit.path, edit.content, pid);
            QJsonObject object;
            object.insert(QStringLiteral("path"), edit.path);
            object.insert(QStringLiteral("content"), edit.content);
            object.insert(QStringLiteral("applied"), wrote);
            applied.append(object);
        }
        QJsonObject appliedFields;
        appliedFields.insert(QStringLiteral("edits"), applied);
        conv = conversations_.updateMessage(cid, index, appliedFields);
        if (pid.isEmpty() && !workspaceFolder_.isEmpty()) {
            syncHomeProject();
            refreshSidebar();
        }
    }

    streaming_ = false;
    streamCid_.clear();
    streamIndex_ = -1;

    if (hasConversation_ && conversationId() == cid) {
        conversation_ = conv;
        chat_->refresh(conversation_);
        syncChatProject();
        chat_->scrollToBottom();
    }

    QList<util::FileRequest> requests = parsed.fileRequests;
    if (imageTurn) {
        // The attached image was already read on-device and its text is in the
        // prompt, so a read_file call for it can only fail and poison the next
        // reply with "could not open the image".
        requests.erase(std::remove_if(requests.begin(), requests.end(),
                                      [](const util::FileRequest &request) {
                                          return isImagePath(request.path);
                                      }),
                       requests.end());
    }
    if ((imageTurn || documentTurn) && !attachedName.isEmpty()) {
        // The document text is already in the prompt; re-reading the same file
        // just duplicates it and can overflow the local model's context.
        requests.erase(std::remove_if(requests.begin(), requests.end(),
                                      [&attachedName](const util::FileRequest &request) {
                                          const QString name = QFileInfo(request.path).fileName();
                                          return !name.isEmpty()
                                                 && name.compare(attachedName, Qt::CaseInsensitive) == 0;
                                      }),
                       requests.end());
    }
    const bool imageToolDropped = requests.size() != parsed.fileRequests.size();
    const bool attachmentTurn = imageTurn || documentTurn;
    const bool promiseLike =
        cleaned.trimmed().isEmpty()
            ? imageToolDropped
            : isDeferredPromise(cleaned) || (attachmentTurn && isImageConfusion(cleaned));

    if (!cancelled && parsed.edits.isEmpty() && parsed.imagePrompts.isEmpty() && requests.isEmpty()
        && promiseRetries_ < 2 && promiseLike) {
        QJsonObject retryConv = conversations_.get(cid);
        QJsonArray history = retryConv.value(QStringLiteral("messages")).toArray();
        if (index >= 0 && index < history.size())
            history.removeAt(index);
        const QString correction =
            imageTurn ? QStringLiteral(
                            "\n\nThe attached image was already read on this device. Its text is in "
                            "this message. Answer my request now using that text, and do not call "
                            "read_file, do not ask for the image path and do not say the image is "
                            "still being processed.")
                      : documentTurn ? QStringLiteral(
                                           "\n\nThe attached file \"%1\" was already opened on this "
                                           "device and its text is in this message above. Answer my "
                                           "request now using that text. Do not ask for the path or "
                                           "name, do not ask me to paste or re-upload it, and do not "
                                           "call read_file on it.")
                                           .arg(attachedName)
                      : QStringLiteral(
                            "\n\nAnswer directly now. Do not fetch, read or search for anything, and "
                            "do not say you will do something later. Give the best answer you know; "
                            "if it is uncertain, say so in one sentence.");
        bool appended = false;
        for (int i = history.size() - 1; i >= 0; --i) {
            QJsonObject message = history.at(i).toObject();
            if (message.value(QStringLiteral("role")).toString() != QLatin1String("user"))
                continue;
            message.insert(QStringLiteral("content"),
                           message.value(QStringLiteral("content")).toString() + correction);
            history.replace(i, message);
            appended = true;
            break;
        }
        if (!appended) {
            QJsonObject message;
            message.insert(QStringLiteral("role"), QStringLiteral("user"));
            message.insert(QStringLiteral("content"), correction.trimmed());
            history.append(message);
        }
        retryConv.insert(QStringLiteral("messages"), history);
        const QJsonArray retryMessages = chatRequest(lastUserText(cid), retryConv);
        QJsonObject blank;
        blank.insert(QStringLiteral("content"), QString());
        const QJsonObject cleared = conversations_.updateMessage(cid, index, blank);
        if (!cleared.isEmpty()) {
            ++promiseRetries_;
            conversation_ = cleared;
            if (hasConversation_ && conversationId() == cid) {
                chat_->refresh(conversation_);
                chat_->scrollToBottom();
            }
            beginReply(cid, index, retryMessages);
            return;
        }
    }

    if (!cancelled && !requests.isEmpty() && readRounds_ < 3) {
        followUpWithFiles(cid, requests);
        if (streaming_) {
            syncBusy();
            refreshSidebar();
            return;
        }
    }

    if (!parsed.imagePrompts.isEmpty()) {
        art_->setGenerating(parsed.imagePrompts.first());
        showArt(true, true);
        const int count = qMin(3, parsed.imagePrompts.size());
        for (int i = 0; i < count; ++i)
            beginImage(parsed.imagePrompts.at(i), cid, index, false);
    }
    if (!cancelled && cleaned.trimmed().isEmpty()) {
        finishError(cid, index,
                    documentTurn
                        ? QStringLiteral("\"%1\" is too long for the built-in model, so the reply came back empty. "
                                         "Try a shorter file or ask about one section at a time.")
                              .arg(attachedName)
                        : QStringLiteral("The model returned an empty reply. Try again, or restart Takoyo."));
        return;
    }
    if (!cancelled)
        home_->celebrate();
    syncBusy();
    refreshSidebar();
}

void MainWindow::finishError(const QString &cid, int index, const QString &message)
{
    QString friendly = message;
    if (friendly.contains(QLatin1String("exceeds the available context"), Qt::CaseInsensitive)
        || friendly.contains(QLatin1String("context size"), Qt::CaseInsensitive))
        friendly = QStringLiteral(
            "That request was too long for the built-in model's 8K context. Shorten the message or the "
            "attached file and try again.");
    QJsonObject conv = conversations_.get(cid);
    if (!conv.isEmpty() && index >= 0 && index < conv.value(QStringLiteral("messages")).toArray().size()) {
        const QString provider = config_.str(QStringLiteral("provider"));
        QJsonObject fields;
        fields.insert(QStringLiteral("content"), QString());
        fields.insert(QStringLiteral("error"), friendly);
        fields.insert(QStringLiteral("error_kind"),
                      provider == QLatin1String("local") ? QStringLiteral("local") : QStringLiteral("provider"));
        conv = conversations_.updateMessage(cid, index, fields);
        if (hasConversation_ && conversationId() == cid) {
            conversation_ = conv;
            chat_->refresh(conversation_);
            syncChatProject();
            chat_->scrollToBottom();
        }
    }
    streaming_ = false;
    streamCid_.clear();
    streamIndex_ = -1;
    syncBusy();
    toast(QStringLiteral("Takoyo hit a snag"), QStringLiteral("close"));
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------
void MainWindow::startDirectImage(const QString &cid, const QString &prompt)
{
    QJsonObject conv = conversations_.append(cid, QStringLiteral("assistant"), QStringLiteral("Painting…"));
    if (conv.isEmpty())
        return;
    const int index = conv.value(QStringLiteral("messages")).toArray().size() - 1;
    conversation_ = conv;
    chat_->refresh(conversation_);
    syncChatProject();
    chat_->scrollToBottom();
    art_->setGenerating(prompt);
    showArt(true, true);
    beginImage(prompt, cid, index, true);
}

void MainWindow::beginImage(const QString &prompt, const QString &cid, int index, bool direct)
{
    ++imageJobs_;
    syncBusy();
    QJsonObject context;
    context.insert(QStringLiteral("cid"), cid);
    context.insert(QStringLiteral("index"), index);
    context.insert(QStringLiteral("direct"), direct);
    context.insert(QStringLiteral("prompt"), prompt);
    backend_->generateImage(prompt, config_.str(QStringLiteral("image_size")),
                            config_.str(QStringLiteral("image_provider")), context);
}

void MainWindow::finishImage(const QJsonObject &meta, const QJsonObject &context)
{
    imageJobs_ = qMax(0, imageJobs_ - 1);
    const QString cid = context.value(QStringLiteral("cid")).toString();
    const int index = context.value(QStringLiteral("index")).toInt(-1);
    const bool direct = context.value(QStringLiteral("direct")).toBool();
    const QString prompt = context.value(QStringLiteral("prompt")).toString();

    if (!cid.isEmpty()) {
        QJsonObject conv = conversations_.get(cid);
        const QJsonArray messages = conv.value(QStringLiteral("messages")).toArray();
        if (index >= 0 && index < messages.size()) {
            QJsonArray images = messages.at(index).toObject().value(QStringLiteral("images")).toArray();
            images.append(meta);
            QJsonObject fields;
            fields.insert(QStringLiteral("images"), images);
            fields.insert(QStringLiteral("error"), QJsonValue::Null);
            if (direct) {
                QString label = meta.value(QStringLiteral("label")).toString();
                if (label.isEmpty())
                    label = prompt;
                fields.insert(QStringLiteral("content"),
                              QStringLiteral("Here’s **%1** — painted with Takoyo Art. "
                                             "Click the image to open it full size.")
                                  .arg(util::shorten(label, 80)));
            }
            conv = conversations_.updateMessage(cid, index, fields);
            if (hasConversation_ && conversationId() == cid) {
                conversation_ = conv;
                chat_->refresh(conversation_);
                syncChatProject();
                chat_->scrollToBottom();
            }
        }
    }

    art_->setArtifact(meta);
    showArt(true, true);
    home_->celebrate();
    syncBusy();
    toast(QStringLiteral("Image ready"), QStringLiteral("image"));
    refreshSidebar();
}

void MainWindow::failImage(const QString &message, const QJsonObject &context)
{
    imageJobs_ = qMax(0, imageJobs_ - 1);
    const QString cid = context.value(QStringLiteral("cid")).toString();
    const int index = context.value(QStringLiteral("index")).toInt(-1);
    const bool direct = context.value(QStringLiteral("direct")).toBool();

    if (!cid.isEmpty()) {
        QJsonObject conv = conversations_.get(cid);
        const QJsonArray messages = conv.value(QStringLiteral("messages")).toArray();
        if (index >= 0 && index < messages.size()) {
            QJsonObject fields;
            fields.insert(QStringLiteral("error"), message);
            fields.insert(QStringLiteral("error_kind"), QStringLiteral("image"));
            if (direct)
                fields.insert(QStringLiteral("content"), QString());
            conv = conversations_.updateMessage(cid, index, fields);
            if (hasConversation_ && conversationId() == cid) {
                conversation_ = conv;
                chat_->refresh(conversation_);
                syncChatProject();
            }
        }
    }

    art_->finishGenerating();
    if (!artVisible_)
        showArt(false);
    syncBusy();
    toast(QStringLiteral("Image generation failed"), QStringLiteral("close"));
}

QJsonObject MainWindow::latestImage() const
{
    const QDir dir(util::imagesDir());
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Time);
    for (const QFileInfo &file : files) {
        QJsonObject meta = util::readJson(dir.filePath(file.completeBaseName() + QStringLiteral(".json")));
        if (meta.value(QStringLiteral("path")).toString().isEmpty())
            meta.insert(QStringLiteral("path"), file.absoluteFilePath());
        if (meta.value(QStringLiteral("prompt")).toString().isEmpty())
            meta.insert(QStringLiteral("prompt"), file.completeBaseName());
        return meta;
    }
    return QJsonObject();
}

void MainWindow::showArtifact(const QJsonObject &meta)
{
    if (meta.isEmpty())
        return;
    art_->setArtifact(meta);
    showArt(true);
}

void MainWindow::saveArtifact(const QJsonObject &meta)
{
    const QString path = meta.value(QStringLiteral("path")).toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        toast(QStringLiteral("That image is missing"), QStringLiteral("close"));
        return;
    }
    const QString target = QFileDialog::getSaveFileName(this, QStringLiteral("Save image"), path,
                                                        QStringLiteral("PNG image (*.png);;All files (*.*)"));
    if (target.isEmpty())
        return;
    if (QFileInfo::exists(target))
        QFile::remove(target);
    if (QFile::copy(path, target))
        toast(QStringLiteral("Image saved"));
    else
        toast(QStringLiteral("Could not save that image"), QStringLiteral("close"));
}

void MainWindow::regenerateArtifact(const QJsonObject &meta)
{
    const QString prompt = meta.value(QStringLiteral("prompt")).toString();
    if (prompt.isEmpty())
        return;
    art_->setGenerating(prompt);
    showArt(true);
    beginImage(prompt, QString(), -1, false);
}

// ---------------------------------------------------------------------------
// Editing projects
// ---------------------------------------------------------------------------
bool MainWindow::writeProjectFile(const QString &path, const QString &content, const QString &projectId)
{
    QString error;
    if (!projects_.writeText(projectId, path, content, &error)) {
        toast(QStringLiteral("Could not save %1: %2").arg(path, error), QStringLiteral("close"));
        return false;
    }
    return true;
}

void MainWindow::setWorkspaceFolder(const QString &path)
{
    const QString folder = QDir(path).absolutePath();
    if (folder.isEmpty() || !QDir(folder).exists())
        return;
    workspaceFolder_ = folder;
    config_.set(QStringLiteral("workspace_folder"), folder, false);
    config_.save();
    if (backend_)
        backend_->setWorkspace(folder);
    syncHomeProject();
    toast(QStringLiteral("Working in ") + QDir(folder).dirName(), QStringLiteral("folder"));
}

bool MainWindow::writeWorkspaceFile(const QString &path, const QString &content)
{
    QString target = path;
    if (!QDir(target).isAbsolute()) {
        if (workspaceFolder_.isEmpty())
            return false;
        target = QDir(workspaceFolder_).filePath(path);
    }
    const QString dir = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(dir)) {
        toast(QStringLiteral("Could not create the folder for %1").arg(path), QStringLiteral("close"));
        return false;
    }
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        toast(QStringLiteral("Could not save %1").arg(path), QStringLiteral("close"));
        return false;
    }
    file.write(content.toUtf8());
    file.close();
    return true;
}

void MainWindow::applyProposal(const QString &path, const QString &content, int messageIndex)
{
    QString pid = conversation_.value(QStringLiteral("project_id")).toString();
    if (pid.isEmpty() && hasProject_)
        pid = project_.id;

    // A selected folder wins over the built-in project store: files land right
    // where the user pointed Takoyo. If nothing is selected, ask where to save.
    if (pid.isEmpty() || !workspaceFolder_.isEmpty()) {
        const bool exists = !workspaceFolder_.isEmpty() && QFileInfo(QDir(workspaceFolder_).filePath(path)).exists();
        QMessageBox box(QMessageBox::Question, QStringLiteral("Apply edit?"),
                        exists ? QStringLiteral("Replace “%1”?").arg(path)
                               : QStringLiteral("Create “%1”?").arg(path),
                        QMessageBox::NoButton, this);
        box.setInformativeText(QStringLiteral("Takoyo's version is a complete file, so the old contents are replaced."));
        QPushButton *apply = box.addButton(QStringLiteral("Apply"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != apply)
            return;

        if (workspaceFolder_.isEmpty()) {
            const QString suggested = QDir(QDir::homePath()).filePath(path);
            const QString target = QFileDialog::getSaveFileName(
                this, QStringLiteral("Where should Takoyo save this file?"), suggested);
            if (target.isEmpty())
                return;
            setWorkspaceFolder(QFileInfo(target).absolutePath());
            if (!writeWorkspaceFile(target, content))
                return;
        } else if (!writeWorkspaceFile(path, content)) {
            return;
        }
        markProposalApplied(messageIndex, path);
        toast(QStringLiteral("Saved ") + path);
        refreshSidebar();
        return;
    }

    ProjectSummary project;
    if (!projects_.get(pid, &project)) {
        toast(QStringLiteral("That project is no longer here"), QStringLiteral("folder"));
        return;
    }
    const bool exists = projects_.fileExists(pid, path);

    QMessageBox box(QMessageBox::Question, QStringLiteral("Apply edit?"),
                    exists ? QStringLiteral("Replace “%1” in %2?").arg(path, project.name)
                           : QStringLiteral("Create “%1” in %2?").arg(path, project.name),
                    QMessageBox::NoButton, this);
    box.setInformativeText(QStringLiteral("Takoyo's version is a complete file, so the old contents are replaced."));
    QPushButton *apply = box.addButton(QStringLiteral("Apply"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != apply)
        return;

    if (!writeProjectFile(path, content, pid))
        return;

    markProposalApplied(messageIndex, path);
    toast(QStringLiteral("Saved ") + path);
    if (auto *dialog = qobject_cast<ProjectFilesDialog *>(filesDialog_.data()))
        dialog->reload();
    refreshSidebar();
}

void MainWindow::markProposalApplied(int messageIndex, const QString &path)
{
    const QString cid = conversationId();
    if (cid.isEmpty() || messageIndex < 0)
        return;
    QJsonObject conv = conversations_.get(cid);
    QJsonArray messages = conv.value(QStringLiteral("messages")).toArray();
    if (messageIndex >= messages.size())
        return;
    QJsonObject message = messages.at(messageIndex).toObject();
    QJsonArray edits = message.value(QStringLiteral("edits")).toArray();
    for (int i = 0; i < edits.size(); ++i) {
        QJsonObject edit = edits.at(i).toObject();
        if (edit.value(QStringLiteral("path")).toString() == path) {
            edit.insert(QStringLiteral("applied"), true);
            edits.replace(i, edit);
        }
    }
    QJsonObject fields;
    fields.insert(QStringLiteral("edits"), edits);
    conv = conversations_.updateMessage(cid, messageIndex, fields);
    if (hasConversation_ && conversationId() == cid) {
        conversation_ = conv;
        chat_->refresh(conversation_);
        syncChatProject();
    }
}

void MainWindow::applyCodeFromChat(const QString &path, const QString &content)
{
    QString pid = conversation_.value(QStringLiteral("project_id")).toString();
    if (pid.isEmpty() && hasProject_)
        pid = project_.id;

    if (pid.isEmpty() || !workspaceFolder_.isEmpty()) {
        const bool exists = !workspaceFolder_.isEmpty() && QFileInfo(QDir(workspaceFolder_).filePath(path)).exists();
        QMessageBox box(QMessageBox::Question, QStringLiteral("Apply this code?"),
                        exists ? QStringLiteral("Overwrite “%1”?").arg(path)
                               : QStringLiteral("Create “%1”?").arg(path),
                        QMessageBox::NoButton, this);
        QPushButton *save = box.addButton(QStringLiteral("Save file"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != save)
            return;

        if (workspaceFolder_.isEmpty()) {
            const QString suggested = QDir(QDir::homePath()).filePath(path);
            const QString target = QFileDialog::getSaveFileName(
                this, QStringLiteral("Where should Takoyo save this file?"), suggested);
            if (target.isEmpty())
                return;
            setWorkspaceFolder(QFileInfo(target).absolutePath());
            if (!writeWorkspaceFile(target, content))
                return;
        } else if (!writeWorkspaceFile(path, content)) {
            return;
        }
        toast(QStringLiteral("Saved ") + path);
        refreshSidebar();
        return;
    }

    ProjectSummary project;
    if (!projects_.get(pid, &project))
        return;
    const bool exists = projects_.fileExists(pid, path);

    QMessageBox box(QMessageBox::Question, QStringLiteral("Apply this code?"),
                    exists ? QStringLiteral("Overwrite “%1” with the new version?").arg(path)
                           : QStringLiteral("Create “%1” in %2?").arg(path, project.name),
                    QMessageBox::NoButton, this);
    QPushButton *save = box.addButton(QStringLiteral("Save file"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != save)
        return;

    if (!writeProjectFile(path, content, pid))
        return;
    toast(QStringLiteral("Saved ") + path);
    if (auto *dialog = qobject_cast<ProjectFilesDialog *>(filesDialog_.data()))
        dialog->reload();
    refreshSidebar();
}

void MainWindow::openProjectFiles()
{
    if (!hasProject_) {
        toast(QStringLiteral("Open a project first"), QStringLiteral("folder"));
        return;
    }
    if (auto *dialog = qobject_cast<ProjectFilesDialog *>(filesDialog_.data())) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    auto *dialog = new ProjectFilesDialog(&projects_, project_.id, project_.name, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    filesDialog_ = dialog;
    dialog->show();
}

// ---------------------------------------------------------------------------
// Projects and chats
// ---------------------------------------------------------------------------
void MainWindow::newProjectDialog()
{
    NewProjectDialog dialog(&projects_, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.createdId().isEmpty()) {
        openProject(dialog.createdId());
        toast(QStringLiteral("Project created"), QStringLiteral("folder"));
    }
}

void MainWindow::showProjectMenu(const QString &id, const QPoint &globalPos)
{
    ProjectSummary project;
    if (!projects_.get(id, &project))
        return;
    QMenu menu(this);
    menu.setFont(theme::font(11));
    theme::style(&menu, menuStyle());
    QAction *newChatHere = menu.addAction(QStringLiteral("New chat here"));
    QAction *openFiles = menu.addAction(QStringLiteral("Open files"));
    menu.addSeparator();
    QAction *rename = menu.addAction(QStringLiteral("Rename"));
    QAction *remove = menu.addAction(QStringLiteral("Delete project"));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == newChatHere) {
        openProject(id);
        newChat();
    } else if (chosen == openFiles) {
        openProject(id);
        openProjectFiles();
    } else if (chosen == rename) {
        renameProject(id);
    } else if (chosen == remove) {
        deleteProject(id);
    }
}

void MainWindow::showConversationMenu(const QString &id, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.setFont(theme::font(11));
    theme::style(&menu, menuStyle());
    QAction *rename = menu.addAction(QStringLiteral("Rename"));
    QAction *exportAction = menu.addAction(QStringLiteral("Export as Markdown…"));
    QAction *remove = menu.addAction(QStringLiteral("Delete chat"));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == rename)
        renameConversation(id);
    else if (chosen == exportAction)
        exportConversation(id);
    else if (chosen == remove)
        deleteConversation(id);
}

void MainWindow::exportConversation(const QString &id)
{
    const QJsonObject conv = conversations_.get(id);
    if (conv.isEmpty()) {
        toast(QStringLiteral("That chat is already gone"), QStringLiteral("close"));
        return;
    }
    const QString title = conv.value(QStringLiteral("title")).toString(QStringLiteral("Takoyo chat"));
    QString suggested = title;
    suggested.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("-"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export chat"),
                                                      QDir::home().filePath(suggested + QStringLiteral(".md")),
                                                      QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty())
        return;

    QString out = QStringLiteral("# %1\n\n").arg(title);
    const QJsonArray messages = conv.value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        if (message.value(QStringLiteral("hidden")).toBool())
            continue;
        const QString role = message.value(QStringLiteral("role")).toString();
        const QString content = message.value(QStringLiteral("content")).toString().trimmed();
        const QJsonArray images = message.value(QStringLiteral("images")).toArray();
        if (content.isEmpty() && images.isEmpty())
            continue;
        out += role == QLatin1String("user") ? QStringLiteral("**You**\n\n")
                                             : QStringLiteral("**Takoyo**\n\n");
        if (!content.isEmpty())
            out += content + QStringLiteral("\n\n");
        for (const QJsonValue &image : images) {
            const QString imagePath = image.toObject().value(QStringLiteral("path")).toString();
            if (!imagePath.isEmpty())
                out += QStringLiteral("![Takoyo Art](%1)\n\n").arg(imagePath);
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        toast(QStringLiteral("Could not write that file"), QStringLiteral("close"));
        return;
    }
    file.write(out.toUtf8());
    file.close();
    toast(QStringLiteral("Chat exported"));
}

void MainWindow::renameProject(const QString &id)
{
    ProjectSummary project;
    if (!projects_.get(id, &project))
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename project"),
                                               QStringLiteral("Project name"), QLineEdit::Normal,
                                               project.name, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    projects_.rename(id, name.trimmed());
    if (hasProject_ && project_.id == id) {
        ProjectSummary updated;
        if (projects_.get(id, &updated))
            project_ = updated;
        syncHomeProject();
        syncChatProject();
    }
    refreshSidebar();
    toast(QStringLiteral("Project renamed"));
}

void MainWindow::deleteProject(const QString &id)
{
    ProjectSummary project;
    if (!projects_.get(id, &project))
        return;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Delete project?"),
                    QStringLiteral("“%1” will move to Takoyo's trash folder. "
                                   "You can still recover it from there.")
                        .arg(project.name),
                    QMessageBox::NoButton, this);
    QPushButton *trash = box.addButton(QStringLiteral("Move to trash"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != trash)
        return;
    projects_.remove(id);
    if (hasProject_ && project_.id == id)
        closeProject();
    refreshSidebar();
    toast(QStringLiteral("Project moved to trash"));
}

void MainWindow::renameConversation(const QString &id)
{
    const QJsonObject conv = conversations_.get(id);
    if (conv.isEmpty())
        return;
    bool ok = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("Rename chat"),
                                                QStringLiteral("Chat title"), QLineEdit::Normal,
                                                conv.value(QStringLiteral("title")).toString(), &ok);
    if (!ok || title.trimmed().isEmpty())
        return;
    conversations_.rename(id, title.trimmed());
    if (hasConversation_ && conversationId() == id)
        conversation_ = conversations_.get(id);
    refreshSidebar();
}

void MainWindow::deleteConversation(const QString &id)
{
    const QJsonObject conv = conversations_.get(id);
    if (conv.isEmpty())
        return;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Delete chat?"),
                    QStringLiteral("“%1” will move to Takoyo's trash.")
                        .arg(conv.value(QStringLiteral("title")).toString(QStringLiteral("Chat"))),
                    QMessageBox::NoButton, this);
    QPushButton *trash = box.addButton(QStringLiteral("Move to trash"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != trash)
        return;
    conversations_.remove(id);
    if (hasConversation_ && conversationId() == id) {
        hasConversation_ = false;
        conversation_ = QJsonObject();
        config_.set(QStringLiteral("last_conversation"), QString());
        chat_->clear();
        showHome();
    }
    refreshSidebar();
    toast(QStringLiteral("Chat moved to trash"));
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void MainWindow::openModelPicker()
{
    if (auto *dialog = qobject_cast<ModelPickerDialog *>(modelPicker_.data())) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    auto *dialog = new ModelPickerDialog(&config_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ModelPickerDialog::settingsRequested, this, &MainWindow::openSettings);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        const QString provider = dialog->provider();
        if (provider.isEmpty())
            return;
        const QString model = dialog->model();
        const bool download = dialog->wantsDownload();
        config_.set(QStringLiteral("provider"), provider, false);
        if (provider == QLatin1String("local") && !model.isEmpty())
            config_.set(QStringLiteral("local_model"), model, false);
        config_.save();
        if (backend_)
            backend_->reloadConfig();
        if (download && backend_)
            backend_->installLocalModel(model);
        updateStatus();
        refreshSidebar();
        if (download)
            toast(QStringLiteral("Downloading %1 in the background").arg(localModelName(model)));
        else if (provider == QLatin1String("local"))
            toast(QStringLiteral("Brain set to %1").arg(localModelName(model)));
        else
            toast(QStringLiteral("Brain set to %1").arg(config_.providerLabel(provider)));
    });
    modelPicker_ = dialog;
    dialog->show();
}

void MainWindow::openPalettePicker()
{
    if (auto *dialog = qobject_cast<PalettePickerDialog *>(palettePicker_.data())) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    auto *dialog = new PalettePickerDialog(&config_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &PalettePickerDialog::palettePreviewRequested, this, [this](const QString &id) {
        // Live preview: apply the palette immediately; Cancel restores the
        // previous one when the dialog is rejected.
        if (theme::loadPalette(id))
            applyTheme();
    });
    connect(dialog, &QDialog::rejected, this, [this]() {
        theme::loadPalette(config_.str(QStringLiteral("palette")));
        applyTheme();
    });
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        const QString id = dialog->selectedPalette();
        if (!id.isEmpty()) {
            theme::loadPalette(id);
            config_.set(QStringLiteral("palette"), id, false);
            config_.save();
            applyTheme();
            toast(QStringLiteral("Palette: %1").arg(theme::paletteName(id)));
        }
    });
    palettePicker_ = dialog;
    dialog->show();
}

void MainWindow::toggleTheme()
{
    theme::setDarkMode(!theme::darkMode());
    config_.set(QStringLiteral("dark_mode"), theme::darkMode(), false);
    config_.save();
    applyTheme();
}

void MainWindow::applyTheme()
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme::map(theme::WHITE));
    setPalette(pal);
    theme::refresh(this);
    if (themeButton_) {
        themeButton_->setIconName(theme::darkMode() ? QStringLiteral("sun") : QStringLiteral("moon"));
        themeButton_->setToolTip(theme::darkMode() ? QStringLiteral("Switch to light mode (Ctrl+Shift+D)")
                                                   : QStringLiteral("Switch to dark mode (Ctrl+Shift+D)"));
    }
    // Rich text documents keep the stylesheet they were built with, so re-render
    // the open conversation once the palette changed.
    if (!streaming_ && stack_ && stack_->currentWidget() == chat_ && hasConversation_)
        chat_->refresh(conversation_);
    update();
}

void MainWindow::openSettings()
{
    if (auto *dialog = qobject_cast<SettingsDialog *>(settingsDialog_.data())) {
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    auto *dialog = new SettingsDialog(&config_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this]() {
        if (backend_)
            backend_->reloadConfig();
        updateStatus();
        refreshSidebar();
    });
    settingsDialog_ = dialog;
    dialog->show();
}

// ---------------------------------------------------------------------------
// Closing
// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (closing_) {
        event->accept();
        return;
    }
    closing_ = true;
    config_.set(QStringLiteral("window_geometry"), util::formatGeometry(this), false);
    config_.set(QStringLiteral("sidebar_open"), sidebarOpen_, false);
    config_.save();
    if (stream_)
        stream_->cancel();
    hide();
    QApplication::processEvents();
    if (backend_)
        backend_->shutdown();
    event->accept();
}
