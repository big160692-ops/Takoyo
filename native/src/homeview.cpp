#include "homeview.h"

#include "composer.h"
#include "icons.h"
#include "store.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QEnterEvent>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kInnerMax = 760;
constexpr int kInnerMargin = 64;
const QString kWideTagline = QStringLiteral("Want to build, generate, or just chat?\nLet me know.");
const QString kNarrowTagline = QStringLiteral("Want to build, generate, or just chat? Let me know.");

class StarterChip : public QAbstractButton {
public:
    StarterChip(const QString &icon, const QString &text, QWidget *parent)
        : QAbstractButton(parent)
        , icon_(icon)
        , text_(text)
    {
        setCursor(Qt::PointingHandCursor);
        setFont(theme::font(11, QStringLiteral("medium")));
        const QFontMetrics metrics(font());
        setFixedSize(14 + 15 + 7 + metrics.horizontalAdvance(text_) + 14, 34);
        setToolTip(text_);
    }

    QSize sizeHint() const override { return {width(), 34}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF body = QRectF(rect()).adjusted(0.7, 0.7, -0.7, -0.7);
        painter.setBrush(theme::map(hovered_ ? theme::HOVER : theme::WHITE));
        painter.setPen(QPen(theme::map(hovered_ ? theme::FAINT : theme::LINE_SOFT), 1.2));
        painter.drawRoundedRect(body, 17, 17);
        const QRectF iconRect(14, (height() - 15) / 2.0, 15, 15);
        icons::paint(painter, icon_, iconRect, theme::map(theme::INK_SOFT));
        painter.setPen(theme::map(theme::INK_SOFT));
        painter.setFont(font());
        painter.drawText(QRectF(14 + 15 + 7, 0, width() - (14 + 15 + 7) - 12, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text_);
    }

    void enterEvent(QEnterEvent *) override
    {
        hovered_ = true;
        update();
    }

    void leaveEvent(QEvent *) override
    {
        hovered_ = false;
        update();
    }

private:
    QString icon_;
    QString text_;
    bool hovered_ = false;
};

class FolderIcon : public QWidget {
public:
    explicit FolderIcon(QWidget *parent) : QWidget(parent) { setFixedSize(14, 14); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        icons::paint(painter, QStringLiteral("folder"), rect(), theme::map(theme::MUTED));
    }
};
} // namespace

