#include "composer.h"

#include "icons.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
QString shortName(const QString &name, int limit = 26)
{
    return util::shorten(name, limit);
}

const QStringList &replyStyles()
{
    static const QStringList styles = {
        QStringLiteral("balanced"), QStringLiteral("short"),
        QStringLiteral("detailed"), QStringLiteral("steps"),
    };
    return styles;
}

QString styleLabel(const QString &style)
{
    if (style == QLatin1String("short"))
        return QStringLiteral("Short");
    if (style == QLatin1String("detailed"))
        return QStringLiteral("Detailed");
    if (style == QLatin1String("steps"))
        return QStringLiteral("Steps");
    return QStringLiteral("Balanced");
}
} // namespace

Composer::Composer(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(152);
    setMaximumHeight(152);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 14, 18, 8);
    outer->setSpacing(4);

    edit_ = new QPlainTextEdit(this);
    edit_->setFrameShape(QFrame::NoFrame);
    edit_->setFont(theme::font(15));
    theme::style(edit_, QStringLiteral(
        "QPlainTextEdit{background:transparent;border:none;color:#0B0B0D;"
        "selection-background-color:#EDEDEF;padding:0px;}"));
    edit_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    edit_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit_->setMinimumHeight(64);
    edit_->installEventFilter(this);
    connect(edit_, &QPlainTextEdit::textChanged, this, [this]() {
        updatePlaceholder();
        refreshButtons();
        emit changed();
    });
    outer->addWidget(edit_, 1);

    placeholder_ = new QLabel(this);
    placeholder_->setFont(theme::font(16));
    theme::style(placeholder_, QStringLiteral("color:#8B8B8F;background:transparent;"));
    placeholder_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    placeholder_->setText(QStringLiteral("Ask anything, or describe what to build…"));

    bottom_ = new QWidget(this);
    bottom_->setFixedHeight(38);
    auto *bottomLayout = new QHBoxLayout(bottom_);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    attachBtn_ = new IconButton(QStringLiteral("paperclip"), 19, bottom_);
    attachBtn_->setColor(theme::INK_SOFT);
    attachBtn_->setHoverColor(theme::INK);
    attachBtn_->setToolTip(QStringLiteral("Attach a file"));
    connect(attachBtn_, &QAbstractButton::clicked, this, &Composer::attachFile);
    bottomLayout->addWidget(attachBtn_);

    folderBtn_ = new IconButton(QStringLiteral("folder"), 19, bottom_);
    folderBtn_->setColor(theme::INK_SOFT);
    folderBtn_->setHoverColor(theme::INK);
    folderBtn_->setToolTip(QStringLiteral("Attach a project folder for context"));
    connect(folderBtn_, &QAbstractButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Choose a folder to share with Takoyo"),
            QString(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!path.isEmpty())
            emit folderChanged(path);
    });
    bottomLayout->addWidget(folderBtn_);

    styleBtn_ = new TextButton(styleLabel(replyStyle_), TextButton::Variant::Secondary, 26, bottom_);
    styleBtn_->setFontSize(9);
    styleBtn_->setToolTip(QStringLiteral("Reply style — click to change (Balanced, Short, Detailed, Steps)"));
    connect(styleBtn_, &QAbstractButton::clicked, this, [this]() {
        const QStringList &styles = replyStyles();
        const int next = (styles.indexOf(replyStyle_) + 1) % styles.size();
        setReplyStyle(styles.at(next));
        emit replyStyleChanged(replyStyle_);
    });
    bottomLayout->addSpacing(6);
    bottomLayout->addWidget(styleBtn_);

    chip_ = new QWidget(bottom_);
    theme::style(chip_, QStringLiteral("background:transparent;"));
    auto *chipLayout = new QHBoxLayout(chip_);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(4);
    auto *chipBody = new QWidget(chip_);
    theme::style(chipBody, QStringLiteral("background:#F2F2F3;border-radius:8px;"));
    auto *chipInner = new QHBoxLayout(chipBody);
    chipInner->setContentsMargins(6, 3, 4, 3);
    chipInner->setSpacing(4);
    auto *clip = new QWidget(chipBody);
    clip->setFixedSize(11, 11);
    class MiniIcon : public QWidget {
    public:
        QString name;
        QColor color;
        MiniIcon(const QString &n, const QColor &c, QWidget *p) : QWidget(p), name(n), color(c) {}
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            icons::paint(painter, name, rect(), theme::map(color));
        }
    };
    auto *clipIcon = new MiniIcon(QStringLiteral("paperclip"), theme::MUTED, chipBody);
    clipIcon->setFixedSize(11, 11);
    chipInner->addWidget(clipIcon);
    chipLabel_ = new QLabel(chipBody);
    chipLabel_->setFont(theme::font(10, QStringLiteral("medium")));
    theme::style(chipLabel_, QStringLiteral("color:#3A3B3E;background:transparent;"));
    chipInner->addWidget(chipLabel_);
    auto *chipClose = new IconButton(QStringLiteral("close"), 11, chipBody);
    chipClose->setColor(theme::MUTED);
    chipClose->setHoverColor(theme::INK);
    connect(chipClose, &QAbstractButton::clicked, this, &Composer::clearAttachment);
    chipInner->addWidget(chipClose);
    chipLayout->addWidget(chipBody);
    chip_->hide();
    bottomLayout->addWidget(chip_);
    bottomLayout->addStretch(1);

    micBtn_ = new IconButton(QStringLiteral("mic"), 19, bottom_);
    micBtn_->setColor(theme::MUTED);
    micBtn_->setHoverColor(theme::INK);
    micBtn_->setToolTip(QStringLiteral("Voice input"));
    connect(micBtn_, &QAbstractButton::clicked, this, &Composer::voiceRequested);
    bottomLayout->addWidget(micBtn_);

    sendBtn_ = new CircleButton(QStringLiteral("arrow-up"), 36, bottom_);
    connect(sendBtn_, &QAbstractButton::clicked, this, [this]() {
        if (!thinking_)
            emit submitted();
    });
    bottomLayout->addWidget(sendBtn_);

    stopBtn_ = new CircleButton(QStringLiteral("stop"), 36, bottom_);
    connect(stopBtn_, &QAbstractButton::clicked, this, &Composer::stopRequested);
    bottomLayout->addWidget(stopBtn_);
    stopBtn_->hide();

    outer->addWidget(bottom_);

    thinkTimer_.setInterval(360);
    connect(&thinkTimer_, &QTimer::timeout, this, &Composer::tickThinking);
    refreshButtons();
    updatePlaceholder();
}

