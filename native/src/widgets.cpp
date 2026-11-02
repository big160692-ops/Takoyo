#include "widgets.h"

#include "icons.h"
#include "theme.h"
#include "util.h"

#include <QEnterEvent>
#include <QDir>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include <cmath>

QPixmap mascotPixmap(int size)
{
    static QPixmap source;
    if (source.isNull())
        source.load(QDir(util::assetsDir()).filePath(QStringLiteral("mascot.png")));
    if (source.isNull())
        return {};
    return source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

namespace {

// The thinking mascot ships as black ink on a white JPG. Turn luminance into
// alpha so it sits on any background like the transparent PNG mascot.
QImage thinkingInk()
{
    static QImage ink;
    if (!ink.isNull())
        return ink;
    QImage raw;
    if (!raw.load(QDir(util::assetsDir()).filePath(QStringLiteral("Mascot thinking.jpg"))))
        return ink;
    raw = raw.convertToFormat(QImage::Format_RGB32);
    QImage out(raw.size(), QImage::Format_ARGB32_Premultiplied);
    int left = raw.width(), top = raw.height(), right = -1, bottom = -1;
    for (int y = 0; y < raw.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(raw.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < raw.width(); ++x) {
            const int alpha = 255 - qGray(row[x]);
            const int a = alpha < 12 ? 0 : alpha;
            dst[x] = qPremultiply(qRgba(theme::INK.red(), theme::INK.green(), theme::INK.blue(), a));
            if (a > 12) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
    }
    if (right > left && bottom > top) {
        const int margin = qMax(2, qMin(raw.width(), raw.height()) / 60);
        const QRect box(qMax(0, left - margin), qMax(0, top - margin),
                        qMin(raw.width() - 1, right + margin) - qMax(0, left - margin) + 1,
                        qMin(raw.height() - 1, bottom + margin) - qMax(0, top - margin) + 1);
        ink = out.copy(box);
    } else {
        ink = out;
    }
    return ink;
}

} // namespace

QPixmap thinkingPixmap(int size)
{
    static QHash<int, QPixmap> cache;
    const auto found = cache.constFind(size);
    if (found != cache.constEnd())
        return found.value();
    const QImage ink = thinkingInk();
    if (ink.isNull())
        return {};
    const QPixmap pixmap = QPixmap::fromImage(
        ink.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    cache.insert(size, pixmap);
    return pixmap;
}

// ---------------------------------------------------------------------------
// RoundedCard
// ---------------------------------------------------------------------------
RoundedCard::RoundedCard(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
}

void RoundedCard::setRadius(int radius)
{
    radius_ = radius;
    update();
}

void RoundedCard::setFill(const QColor &fill)
{
    fill_ = fill;
    update();
}

void RoundedCard::setOutline(const QColor &outline)
{
    outline_ = outline;
    update();
}

void RoundedCard::setThickness(qreal thickness)
{
    thickness_ = thickness;
    update();
}

void RoundedCard::setPadding(int padding)
{
    padding_ = padding;
    layout_->setContentsMargins(padding, padding, padding, padding);
    update();
}

void RoundedCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal inset = outline_.isValid() ? thickness_ / 2.0 : 0.0;
    const QRectF rect = QRectF(this->rect()).adjusted(inset, inset, -inset, -inset);
    painter.setBrush(fill_);
    painter.setPen(outline_.isValid() ? QPen(outline_, thickness_) : QPen(Qt::NoPen));
    painter.drawRoundedRect(rect, radius_, radius_);
}

// ---------------------------------------------------------------------------
// IconButton
// ---------------------------------------------------------------------------
IconButton::IconButton(const QString &icon, int iconSize, QWidget *parent)
    : QAbstractButton(parent)
    , icon_(icon)
    , iconSize_(iconSize)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(iconSize + pad_ * 2, iconSize + pad_ * 2);
}

void IconButton::setIconName(const QString &icon)
{
    icon_ = icon;
    update();
}

void IconButton::setColor(const QColor &color)
{
    color_ = color;
    update();
}

void IconButton::setHoverColor(const QColor &color)
{
    hoverColor_ = color;
    update();
}

void IconButton::setHoverBackground(const QColor &color)
{
    hoverBackground_ = color;
    update();
}

void IconButton::setCircleFill(const QColor &color)
{
    circleFill_ = color;
    update();
}

void IconButton::setCircleOutline(const QColor &color)
{
    circleOutline_ = color;
    update();
}

QSize IconButton::sizeHint() const
{
    return {iconSize_ + pad_ * 2, iconSize_ + pad_ * 2};
}

void IconButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF full = QRectF(this->rect());
    if (circleFill_.alpha() > 0) {
        painter.setPen(circleOutline_.alpha() > 0 ? QPen(circleOutline_, 1.2) : QPen(Qt::NoPen));
        painter.setBrush(circleFill_);
        painter.drawEllipse(full.adjusted(1, 1, -1, -1));
    } else if (isEnabled() && underMouse() && hoverBackground_.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(hoverBackground_);
        painter.drawRoundedRect(full, 9, 9);
    }
    const QColor tint = isEnabled() ? (underMouse() && hoverColor_.isValid() ? hoverColor_ : color_) : theme::FAINT;
    const qreal side = iconSize_;
    const QRectF iconRect((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    icons::paint(painter, icon_, iconRect, tint);
}

void IconButton::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void IconButton::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

CircleButton::CircleButton(const QString &icon, int diameter, QWidget *parent)
    : IconButton(icon, static_cast<int>(diameter * 0.42), parent)
{
    setFixedSize(diameter, diameter);
    setCircleFill(theme::INK);
    setColor(theme::WHITE);
    setHoverColor(theme::WHITE);
}

// ---------------------------------------------------------------------------
// Wordmark
// ---------------------------------------------------------------------------
Wordmark::Wordmark(int height, QWidget *parent)
    : QWidget(parent)
    , height_(height)
{
    setFixedHeight(height);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize Wordmark::sizeHint() const
{
    QFont f = theme::font(static_cast<int>(height_ * 1.32), QStringLiteral("bold"));
    f.setLetterSpacing(QFont::AbsoluteSpacing, -height_ * 0.04);
    QFontMetrics metrics(f);
    return {metrics.horizontalAdvance(QStringLiteral("TAKOYO")) + 2, height_};
}

void Wordmark::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont f = theme::font(static_cast<int>(height_ * 1.32), QStringLiteral("bold"));
    f.setLetterSpacing(QFont::AbsoluteSpacing, -height_ * 0.04);
    painter.setFont(f);
    painter.setPen(theme::INK);
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("TAKOYO"));
}

// ---------------------------------------------------------------------------
// Mascot
// ---------------------------------------------------------------------------
MascotLabel::MascotLabel(int size, QWidget *parent)
    : QWidget(parent)
    , size_(size)
{
    setFixedSize(size, size);
}

QSize MascotLabel::sizeHint() const
{
    return {size_, size_};
}

void MascotLabel::setMascotSize(int size)
{
    size_ = size;
    setFixedSize(size, size);
    update();
}

void MascotLabel::setThinking(bool thinking)
{
    if (thinking_ == thinking)
        return;
    thinking_ = thinking;
    update();
}

void MascotLabel::setFloatOffset(qreal offset)
{
    if (qFuzzyCompare(offset_ + 1.0, offset + 1.0))
        return;
    offset_ = offset;
    update();
}

void MascotLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!qFuzzyIsNull(offset_))
        painter.translate(0, offset_);
    const QPixmap pixmap = thinking_ ? thinkingPixmap(size_) : mascotPixmap(size_);
    if (pixmap.isNull())
        return;
    QRectF target(QPointF(0, 0), QSizeF(pixmap.size()));
    target.moveCenter(QRectF(rect()).center());
    painter.drawPixmap(target, pixmap, QRectF(pixmap.rect()));
}

