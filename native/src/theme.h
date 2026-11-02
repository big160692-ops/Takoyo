#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace theme {

inline const QColor WHITE("#FFFFFF");
inline const QColor INK("#0B0B0D");
inline const QColor INK_SOFT("#3A3B3E");
inline const QColor MUTED("#8B8B8F");
inline const QColor FAINT("#B9B9BD");
inline const QColor LINE("#111113");
inline const QColor LINE_SOFT("#E7E7E9");
inline const QColor HOVER("#F4F4F5");
inline const QColor ACTIVE("#EDEDEF");
inline const QColor SEARCH_BG("#DEDEDF");
inline const QColor CHIP("#F2F2F3");
inline const QColor DANGER("#D9433A");
inline const QColor SUCCESS("#0F9D58");
inline const QColor CODE_RED("#E2453C");
inline const QColor CODE_GREEN("#12A15A");
inline const QColor CODE_BLUE("#1B4FA0");
inline const QColor ERROR_BG("#FFF7F6");
inline const QColor ERROR_LINE("#F1D3D0");

inline constexpr int RADIUS_CARD = 18;
inline constexpr int RADIUS_INPUT = 16;
inline constexpr int RADIUS_BUBBLE = 15;

void loadFonts();
QFont font(int pixelSize, const QString &weight = QStringLiteral("regular"));
QFont mono(int pixelSize, bool bold = false);
QString familyFor(const QString &weight);

} // namespace theme
