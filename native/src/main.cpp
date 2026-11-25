#include "mainwindow.h"
#include "config.h"
#include "theme.h"
#include "util.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Takoyo"));
    QApplication::setOrganizationName(QStringLiteral("Takoyo"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    theme::loadFonts();
    QApplication::setFont(theme::font(13));

    // Load the active palette and light/dark toggle before any widget is
    // constructed so the first paint already reflects the user's choice.
    Config bootConfig;
    theme::setDarkMode(bootConfig.flag(QStringLiteral("dark_mode")));
    theme::loadPalette(bootConfig.str(QStringLiteral("palette")));

    const QString iconPath = QDir(util::assetsDir()).filePath(QStringLiteral("takoyo.ico"));
    if (QFileInfo::exists(iconPath))
        QApplication::setWindowIcon(QIcon(iconPath));

    MainWindow window;
    window.show();
    return QApplication::exec();
}