// ---------------------------------------------------------------------------
// StatusDot
// ---------------------------------------------------------------------------
StatusDot::StatusDot(int size, QWidget *parent)
    : QWidget(parent)
    , size_(size)
    , color_(theme::FAINT)
    , baseColor_(theme::FAINT)
{
    setFixedSize(size, size);
    timer_.setInterval(60);
    connect(&timer_, &QTimer::timeout, this, [this]() {
        phase_ += 0.09;
        const qreal wave = (std::sin(phase_) + 1.0) / 2.0;
        const qreal t = 0.15 + wave * 0.55;
        color_ = QColor(
            static_cast<int>(theme::SUCCESS.red() + (0x5A - theme::SUCCESS.red()) * t),
            static_cast<int>(theme::SUCCESS.green() + (0xC9 - theme::SUCCESS.green()) * t),
            static_cast<int>(theme::SUCCESS.blue() + (0x8D - theme::SUCCESS.blue()) * t));
        update();
    });
}

void StatusDot::setColor(const QColor &color)
{
    baseColor_ = color;
    color_ = color;
    update();
}

void StatusDot::setPulsing(bool pulsing)
{
    if (pulsing && !timer_.isActive()) {
        phase_ = 0;
        timer_.start();
    } else if (!pulsing && timer_.isActive()) {
        timer_.stop();
        color_ = baseColor_;
        update();
    }
}

void StatusDot::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color_);
    painter.drawEllipse(QRectF(1, 1, width() - 2, height() - 2));
}

// ---------------------------------------------------------------------------
// Toast
// ---------------------------------------------------------------------------
namespace {
class ToastGlyph : public QWidget {
public:
    ToastGlyph(const QString &name, const QColor &color, QWidget *parent)
        : QWidget(parent)
        , name_(name)
        , color_(color)
    {
        setFixedSize(15, 15);
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
} // namespace

Toast::Toast(QWidget *parent, const QString &text, const QString &icon)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 18, 10);
    layout->setSpacing(9);
    layout->addWidget(new ToastGlyph(icon, theme::INK, this));

