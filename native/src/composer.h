#pragma once

#include <QJsonObject>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class IconButton;
class CircleButton;
class TextButton;

class Composer : public QWidget {
    Q_OBJECT
public:
    explicit Composer(QWidget *parent = nullptr);

    QString text() const;
    void setText(const QString &text);
    void clear();
    void focusInput();
    void setThinking(bool thinking, const QString &label = QStringLiteral("Thinking"));
    bool hasAttachment() const { return attachmentLoaded_; }
    QJsonObject attachment() const;
    void clearAttachment();
    void setStopVisible(bool visible);
    void promptAttachment() { attachFile(); }
    void setReplyStyle(const QString &style);
    QString replyStyle() const { return replyStyle_; }

signals:
    void submitted();
    void stopRequested();
    void voiceRequested();
    void changed();
    void replyStyleChanged(const QString &style);
    void folderChanged(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshButtons();
    void attachFile();
    void updatePlaceholder();
    void tickThinking();

    QPlainTextEdit *edit_ = nullptr;
    QLabel *placeholder_ = nullptr;
    QWidget *bottom_ = nullptr;
    QWidget *chip_ = nullptr;
    QLabel *chipLabel_ = nullptr;
    IconButton *folderBtn_ = nullptr;
    IconButton *attachBtn_ = nullptr;
    TextButton *styleBtn_ = nullptr;
    IconButton *micBtn_ = nullptr;
    CircleButton *sendBtn_ = nullptr;
    CircleButton *stopBtn_ = nullptr;

    bool thinking_ = false;
    bool stopVisible_ = false;
    bool attachmentLoaded_ = false;
    QString attachmentName_;
    QString attachmentContent_;
    QString attachmentPath_;
    QString attachmentKind_;
    QString attachmentMime_;
    qint64 attachmentSize_ = 0;
    QString replyStyle_ = QStringLiteral("balanced");

    QTimer thinkTimer_;
    int thinkPhase_ = 0;
    QString thinkBase_ = QStringLiteral("Thinking");
    bool placeholderOn_ = false;
};
