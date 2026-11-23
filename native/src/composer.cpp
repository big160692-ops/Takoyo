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
#include <QPushButton>
#include <QResizeEvent>
#include <QStringList>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
QString shortName(const QString &name, int limit = 26)
{
    return util::shorten(name, limit);
}

// Response-style presets offered by the composer's "Balanced" chip.
const QStringList kPresets = {QStringLiteral("Concise"), QStringLiteral("Balanced"),
                              QStringLiteral("Detailed")};
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
    edit_->setAccessibleName(QStringLiteral("Message"));
    edit_->setAccessibleDescription(QStringLiteral("Type a message. Press Enter to send, Shift+Enter for a new line."));
    edit_->setFont(theme::font(15));
    edit_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit{background:transparent;border:none;color:%1;"
        "selection-background-color:%2;padding:0px;}")
        .arg(theme::INK.name(), theme::ACTIVE.name()));
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
    placeholder_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(theme::MUTED.name()));
    placeholder_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    placeholder_->setText(QStringLiteral("Ask anything, or describe what to build…"));

    bottom_ = new QWidget(this);
    bottom_->setFixedHeight(38);
    auto *bottomLayout = new QHBoxLayout(bottom_);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    attachBtn_ = new IconButton(QStringLiteral("paperclip"), 19, bottom_);
    attachBtn_->setColor(&theme::INK_SOFT);
    attachBtn_->setHoverColor(&theme::INK);
    attachBtn_->setToolTip(QStringLiteral("Attach a file"));
    connect(attachBtn_, &QAbstractButton::clicked, this, &Composer::attachFile);
    bottomLayout->addWidget(attachBtn_);

    folderBtn_ = new IconButton(QStringLiteral("folder"), 19, bottom_);
    folderBtn_->setColor(&theme::INK_SOFT);
    folderBtn_->setHoverColor(&theme::INK);
    folderBtn_->setToolTip(QStringLiteral("Pick a working folder for created files"));
    connect(folderBtn_, &QAbstractButton::clicked, this, [this]() {
        if (!folderPath_.isEmpty()) {
            folderPath_.clear();
            folderLabel_->hide();
            folderBtn_->setToolTip(QStringLiteral("Pick a working folder for created files"));
            emit folderChanged(QString());
            return;
        }
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Pick a working folder"), QDir::homePath());
        if (dir.isEmpty())
            return;
        folderPath_ = dir;
        folderLabel_->setText(util::shorten(QFileInfo(dir).fileName(), 20));
        folderLabel_->show();
        folderBtn_->setToolTip(QStringLiteral("Clear working folder"));
        emit folderChanged(dir);
    });
    bottomLayout->addWidget(folderBtn_);

    folderLabel_ = new QLabel(bottom_);
    folderLabel_->setFont(theme::font(10, QStringLiteral("medium")));
    folderLabel_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(theme::INK_SOFT.name()));
    folderLabel_->hide();
    bottomLayout->addWidget(folderLabel_);
    bottomLayout->addSpacing(4);

    chip_ = new QWidget(bottom_);
    chip_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *chipLayout = new QHBoxLayout(chip_);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(4);
    chipBody_ = new QWidget(chip_);
    chipBody_->setStyleSheet(QStringLiteral("background:%1;border-radius:8px;").arg(theme::CHIP.name()));
    auto *chipInner = new QHBoxLayout(chipBody_);
    chipInner->setContentsMargins(6, 3, 4, 3);
    chipInner->setSpacing(4);
    auto *clip = new QWidget(chipBody_);
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
            icons::paint(painter, name, rect(), color);
        }
    };
    auto *clipIcon = new MiniIcon(QStringLiteral("paperclip"), theme::MUTED, chipBody_);
    clipIcon->setFixedSize(11, 11);
    chipInner->addWidget(clipIcon);
    chipLabel_ = new QLabel(chipBody_);
    chipLabel_->setFont(theme::font(10, QStringLiteral("medium")));
    chipLabel_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(theme::INK_SOFT.name()));
    chipInner->addWidget(chipLabel_);
    auto *chipClose = new IconButton(QStringLiteral("close"), 11, chipBody_);
    chipClose->setColor(&theme::MUTED);
    chipClose->setHoverColor(&theme::INK);
    chipClose->setToolTip(QStringLiteral("Remove attachment"));
    connect(chipClose, &QAbstractButton::clicked, this, &Composer::clearAttachment);
    chipInner->addWidget(chipClose);
    chipLayout->addWidget(chipBody_);
    chip_->hide();
    bottomLayout->addWidget(chip_);

    presetBtn_ = new QPushButton(kPresets.at(presetIndex_), bottom_);
    presetBtn_->setCursor(Qt::PointingHandCursor);
    presetBtn_->setFixedHeight(26);
    presetBtn_->setFont(theme::font(11, QStringLiteral("medium")));
    presetBtn_->setToolTip(QStringLiteral("Response style"));
    presetBtn_->setAccessibleName(QStringLiteral("Response style"));
    presetBtn_->setAccessibleDescription(QStringLiteral("Cycle the response style: Concise, Balanced, Detailed."));
    presetBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;color:%1;border:1px solid %2;"
        "border-radius:13px;padding:0 13px;}"
        "QPushButton:hover{background:%3;}"
        "QPushButton:focus{border:1px solid %1;}")
        .arg(theme::INK.name(), theme::BORDER.name(), theme::HOVER.name()));
    connect(presetBtn_, &QPushButton::clicked, this, &Composer::cyclePreset);
    bottomLayout->addWidget(presetBtn_);
    bottomLayout->addSpacing(8);
    bottomLayout->addStretch(1);

    micBtn_ = new IconButton(QStringLiteral("mic"), 19, bottom_);
    micBtn_->setColor(&theme::MUTED);
    micBtn_->setHoverColor(&theme::INK);
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

