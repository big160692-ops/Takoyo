#include "chatview.h"

#include "composer.h"
#include "icons.h"
#include "store.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

class RichTextBlock : public QTextBrowser {
public:
    explicit RichTextBlock(int size = 15, QWidget *parent = nullptr)
        : QTextBrowser(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setReadOnly(true);
        setOpenExternalLinks(false);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setStyleSheet(QStringLiteral("QTextBrowser{background:transparent;border:none;}"));
        viewport()->setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QPalette pal = viewport()->palette();
        pal.setColor(QPalette::Base, Qt::transparent);
        pal.setColor(QPalette::Text, theme::INK);
        viewport()->setPalette(pal);

        document()->setDocumentMargin(0);
        document()->setDefaultFont(theme::font(size));
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                [this]() { syncHeight(); });
        connect(this, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
            QDesktopServices::openUrl(url);
        });
    }

    void setRichText(const QString &markdown)
    {
        const QString css = QStringLiteral(
            "p{margin:0 0 6px 0;} ul,ol{margin:0 0 6px 12px;} li{margin:0 0 3px 0;}"
            "h1,h2,h3,h4{font-weight:600;margin:8px 0 6px 0;} code{font-family:Consolas;background:#F2F2F3;}"
            "pre{background:#F2F2F3;margin:4px 0;padding:2px;} blockquote{color:#3A3B3E;margin:0 0 6px 10px;}"
            "a{color:#1C55A5;}");
        document()->setDefaultStyleSheet(css);
        document()->setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
        syncHeight();
    }

    QSize sizeHint() const override
    {
        return {200, height()};
    }

    void syncHeight()
    {
        if (updating_ || width() <= 0)
            return;
        updating_ = true;
        const int textWidth = qMax(40, viewport()->width());
        document()->setTextWidth(textWidth);
        const int target = static_cast<int>(std::ceil(document()->size().height())) + 2;
        if (target != height())
            setFixedHeight(target);
        updating_ = false;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTextBrowser::resizeEvent(event);
        syncHeight();
    }

private:
    bool updating_ = false;
};

namespace {

class IconGlyph : public QWidget {
public:
    IconGlyph(const QString &name, const QColor &color, int size, QWidget *parent)
        : QWidget(parent)
        , name_(name)
        , color_(color)
    {
        setFixedSize(size, size);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        icons::paint(p, name_, rect(), color_);
    }

private:
    QString name_;
    QColor color_;
};

class CodeCard : public RoundedCard {
public:
    CodeCard(const QString &language, const QString &filename, const QString &code, QWidget *parent,
             std::function<void()> onApply)
        : RoundedCard(parent)
    {
        setRadius(13);
        setFill(theme::WHITE);
        setOutline(theme::LINE);
        setThickness(1.6);
        setPadding(16);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *head = new QWidget(this);
        auto *headLayout = new QHBoxLayout(head);
        headLayout->setContentsMargins(0, 0, 0, 0);
        headLayout->setSpacing(6);

        if (!language.isEmpty()) {
            auto *lang = new QLabel(language.toUpper(), head);
            lang->setFont(theme::font(9, QStringLiteral("semibold")));
            lang->setStyleSheet(QStringLiteral("color:#8B8B8F;background:transparent;"));
            headLayout->addWidget(lang);
        }
        auto *name = new QLabel(filename.isEmpty() ? (language.isEmpty() ? QStringLiteral("code") : filename)
                                                   : filename,
                                head);
        name->setFont(theme::font(10, QStringLiteral("medium")));
        name->setStyleSheet(QStringLiteral("color:#3A3B3E;background:transparent;"));
        headLayout->addWidget(name);
        headLayout->addStretch(1);

        auto *copy = new IconButton(QStringLiteral("copy"), 15, head);
        copy->setFixedSize(23, 23);
        copy->setColor(theme::MUTED);
        copy->setHoverColor(theme::INK);
        copy->setHoverBackground(theme::HOVER);
        copy->setToolTip(QStringLiteral("Copy"));
        connect(copy, &QAbstractButton::clicked, this, [code]() {
            QApplication::clipboard()->setText(code);
        });
        headLayout->addWidget(copy);
        if (onApply) {
            auto *apply = new IconButton(QStringLiteral("pencil"), 15, head);
            apply->setFixedSize(23, 23);
            apply->setColor(theme::MUTED);
            apply->setHoverColor(theme::INK);
            apply->setHoverBackground(theme::HOVER);
            apply->setToolTip(QStringLiteral("Apply to project"));
            connect(apply, &QAbstractButton::clicked, this, [onApply]() { onApply(); });
            headLayout->addWidget(apply);
        }
        content()->addWidget(head);

        auto *view = new QPlainTextEdit(code, this);
        view->setReadOnly(true);
        view->setFrameShape(QFrame::NoFrame);
        view->setFont(theme::mono(13));
        view->setStyleSheet(QStringLiteral(
            "QPlainTextEdit{background:transparent;border:none;color:#0B0B0D;selection-background-color:#DEDEDF;"
            "selection-color:#0B0B0D;}"));
        view->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        const int lines = qMax(1, code.count('\n') + 1);
        view->setFixedHeight(qMin(lines, 22) * view->fontMetrics().lineSpacing() + 8);
        content()->addWidget(view);
    }
};

class RoundedImage : public QWidget {
public:
    RoundedImage(const QString &path, int width, int height, int radius, QWidget *parent)
        : QWidget(parent)
        , radius_(radius)
    {
        pixmap_.load(path);
        setFixedSize(width, height);
        setCursor(Qt::PointingHandCursor);
    }

