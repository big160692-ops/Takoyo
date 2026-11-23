#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include "config.h"
#include "store.h"
#include "util.h"

class ArtPanel;
class Backend;
class ChatStream;
class ChatView;
class Composer;
class HomeView;
class IconButton;
class QStackedWidget;
class QVariantAnimation;
class Sidebar;
class SlideHost;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void wireSidebar();
    void wireViews();
    void wireComposers();
    void wireBackend();
    void wireArt();

    void setSidebarOpen(bool open, bool animate);
    void toggleSidebar();
    void animateSlide(SlideHost *host, QVariantAnimation **animation, int end, int duration);
    void showArt(bool visible, bool silent = false);
    void toggleArt();
    void showHome();
    void showChat();
    void syncChatProject();
    void syncHomeProject();

    void newChat();
    void openProject(const QString &id);
    void loadConversation(const QString &id);
    void closeProject();
    void refreshSidebar();
    void applyTheme();
    void syncBusy();
    void updateStatus(const QJsonObject &health = QJsonObject());
    void toast(const QString &text, const QString &icon = QStringLiteral("check"));
    void startVoice();

    bool busy() const { return streaming_ || imageJobs_ > 0; }
    QString conversationId() const;

    void sendFromComposer(Composer *composer);
    void stopStreaming();
    void beginReply(const QString &cid, int index, const QJsonArray &messages);
    QJsonArray chatRequest(const QString &prompt, const QJsonObject &conversation = QJsonObject()) const;
    QString lastUserText(const QString &cid) const;
    QString readRequestedFiles(const QList<util::FileRequest> &requests) const;
    QStringList mentionedFilePaths(const QString &text) const;
    void followUpWithFiles(const QString &cid, const QList<util::FileRequest> &requests);
    void finishReply(const QString &cid, int index, const QString &full, bool cancelled);
    void finishError(const QString &cid, int index, const QString &message, const QString &partial = QString());
    void startDirectImage(const QString &cid, const QString &prompt);
    void beginImage(const QString &prompt, const QString &cid, int index, bool direct);
    void finishImage(const QJsonObject &meta, const QJsonObject &context);
    void failImage(const QString &message, const QJsonObject &context);
    void showArtifact(const QJsonObject &meta);
    void saveArtifact(const QJsonObject &meta);
    void regenerateArtifact(const QJsonObject &meta);
    QJsonObject latestImage() const;

    void applyProposal(const QString &path, const QString &content, int messageIndex);
    void applyCodeFromChat(const QString &path, const QString &content);
    bool writeProjectFile(const QString &path, const QString &content, const QString &projectId);
    void openProjectFiles();
    void openSettings();
    void newProjectDialog();
    void showProjectMenu(const QString &id, const QPoint &globalPos);
    void showConversationMenu(const QString &id, const QPoint &globalPos);
    void renameProject(const QString &id);
    void deleteProject(const QString &id);
    void renameConversation(const QString &id);
    void deleteConversation(const QString &id);

    Config config_;
    ProjectStore projects_;
    ConversationStore conversations_;
    Backend *backend_ = nullptr;

    Sidebar *sidebar_ = nullptr;
    SlideHost *sidebarHost_ = nullptr;
    HomeView *home_ = nullptr;
    ChatView *chat_ = nullptr;
    ArtPanel *art_ = nullptr;
    SlideHost *artHost_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QWidget *logo_ = nullptr;
    QWidget *mainArea_ = nullptr;
    QWidget *header_ = nullptr;
    QWidget *body_ = nullptr;
    IconButton *artButton_ = nullptr;
    QPointer<QWidget> filesDialog_;
    QPointer<QWidget> settingsDialog_;
QPointer<QWidget> paletteDialog_;
QString workingFolder_;

    QVariantAnimation *sidebarAnim_ = nullptr;
    QVariantAnimation *artAnim_ = nullptr;

    bool sidebarOpen_ = false;
    bool artVisible_ = false;
    bool hasProject_ = false;
    ProjectSummary project_;
    QJsonObject conversation_;
    bool hasConversation_ = false;

    ChatStream *stream_ = nullptr;
    bool streaming_ = false;
    QString streamCid_;
    int streamIndex_ = -1;
    int readRounds_ = 0;
    int promiseRetries_ = 0;
    int imageJobs_ = 0;
    bool closing_ = false;
};
