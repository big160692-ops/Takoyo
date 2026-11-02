#pragma once

#include <QColor>
#include <QPoint>
#include <QTimer>
#include <QWidget>

#include "store.h"

class QLabel;
class QVBoxLayout;
class SearchField;
class StatusDot;

class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget *parent = nullptr);

    void refresh(const QString &query, const QString &selectedProject, const QString &selectedConversation);
    QString query() const;
    void setStatus(const QString &text, const QColor &color, bool pulsing);

signals:
    void openProjectRequested(const QString &id);
    void openConversationRequested(const QString &id);
    void newProjectRequested();
    void projectMenuRequested(const QString &id, const QPoint &globalPos);
    void conversationMenuRequested(const QString &id, const QPoint &globalPos);
    void settingsRequested();
    void hideRequested();
    void homeRequested();

private:
    void buildProjectRow(QVBoxLayout *box, const ProjectSummary &project, bool selected);
    void buildRecentRow(QVBoxLayout *box, const ConversationSummary &summary, bool selected);
    void clearBox(QVBoxLayout *box);
    void applyQuery();

    SearchField *search_ = nullptr;
    QVBoxLayout *projectsBox_ = nullptr;
    QVBoxLayout *recentsBox_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    StatusDot *dot_ = nullptr;
    QTimer debounce_;
    QString query_;
    QString selectedProject_;
    QString selectedConversation_;
};