    bool valid() const { return !pixmap_.isNull(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), radius_, radius_);
        painter.setClipPath(clip);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::ACTIVE);
        painter.drawRect(rect());
        if (!pixmap_.isNull()) {
            const QSize scaled = pixmap_.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
            QPixmap drawn = pixmap_.scaled(scaled, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const QRect source((drawn.width() - width()) / 2, (drawn.height() - height()) / 2, width(), height());
            painter.drawPixmap(rect(), drawn, source);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            emitClicked();
        QWidget::mouseReleaseEvent(event);
    }

public:
    std::function<void()> onClick;

private:
    void emitClicked()
    {
        if (onClick)
            onClick();
    }

    QPixmap pixmap_;
    int radius_ = 10;
};

} // namespace

// ---------------------------------------------------------------------------
// ChatView
// ---------------------------------------------------------------------------
ChatView::ChatView(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    setupProjectBar();
    outer->addWidget(projectBar_);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setStyleSheet(QStringLiteral(
        "QScrollArea{background:#FFFFFF;border:none;}"
        "QScrollBar:vertical{width:10px;background:transparent;margin:2px 2px 2px 0;}"
        "QScrollBar::handle:vertical{background:#DEDEDF;border-radius:4px;min-height:36px;}"
        "QScrollBar::handle:vertical:hover{background:#C9C9CC;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"));
    scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    column_ = new QWidget(scroll_);
    column_->setStyleSheet(QStringLiteral("background:#FFFFFF;"));
    columnLayout_ = new QVBoxLayout(column_);
    columnLayout_->setContentsMargins(0, 4, 0, 0);
    columnLayout_->setSpacing(0);
    columnLayout_->addStretch(1);
    scroll_->setWidget(column_);
    outer->addWidget(scroll_, 1);

    scrollBtn_ = new IconButton(QStringLiteral("arrow-down"), 17, scroll_);
    scrollBtn_->setColor(theme::INK);
    scrollBtn_->setHoverColor(theme::INK);
    scrollBtn_->setHoverBackground(theme::HOVER);
    scrollBtn_->setCircleOutline(theme::INK);
    scrollBtn_->setCircleFill(theme::WHITE);
    scrollBtn_->setFixedSize(32, 32);
    scrollBtn_->setToolTip(QStringLiteral("Jump to latest"));
    connect(scrollBtn_, &QAbstractButton::clicked, this, &ChatView::scrollToBottom);
    scrollBtn_->hide();

    composerHost_ = new QWidget(this);
    auto *hostLayout = new QHBoxLayout(composerHost_);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    composer_ = new Composer(composerHost_);
    hostLayout->addWidget(composer_);
    outer->addWidget(composerHost_);
    composerHost_->setContentsMargins(composerPad_, 4, composerPad_, 14);

    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        const QScrollBar *bar = scroll_->verticalScrollBar();
        atBottom_ = bar->value() >= bar->maximum() - 4;
        updateScrollButton();
    });
    connect(scroll_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) {
        updateScrollButton();
    });

    liveTimer_.setInterval(60);
    connect(&liveTimer_, &QTimer::timeout, this, &ChatView::flushLive);
    liveDots_.setInterval(380);
    connect(&liveDots_, &QTimer::timeout, this, [this]() {
        if (!liveLabel_)
            return;
        livePhase_++;
        liveLabel_->setText(QStringLiteral("  Takoyo is thinking") + QString(livePhase_ % 3 + 1, QChar('.')));
    });

    setProject(nullptr);
}

