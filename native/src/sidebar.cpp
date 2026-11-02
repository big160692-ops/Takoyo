#include "sidebar.h"

#include <QMouseEvent>

#include "icons.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <functional>

namespace {

class ClickableWidget : public QWidget {
public:
    explicit ClickableWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onClick)
            onClick();
        QWidget::mouseReleaseEvent(event);
    }
};

class SmallIcon : public QWidget {
public:
    SmallIcon(const QString &name, const QColor &color, int size, QWidget *parent)
        : QWidget(parent)
        , name_(name)
        , color_(color)
    {
        setFixedSize(size, size);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        icons::paint(painter, name_, rect(), color_);
    }

private:
    QString name_;
    QColor color_;
};

QLabel *makeLabel(const QString &text, int size, const QColor &color, const QString &weight = QStringLiteral("regular"),
                  QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setFont(theme::font(size, weight));
    label->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(color.name()));
    return label;
}

void addSectionHeading(QVBoxLayout *layout, const QString &text, QWidget *parent, std::function<void()> onNew = {})
{
    auto *head = new QWidget(parent);
    auto *row = new QHBoxLayout(head);
    row->setContentsMargins(10, 0, 10, 0);
    row->setSpacing(0);
    auto *label = makeLabel(text, 11, theme::INK, QStringLiteral("semibold"), head);
    row->addWidget(label);
    row->addStretch(1);
    if (onNew) {
        auto *plus = new IconButton(QStringLiteral("plus"), 17, head);
        plus->setColor(theme::INK);
        plus->setHoverColor(theme::INK);
        plus->setToolTip(QStringLiteral("New project"));
        QObject::connect(plus, &QAbstractButton::clicked, plus, onNew);
        row->addWidget(plus);
    }
    layout->addWidget(head);
}

} // namespace

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(396);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 0, 16);
    outer->setSpacing(0);

    auto *card = new RoundedCard(this);
    card->setRadius(22);
    card->setFill(theme::WHITE);
    card->setOutline(theme::INK);
    card->setThickness(1.6);
    card->setPadding(8);
    outer->addWidget(card);

    auto *body = card->content();

    auto *head = new QWidget(card);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(18, 16, 18, 12);
    headLayout->setSpacing(9);
    auto *toggle = new IconButton(QStringLiteral("sidebar"), 24, head);
    toggle->setColor(theme::INK);
    toggle->setHoverColor(theme::INK);
    toggle->setToolTip(QStringLiteral("Hide sidebar"));
    connect(toggle, &QAbstractButton::clicked, this, &Sidebar::hideRequested);
    headLayout->addWidget(toggle);

    auto *brand = new ClickableWidget(head);
    brand->setFixedHeight(20);
    auto *brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->addWidget(new Wordmark(19, brand));
    brand->onClick = [this]() { emit homeRequested(); };
    headLayout->addWidget(brand);
    headLayout->addStretch(1);
    body->addWidget(head);

    search_ = new SearchField(card);
    auto *searchWrap = new QWidget(card);
    auto *searchLayout = new QHBoxLayout(searchWrap);
    searchLayout->setContentsMargins(8, 0, 8, 12);
    searchLayout->addWidget(search_);
    body->addWidget(searchWrap);

    auto *scroll = new QScrollArea(card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea{background:transparent;border:none;}"
        "QScrollBar:vertical{width:8px;background:transparent;margin:2px 0 2px 0;}"
        "QScrollBar::handle:vertical{background:#E7E7E9;border-radius:4px;min-height:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"));
    auto *content = new QWidget(scroll);
    content->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 4);
    contentLayout->setSpacing(3);

    addSectionHeading(contentLayout, QStringLiteral("PROJECTS"), content, [this]() { emit newProjectRequested(); });
    auto *projectsHost = new QWidget(content);
    projectsBox_ = new QVBoxLayout(projectsHost);
    projectsBox_->setContentsMargins(4, 0, 4, 10);
    projectsBox_->setSpacing(2);
    contentLayout->addWidget(projectsHost);

    addSectionHeading(contentLayout, QStringLiteral("RECENTS"), content);
    auto *recentsHost = new QWidget(content);
    recentsBox_ = new QVBoxLayout(recentsHost);
    recentsBox_->setContentsMargins(4, 0, 4, 12);
    recentsBox_->setSpacing(2);
    contentLayout->addWidget(recentsHost);
    contentLayout->addStretch(1);
    scroll->setWidget(content);
    body->addWidget(scroll, 1);

    auto *footer = new QWidget(card);
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(0);
    auto *line = new QWidget(footer);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#E7E7E9;"));
    auto *lineWrap = new QWidget(footer);
    auto *lineLayout = new QHBoxLayout(lineWrap);
    lineLayout->setContentsMargins(16, 4, 16, 0);
    lineLayout->addWidget(line);
    footerLayout->addWidget(lineWrap);

    auto *status = new HoverRow(footer);
    auto *statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(16, 10, 16, 10);
    statusLayout->setSpacing(8);
    dot_ = new StatusDot(9, status);
    statusLayout->addWidget(dot_);
    statusLabel_ = makeLabel(QStringLiteral("Built-in AI"), 10, theme::MUTED, QStringLiteral("medium"), status);
    statusLayout->addWidget(statusLabel_);
    statusLayout->addStretch(1);
    auto *gear = new IconButton(QStringLiteral("gear"), 15, status);
    gear->setColor(theme::MUTED);
    gear->setHoverColor(theme::INK);
    connect(gear, &QAbstractButton::clicked, this, &Sidebar::settingsRequested);
    statusLayout->addWidget(gear);
    connect(status, &HoverRow::clicked, this, &Sidebar::settingsRequested);
    auto *statusWrap = new QWidget(footer);
    auto *statusWrapLayout = new QHBoxLayout(statusWrap);
    statusWrapLayout->setContentsMargins(8, 4, 8, 8);
    statusWrapLayout->addWidget(status);
    footerLayout->addWidget(statusWrap);
    body->addWidget(footer);

    debounce_.setSingleShot(true);
    debounce_.setInterval(120);
    connect(&debounce_, &QTimer::timeout, this, &Sidebar::applyQuery);
    connect(search_->edit(), &QLineEdit::textChanged, this, [this](const QString &) {
        debounce_.start();
    });
}