QString Composer::text() const
{
    return edit_->toPlainText();
}

void Composer::setText(const QString &text)
{
    edit_->setPlainText(text);
    edit_->moveCursor(QTextCursor::End);
    edit_->setFocus();
    updatePlaceholder();
    refreshButtons();
}

void Composer::clear()
{
    edit_->clear();
    clearAttachment();
    updatePlaceholder();
    refreshButtons();
}

void Composer::focusInput()
{
    edit_->setFocus();
}

void Composer::clearAttachment()
{
    attachmentLoaded_ = false;
    attachmentName_.clear();
    attachmentContent_.clear();
    attachmentPath_.clear();
    attachmentKind_.clear();
    attachmentMime_.clear();
    attachmentSize_ = 0;
    chip_->hide();
    refreshButtons();
}

QJsonObject Composer::attachment() const
{
    if (!attachmentLoaded_)
        return {};
    QJsonObject object;
    object.insert(QStringLiteral("name"), attachmentName_);
    object.insert(QStringLiteral("path"), attachmentPath_);
    if (attachmentKind_ == QLatin1String("image")) {
        object.insert(QStringLiteral("kind"), QStringLiteral("image"));
        object.insert(QStringLiteral("mime"), attachmentMime_);
        object.insert(QStringLiteral("size"), static_cast<double>(attachmentSize_));
    } else {
        object.insert(QStringLiteral("kind"), QStringLiteral("text"));
        object.insert(QStringLiteral("content"), attachmentContent_);
    }
    return object;
}