void Composer::applyTheme()
{
    edit_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit{background:transparent;border:none;color:%1;"
        "selection-background-color:%2;padding:0px;}")
        .arg(theme::INK.name(), theme::ACTIVE.name()));
    placeholder_->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(theme::MUTED.name()));
    if (folderLabel_)
        folderLabel_->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;").arg(theme::INK_SOFT.name()));
    chipBody_->setStyleSheet(
        QStringLiteral("background:%1;border-radius:8px;").arg(theme::CHIP.name()));
    chipLabel_->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(theme::INK_SOFT.name()));
    presetBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;color:%1;border:1px solid %2;"
        "border-radius:13px;padding:0 13px;}"
        "QPushButton:hover{background:%3;}"
        "QPushButton:focus{border:1px solid %1;}")
        .arg(theme::INK.name(), theme::BORDER.name(), theme::HOVER.name()));
}

void Composer::setFolder(const QString &path)
{
    folderPath_ = path;
    if (path.isEmpty()) {
        folderLabel_->hide();
        folderBtn_->setToolTip(QStringLiteral("Pick a working folder for created files"));
    } else {
        folderLabel_->setText(util::shorten(QFileInfo(path).fileName(), 20));
        folderLabel_->show();
        folderBtn_->setToolTip(QStringLiteral("Clear working folder"));
    }
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

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = file.read(160001);
    file.close();
    QString content;
    if (data.size() > 160000) {
        content = QString::fromUtf8(data.left(160000));
        content += QStringLiteral("\n… (truncated)");
    } else {
        content = QString::fromUtf8(data);
        if (content.contains(QChar::ReplacementCharacter))
            content = QStringLiteral("[binary file · %1 bytes · path: %2]")
                          .arg(data.size())
                          .arg(QFileInfo(path).absoluteFilePath());
    }
    attachmentLoaded_ = true;
    attachmentKind_ = QStringLiteral("text");
    attachmentMime_.clear();
    attachmentSize_ = data.size();
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

void Composer::cyclePreset()
{
    presetIndex_ = (presetIndex_ + 1) % kPresets.size();
    const QString preset = kPresets.at(presetIndex_);
    if (presetBtn_)
        presetBtn_->setText(preset);
    emit presetChanged(preset);
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
    painter.setBrush(theme::WHITE);
    painter.setPen(QPen(theme::INK, 1.5));
    painter.drawRoundedRect(rect, 18, 18);
}
