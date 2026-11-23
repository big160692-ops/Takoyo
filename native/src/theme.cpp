#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QStringList>

#include <algorithm>

#include "util.h"

namespace {
QString g_regular = QStringLiteral("Poppins");
QString g_medium = QStringLiteral("Poppins");
QString g_semibold = QStringLiteral("Poppins");
QString g_bold = QStringLiteral("Poppins");
QString g_mono = QStringLiteral("Consolas");
QString g_paletteId = QStringLiteral("dark");

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
} // namespace

namespace theme {

Palette darkPalette()
{
    Palette p;
    p.white = QColor(QStringLiteral("#0B0B0D"));
    p.ink = QColor(QStringLiteral("#FFFFFF"));
    p.inkSoft = QColor(QStringLiteral("#C9C9CE"));
    p.muted = QColor(QStringLiteral("#8B8B8F"));
    p.faint = QColor(QStringLiteral("#6E6E73"));
    p.line = QColor(QStringLiteral("#E7E7E9"));
    p.lineSoft = QColor(QStringLiteral("#26262A"));
    p.border = QColor(QStringLiteral("#3A3B3E"));
    p.hover = QColor(QStringLiteral("#1C1C20"));
    p.active = QColor(QStringLiteral("#24242A"));
    p.searchBg = QColor(QStringLiteral("#16161A"));
    p.chip = QColor(QStringLiteral("#17171B"));
    p.danger = QColor(QStringLiteral("#FF6B60"));
    p.success = QColor(QStringLiteral("#2FBF71"));
    p.codeRed = QColor(QStringLiteral("#FF7B72"));
    p.codeGreen = QColor(QStringLiteral("#56D364"));
    p.codeBlue = QColor(QStringLiteral("#79B8FF"));
    p.errorBg = QColor(QStringLiteral("#1A1113"));
    p.errorLine = QColor(QStringLiteral("#4A2E2C"));
    return p;
}

void setPalette(const Palette &p)
{
    WHITE = p.white;
    INK = p.ink;
    INK_SOFT = p.inkSoft;
    MUTED = p.muted;
    FAINT = p.faint;
    LINE = p.line;
    LINE_SOFT = p.lineSoft;
    BORDER = p.border;
    HOVER = p.hover;
    ACTIVE = p.active;
    SEARCH_BG = p.searchBg;
    CHIP = p.chip;
    DANGER = p.danger;
    SUCCESS = p.success;
    CODE_RED = p.codeRed;
    CODE_GREEN = p.codeGreen;
    CODE_BLUE = p.codeBlue;
    ERROR_BG = p.errorBg;
    ERROR_LINE = p.errorLine;
}

QString currentPaletteId()
{
    return g_paletteId;
}

void applyAppPalette()
{
    // Give Qt's own widgets (tooltips, combo popups, scrollbars, menus) the
    // current ground too, so nothing falls back to the system light palette.
    // Remember WHITE is the ground and INK is the text in this theme.
    QPalette dark = QApplication::palette();
    dark.setColor(QPalette::Window, WHITE);
    dark.setColor(QPalette::WindowText, INK);
    dark.setColor(QPalette::Base, WHITE);
    dark.setColor(QPalette::AlternateBase, ACTIVE);
    dark.setColor(QPalette::ToolTipBase, CHIP);
    dark.setColor(QPalette::ToolTipText, INK);
    dark.setColor(QPalette::Text, INK);
    dark.setColor(QPalette::Button, WHITE);
    dark.setColor(QPalette::ButtonText, INK);
    dark.setColor(QPalette::BrightText, DANGER);
    dark.setColor(QPalette::Link, CODE_BLUE);
    dark.setColor(QPalette::LinkVisited, CODE_BLUE);
    dark.setColor(QPalette::Highlight, ACTIVE);
    dark.setColor(QPalette::HighlightedText, INK);
    dark.setColor(QPalette::PlaceholderText, MUTED);
    dark.setColor(QPalette::Disabled, QPalette::Text, FAINT);
    dark.setColor(QPalette::Disabled, QPalette::WindowText, FAINT);
    dark.setColor(QPalette::Disabled, QPalette::ButtonText, FAINT);
    qApp->setPalette(dark);
}

namespace {
// JSON key -> Palette member, for the 18 colour roles.
struct RoleKey {
    const char *key;
    QColor Palette::*member;
};
const RoleKey kRoleKeys[] = {
    {"WHITE", &Palette::white},         {"INK", &Palette::ink},
    {"INK_SOFT", &Palette::inkSoft},    {"MUTED", &Palette::muted},
    {"FAINT", &Palette::faint},         {"LINE", &Palette::line},
    {"LINE_SOFT", &Palette::lineSoft},  {"BORDER", &Palette::border},
    {"HOVER", &Palette::hover},
    {"ACTIVE", &Palette::active},       {"SEARCH_BG", &Palette::searchBg},
    {"CHIP", &Palette::chip},           {"DANGER", &Palette::danger},
    {"SUCCESS", &Palette::success},     {"CODE_RED", &Palette::codeRed},
    {"CODE_GREEN", &Palette::codeGreen},{"CODE_BLUE", &Palette::codeBlue},
    {"ERROR_BG", &Palette::errorBg},    {"ERROR_LINE", &Palette::errorLine},
};

bool readPaletteFile(const QString &path, Palette *out, QString *name)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject object = doc.object();
    if (name)
        *name = object.value(QStringLiteral("name")).toString();

    // Start from the bundled dark defaults: partial files are allowed and a
    // missing/invalid key can never produce an unstyled screen.
    Palette palette = darkPalette();
    int valid = 0;
    for (const RoleKey &role : kRoleKeys) {
        const QString raw = object.value(QLatin1String(role.key)).toString();
        if (raw.isEmpty())
            continue;
        const QColor color(raw);
        if (!color.isValid())
            continue;
        palette.*(role.member) = color;
        ++valid;
    }
    if (valid == 0)
        return false;
    if (out)
        *out = palette;
    return true;
}
} // namespace

bool loadPalette(const QString &id)
{
    if (id.isEmpty() || id == QLatin1String("dark")) {
        setPalette(darkPalette());
        g_paletteId = QStringLiteral("dark");
        return true;
    }
    // ids are file stems: reject anything that could escape the palettes dir.
    for (const QChar c : id) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            return false;
    }
    const QString path = QDir(util::assetsDir())
                             .filePath(QStringLiteral("palettes/") + id + QStringLiteral(".json"));
    Palette palette;
    if (!readPaletteFile(path, &palette, nullptr))
        return false;
    setPalette(palette);
    g_paletteId = id;
    return true;
}

QList<PaletteInfo> availablePalettes()
{
    QList<PaletteInfo> out;
    const QDir dir(QDir(util::assetsDir()).filePath(QStringLiteral("palettes")));
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        PaletteInfo info;
        info.id = QFileInfo(file).completeBaseName();
        QString name;
        readPaletteFile(dir.filePath(file), nullptr, &name);
        info.name = name.isEmpty() ? info.id : name;
        out.append(info);
    }
    if (out.isEmpty())
        out.append(PaletteInfo{QStringLiteral("dark"), QStringLiteral("Dark")});
    std::sort(out.begin(), out.end(),
              [](const PaletteInfo &a, const PaletteInfo &b) { return a.name < b.name; });
    return out;
}

QString paletteName(const QString &id)
{
    const auto palettes = availablePalettes();
    for (const PaletteInfo &info : palettes) {
        if (info.id == id)
            return info.name;
    }
    return id;
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
