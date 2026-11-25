#include "icons.h"
#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>

namespace {
constexpr qreal kPi = 3.14159265358979323846;
}

namespace {

struct Canvas {
    QPainter &p;
    QRectF r;
    qreal s;
    qreal w;
    qreal thin;

    QPointF at(qreal x, qreal y) const { return QPointF(r.left() + x * s, r.top() + y * s); }

    QRectF box(qreal x0, qreal y0, qreal x1, qreal y1) const
    {
        return QRectF(at(x0, y0), at(x1, y1)).normalized();
    }

    void line(const QList<QPointF> &points, const QColor &color, qreal width) const
    {
        QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        if (points.size() == 2) {
            p.drawLine(points[0], points[1]);
            return;
        }
        QPainterPath path(points[0]);
        for (int i = 1; i < points.size(); ++i)
            path.lineTo(points[i]);
        p.drawPath(path);
    }

    void outlineRect(const QRectF &rect, qreal radius, const QColor &color, qreal width) const
    {
        p.setPen(QPen(color, width));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect.adjusted(width / 2, width / 2, -width / 2, -width / 2), radius, radius);
    }

    void arc(const QRectF &rect, int startPil, int endPil, const QColor &color, qreal width) const
    {
        p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(rect, -startPil * 16, (startPil - endPil) * 16);
    }

    QRectF around(qreal cx, qreal cy, qreal radius) const
    {
        return box(cx - radius, cy - radius, cx + radius, cy + radius);
    }
};

} // namespace

namespace icons {

void paint(QPainter &painter, const QString &name, const QRectF &rect, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.topLeft());
    const qreal s = qMin(rect.width(), rect.height());
    Canvas c{painter, QRectF(0, 0, s, s), s, qMax<qreal>(1.3, s * 0.068), qMax<qreal>(1.15, s * 0.052)};

