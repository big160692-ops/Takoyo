#include "homeview.h"

#include "composer.h"
#include "icons.h"
#include "store.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QFontMetrics>
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

// An outlined quick-action pill shown under the home tagline.
class ActionChip : public QAbstractButton {
public:
    ActionChip(const QString &icon, const QString &label, QWidget *parent)
        : QAbstractButton(parent)
        , icon_(icon)
        , label_(label)
    {
        setCursor(Qt::PointingHandCursor);
        setFont(theme::font(13, QStringLiteral("medium")));
        setToolTip(label);
        setAccessibleName(label);
        setFocusPolicy(Qt::StrongFocus);
        setFixedHeight(40);
        const int textWidth = QFontMetrics(font()).horizontalAdvance(label_);
        setFixedWidth(textWidth + kIconSize + kGap + kPadX * 2);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(rect()).adjusted(0.7, 0.7, -0.7, -0.7);
        painter.setPen(QPen(hasFocus() ? theme::INK : theme::BORDER, 1.2));
        painter.setBrush(underMouse() ? theme::HOVER : theme::WHITE);
        painter.drawRoundedRect(box, height() / 2.0, height() / 2.0);

        const QFontMetrics metrics(font());
        const int textWidth = metrics.horizontalAdvance(label_);
        const int total = kIconSize + kGap + textWidth;
        const int left = (width() - total) / 2;
        icons::paint(painter, icon_,
                     QRectF(left, (height() - kIconSize) / 2.0, kIconSize, kIconSize),
                     theme::INK);
        painter.setFont(font());
        painter.setPen(theme::INK);
        painter.drawText(QRect(left + kIconSize + kGap, 0, textWidth, height()),
                         Qt::AlignVCenter, label_);
    }

private:
    static constexpr int kIconSize = 16;
    static constexpr int kGap = 8;
    static constexpr int kPadX = 14;
    QString icon_;
    QString label_;
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
    scopeLabel_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(theme::MUTED.name()));
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
    tagline_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(theme::INK.name()));
    tagline_->setAlignment(Qt::AlignCenter);
    tagline_->setWordWrap(true);
    tagline_->setMargin(0);
    innerLayout->addWidget(tagline_);
    innerLayout->addSpacing(22);

    auto *chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(10);
    chipRow->addStretch(1);
    const struct {
        const char *icon;
        const char *label;
        const char *prompt;
    } kActions[] = {
        {"code", "Write code", "Write code for: "},
        {"image", "Generate image", "Generate an image of "},
        {"file", "Summarize a file", "Summarize this file: "},
        {"brain", "Brainstorm", "Brainstorm ideas for "},
    };
    for (const auto &action : kActions) {
        auto *chip = new ActionChip(QString::fromLatin1(action.icon),
                                    QString::fromLatin1(action.label), inner_);
        const QString prompt = QString::fromLatin1(action.prompt);
        connect(chip, &QAbstractButton::clicked, this, [this, prompt]() {
            composer_->setText(prompt);
        });
        chipRow->addWidget(chip);
    }
    chipRow->addStretch(1);
    innerLayout->addLayout(chipRow);
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

void HomeView::applyTheme()
{
    scopeLabel_->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(theme::MUTED.name()));
    tagline_->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(theme::INK.name()));
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
