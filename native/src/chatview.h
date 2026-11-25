#pragma once

#include <QJsonObject>
#include <QList>
#include <QTimer>
#include <QWidget>

class Composer;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class IconButton;
struct ProjectSummary;

class ChatView : public QWidget {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);

    Composer *composer() const { return composer_; }
    void setProject(const ProjectSummary *project);
    void refresh(const QJsonObject &conversation);
    void clear();
    void scrollToBottom();

    void startStream();
    void appendStream(const QString &text);
    void stopStreamUi();

signals:
    void applyEditRequested(const QString &path, const QString &content, int messageIndex);
    void projectFilesRequested();
    void closeProjectRequested();
    void newChatRequested();
    void settingsRequested();
    void imageRequested(const QJsonObject &meta);
    void saveImageRequested(const QJsonObject &meta);
    void regenerateRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupProjectBar();
    void renderMessage(const QJsonObject &message, int index, bool last);
    QWidget *addRow();
    void addUserMessage(QWidget *row, const QJsonObject &message);
    void addAssistantMessage(QWidget *row, const QJsonObject &message, int index);
    void addImageCard(QWidget *column, const QJsonObject &meta);
    void addProposalCard(QWidget *column, const QJsonObject &edit, int index);
    void addErrorCard(QWidget *column, const QString &message, const QString &kind);
    void flushLive();
    void updateScrollButton();
    void placeScrollButton();

    QWidget *projectBar_ = nullptr;
    QLabel *projectName_ = nullptr;
    QLabel *projectDesc_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QWidget *column_ = nullptr;
    QVBoxLayout *columnLayout_ = nullptr;
    QWidget *composerHost_ = nullptr;
    Composer *composer_ = nullptr;
    IconButton *scrollBtn_ = nullptr;

    QWidget *liveRow_ = nullptr;
    QLabel *liveLabel_ = nullptr;
    QWidget *liveColumn_ = nullptr;
    QVBoxLayout *liveColumnLayout_ = nullptr;
    QList<QWidget *> liveWidgets_;
    QList<bool> liveIsCode_;
    QTimer liveTimer_;
    QString liveText_;
    bool livePending_ = false;
    int livePhase_ = 0;
    QTimer liveDots_;
    int composerPad_ = 16;
    bool atBottom_ = true;
};
