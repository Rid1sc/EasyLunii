#include "app/devicepanel.h"
#include "app/librarypage.h"
#include "app/mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QPixmapCache>
#include <QStyleHints>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("easylunii");
    QApplication::setApplicationDisplayName("EasyLunii");
    QApplication::setOrganizationName("easylunii");
    QApplication::setApplicationVersion(QStringLiteral(EASYLUNII_VERSION));
    QApplication::setDesktopFileName("easylunii");
    QPixmapCache::setCacheLimit(48 * 1024);   // vignettes affichées (Ko)
#ifdef Q_OS_WIN
    // pas de thème d'icônes sous Windows : sous-ensemble de Breeze livré dans icons/
    const QString icons = QApplication::applicationDirPath() + "/icons";
    if (QDir(icons).exists()) {
        QIcon::setThemeSearchPaths({icons});
        const auto applyTheme = []() {
            QIcon::setThemeName(QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
                                    ? "breeze-dark" : "breeze");
        };
        applyTheme();
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, applyTheme);
    }
#endif

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption devOpt("device", QApplication::translate("main", "Ouvrir une Lunii montée dans <dossier>."), "dossier");
    cli.addOption(devOpt);
    cli.addPositionalArgument("pack", QApplication::translate("main", "Pack STUdio à ouvrir dans l'éditeur."));
    cli.process(app);

    MainWindow w;
    if (cli.isSet(devOpt))
        w.libraryPage()->devicePanel()->openPath(cli.value(devOpt));
    if (!cli.positionalArguments().isEmpty())
        w.openInEditor(cli.positionalArguments().first());
    w.show();

    // aide au développement : EASYLUNII_GRAB=fichier.png enregistre une capture de la fenêtre
    // toutes les 3 s (fiable même quand la fenêtre est masquée par d'autres)
    const QString grab = qEnvironmentVariable("EASYLUNII_GRAB");
    if (!grab.isEmpty()) {
        auto *t = new QTimer(&w);
        QObject::connect(t, &QTimer::timeout, &w, [&w, grab]() { w.grab().save(grab); });
        t->start(3000);
    }
    return app.exec();
}
