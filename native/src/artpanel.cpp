#include "artpanel.h"

#include "icons.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QEnterEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVBoxLayout>

ArtPanel::ArtPanel(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(360);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);

    state_ = new QWidget(this);
    auto *stateLayout = new QVBoxLayout(state_);
    stateLayout->setContentsMargins(0, 0, 0, 0);
    stateLayout->setSpacing(0);
    stateLayout->addStretch(1);
    auto *mascot = new MascotLabel(92, state_);
    stateLayout->addWidget(mascot, 0, Qt::AlignHCenter);
    stateLayout->addSpacing(8);
    stateTitle_ = new QLabel(QStringLiteral("Takoyo Art"), state_);
    stateTitle_->setFont(theme::font(17, QStringLiteral("semibold")));
    theme::style(stateTitle_, QStringLiteral("color:#0B0B0D;background:transparent;"));
    stateTitle_->setAlignment(Qt::AlignCenter);
    stateLayout->addWidget(stateTitle_);
    stateSub_ = new QLabel(QStringLiteral("Images you make will appear here"), state_);
    stateSub_->setFont(theme::font(11));
    theme::style(stateSub_, QStringLiteral("color:#8B8B8F;background:transparent;"));
    stateSub_->setAlignment(Qt::AlignCenter);
    stateSub_->setWordWrap(true);
    stateSub_->setFixedWidth(300);
    stateLayout->addWidget(stateSub_, 0, Qt::AlignHCenter);
    stateLayout->addStretch(1);

    openBtn_ = new IconButton(QStringLiteral("external"), 16, this);
    saveBtn_ = new IconButton(QStringLiteral("download"), 16, this);
    regenBtn_ = new IconButton(QStringLiteral("refresh"), 16, this);
    closeBtn_ = new IconButton(QStringLiteral("close"), 16, this);
    for (IconButton *button : {openBtn_, saveBtn_, regenBtn_, closeBtn_}) {
        button->setColor(theme::INK_SOFT);
        button->setHoverColor(theme::INK);
    }
    openBtn_->setToolTip(QStringLiteral("Open image"));
    saveBtn_->setToolTip(QStringLiteral("Save a copy"));
    regenBtn_->setToolTip(QStringLiteral("Paint it again"));
    closeBtn_->setToolTip(QStringLiteral("Hide art panel"));
    connect(openBtn_, &QAbstractButton::clicked, this, &ArtPanel::openRequested);
    connect(saveBtn_, &QAbstractButton::clicked, this, &ArtPanel::saveRequested);
    connect(regenBtn_, &QAbstractButton::clicked, this, &ArtPanel::regenerateRequested);
    connect(closeBtn_, &QAbstractButton::clicked, this, &ArtPanel::closeRequested);

    pulseTimer_.setInterval(420);
    connect(&pulseTimer_, &QTimer::timeout, this, [this]() {
        if (!generating_)
            return;
        pulsePhase_++;
        stateTitle_->setText(QStringLiteral("Painting") + QString(pulsePhase_ % 3 + 1, QChar('.')));
    });

    setChromeVisible(false);
    setEmpty();
}

QRectF ArtPanel::cardRect() const
{
    return QRectF(rect()).adjusted(0.8, 16.8, -16.8, -16.8);
}

QRectF ArtPanel::stageRect() const
{
    return cardRect().adjusted(14, 14, -14, -14);
}

void ArtPanel::setEmpty()
{
    meta_ = QJsonObject();
    pixmap_ = QPixmap();
    generating_ = false;
    stopPulse();
    stateTitle_->setText(QStringLiteral("Takoyo Art"));
    stateSub_->setText(QStringLiteral("Images you make will appear here"));
    state_->show();
    setChromeVisible(false);
    update();
    placeChildren();
}

void ArtPanel::setGenerating(const QString &prompt)
{
    meta_ = QJsonObject();
    pixmap_ = QPixmap();
    generating_ = true;
    prompt_ = prompt;
    stateTitle_->setText(QStringLiteral("Painting"));
    stateSub_->setText(util::shorten(prompt, 60));
    state_->show();
    setChromeVisible(false);
    startPulse();
    update();
    placeChildren();
}