    auto *label = new QLabel(text, this);
    label->setFont(theme::font(12, QStringLiteral("medium")));
    label->setStyleSheet(QStringLiteral("color:#0B0B0D;background:transparent;"));
    layout->addWidget(label);
    adjustSize();

    life_.setSingleShot(true);
    life_.setInterval(1900);
    connect(&life_, &QTimer::timeout, this, [this]() {
        auto *effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
        auto *fade = new QPropertyAnimation(effect, "opacity", this);
        fade->setDuration(260);
        fade->setStartValue(1.0);
        fade->setEndValue(0.0);
        connect(fade, &QPropertyAnimation::finished, this, &QObject::deleteLater);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    });
    life_.start();
}

void Toast::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect = QRectF(this->rect()).adjusted(0.8, 0.8, -0.8, -0.8);
    painter.setBrush(theme::WHITE);
    painter.setPen(QPen(theme::INK, 1.4));
    painter.drawRoundedRect(rect, 13, 13);
}

void Toast::popup(const QPoint &topCenter)
{
    show();
    raise();
    move(topCenter.x() - width() / 2, topCenter.y());
}

// ---------------------------------------------------------------------------
// SearchField
// ---------------------------------------------------------------------------
SearchField::SearchField(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(42);
    edit_ = new QLineEdit(this);
    edit_->setFrame(false);
    edit_->setFont(theme::font(14));
    edit_->setPlaceholderText(QStringLiteral("Search"));
    edit_->setStyleSheet(QStringLiteral(
        "QLineEdit{background:transparent;border:none;color:#3A3B3E;selection-background-color:#DEDEDF;padding:0px;}"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(44, 0, 16, 0);
    layout->addWidget(edit_);
}

void SearchField::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect = QRectF(this->rect()).adjusted(0.8, 0.8, -0.8, -0.8);
    painter.setBrush(theme::SEARCH_BG);
    painter.setPen(QPen(theme::INK, 1.6));
    painter.drawRoundedRect(rect, qMin(22.0, height() / 2.0), qMin(22.0, height() / 2.0));
    icons::paint(painter, QStringLiteral("search"), QRectF(13, (height() - 18) / 2.0, 18, 18), theme::INK);
}

// ---------------------------------------------------------------------------
// TextButton
// ---------------------------------------------------------------------------
TextButton::TextButton(const QString &text, Variant variant, int height, QWidget *parent)
    : QAbstractButton(parent)
    , variant_(variant)
    , height_(height)
{
    setText(text);
    setFixedHeight(height);
    setCursor(Qt::PointingHandCursor);
    setFont(theme::font(11, QStringLiteral("semibold")));
    setFocusPolicy(Qt::NoFocus);
}

void TextButton::setFontSize(int size)
{
    fontSize_ = size;
    setFont(theme::font(size, QStringLiteral("semibold")));
    updateGeometry();
    update();
}

QSize TextButton::sizeHint() const
{
    const QFontMetrics metrics(font());
    return {metrics.horizontalAdvance(text()) + 30, height_};
}

void TextButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect = QRectF(this->rect()).adjusted(0.8, 0.8, -0.8, -0.8);
    QColor fill = theme::WHITE;
    QColor line = theme::INK;
    QColor textColor = theme::INK;
    if (variant_ == Variant::Primary) {
        fill = theme::INK;
        line = theme::INK;
        textColor = theme::WHITE;
    } else if (variant_ == Variant::Danger) {
        fill = theme::WHITE;
        line = theme::DANGER;
        textColor = theme::DANGER;
    }
    if (!isEnabled())
        textColor = theme::FAINT;
    painter.setBrush(fill);
    painter.setPen(QPen(line, 1.3));
    painter.drawRoundedRect(rect, 10, 10);
    painter.setFont(theme::font(fontSize_, QStringLiteral("semibold")));
    painter.setPen(textColor);
    painter.drawText(rect, Qt::AlignCenter, text());
}

// ---------------------------------------------------------------------------
// HoverRow
// ---------------------------------------------------------------------------
HoverRow::HoverRow(QWidget *parent)
    : QWidget(parent)
    , base_(theme::WHITE)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
}

void HoverRow::setSelected(bool selected)
{
    selected_ = selected;
    update();
}

void HoverRow::setBaseColor(const QColor &color)
{
    base_ = color;
    update();
}

void HoverRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor color = selected_ ? theme::CHIP : base_;
    if (hovered_ && !selected_)
        color = theme::HOVER;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(rect()), 10, 10);
}

void HoverRow::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void HoverRow::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

void HoverRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
    QWidget::mouseReleaseEvent(event);
}