void ChatView::setupProjectBar()
{
    projectBar_ = new QWidget(this);
    projectBar_->setStyleSheet(QStringLiteral("background:#FFFFFF;"));
    auto *layout = new QHBoxLayout(projectBar_);
    layout->setContentsMargins(28, 0, 28, 2);
    layout->setSpacing(7);

    class FolderIcon : public QWidget {
    public:
        explicit FolderIcon(QWidget *parent) : QWidget(parent) { setFixedSize(15, 15); }
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            icons::paint(p, QStringLiteral("folder"), rect(), theme::INK);
        }
    };
    layout->addWidget(new FolderIcon(projectBar_));
    projectName_ = new QLabel(projectBar_);
    projectName_->setFont(theme::font(13, QStringLiteral("semibold")));
    projectName_->setStyleSheet(QStringLiteral("color:#0B0B0D;background:transparent;"));
    layout->addWidget(projectName_);
    projectDesc_ = new QLabel(projectBar_);
    projectDesc_->setFont(theme::font(10));
    projectDesc_->setStyleSheet(QStringLiteral("color:#8B8B8F;background:transparent;"));
    layout->addWidget(projectDesc_);
    layout->addStretch(1);

    auto *files = new TextButton(QStringLiteral("Files"), TextButton::Variant::Secondary, 29, projectBar_);
    files->setFontSize(10);
    files->setFixedWidth(62);
    connect(files, &QAbstractButton::clicked, this, &ChatView::projectFilesRequested);
    layout->addWidget(files);

    auto *plus = new IconButton(QStringLiteral("plus"), 16, projectBar_);
    plus->setColor(theme::MUTED);
    plus->setHoverColor(theme::INK);
    plus->setToolTip(QStringLiteral("New chat in this project"));
    connect(plus, &QAbstractButton::clicked, this, &ChatView::newChatRequested);
    layout->addWidget(plus);

    auto *close = new IconButton(QStringLiteral("close"), 13, projectBar_);
    close->setColor(theme::MUTED);
    close->setHoverColor(theme::INK);
    close->setToolTip(QStringLiteral("Close project"));
    connect(close, &QAbstractButton::clicked, this, &ChatView::closeProjectRequested);
    layout->addWidget(close);

    projectBar_->hide();
}

void ChatView::setProject(const ProjectSummary *project)
{
    if (!project) {
        projectBar_->hide();
        return;
    }
    projectName_->setText(project->name);
    const QString description = util::shorten(project->description, 60);
    projectDesc_->setText(description.isEmpty() ? QString() : QStringLiteral("· ") + description);
    projectBar_->show();
}

