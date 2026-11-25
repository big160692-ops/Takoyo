#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <QStyle>
#include <QStyleHints>
#include <QVariant>
#include <QWidget>

#include <iterator>

#include "util.h"

namespace theme {

namespace {
QString g_regular = QStringLiteral("Poppins");
QString g_medium = QStringLiteral("Poppins");
QString g_semibold = QStringLiteral("Poppins");
QString g_bold = QStringLiteral("Poppins");
QString g_mono = QStringLiteral("Consolas");

QString pickFamily(const QStringList &families, const QStringList &needles)
{
    for (const QString &needle : needles) {
        for (const QString &family : families) {
            if (family.contains(needle, Qt::CaseInsensitive))
                return family;
        }
    }
    return QString();
}

// The canonical light-mode token hex values. Every QSS string in the UI is
// authored against these; remap() rewrites them to the active palette.
struct TokenDef {
    const char *name;
    const char *lightHex;
};

const TokenDef kLightTokens[] = {
    {"WHITE",      "#FFFFFF"},
    {"INK",        "#0B0B0D"},
    {"INK_SOFT",   "#3A3B3E"},
    {"MUTED",      "#8B8B8F"},
    {"MUTED_STRONG","#6C6D72"},
    {"FAINT",      "#B9B9BD"},
    {"LINE",       "#111113"},
    {"LINE_SOFT",  "#E7E7E9"},
    {"BORDER",     "#3A3B3E"},
    {"HOVER",      "#F4F4F5"},
    {"ACTIVE",     "#EDEDEF"},
    {"SEARCH_BG",  "#DEDEDF"},
    {"CHIP",       "#F2F2F3"},
    {"DANGER",     "#D9433A"},
    {"SUCCESS",    "#0F9D58"},
    {"CODE_RED",   "#E2453C"},
    {"CODE_GREEN", "#12A15A"},
    {"CODE_BLUE",  "#1B4FA0"},
    {"ERROR_BG",   "#F9ECEA"},
    {"ERROR_LINE", "#F0C4C0"},
    // A few derived/legacy hexes that appear in QSS but are not JSON tokens.
    {"C9C9CC",     "#C9C9CC"},
    {"1C55A5",     "#1C55A5"},
    {"F1D3D0",     "#F1D3D0"},
    {"FFF7F6",     "#FFF7F6"},
};

constexpr int kTokenCount = int(std::size(kLightTokens));

// Relative luminance (WCAG formula). Used to decide whether a palette is
// dark-native and to synthesise the opposite variant.
double srgbToLinear(double v)
{
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &c)
{
    return 0.2126 * srgbToLinear(c.redF())
         + 0.7152 * srgbToLinear(c.greenF())
         + 0.0722 * srgbToLinear(c.blueF());
}

// Invert a color's luminance while preserving hue/saturation roughly. Used to
// synthesise a light variant of a dark palette (and vice versa).
QColor invertLuminance(const QColor &c)
{
    const double L = luminance(c);
    const double target = 1.0 - L;
    // Scale in linear RGB space toward the target luminance.
    const double r = c.redF();
    const double g = c.greenF();
    const double b = c.blueF();
    const double cur = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    if (cur <= 0.0001) {
        // Pure black -> pick a light neutral preserving nothing.
        const int v = qRound(target * 255.0);
        return QColor(v, v, v);
    }
    const double scale = target / cur;
    auto clamp8 = [](double v) { return qBound(0, qRound(v * 255.0), 255); };
    return QColor(clamp8(r * scale), clamp8(g * scale), clamp8(b * scale));
}

QColor darken(const QColor &c, double factor)
{
    return QColor(qBound(0, qRound(c.redF() * factor * 255.0), 255),
                  qBound(0, qRound(c.greenF() * factor * 255.0), 255),
                  qBound(0, qRound(c.blueF() * factor * 255.0), 255));
}

QColor lighten(const QColor &c, double factor)
{
    auto ch = [factor](double v) { return qBound(0, qRound(v * 255.0 + (255 - v * 255.0) * factor), 255); };
    return QColor(ch(c.redF()), ch(c.greenF()), ch(c.blueF()));
}

} // namespace

namespace {

bool g_dark = false;
QString g_paletteId = QStringLiteral("light");
Palette g_palette;
QColor g_activeTokens[kTokenCount];
bool g_ready = false;
quint64 g_generation = 0;

void applySurface(QWidget *widget)
{
    QPalette pal = widget->palette();
    pal.setColor(QPalette::Window, g_activeTokens[0]); // WHITE
    widget->setPalette(pal);
}

void applyTextPalette(QWidget *widget)
{
    const QColor &ink = g_activeTokens[1]; // INK
    QPalette pal = widget->palette();
    pal.setColor(QPalette::Text, ink);
    pal.setColor(QPalette::WindowText, ink);
    widget->setPalette(pal);
}

void applyTo(QWidget *widget)
{
    const QVariant lightCss = widget->property("_takoyo_light_css");
    if (lightCss.isValid())
        widget->setStyleSheet(remap(lightCss.toString()));
    if (widget->property("_takoyo_surface").toBool())
        applySurface(widget);
    if (widget->property("_takoyo_text_palette").toBool())
        applyTextPalette(widget);
    widget->update();
}

// Read a single palette JSON file into a Palette and its id/name.
bool readPaletteFile(const QString &path, Palette &out, QString &outId)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject o = doc.object();
    auto col = [&o](const char *key, const QColor &fallback) {
        return QColor(o.value(QLatin1String(key)).toString(fallback.name()));
    };
    out.id = o.value(QStringLiteral("name")).toString().toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
    // Derive id from filename when the JSON name is empty.
    out.name = o.value(QStringLiteral("name")).toString();
    out.white     = col("WHITE",      QColor("#FFFFFF"));
    out.ink       = col("INK",        QColor("#0B0B0D"));
    out.inkSoft   = col("INK_SOFT",   QColor("#3A3B3E"));
    out.muted     = col("MUTED",      QColor("#8B8B8F"));
    out.faint     = col("FAINT",      QColor("#B9B9BD"));
    out.line      = col("LINE",       QColor("#111113"));
    out.lineSoft  = col("LINE_SOFT",  QColor("#E7E7E9"));
    out.border    = col("BORDER",     QColor("#3A3B3E"));
    out.hover     = col("HOVER",      QColor("#F4F4F5"));
    out.active    = col("ACTIVE",     QColor("#EDEDEF"));
    out.searchBg  = col("SEARCH_BG",  QColor("#DEDEDF"));
    out.chip      = col("CHIP",       QColor("#F2F2F3"));
    out.danger    = col("DANGER",     QColor("#D9433A"));
    out.success   = col("SUCCESS",    QColor("#0F9D58"));
    out.codeRed   = col("CODE_RED",   QColor("#E2453C"));
    out.codeGreen = col("CODE_GREEN", QColor("#12A15A"));
    out.codeBlue  = col("CODE_BLUE",  QColor("#1B4FA0"));
    out.errorBg   = col("ERROR_BG",   QColor("#F9ECEA"));
    out.errorLine = col("ERROR_LINE", QColor("#F0C4C0"));
    outId = out.id;
    return true;
}

// Fill g_activeTokens from g_palette, applying the dark_mode toggle. When the
// palette's native form disagrees with the toggle, synthesise the opposite
// variant by inverting luminance on every token.
void recomputeActiveTokens()
{
    const bool nativeDark = luminance(g_palette.white) < luminance(g_palette.ink);
    const bool needInvert = (g_dark != nativeDark);
    auto pick = [needInvert](const QColor &c) { return needInvert ? invertLuminance(c) : c; };
    g_activeTokens[0]  = pick(g_palette.white);
    g_activeTokens[1]  = pick(g_palette.ink);
    g_activeTokens[2]  = pick(g_palette.inkSoft);
    g_activeTokens[3]  = pick(g_palette.muted);
    // MUTED_STRONG: a darker (light) / lighter (dark) muted for small legible copy.
    g_activeTokens[4]  = g_dark ? lighten(g_activeTokens[3], 0.12) : darken(g_activeTokens[3], 0.82);
    g_activeTokens[5]  = pick(g_palette.faint);
    g_activeTokens[6]  = pick(g_palette.line);
    g_activeTokens[7]  = pick(g_palette.lineSoft);
    g_activeTokens[8]  = pick(g_palette.border);
    g_activeTokens[9]  = pick(g_palette.hover);
    g_activeTokens[10] = pick(g_palette.active);
    g_activeTokens[11] = pick(g_palette.searchBg);
    g_activeTokens[12] = pick(g_palette.chip);
    g_activeTokens[13] = pick(g_palette.danger);
    g_activeTokens[14] = pick(g_palette.success);
    g_activeTokens[15] = pick(g_palette.codeRed);
    g_activeTokens[16] = pick(g_palette.codeGreen);
    g_activeTokens[17] = pick(g_palette.codeBlue);
    g_activeTokens[18] = pick(g_palette.errorBg);
    g_activeTokens[19] = pick(g_palette.errorLine);
    // Derived/legacy hexes map to their nearest active token.
    g_activeTokens[20] = g_activeTokens[5];   // C9C9CC -> FAINT
    g_activeTokens[21] = g_activeTokens[17];  // 1C55A5 -> CODE_BLUE
    g_activeTokens[22] = g_activeTokens[19];  // F1D3D0 -> ERROR_LINE
    g_activeTokens[23] = g_activeTokens[18];  // FFF7F6 -> ERROR_BG
    g_ready = true;
    ++g_generation;
}

} // namespace

