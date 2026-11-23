#include "config.h"
#include "mainwindow.h"
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

    // Apply the configured colour palette before the first paint. A missing
    // key or corrupt palette file falls back to the bundled dark palette.
    Config config;
    theme::loadPalette(config.str(QStringLiteral("palette")));
    theme::applyAppPalette();

    const QString iconPath = QDir(util::assetsDir()).filePath(QStringLiteral("takoyo.ico"));
    if (QFileInfo::exists(iconPath))
        QApplication::setWindowIcon(QIcon(iconPath));

    MainWindow window;
    window.show();
    return QApplication::exec();
}