void ChatView::clear()
{
    liveTimer_.stop();
    liveDots_.stop();
    liveText_.clear();
    liveRow_ = nullptr;
    liveBlock_ = nullptr;
    liveLabel_ = nullptr;
    while (columnLayout_->count() > 1) {
        QLayoutItem *item = columnLayout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    setProject(nullptr);
}

void ChatView::refresh(const QJsonObject &conversation)
{
    clear();
    const QJsonArray messages = conversation.value(QStringLiteral("messages")).toArray();
    if (messages.isEmpty())
        return;
    for (int i = 0; i < messages.size(); ++i)
        renderMessage(messages.at(i).toObject(), i, i == messages.size() - 1);
    QTimer::singleShot(30, this, &ChatView::scrollToBottom);
}

QWidget *ChatView::addRow()
{
    auto *row = new QWidget(column_);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(34, 9, 34, 3);
    layout->setSpacing(9);
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    columnLayout_->insertWidget(columnLayout_->count() - 1, row);
    return row;
}

void ChatView::renderMessage(const QJsonObject &message, int index, bool last)
{
    if (message.value(QStringLiteral("hidden")).toBool())
        return;
    const QString role = message.value(QStringLiteral("role")).toString();
    if (role == QLatin1String("user")) {
        QWidget *row = addRow();
        addUserMessage(row, message);
        return;
    }
    const QString content = message.value(QStringLiteral("content")).toString();
    const QJsonArray images = message.value(QStringLiteral("images")).toArray();
    const QJsonArray edits = message.value(QStringLiteral("edits")).toArray();
    const QString error = message.value(QStringLiteral("error")).toString();
    if (content.trimmed().isEmpty() && images.isEmpty() && edits.isEmpty() && error.isEmpty())
        return;

    QWidget *row = addRow();
    auto *layout = static_cast<QHBoxLayout *>(row->layout());
    auto *mascot = new MascotLabel(25, row);
    layout->addWidget(mascot, 0, Qt::AlignTop);
    auto *column = new QWidget(row);
    column->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, last ? 6 : 2);
    columnLayout->setSpacing(0);
    layout->addWidget(column, 1);

    if (!content.trimmed().isEmpty()) {
        for (const util::Segment &segment : util::splitFences(content)) {
            if (!segment.code) {
                auto *rich = new RichTextBlock(15, column);
                rich->setRichText(segment.text.trimmed());
                columnLayout->addWidget(rich);
            } else {
                const auto info = util::splitInfo(segment.info);
                QPointer<ChatView> self(this);
                std::function<void()> apply;
                if (!info.second.isEmpty()) {
                    apply = [self, info, segment, index]() {
                        if (self)
                            emit self->applyEditRequested(info.second, segment.text, index);
                    };
                }
                auto *card = new CodeCard(info.first, info.second, segment.text, column, apply);
                columnLayout->addWidget(card);
                columnLayout->addSpacing(8);
            }
        }
    }
    for (const QJsonValue &value : images)
        addImageCard(column, value.toObject());
    for (const QJsonValue &value : edits)
        addProposalCard(column, value.toObject(), index);
    if (!error.isEmpty())
        addErrorCard(column, error, message.value(QStringLiteral("error_kind")).toString());
}

void ChatView::addUserMessage(QWidget *row, const QJsonObject &message)
{
    auto *layout = static_cast<QHBoxLayout *>(row->layout());
    layout->addStretch(1);
    auto *card = new RoundedCard(row);
    card->setRadius(16);
    card->setFill(theme::WHITE);
    card->setOutline(theme::INK);
    card->setThickness(1.45);
    card->setPadding(14);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    const QString text = message.value(QStringLiteral("content")).toString();
    if (!text.trimmed().isEmpty()) {
        auto *label = new QLabel(text, card);
        label->setFont(theme::font(14));
        label->setStyleSheet(QStringLiteral("color:#0B0B0D;background:transparent;"));
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setMaximumWidth(492);
        card->content()->addWidget(label);
    }
    const QJsonObject attachment = message.value(QStringLiteral("attachment")).toObject();
    const QString filename = attachment.value(QStringLiteral("name")).toString();
    if (!filename.isEmpty()) {
        auto *chip = new QLabel(QStringLiteral("  ") + util::shorten(filename, 30), card);
        chip->setFont(theme::font(10, QStringLiteral("medium")));
        chip->setStyleSheet(
            QStringLiteral("background:#F2F2F3;color:#3A3B3E;border-radius:8px;padding:4px 8px;"));
        card->content()->addWidget(chip, 0, Qt::AlignLeft);
        if (!text.trimmed().isEmpty())
            card->content()->setSpacing(8);
    }
    layout->addWidget(card, 0, Qt::AlignRight);
}