QList<PaletteInfo> availablePalettes()
{
    QList<PaletteInfo> out;
    const QDir dir(QDir(util::assetsDir()).filePath(QStringLiteral("palettes")));
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        Palette p;
        QString id;
        if (!readPaletteFile(dir.filePath(file), p, id))
            continue;
        PaletteInfo info;
        info.id = file.left(file.size() - 5); // strip .json
        info.name = p.name.isEmpty() ? info.id : p.name;
        out.append(info);
    }
    return out;
}

QString currentPaletteId()
{
    return g_paletteId;
}

QString paletteName(const QString &id)
{
    const QDir dir(QDir(util::assetsDir()).filePath(QStringLiteral("palettes")));
    Palette p;
    QString pid;
    if (readPaletteFile(dir.filePath(id + QStringLiteral(".json")), p, pid) && !p.name.isEmpty())
        return p.name;
    return id;
}

bool darkPalette()
{
    return luminance(g_palette.white) < luminance(g_palette.ink);
}

quint64 themeGeneration()
{
    return g_generation;
}

bool readPalette(const QString &id, Palette &out)
{
    if (id.isEmpty())
        return false;
    const QDir dir(QDir(util::assetsDir()).filePath(QStringLiteral("palettes")));
    Palette p;
    QString pid;
    if (!readPaletteFile(dir.filePath(id + QStringLiteral(".json")), p, pid))
        return false;
    p.id = id;
    out = p;
    return true;
}