void ArtPanel::setArtifact(const QJsonObject &meta)
{
    meta_ = meta;
    prompt_ = meta.value(QStringLiteral("label")).toString();
    if (prompt_.isEmpty())
        prompt_ = meta.value(QStringLiteral("prompt")).toString();
    generating_ = false;
    stopPulse();
    const QString path = meta.value(QStringLiteral("path")).toString();
    pixmap_ = QFileInfo::exists(path) ? QPixmap(path) : QPixmap();
    state_->hide();
    setChromeVisible(true);
    update();
    placeChildren();
}

void ArtPanel::finishGenerating()
{
    generating_ = false;
    stopPulse();
    if (!meta_.isEmpty())
        setArtifact(meta_);
    else
        setEmpty();
}

void ArtPanel::startPulse()
{
    pulsePhase_ = 0;
    pulseTimer_.start();
}

void ArtPanel::stopPulse()
{
    pulseTimer_.stop();
}

void ArtPanel::setChromeVisible(bool visible)
{
    chrome_ = visible && !meta_.isEmpty();
    for (IconButton *button : {openBtn_, saveBtn_, regenBtn_, closeBtn_})
        button->setVisible(chrome_);
    update();
}

void ArtPanel::placeChildren()
{
    const QRectF stage = stageRect();
    if (state_->isVisible()) {
        state_->setGeometry(QRectF(stage).toRect());
    }
    if (chrome_) {
        const int buttonSize = 28;
        const int total = buttonSize * 4;
        const int y = static_cast<int>(stage.bottom()) - 14 - buttonSize;
        const int x = static_cast<int>(stage.right()) - 14 - total;
        openBtn_->move(x, y);
        saveBtn_->move(x + buttonSize, y);
        regenBtn_->move(x + buttonSize * 2, y);
        closeBtn_->move(x + buttonSize * 3, y);
        for (IconButton *button : {openBtn_, saveBtn_, regenBtn_, closeBtn_})
            button->raise();
    }
}

void ArtPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    placeChildren();
}

void ArtPanel::enterEvent(QEnterEvent *event)
{
    if (!meta_.isEmpty())
        setChromeVisible(true);
    QWidget::enterEvent(event);
}

void ArtPanel::leaveEvent(QEvent *event)
{
    setChromeVisible(false);
    QWidget::leaveEvent(event);
}

void ArtPanel::mouseMoveEvent(QMouseEvent *event)
{
    QWidget::mouseMoveEvent(event);
}

void ArtPanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF card = cardRect();
    painter.setBrush(theme::map(theme::WHITE));
    painter.setPen(QPen(theme::map(theme::INK), 1.6));
    painter.drawRoundedRect(card, 22, 22);

    const QRectF stage = stageRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme::map(theme::CHIP));
    painter.drawRect(stage);

    if (!pixmap_.isNull() && !generating_) {
        QPainterPath clip;
        clip.addRoundedRect(stage.adjusted(1, 1, -1, -1), 16, 16);
        painter.save();
        painter.setClipPath(clip);
        const QSize target = stage.adjusted(1, 1, -1, -1).size().toSize();
        const QPixmap scaled = pixmap_.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QRect source((scaled.width() - target.width()) / 2, (scaled.height() - target.height()) / 2,
                           target.width(), target.height());
        painter.drawPixmap(stage.adjusted(1, 1, -1, -1), scaled, QRectF(source));
        painter.restore();
    }

    if (chrome_) {
        const int buttonSize = 28;
        const int total = buttonSize * 4;
        const QRectF tools(stage.right() - 14 - total - 6, stage.bottom() - 14 - buttonSize - 6,
                           total + 12, buttonSize + 12);
        painter.setPen(QPen(theme::map(theme::LINE_SOFT), 1));
        painter.setBrush(theme::map(theme::WHITE));
        painter.drawRoundedRect(tools, 11, 11);

        if (!prompt_.isEmpty()) {
            const QString text = util::shorten(prompt_, 48);
            QFont font = theme::font(9, QStringLiteral("medium"));
            painter.setFont(font);
            const QFontMetrics metrics(font);
            const int textWidth = metrics.horizontalAdvance(text);
            const QRectF chip(stage.left() + 14, stage.bottom() - 14 - 28, textWidth + 22, 28);
            painter.setPen(QPen(theme::map(theme::LINE_SOFT), 1));
            painter.setBrush(theme::map(theme::WHITE));
            painter.drawRoundedRect(chip, 9, 9);
            painter.setPen(theme::map(theme::INK_SOFT));
            painter.drawText(chip, Qt::AlignCenter, text);
        }
    }
}