void ChatView::addImageCard(QWidget *column, const QJsonObject &meta)
{
    const QString path = meta.value(QStringLiteral("path")).toString();
    auto *card = new RoundedCard(column);
    card->setRadius(13);
    card->setFill(theme::CHIP);
    card->setOutline(QColor());
    card->setThickness(0);
    card->setPadding(8);
    card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *image = new RoundedImage(path, 320, 214, 10, card);
    if (!image->valid()) {
        card->deleteLater();
        return;
    }
    QPointer<ChatView> self(this);
    image->onClick = [self, meta]() {
        if (self)
            emit self->imageRequested(meta);
    };
    card->content()->addWidget(image, 0, Qt::AlignLeft);

    QString label = meta.value(QStringLiteral("label")).toString();
    if (label.isEmpty())
        label = meta.value(QStringLiteral("prompt")).toString();
    auto *caption = new QLabel(QStringLiteral("Takoyo Art · ") + util::shorten(label, 48), card);
    caption->setFont(theme::font(9));
    caption->setStyleSheet(QStringLiteral("color:#8B8B8F;background:transparent;"));
    card->content()->addWidget(caption);

    auto *actions = new QWidget(card);
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(6);
    auto *view = new TextButton(QStringLiteral("View"), TextButton::Variant::Secondary, 26, actions);
    view->setFontSize(9);
    view->setFixedWidth(54);
    connect(view, &QAbstractButton::clicked, this, [this, meta]() { emit imageRequested(meta); });
    actionsLayout->addWidget(view);
    auto *save = new TextButton(QStringLiteral("Save"), TextButton::Variant::Secondary, 26, actions);
    save->setFontSize(9);
    save->setFixedWidth(54);
    connect(save, &QAbstractButton::clicked, this, [this, meta]() { emit saveImageRequested(meta); });
    actionsLayout->addWidget(save);
    actionsLayout->addStretch(1);
    card->content()->addWidget(actions);

    auto *wrapper = new QWidget(column);
    auto *wrapperLayout = new QHBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(0, 7, 0, 6);
    wrapperLayout->addWidget(card, 0, Qt::AlignLeft);
    wrapperLayout->addStretch(1);
    static_cast<QVBoxLayout *>(column->layout())->addWidget(wrapper);
}

void ChatView::addProposalCard(QWidget *column, const QJsonObject &edit, int index)
{
    auto *card = new RoundedCard(column);
    card->setRadius(13);
    card->setFill(theme::CHIP);
    card->setOutline(QColor());
    card->setThickness(0);
    card->setPadding(13);
    card->setMaximumWidth(646);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *head = new QWidget(card);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(7);
    headLayout->addWidget(new IconGlyph(QStringLiteral("code"), theme::INK, 15, head));
    auto *path = new QLabel(edit.value(QStringLiteral("path")).toString(QStringLiteral("file")), head);
    path->setFont(theme::font(11, QStringLiteral("semibold")));
    path->setStyleSheet(QStringLiteral("color:#0B0B0D;background:transparent;"));
    headLayout->addWidget(path);
    headLayout->addStretch(1);
    const bool applied = edit.value(QStringLiteral("applied")).toBool();
    if (applied) {
        auto *state = new QLabel(QStringLiteral("Applied"), head);
        state->setFont(theme::font(9, QStringLiteral("medium")));
        state->setStyleSheet(QStringLiteral("color:#0F9D58;background:transparent;"));
        headLayout->addWidget(new IconGlyph(QStringLiteral("check"), theme::SUCCESS, 13, head));
        headLayout->addWidget(state);
    } else {
        auto *apply = new TextButton(QStringLiteral("Apply"), TextButton::Variant::Primary, 27, head);
        apply->setFontSize(9);
        apply->setFixedWidth(56);
        connect(apply, &QAbstractButton::clicked, this, [this, edit, index]() {
            emit applyEditRequested(edit.value(QStringLiteral("path")).toString(),
                                    edit.value(QStringLiteral("content")).toString(), index);
        });
        headLayout->addWidget(apply);
    }
    card->content()->addWidget(head);

    QStringList lines = edit.value(QStringLiteral("content")).toString().split('\n');
    QString preview = lines.mid(0, 9).join('\n');
    if (lines.size() > 9)
        preview += QStringLiteral("\n… %1 more lines").arg(lines.size() - 9);
    auto *code = new QLabel(preview, card);
    code->setTextFormat(Qt::PlainText);
    code->setFont(theme::mono(10));
    code->setStyleSheet(QStringLiteral("color:#3A3B3E;background:transparent;"));
    code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    code->setWordWrap(true);
    card->content()->addWidget(code);

    auto *wrapper = new QWidget(column);
    auto *wrapperLayout = new QHBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(0, 7, 0, 6);
    wrapperLayout->addWidget(card, 1, Qt::AlignLeft);
    wrapperLayout->addStretch(1);
    static_cast<QVBoxLayout *>(column->layout())->addWidget(wrapper);
}