void Composer::attachFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Attach a file"));
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable())
        return;

    static const QHash<QString, QString> imageMimes = {
        {QStringLiteral("png"), QStringLiteral("image/png")},
        {QStringLiteral("jpg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("jpeg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("gif"), QStringLiteral("image/gif")},
        {QStringLiteral("webp"), QStringLiteral("image/webp")},
        {QStringLiteral("bmp"), QStringLiteral("image/bmp")},
    };
    const QString mime = imageMimes.value(info.suffix().toLower());
    if (!mime.isEmpty()) {
        attachmentLoaded_ = true;
        attachmentKind_ = QStringLiteral("image");
        attachmentMime_ = mime;
        attachmentSize_ = info.size();
        attachmentContent_.clear();
        attachmentName_ = info.fileName();
        attachmentPath_ = path;
        chipLabel_->setText(shortName(attachmentName_));
        chip_->show();
        updatePlaceholder();
        refreshButtons();
        emit changed();
        return;
    }

    QString content = util::documentText(path);
    if (content.isEmpty()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QByteArray data = file.read(160001);
        file.close();
        if (data.size() > 160000) {
            content = QString::fromUtf8(data.left(160000));
            content += QStringLiteral("\n… (truncated)");
        } else {
            content = QString::fromUtf8(data);
            if (content.contains(QChar::ReplacementCharacter)) {
                content = QStringLiteral(
                              "[Binary file “%1” (%2 bytes). Takoyo could not extract readable text "
                              "from it. Do not ask for the file path. If the user wants it summarized, "
                              "say this format is not readable here and suggest exporting it as PDF, "
                              "DOCX or plain text.]")
                              .arg(info.fileName())
                              .arg(data.size());
            }
        }
    }
    attachmentLoaded_ = true;
    attachmentKind_ = QStringLiteral("text");
    attachmentMime_.clear();
    attachmentSize_ = info.size();
    attachmentName_ = info.fileName();
    attachmentContent_ = content;
    attachmentPath_ = path;
    chipLabel_->setText(shortName(attachmentName_));
    chip_->show();
    updatePlaceholder();
    refreshButtons();
    emit changed();
}

void Composer::setThinking(bool thinking, const QString &label)
{
    thinking_ = thinking;
    if (thinking) {
        thinkBase_ = label;
        while (thinkBase_.endsWith(QChar(0x2026)) || thinkBase_.endsWith('.'))
            thinkBase_.chop(1);
        thinkBase_ = thinkBase_.trimmed();
        if (thinkBase_.isEmpty())
            thinkBase_ = QStringLiteral("Thinking");
        edit_->setReadOnly(true);
        thinkPhase_ = 0;
        thinkTimer_.start();
        updatePlaceholder();
    } else {
        thinkTimer_.stop();
        edit_->setReadOnly(false);
        updatePlaceholder();
    }
    refreshButtons();
}

void Composer::setStopVisible(bool visible)
{
    stopVisible_ = visible;
    refreshButtons();
}

void Composer::setReplyStyle(const QString &style)
{
    replyStyle_ = replyStyles().contains(style) ? style : QStringLiteral("balanced");
    if (styleBtn_)
        styleBtn_->setText(styleLabel(replyStyle_));
}

void Composer::tickThinking()
{
    if (!thinking_) {
        thinkTimer_.stop();
        return;
    }
    ++thinkPhase_;
    const int dots = thinkPhase_ % 3 + 1;
    placeholder_->setText(QStringLiteral("  ") + thinkBase_ + QString(dots, QChar('.')));
}

void Composer::refreshButtons()
{
    const bool hasText = !edit_->toPlainText().trimmed().isEmpty();
    if (thinking_) {
        micBtn_->hide();
        sendBtn_->hide();
        stopBtn_->setVisible(stopVisible_);
        attachBtn_->setEnabled(false);
        return;
    }
    attachBtn_->setEnabled(true);
    stopBtn_->hide();
    if (hasText || attachmentLoaded_) {
        micBtn_->hide();
        sendBtn_->show();
    } else {
        sendBtn_->hide();
        micBtn_->show();
    }
}

void Composer::updatePlaceholder()
{
    const bool show = thinking_ ? true : edit_->toPlainText().isEmpty();
    placeholderOn_ = show;
    if (!show) {
        placeholder_->hide();
        return;
    }
    if (thinking_) {
        const QPixmap icon = thinkingPixmap(17);
        placeholder_->setPixmap(icon.isNull() ? mascotPixmap(17) : icon);
        const int dots = thinkPhase_ % 3 + 1;
        placeholder_->setText(QStringLiteral("  ") + thinkBase_ + QString(dots, QChar('.')));
    } else {
        placeholder_->setPixmap(QPixmap());
        placeholder_->setText(QStringLiteral("Ask anything, or describe what to build…"));
    }
    placeholder_->adjustSize();
    placeholder_->move(18, 13);
    placeholder_->show();
    placeholder_->raise();
}

void Composer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (placeholderOn_) {
        placeholder_->adjustSize();
        placeholder_->move(18, 13);
    }
}

bool Composer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == edit_ && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && !(key->modifiers() & Qt::ShiftModifier)) {
            if (!thinking_)
                emit submitted();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void Composer::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect = QRectF(this->rect()).adjusted(0.8, 0.8, -0.8, -0.8);
    painter.setBrush(theme::map(theme::WHITE));
    painter.setPen(QPen(theme::map(theme::INK), 1.5));
    painter.drawRoundedRect(rect, 18, 18);
}
