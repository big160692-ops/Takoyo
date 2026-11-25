#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QPalette>
#include <QString>

class QWidget;

namespace theme {

// Canonical light-mode token colors. These NEVER change at runtime: QSS
// strings across the UI are authored against them (often via
// `QStringLiteral("color:%1;").arg(theme::INK.name())`), and remap() rewrites
// those literals to the active palette at paint time. Use map() to resolve a
// token to the active palette color when painting.
inline const QColor WHITE("#FFFFFF");
inline const QColor INK("#0B0B0D");
inline const QColor INK_SOFT("#3A3B3E");
inline const QColor MUTED("#8B8B8F");
// Secondary copy that still has to stay legible at small sizes on white.
inline const QColor MUTED_STRONG("#6C6D72");
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
inline const QColor ERROR_BG("#F9ECEA");
inline const QColor ERROR_LINE("#F0C4C0");

inline constexpr int RADIUS_CARD = 18;
inline constexpr int RADIUS_INPUT = 16;
inline constexpr int RADIUS_BUBBLE = 15;

// A palette is a named set of token colors loaded from assets/palettes/<id>.json.
struct Palette {
    QString id;
    QString name;
    QColor white;
    QColor ink;
    QColor inkSoft;
    QColor muted;
    QColor faint;
    QColor line;
    QColor lineSoft;
    QColor border;
    QColor hover;
    QColor active;
    QColor searchBg;
    QColor chip;
    QColor danger;
    QColor success;
    QColor codeRed;
    QColor codeGreen;
    QColor codeBlue;
    QColor errorBg;
    QColor errorLine;
};

struct PaletteInfo {
    QString id;
    QString name;
};

// Palette discovery and selection.
QList<PaletteInfo> availablePalettes();
QString currentPaletteId();
QString paletteName(const QString &id);
bool loadPalette(const QString &id);
bool setPalette(const Palette &palette);
bool darkPalette();
// Read a palette's tokens without mutating global theme state. Returns false
// if the palette id is unknown or the file is missing.
bool readPalette(const QString &id, Palette &out);

// Increments whenever the active palette or light/dark mode changes. Widgets
// that cache palette-dependent pixmaps (e.g. the mascot) key their caches by
// this so a theme switch invalidates stale entries.
quint64 themeGeneration();

// The tokens above describe the active palette. darkMode() is an independent
// toggle: when true, remap()/map() resolve to the dark variant of the active
// palette; when false, the light variant (synthesised by luminance inversion
// for palettes that ship only dark tokens).
bool darkMode();
void setDarkMode(bool dark);
QColor map(const QColor &color);
QString remap(const QString &lightCss);

// Stylesheet text is authored in light colors; style() remembers it so a
// theme switch can restyle widgets that were built earlier.
void style(QWidget *widget, const QString &lightCss);
void setSurface(QWidget *widget);
void setTextPalette(QWidget *widget);
void refresh(QWidget *root);
QPalette appPalette();
void applyAppPalette();

void loadFonts();
QFont font(int pixelSize, const QString &weight = QStringLiteral("regular"));
QFont mono(int pixelSize, bool bold = false);
QString familyFor(const QString &weight);

} // namespace theme