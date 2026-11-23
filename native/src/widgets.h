#pragma once

#include "theme.h"

#include <QAbstractButton>
#include <QColor>
#include <QLabel>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

class QLineEdit;
class QVBoxLayout;

QPixmap mascotPixmap(int size);
QPixmap thinkingPixmap(int size);

class RoundedCard : public QWidget {
    Q_OBJECT
public:
    explicit RoundedCard(QWidget *parent = nullptr);

    void setRadius(int radius);
    void setFill(const QColor &fill);
    void setFill(const QColor *fill); // tracks a palette global across switches
    void setOutline(const QColor &outline);
    void setOutline(const QColor *outline); // tracks a palette global
    void setThickness(qreal thickness);
    void setPadding(int padding);
    QVBoxLayout *content() const { return layout_; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int radius_ = 18;
    QColor fill_ = theme::WHITE;
    const QColor *fillPtr_ = &theme::WHITE; // default: follows the palette
    QColor outline_;
    const QColor *outlinePtr_ = nullptr;
    qreal thickness_ = 1.5;
    int padding_ = 0;
    QVBoxLayout *layout_ = nullptr;
};

class IconButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit IconButton(const QString &icon, int iconSize, QWidget *parent = nullptr);

    void setToolTip(const QString &toolTip);
    void setIconName(const QString &icon);
    void setColor(const QColor &color);
    void setColor(const QColor *color); // tracks a palette global across switches
    void setHoverColor(const QColor &color);
    void setHoverColor(const QColor *color);
    void setHoverBackground(const QColor &color);
    void setCircleFill(const QColor &color);
    void setCircleFill(const QColor *color);
    void setCircleOutline(const QColor &color);
    void setCircleOutline(const QColor *color);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString icon_;
    int iconSize_ = 18;
    int pad_ = 6;
    QColor color_ = theme::INK;
    QColor hoverColor_ = theme::INK;
    QColor hoverBackground_ = QColor(Qt::transparent);
    QColor circleFill_ = QColor(Qt::transparent);
    QColor circleOutline_ = QColor(Qt::transparent);
    // Non-null when the value is tracked from a theme:: palette global, so the
    // button repaints correctly after a live palette switch. The icon colour
    // and hover colour default to tracking the palette.
    const QColor *colorPtr_ = &theme::INK;
    const QColor *hoverColorPtr_ = &theme::INK;
    const QColor *circleFillPtr_ = nullptr;
    const QColor *circleOutlinePtr_ = nullptr;
};

class CircleButton : public IconButton {
public:
    explicit CircleButton(const QString &icon, int diameter = 36, QWidget *parent = nullptr);
};

class Wordmark : public QWidget {
    Q_OBJECT
public:
    explicit Wordmark(int height = 20, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int height_;
};

class MascotLabel : public QWidget {
    Q_OBJECT
public:
    explicit MascotLabel(int size, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    void setMascotSize(int size);
    void setThinking(bool thinking);
    void setFloatOffset(qreal offset);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int size_;
    bool thinking_ = false;
    qreal offset_ = 0;
};

class StatusDot : public QWidget {
    Q_OBJECT
public:
    explicit StatusDot(int size = 9, QWidget *parent = nullptr);
    void setColor(const QColor &color);
    void setPulsing(bool pulsing);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int size_;
    QColor color_;
    QColor baseColor_;
    QTimer timer_;
    qreal phase_ = 0;
};

class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget *parent, const QString &text, const QString &icon = QStringLiteral("check"));
    void popup(const QPoint &topCenter);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer life_;
};

class SearchField : public QWidget {
    Q_OBJECT
public:
    explicit SearchField(QWidget *parent = nullptr);
    QLineEdit *edit() const { return edit_; }
    void applyTheme();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLineEdit *edit_ = nullptr;
};

class TextButton : public QAbstractButton {
    Q_OBJECT
public:
    enum class Variant { Primary, Secondary, Danger };
    explicit TextButton(const QString &text, Variant variant = Variant::Primary, int height = 38, QWidget *parent = nullptr);
    void setFontSize(int size);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Variant variant_;
    int fontSize_ = 11;
    int height_;
};

class HoverRow : public QWidget {
    Q_OBJECT
public:
    explicit HoverRow(QWidget *parent = nullptr);
    void setSelected(bool selected);
    void setBaseColor(const QColor &color);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool selected_ = false;
    bool hovered_ = false;
    QColor base_;
};