bool loadPalette(const QString &id)
{
    Palette p;
    if (!readPalette(id, p))
        return false;
    g_palette = p;
    g_paletteId = id;
    recomputeActiveTokens();
    applyAppPalette();
    if (qApp) {
        for (QWidget *w : qApp->allWidgets())
            applyTo(w);
    }
    return true;
}

bool setPalette(const Palette &palette)
{
    if (palette.id.isEmpty())
        return false;
    g_palette = palette;
    g_paletteId = palette.id;
    recomputeActiveTokens();
    applyAppPalette();
    if (qApp) {
        for (QWidget *w : qApp->allWidgets())
            applyTo(w);
    }
    return true;
}

bool darkMode()
{
    return g_dark;
}

QColor map(const QColor &color)
{
    // Resolve a canonical light-mode token to the active palette color. This is
    // what keeps every widget palette-aware: they store canonical tokens like
    // theme::INK and call map() at paint time. Unknown colors pass through.
    if (!g_ready || !color.isValid() || color.alpha() == 0)
        return color;
    const QString name = color.name(QColor::HexRgb);
    for (int i = 0; i < kTokenCount; ++i) {
        if (name.compare(QLatin1String(kLightTokens[i].lightHex), Qt::CaseInsensitive) == 0)
            return g_activeTokens[i];
    }
    return color;
}

QString remap(const QString &lightCss)
{
    if (lightCss.isEmpty() || !g_ready)
        return lightCss;
    // When the active tokens equal the canonical light tokens (light palette in
    // light mode), there is nothing to rewrite.
    bool identical = true;
    for (int i = 0; i < kTokenCount; ++i) {
        if (g_activeTokens[i].name().compare(QLatin1String(kLightTokens[i].lightHex), Qt::CaseInsensitive) != 0) {
            identical = false;
            break;
        }
    }
    if (identical)
        return lightCss;
    QString out = lightCss;
    for (int i = 0; i < kTokenCount; ++i)
        out.replace(QLatin1String(kLightTokens[i].lightHex),
                    QStringLiteral("@TKCOLOR%1@").arg(i), Qt::CaseInsensitive);
    for (int i = 0; i < kTokenCount; ++i)
        out.replace(QStringLiteral("@TKCOLOR%1@").arg(i), g_activeTokens[i].name());
    return out;
}

void style(QWidget *widget, const QString &lightCss)
{
    if (!widget)
        return;
    widget->setProperty("_takoyo_light_css", lightCss);
    widget->setStyleSheet(remap(lightCss));
}

void setSurface(QWidget *widget)
{
    if (!widget)
        return;
    widget->setAutoFillBackground(true);
    widget->setProperty("_takoyo_surface", true);
    applySurface(widget);
}

void setTextPalette(QWidget *widget)
{
    if (!widget)
        return;
    widget->setProperty("_takoyo_text_palette", true);
    applyTextPalette(widget);
}