void ChatView::addErrorCard(QWidget *column, const QString &message, const QString &kind)
{
    auto *card = new RoundedCard(column);
    card->setRadius(13);
    card->setFill(theme::ERROR_BG);
    card->setOutline(theme::ERROR_LINE);
    card->setThickness(1.2);
    card->setPadding(13);

    auto *title = new QLabel(QStringLiteral("Takoyo hit a snag"), card);
    title->setFont(theme::font(10, QStringLiteral("semibold")));
    title->setStyleSheet(QStringLiteral("color:#D9433A;background:transparent;"));
    card->content()->addWidget(title);

    auto *body = new QLabel(message, card);
    body->setFont(theme::font(11));
    body->setWordWrap(true);
    body->setMaximumWidth(520);
    body->setStyleSheet(QStringLiteral("color:#3A3B3E;background:transparent;"));
    card->content()->addWidget(body);

    if (kind == QLatin1String("local") || kind == QLatin1String("provider") || kind == QLatin1String("image")) {
        auto *button = new TextButton(kind == QLatin1String("local") ? QStringLiteral("Set up built-in AI")
                                                                    : QStringLiteral("Open settings"),
                                      TextButton::Variant::Secondary, 27, card);
        button->setFontSize(9);
        connect(button, &QAbstractButton::clicked, this, &ChatView::settingsRequested);
        card->content()->addWidget(button, 0, Qt::AlignLeft);
    }

    auto *wrapper = new QWidget(column);
    auto *wrapperLayout = new QHBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(0, 7, 0, 6);
    wrapperLayout->addWidget(card, 0, Qt::AlignLeft);
    wrapperLayout->addStretch(1);
    static_cast<QVBoxLayout *>(column->layout())->addWidget(wrapper);
}

void ChatView::startStream()
{
    liveText_.clear();
    livePending_ = false;
    livePhase_ = 0;

    liveRow_ = addRow();
    auto *layout = static_cast<QHBoxLayout *>(liveRow_->layout());
    auto *liveMascot = new MascotLabel(25, liveRow_);
    liveMascot->setThinking(true);
    layout->addWidget(liveMascot, 0, Qt::AlignTop);
    auto *column = new QWidget(liveRow_);
    column->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 6);
    columnLayout->setSpacing(2);
    layout->addWidget(column, 1);

    liveLabel_ = new QLabel(QStringLiteral("  Takoyo is thinking"), column);
    liveLabel_->setFont(theme::font(12));
    liveLabel_->setStyleSheet(QStringLiteral("color:#8B8B8F;background:transparent;"));
    QPixmap mascot = thinkingPixmap(17);
    if (!mascot.isNull())
        liveLabel_->setPixmap(mascot);
    columnLayout->addWidget(liveLabel_);

    liveBlock_ = new RichTextBlock(15, column);
    liveBlock_->hide();
    columnLayout->addWidget(liveBlock_);

    liveDots_.start();
    QTimer::singleShot(50, this, &ChatView::scrollToBottom);
}

void ChatView::appendStream(const QString &text)
{
    if (!liveBlock_)
        return;
    liveText_ += text;
    if (!livePending_) {
        livePending_ = true;
        liveTimer_.start();
    }
}

void ChatView::flushLive()
{
    liveTimer_.stop();
    livePending_ = false;
    if (!liveBlock_)
        return;
    if (liveLabel_) {
        liveDots_.stop();
        liveLabel_->deleteLater();
        liveLabel_ = nullptr;
        liveBlock_->show();
    }
    liveBlock_->setRichText(liveText_);
    if (atBottom_)
        scrollToBottom();
}

void ChatView::stopStreamUi()
{
    liveTimer_.stop();
    liveDots_.stop();
    if (liveLabel_) {
        liveLabel_->deleteLater();
        liveLabel_ = nullptr;
    }
}

void ChatView::scrollToBottom()
{
    QScrollBar *bar = scroll_->verticalScrollBar();
    bar->setValue(bar->maximum());
    atBottom_ = true;
    updateScrollButton();
}

void ChatView::updateScrollButton()
{
    const QScrollBar *bar = scroll_->verticalScrollBar();
    const bool tall = bar->maximum() > 8;
    const bool show = tall && bar->value() < bar->maximum() - 24;
    scrollBtn_->setVisible(show);
    if (show)
        placeScrollButton();
}

void ChatView::placeScrollButton()
{
    const int x = scroll_->width() - scrollBtn_->width() - 18;
    const int y = scroll_->height() - scrollBtn_->height() - 10;
    scrollBtn_->move(qMax(0, x), qMax(0, y));
    scrollBtn_->raise();
}

void ChatView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int pad = qMax(16, (width() - 760) / 2);
    if (pad != composerPad_) {
        composerPad_ = pad;
        composerHost_->setContentsMargins(pad, 4, pad, 14);
    }
    if (scrollBtn_)
        placeScrollButton();
}