void Sidebar::refresh(const QString &query, const QString &selectedProject, const QString &selectedConversation)
{
    query_ = query.trimmed();
    selectedProject_ = selectedProject;
    selectedConversation_ = selectedConversation;
    applyQuery();
}

QString Sidebar::query() const
{
    return search_->edit()->text();
}

void Sidebar::applyQuery()
{
    const ProjectStore projects(util::projectsDir());
    const ConversationStore conversations(util::conversationsDir());

    clearBox(projectsBox_);
    const QList<ProjectSummary> projectItems = projects.list(query_);
    if (projectItems.isEmpty()) {
        auto *empty = makeLabel(query_.isEmpty() ? QStringLiteral("No projects yet") : QStringLiteral("No matches"), 10,
                                theme::FAINT, QStringLiteral("regular"), this);
        empty->setContentsMargins(10, 2, 10, 4);
        projectsBox_->addWidget(empty);
    }
    for (const ProjectSummary &project : projectItems.mid(0, 24))
        buildProjectRow(projectsBox_, project, project.id == selectedProject_);

    clearBox(recentsBox_);
    const QList<ConversationSummary> recentItems = conversations.summaries(QString(), query_);
    if (recentItems.isEmpty()) {
        auto *empty = makeLabel(query_.isEmpty() ? QStringLiteral("Your chats will live here") : QStringLiteral("No matches"),
                                10, theme::FAINT, QStringLiteral("regular"), this);
        empty->setContentsMargins(10, 2, 10, 2);
        recentsBox_->addWidget(empty);
    }
    for (const ConversationSummary &summary : recentItems.mid(0, 30))
        buildRecentRow(recentsBox_, summary, summary.id == selectedConversation_);
}

void Sidebar::clearBox(QVBoxLayout *box)
{
    while (QLayoutItem *item = box->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

void Sidebar::buildProjectRow(QVBoxLayout *box, const ProjectSummary &project, bool selected)
{
    auto *row = new HoverRow(this);
    row->setSelected(selected);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);
    layout->addWidget(new SmallIcon(QStringLiteral("folder"), selected ? theme::INK : theme::MUTED, 15, row));

    auto *text = new QWidget(row);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);
    auto *name = makeLabel(util::shorten(project.name, 30), 12, theme::INK, QStringLiteral("medium"), text);
    textLayout->addWidget(name);
    const int count = project.fileCount;
    textLayout->addWidget(makeLabel(count == 1 ? QStringLiteral("1 file") : QStringLiteral("%1 files").arg(count), 9,
                                    theme::MUTED, QStringLiteral("regular"), text));
    layout->addWidget(text, 1);

    auto *more = new IconButton(QStringLiteral("more"), 14, row);
    more->setColor(theme::MUTED);
    more->setHoverColor(theme::INK);
    connect(more, &QAbstractButton::clicked, this, [this, row, project]() {
        emit projectMenuRequested(project.id, row->mapToGlobal(QPoint(row->width() - 4, row->height())));
    });
    layout->addWidget(more);

    connect(row, &HoverRow::clicked, this, [this, project]() { emit openProjectRequested(project.id); });
    row->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(row, &QWidget::customContextMenuRequested, this, [this, row, project](const QPoint &pos) {
        emit projectMenuRequested(project.id, row->mapToGlobal(pos));
    });
    box->addWidget(row);
}

void Sidebar::buildRecentRow(QVBoxLayout *box, const ConversationSummary &summary, bool selected)
{
    auto *row = new HoverRow(this);
    row->setSelected(selected);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(8);
    layout->addWidget(new SmallIcon(QStringLiteral("chat"), selected ? theme::INK : theme::MUTED, 14, row));

    auto *text = new QWidget(row);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);
    textLayout->addWidget(makeLabel(util::shorten(summary.title, 34), 11, theme::INK, QStringLiteral("medium"), text));

    QStringList bits;
    if (!summary.projectId.isEmpty()) {
        const ProjectStore store(util::projectsDir());
        ProjectSummary project;
        if (store.get(summary.projectId, &project))
            bits << project.name;
    }
    if (summary.hasImage)
        bits << QStringLiteral("image");
    if (!bits.isEmpty())
        textLayout->addWidget(makeLabel(bits.join(QStringLiteral(" · ")), 9, theme::MUTED, QStringLiteral("regular"),
                                        text));
    layout->addWidget(text, 1);
    layout->addWidget(makeLabel(util::ago(summary.updated), 9, theme::FAINT, QStringLiteral("regular"), row), 0,
                      Qt::AlignRight | Qt::AlignVCenter);

    connect(row, &HoverRow::clicked, this, [this, summary]() { emit openConversationRequested(summary.id); });
    row->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(row, &QWidget::customContextMenuRequested, this, [this, row, summary](const QPoint &pos) {
        emit conversationMenuRequested(summary.id, row->mapToGlobal(pos));
    });
    box->addWidget(row);
}

void Sidebar::setStatus(const QString &text, const QColor &color, bool pulsing)
{
    statusLabel_->setText(text);
    dot_->setColor(color);
    dot_->setPulsing(pulsing);
}
