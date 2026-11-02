#pragma once

#include <QJsonObject>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

class QLabel;
class QWidget;
class IconButton;

class ArtPanel : public QWidget {
    Q_OBJECT
public:
    explicit ArtPanel(QWidget *parent = nullptr);

    void setEmpty();
    void setGenerating(const QString &prompt);
    void setArtifact(const QJsonObject &meta);
    void finishGenerating();
    bool hasArtifact() const { return !meta_.isEmpty(); }
    QJsonObject artifact() const { return meta_; }

signals:
    void openRequested();
    void saveRequested();
    void regenerateRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void placeChildren();
    void setChromeVisible(bool visible);
    QRectF stageRect() const;
    QRectF cardRect() const;
    void startPulse();
    void stopPulse();

    QJsonObject meta_;
    QPixmap pixmap_;
    bool generating_ = false;
    bool chrome_ = false;
    QString prompt_;

    QWidget *state_ = nullptr;
    QLabel *stateTitle_ = nullptr;
    QLabel *stateSub_ = nullptr;
    IconButton *openBtn_ = nullptr;
    IconButton *saveBtn_ = nullptr;
    IconButton *regenBtn_ = nullptr;
    IconButton *closeBtn_ = nullptr;
    QTimer pulseTimer_;
    int pulsePhase_ = 0;
};
