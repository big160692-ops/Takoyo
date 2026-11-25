#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class Composer;
class QLabel;
class MascotLabel;

struct ProjectSummary;

class HomeView : public QWidget {
    Q_OBJECT
public:
    explicit HomeView(QWidget *parent = nullptr);

    Composer *composer() const { return composer_; }
    void setProject(const ProjectSummary *project);
    void setWorkspaceFolder(const QString &path);
    void startFloat();
    void stopFloat();
    void setMascotThinking(bool thinking);
    void celebrate();

signals:
    void projectFilesRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void updateFloat();
    void layoutInner();

    QWidget *inner_ = nullptr;
    QWidget *scope_ = nullptr;
    QLabel *scopeLabel_ = nullptr;
    QWidget *mascotHolder_ = nullptr;
    MascotLabel *mascot_ = nullptr;
    QLabel *tagline_ = nullptr;
    Composer *composer_ = nullptr;
    bool narrow_ = false;
    bool floating_ = false;
    QTimer floatTimer_;
    QElapsedTimer floatClock_;
    int innerWidth_ = 0;
};