    if (name == QLatin1String("sidebar")) {
        c.outlineRect(c.box(0.13, 0.16, 0.87, 0.84), s * 0.16, color, c.w);
        c.line({c.at(0.40, 0.16), c.at(0.40, 0.84)}, color, c.w);
    } else if (name == QLatin1String("search")) {
        painter.setPen(QPen(color, c.w));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(c.box(0.14, 0.13, 0.65, 0.64));
        c.line({c.at(0.60, 0.60), c.at(0.85, 0.85)}, color, c.w);
    } else if (name == QLatin1String("plus")) {
        painter.setPen(QPen(color, c.thin));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(c.box(0.13, 0.13, 0.87, 0.87));
        c.line({c.at(0.29, 0.50), c.at(0.71, 0.50)}, color, c.thin);
        c.line({c.at(0.50, 0.29), c.at(0.50, 0.71)}, color, c.thin);
    } else if (name == QLatin1String("plus-fill")) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(c.box(0.10, 0.10, 0.90, 0.90));
        c.line({c.at(0.30, 0.50), c.at(0.70, 0.50)}, Qt::white, c.w);
        c.line({c.at(0.50, 0.30), c.at(0.50, 0.70)}, Qt::white, c.w);
    } else if (name == QLatin1String("paperclip")) {
        // Three parallel wires joined by a bottom turn and a top turn, like a
        // real clip, then tilted so it reads as the usual attachment glyph.
        painter.save();
        painter.translate(c.at(0.5, 0.5));
        painter.rotate(38);
        painter.translate(-c.at(0.5, 0.5));
        QPainterPath clip;
        const qreal left = 0.33;
        const qreal middle = 0.50;
        const qreal right = 0.67;
        clip.moveTo(c.at(middle, 0.36));
        clip.lineTo(c.at(middle, 0.61));
        clip.arcTo(c.around((left + middle) / 2, 0.61, (middle - left) / 2), 0, -180);
        clip.lineTo(c.at(left, 0.23));
        clip.arcTo(c.around((left + right) / 2, 0.23, (right - left) / 2), 180, -180);
        clip.lineTo(c.at(right, 0.61));
        painter.setPen(QPen(color, c.thin, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(clip);
        painter.restore();
    } else if (name == QLatin1String("mic")) {
        c.outlineRect(c.box(0.375, 0.09, 0.625, 0.55), s * 0.125, color, c.w);
        c.arc(c.box(0.24, 0.31, 0.76, 0.75), 0, 180, color, c.w);
        c.line({c.at(0.50, 0.75), c.at(0.50, 0.87)}, color, c.w);
        c.line({c.at(0.32, 0.88), c.at(0.68, 0.88)}, color, c.w);
    } else if (name == QLatin1String("stop")) {
        painter.setPen(QPen(color, qMax<qreal>(2.0, s * 0.13), Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(c.box(0.24, 0.24, 0.76, 0.76), s * 0.04, s * 0.04);
    } else if (name == QLatin1String("arrow-down")) {
        c.line({c.at(0.50, 0.16), c.at(0.50, 0.80)}, color, c.w);
        c.line({c.at(0.28, 0.58), c.at(0.50, 0.80), c.at(0.72, 0.58)}, color, c.w);
    } else if (name == QLatin1String("arrow-up")) {
        c.line({c.at(0.50, 0.84), c.at(0.50, 0.20)}, color, c.w);
        c.line({c.at(0.28, 0.42), c.at(0.50, 0.20), c.at(0.72, 0.42)}, color, c.w);
    } else if (name == QLatin1String("arrow-left")) {
        c.line({c.at(0.84, 0.50), c.at(0.20, 0.50)}, color, c.w);
        c.line({c.at(0.42, 0.28), c.at(0.20, 0.50), c.at(0.42, 0.72)}, color, c.w);
    } else if (name == QLatin1String("gear")) {
        QPainterPath cog;
        const int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i) {
            const qreal a = kPi * 2 * i / (teeth * 2) - kPi / 2;
            const qreal r = (i % 2 == 0) ? 0.47 : 0.34;
            const QPointF point = c.at(0.5 + r * std::cos(a), 0.5 + r * std::sin(a));
            if (i == 0)
                cog.moveTo(point);
            else
                cog.lineTo(point);
        }
        cog.closeSubpath();
        QPainterPath hole;
        hole.addEllipse(c.box(0.40, 0.40, 0.60, 0.60));
        painter.setPen(QPen(color, c.thin, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(cog);
        painter.drawPath(hole);
    } else if (name == QLatin1String("sun")) {
        painter.setPen(QPen(color, c.thin, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(c.box(0.34, 0.34, 0.66, 0.66));
        for (int i = 0; i < 8; ++i) {
            const qreal a = kPi * 2 * i / 8;
            c.line({c.at(0.5 + 0.23 * std::cos(a), 0.5 + 0.23 * std::sin(a)),
                    c.at(0.5 + 0.41 * std::cos(a), 0.5 + 0.41 * std::sin(a))},
                   color, c.w);
        }
    } else if (name == QLatin1String("moon")) {
        QPainterPath crescent;
        crescent.setFillRule(Qt::OddEvenFill);
        crescent.addEllipse(c.box(0.16, 0.14, 0.78, 0.88));
        crescent.addEllipse(c.box(0.40, 0.04, 1.02, 0.78));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(crescent);
    } else if (name == QLatin1String("close")) {
        c.line({c.at(0.24, 0.24), c.at(0.76, 0.76)}, color, c.w);
        c.line({c.at(0.76, 0.24), c.at(0.24, 0.76)}, color, c.w);
    } else if (name == QLatin1String("file")) {
        c.line({c.at(0.28, 0.10), c.at(0.60, 0.10), c.at(0.76, 0.26), c.at(0.76, 0.90), c.at(0.28, 0.90),
                c.at(0.28, 0.10)},
               color, c.thin);
        c.line({c.at(0.60, 0.10), c.at(0.60, 0.26), c.at(0.76, 0.26)}, color, c.thin);
        c.line({c.at(0.38, 0.46), c.at(0.66, 0.46)}, color, c.thin);
        c.line({c.at(0.38, 0.62), c.at(0.66, 0.62)}, color, c.thin);
    } else if (name == QLatin1String("folder")) {
        c.line({c.at(0.13, 0.30), c.at(0.36, 0.30), c.at(0.44, 0.40), c.at(0.87, 0.40), c.at(0.87, 0.82),
                c.at(0.13, 0.82), c.at(0.13, 0.30)},
               color, c.thin);
    } else if (name == QLatin1String("trash")) {
        c.line({c.at(0.20, 0.28), c.at(0.80, 0.28)}, color, c.thin);
        c.arc(c.box(0.38, 0.14, 0.62, 0.32), 180, 360, color, c.thin);
        c.line({c.at(0.26, 0.30), c.at(0.31, 0.86), c.at(0.69, 0.86), c.at(0.74, 0.30)}, color, c.thin);
        c.line({c.at(0.43, 0.42), c.at(0.45, 0.74)}, color, c.thin);
        c.line({c.at(0.57, 0.42), c.at(0.55, 0.74)}, color, c.thin);
    } else if (name == QLatin1String("copy")) {
        c.outlineRect(c.box(0.31, 0.31, 0.83, 0.83), s * 0.08, color, c.thin);
        c.outlineRect(c.box(0.17, 0.17, 0.69, 0.69), s * 0.08, color, c.thin);
    } else if (name == QLatin1String("check")) {
        c.line({c.at(0.18, 0.52), c.at(0.42, 0.76), c.at(0.82, 0.26)}, color, c.w);
    } else if (name == QLatin1String("external")) {
        c.line({c.at(0.20, 0.34), c.at(0.20, 0.82), c.at(0.68, 0.82), c.at(0.68, 0.66)}, color, c.thin);
        c.line({c.at(0.46, 0.54), c.at(0.82, 0.18)}, color, c.thin);
        c.line({c.at(0.58, 0.18), c.at(0.82, 0.18), c.at(0.82, 0.42)}, color, c.thin);
    } else if (name == QLatin1String("sparkle")) {
        QPolygonF star;
        star << c.at(0.50, 0.08) << c.at(0.59, 0.41) << c.at(0.92, 0.50) << c.at(0.59, 0.59)
             << c.at(0.50, 0.92) << c.at(0.41, 0.59) << c.at(0.08, 0.50) << c.at(0.41, 0.41);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(star);
    } else if (name == QLatin1String("image")) {
        c.outlineRect(c.box(0.12, 0.16, 0.88, 0.84), s * 0.10, color, c.thin);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(c.box(0.27, 0.29, 0.43, 0.45));
        c.line({c.at(0.16, 0.74), c.at(0.38, 0.50), c.at(0.55, 0.66), c.at(0.68, 0.54), c.at(0.84, 0.72)},
               color, c.thin);
    } else if (name == QLatin1String("more")) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for (qreal x : {0.25, 0.50, 0.75})
            painter.drawEllipse(c.box(x - 0.055, 0.445, x + 0.055, 0.555));
    } else if (name == QLatin1String("chat")) {
        c.outlineRect(c.box(0.12, 0.14, 0.88, 0.70), s * 0.16, color, c.thin);
        c.line({c.at(0.32, 0.70), c.at(0.24, 0.86), c.at(0.48, 0.70)}, color, c.thin);
    } else if (name == QLatin1String("history")) {
        painter.setPen(QPen(color, c.thin));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(c.box(0.13, 0.13, 0.87, 0.87));
        c.line({c.at(0.50, 0.28), c.at(0.50, 0.52), c.at(0.68, 0.62)}, color, c.thin);
    } else if (name == QLatin1String("chevron-down")) {
        c.line({c.at(0.26, 0.40), c.at(0.50, 0.64), c.at(0.74, 0.40)}, color, c.w);
    } else if (name == QLatin1String("chevron-right")) {
        c.line({c.at(0.40, 0.24), c.at(0.64, 0.50), c.at(0.40, 0.76)}, color, c.w);
    } else if (name == QLatin1String("download")) {
        c.line({c.at(0.50, 0.14), c.at(0.50, 0.64)}, color, c.w);
        c.line({c.at(0.30, 0.46), c.at(0.50, 0.66), c.at(0.70, 0.46)}, color, c.w);
        c.line({c.at(0.18, 0.72), c.at(0.18, 0.86), c.at(0.82, 0.86), c.at(0.82, 0.72)}, color, c.w);
    } else if (name == QLatin1String("pencil")) {
        c.line({c.at(0.20, 0.82), c.at(0.28, 0.58), c.at(0.68, 0.18), c.at(0.84, 0.34), c.at(0.44, 0.74),
                c.at(0.20, 0.82)},
               color, c.thin);
        c.line({c.at(0.28, 0.58), c.at(0.44, 0.74)}, color, c.thin);
    } else if (name == QLatin1String("refresh")) {
        c.arc(c.box(0.18, 0.18, 0.82, 0.82), 30, 300, color, c.thin);
        c.line({c.at(0.78, 0.16), c.at(0.86, 0.40), c.at(0.62, 0.40)}, color, c.thin);
    } else if (name == QLatin1String("code")) {
        c.line({c.at(0.38, 0.26), c.at(0.16, 0.50), c.at(0.38, 0.74)}, color, c.thin);
        c.line({c.at(0.62, 0.26), c.at(0.84, 0.50), c.at(0.62, 0.74)}, color, c.thin);
    } else if (name == QLatin1String("brain")) {
        c.arc(c.box(0.16, 0.18, 0.62, 0.64), 60, 300, color, c.thin);
        c.arc(c.box(0.38, 0.18, 0.84, 0.64), 240, 120, color, c.thin);
        c.line({c.at(0.23, 0.70), c.at(0.23, 0.82), c.at(0.77, 0.82), c.at(0.77, 0.70)}, color, c.thin);
        c.line({c.at(0.50, 0.24), c.at(0.50, 0.70)}, color, c.thin);
    } else if (name == QLatin1String("palette")) {
        // Painter's palette: a rounded blob with a thumb hole and a few paint
        // dabs along the rim.
        painter.setPen(QPen(color, c.thin, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath body;
        body.addEllipse(c.box(0.14, 0.12, 0.86, 0.82));
        painter.drawPath(body);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(c.box(0.30, 0.26, 0.40, 0.36));
        painter.drawEllipse(c.box(0.44, 0.24, 0.54, 0.34));
        painter.drawEllipse(c.box(0.58, 0.28, 0.68, 0.38));
        painter.drawEllipse(c.box(0.60, 0.44, 0.70, 0.54));
        // thumb hole
        painter.setBrush(theme::map(theme::WHITE));
        painter.drawEllipse(c.box(0.36, 0.52, 0.48, 0.64));
    } else {
        painter.setPen(QPen(color, c.thin));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(c.box(0.2, 0.2, 0.8, 0.8));
    }
    painter.restore();
}

} // namespace icons