HomeView::HomeView(QWidget *parent)
    : QWidget(parent)
{
    theme::setSurface(this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addStretch(1);

    inner_ = new QWidget(this);
    auto *innerLayout = new QVBoxLayout(inner_);
    innerLayout->setContentsMargins(0, 0, 0, 0);
    innerLayout->setSpacing(0);

    scope_ = new QWidget(inner_);
    auto *scopeLayout = new QHBoxLayout(scope_);
    scopeLayout->setContentsMargins(0, 0, 0, 0);
    scopeLayout->setSpacing(6);
    scopeLayout->addStretch(1);
    scopeLayout->addWidget(new FolderIcon(scope_));
    scopeLabel_ = new QLabel(scope_);
    scopeLabel_->setFont(theme::font(12, QStringLiteral("medium")));
    theme::style(scopeLabel_, QStringLiteral("color:%1;background:transparent;").arg(theme::MUTED_STRONG.name()));
    scopeLabel_->setCursor(Qt::PointingHandCursor);
    scopeLayout->addWidget(scopeLabel_);
    scopeLayout->addStretch(1);
    scope_->hide();
    innerLayout->addWidget(scope_);

    mascotHolder_ = new QWidget(inner_);
    mascotHolder_->setFixedSize(260, 256);
    mascot_ = new MascotLabel(238, mascotHolder_);
    mascot_->move((260 - 238) / 2, (256 - 238) / 2);
    innerLayout->addWidget(mascotHolder_, 0, Qt::AlignHCenter);
    innerLayout->addSpacing(18);

    tagline_ = new QLabel(kWideTagline, inner_);
    tagline_->setFont(theme::font(26));
    theme::style(tagline_, QStringLiteral("color:#0B0B0D;background:transparent;"));
    tagline_->setAlignment(Qt::AlignCenter);
    tagline_->setWordWrap(true);
    tagline_->setMargin(0);
    innerLayout->addWidget(tagline_);
    innerLayout->addSpacing(18);

    auto *chips = new QHBoxLayout();
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(8);
    chips->addStretch(1);
    const auto addChip = [&](const QString &icon, const QString &text, const QString &seed, bool attach) {
        auto *chip = new StarterChip(icon, text, inner_);
        connect(chip, &QAbstractButton::clicked, this, [this, seed, attach]() {
            if (attach) {
                composer_->promptAttachment();
                if (!composer_->hasAttachment())
                    return;
            }
            composer_->setText(seed);
            composer_->focusInput();
        });
        chips->addWidget(chip);
    };
    addChip(QStringLiteral("code"), QStringLiteral("Write code"), QStringLiteral("Write code that "), false);
    addChip(QStringLiteral("image"), QStringLiteral("Generate image"), QStringLiteral("create an image of "), false);
    addChip(QStringLiteral("file"), QStringLiteral("Summarize a file"), QStringLiteral("Summarize this file"), true);
    addChip(QStringLiteral("sparkle"), QStringLiteral("Brainstorm"), QStringLiteral("Brainstorm ideas for "), false);
    chips->addStretch(1);
    innerLayout->addLayout(chips);
    innerLayout->addSpacing(22);

    composer_ = new Composer(inner_);
    innerLayout->addWidget(composer_);

    auto *center = new QHBoxLayout();
    center->setContentsMargins(0, 0, 0, 0);
    center->addStretch(1);
    center->addWidget(inner_);
    center->addStretch(1);
    outer->addLayout(center);
    outer->addStretch(1);

    floatTimer_.setInterval(16);
    floatTimer_.setTimerType(Qt::PreciseTimer);
    connect(&floatTimer_, &QTimer::timeout, this, &HomeView::updateFloat);
    layoutInner();
}

void HomeView::setProject(const ProjectSummary *project)
{
    if (project) {
        scopeLabel_->setText(QStringLiteral("Working in ") + util::shorten(project->name, 30));
        scope_->show();
    } else {
        scope_->hide();
    }
}

void HomeView::setWorkspaceFolder(const QString &path)
{
    if (path.isEmpty()) {
        scope_->hide();
        return;
    }
    const QString name = QDir(path).dirName();
    scopeLabel_->setText(QStringLiteral("Working in ") + util::shorten(name.isEmpty() ? path : name, 30));
    scope_->setToolTip(path);
    scope_->show();
}

void HomeView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutInner();
}

void HomeView::layoutInner()
{
    int target = qMin(kInnerMax, qMax(220, width() - kInnerMargin));
    target = qMin(target, qMax(1, width() - 8));
    if (target == innerWidth_)
        return;
    innerWidth_ = target;
    inner_->setFixedWidth(target);
    const bool narrow = target < 660;
    if (narrow != narrow_) {
        narrow_ = narrow;
        tagline_->setText(narrow ? kNarrowTagline : kWideTagline);
    }
    tagline_->setFixedWidth(target);
}

void HomeView::mousePressEvent(QMouseEvent *event)
{
    if (scope_->isVisible()) {
        const QRect area(scope_->mapTo(this, QPoint(0, 0)), scope_->size());
        if (area.contains(event->position().toPoint())) {
            emit projectFilesRequested();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void HomeView::startFloat()
{
    if (floating_)
        return;
    floating_ = true;
    floatClock_.start();
    floatTimer_.start();
}

void HomeView::setMascotThinking(bool thinking)
{
    mascot_->setThinking(thinking);
}

void HomeView::celebrate()
{
    mascot_->celebrate();
}

void HomeView::stopFloat()
{
    floating_ = false;
    floatTimer_.stop();
    mascot_->setFloatOffset(0);
}

void HomeView::updateFloat()
{
    if (!floating_)
        return;
    const qreal elapsed = floatClock_.elapsed() / 1000.0;
    mascot_->setFloatOffset(std::sin(elapsed * 2.0 * 3.14159265358979323846 / 4.6) * 5.0);
}
