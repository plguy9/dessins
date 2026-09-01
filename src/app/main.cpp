#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QPixmap>
#include <QSettings>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Arcus"));
    QCoreApplication::setApplicationName(QStringLiteral("Arcus"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ARCUS_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral("Arcus"));

    // Les boutons standards des boites de dialogue, les boites de fichiers et
    // les messages d'erreur viennent de Qt, pas de nous. Sans ses traductions,
    // une fenetre entierement francaise affiche « Cancel » a cote
    // d'« Appliquer ». L'interface n'existe qu'en francais : on impose donc la
    // locale plutot que de suivre celle du systeme.
    static QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(QLocale::French), QStringLiteral("qtbase"),
                          QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    // Le theme est pose avant la premiere fenetre : sans cela le premier
    // affichage clignote au style du systeme avant de basculer.
    QSettings settings;
    dsn::Theme::apply(app, settings.value(QStringLiteral("ui/darkTheme"), true).toBool());
    app.setWindowIcon(dsn::Icons::appIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(
            QStringLiteral("Logiciel de dessin électrique — schémas de commande et de "
                           "puissance, unifilaires, circuits électroniques."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("projet"),
                                 QStringLiteral("Projet .dsn à ouvrir au démarrage."));

    // Capture de la fenetre puis sortie. Sert a documenter l'interface et a
    // verifier en integration continue qu'elle se monte reellement, ce qu'un
    // simple lancement ne prouve pas.
    QCommandLineOption screenshot(
            QStringLiteral("screenshot"),
            QStringLiteral("Capture la fenêtre dans <fichier> puis quitte."),
            QStringLiteral("fichier"));
    parser.addOption(screenshot);
    parser.process(app);

    dsn::MainWindow window;
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openFile(files.first());
    window.show();

    // Ecran d'accueil au demarrage, sauf si un projet est demande en ligne de
    // commande ou si l'on capture la fenetre : ouvrir sur une feuille blanche
    // muette ne montre rien de ce que le logiciel sait faire. Une case le
    // desactive pour de bon.
    if (files.isEmpty() && !parser.isSet(screenshot)
        && settings.value(QStringLiteral("ui/showStartPage"), true).toBool()) {
        QTimer::singleShot(0, &window, [&window] { window.showStartPage(); });
    }

    if (parser.isSet(screenshot)) {
        const QString path = parser.value(screenshot);
        // Un seul aller-retour de la boucle d'evenements suffit a laisser les
        // panneaux se disposer et les vignettes se peindre.
        QTimer::singleShot(400, &app, [&window, path] {
            const bool ok = window.grab().save(path, "PNG");
            qInfo("%s %s", ok ? "capture écrite :" : "capture impossible :",
                  qUtf8Printable(path));
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    return app.exec();
}
