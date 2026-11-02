#include "homeview.h"

#include "composer.h"
#include "icons.h"
#include "store.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

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
const QString kWideTagline = QStringLiteral("Want to build, generate, or just want a\nchat. Let me Know");
const QString kNarrowTagline = QStringLiteral("Want to build, generate, or just want a chat. Let me Know");

class FolderIcon : public QWidget {
public:
    explicit FolderIcon(QWidget *parent) : QWidget(parent) { setFixedSize(14, 14); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        icons::paint(painter, QStringLiteral("folder"), rect(), theme::MUTED);
    }
};
} // namespace

HomeView::HomeView(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme::WHITE);
    setPalette(pal);

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
    scopeLabel_->setFont(theme::font(11, QStringLiteral("medium")));
    scopeLabel_->setStyleSheet(QStringLiteral("color:#8B8B8F;background:transparent;"));
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
    tagline_->setStyleSheet(QStringLiteral("color:#0B0B0D;background:transparent;"));
    tagline_->setAlignment(Qt::AlignCenter);
    tagline_->setWordWrap(true);
    tagline_->setMargin(0);
    innerLayout->addWidget(tagline_);
    innerLayout->addSpacing(24);

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