void refresh(QWidget *root)
{
    if (!root)
        return;
    applyTo(root);
    const QList<QWidget *> children = root->findChildren<QWidget *>();
    for (QWidget *widget : children)
        applyTo(widget);
}

QPalette appPalette()
{
    if (!g_ready)
        loadPalette(g_paletteId);
    QPalette pal;
    pal.setColor(QPalette::Window, g_activeTokens[0]);     // WHITE
    pal.setColor(QPalette::WindowText, g_activeTokens[1]); // INK
    pal.setColor(QPalette::Base, g_activeTokens[9]);       // HOVER
    pal.setColor(QPalette::AlternateBase, g_activeTokens[10]);
    pal.setColor(QPalette::ToolTipBase, g_activeTokens[10]);
    pal.setColor(QPalette::ToolTipText, g_activeTokens[1]);
    pal.setColor(QPalette::Text, g_activeTokens[1]);
    pal.setColor(QPalette::Button, g_activeTokens[9]);
    pal.setColor(QPalette::ButtonText, g_activeTokens[1]);
    pal.setColor(QPalette::BrightText, g_activeTokens[13]); // DANGER
    pal.setColor(QPalette::Highlight, g_activeTokens[10]);  // ACTIVE
    pal.setColor(QPalette::HighlightedText, g_activeTokens[0]);
    pal.setColor(QPalette::Link, g_activeTokens[17]);       // CODE_BLUE
    pal.setColor(QPalette::PlaceholderText, g_activeTokens[5]); // FAINT
    pal.setColor(QPalette::Light, g_activeTokens[9]);
    pal.setColor(QPalette::Midlight, g_activeTokens[10]);
    pal.setColor(QPalette::Mid, g_activeTokens[11]);
    pal.setColor(QPalette::Dark, g_activeTokens[6]);
    pal.setColor(QPalette::Shadow, QColor("#000000"));
    pal.setColor(QPalette::Disabled, QPalette::Text, g_activeTokens[5]);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, g_activeTokens[5]);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, g_activeTokens[5]);
    pal.setColor(QPalette::Disabled, QPalette::Base, g_activeTokens[9]);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, g_activeTokens[10]);
    return pal;
}

void applyAppPalette()
{
    if (qApp)
        qApp->setPalette(appPalette());
}

void setDarkMode(bool dark)
{
    if (g_dark == dark && g_ready)
        return;
    g_dark = dark;
    recomputeActiveTokens();
    applyAppPalette();
    if (qApp) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        qApp->styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
#endif
    }
}

void loadFonts()
{
    const QDir dir(QDir(util::assetsDir()).filePath(QStringLiteral("fonts")));
    QStringList loaded;
    const QStringList files = dir.entryList({QStringLiteral("*.ttf")}, QDir::Files);
    for (const QString &file : files) {
        const int id = QFontDatabase::addApplicationFont(dir.filePath(file));
        if (id < 0)
            continue;
        loaded << QFontDatabase::applicationFontFamilies(id);
    }
    if (loaded.isEmpty())
        return;

    g_regular = pickFamily(loaded, {QStringLiteral("Poppins Regular"), QStringLiteral("Poppins")});
    g_medium = pickFamily(loaded, {QStringLiteral("Poppins Medium"), QStringLiteral("Poppins")});
    g_semibold = pickFamily(loaded, {QStringLiteral("Poppins SemiBold"), QStringLiteral("Poppins")});
    g_bold = pickFamily(loaded, {QStringLiteral("Poppins ExtraBold"), QStringLiteral("Poppins"), QStringLiteral("Poppins SemiBold")});
    if (g_regular.isEmpty())
        g_regular = QStringLiteral("Poppins");
    if (g_medium.isEmpty())
        g_medium = g_regular;
    if (g_semibold.isEmpty())
        g_semibold = g_regular;
    if (g_bold.isEmpty())
        g_bold = g_semibold;
}

QString familyFor(const QString &weight)
{
    const QString w = weight.toLower();
    if (w == QLatin1String("semibold") || w == QLatin1String("semi") || w == QLatin1String("600"))
        return g_semibold;
    if (w == QLatin1String("medium") || w == QLatin1String("500"))
        return g_medium;
    if (w == QLatin1String("bold") || w == QLatin1String("extrabold") || w == QLatin1String("700")
        || w == QLatin1String("800"))
        return g_bold;
    return g_regular;
}

QFont font(int pixelSize, const QString &weight)
{
    QFont f(familyFor(weight));
    f.setPixelSize(pixelSize);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont mono(int pixelSize, bool bold)
{
    QFont f(g_mono);
    f.setPixelSize(pixelSize);
    if (bold)
        f.setBold(true);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

} // namespace theme