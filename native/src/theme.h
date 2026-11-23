#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QString>

namespace theme {

// Colour roles.
//
// NOTE ON NAMES: `WHITE` and `INK` are historical *role* names, not literal
// colours. They are inherited from the original light "white-and-ink" theme:
//   WHITE     -> the app ground / surface colour (dark in this theme)
//   INK       -> the primary text colour (light in this theme)
//   INK_SOFT  -> the secondary text colour
// Every other name (MUTED, LINE, CHIP, …) still means what it says.
//
// These are mutable globals: `setPalette()` / `loadPalette()` reassign them at
// runtime so the whole UI can recolour without a restart. Custom-painted
// widgets read them at paint time; stylesheet owners re-run their sheets via
// applyTheme()/theme::restyle().
inline QColor WHITE("#0B0B0D");
inline QColor INK("#FFFFFF");
inline QColor INK_SOFT("#C9C9CE");
inline QColor MUTED("#8B8B8F");
inline QColor FAINT("#6E6E73");
inline QColor LINE("#E7E7E9");
inline QColor LINE_SOFT("#26262A");
inline QColor BORDER("#3A3B3E");
inline QColor HOVER("#1C1C20");
inline QColor ACTIVE("#24242A");
inline QColor SEARCH_BG("#16161A");
inline QColor CHIP("#17171B");
inline QColor DANGER("#FF6B60");
inline QColor SUCCESS("#2FBF71");
inline QColor CODE_RED("#FF7B72");
inline QColor CODE_GREEN("#56D364");
inline QColor CODE_BLUE("#79B8FF");
inline QColor ERROR_BG("#1A1113");
inline QColor ERROR_LINE("#4A2E2C");

inline constexpr int RADIUS_CARD = 18;
inline constexpr int RADIUS_INPUT = 16;
inline constexpr int RADIUS_BUBBLE = 15;

// A complete set of the 19 colour roles above.
struct Palette {
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

// A palette as listed in the Settings picker.
struct PaletteInfo {
    QString id;    // file stem, e.g. "solarized-dark"
    QString name;  // display name, e.g. "Solarized Dark"
};

// The bundled dark palette (also the fallback for missing/corrupt files).
Palette darkPalette();

// Assign every role from a palette.
void setPalette(const Palette &palette);

// Load `assets/palettes/<id>.json` and apply it. Keys are the role names
// (WHITE, INK, …) plus an optional "name". Missing/invalid keys fall back to
// the dark defaults so a partial file can never blank the UI. Returns false
// (and leaves the current colours untouched) if the file is unreadable or
// contains no usable colours.
bool loadPalette(const QString &id);

// Id of the palette currently applied ("dark" until loadPalette succeeds).
QString currentPaletteId();

// Apply the current palette to Qt's application-wide QPalette (tooltips,
// combo popups and other native widgets). Call once after QApplication exists
// and again whenever the palette changes.
void applyAppPalette();

// Palettes shipped in assets/palettes, sorted by display name. Always returns
// at least the bundled dark entry, even if the directory is missing.
QList<PaletteInfo> availablePalettes();

// Display name for an id (falls back to the id itself).
QString paletteName(const QString &id);

void loadFonts();
QFont font(int pixelSize, const QString &weight = QStringLiteral("regular"));
QFont mono(int pixelSize, bool bold = false);
QString familyFor(const QString &weight);

} // namespace theme
