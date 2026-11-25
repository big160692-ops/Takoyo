#pragma once

#include <QColor>
#include <QRectF>
#include <QString>

class QPainter;

namespace icons {

void paint(QPainter &painter, const QString &name, const QRectF &rect, const QColor &color);

} // namespace icons
