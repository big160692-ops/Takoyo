#include "theme.h"

#include <QDir>
#include <QFontDatabase>
#include <QStringList>

#include "util.h"

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
} // namespace

namespace theme {

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
