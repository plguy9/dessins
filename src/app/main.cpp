#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLocale>
#include <QPixmap>
#include <QSettings>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Dessins"));
    QCoreApplication::setApplicationName(QStringLiteral("Dessins"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setApplicationDisplayName(QStringLiteral("Dessins"));

    // Le theme est pose avant la premiere fenetre : sans cela le premier
    // affichage clignote au style du systeme avant de basculer.
    QSettings settings;
    dsn::Theme::apply(app, settings.value(QStringLiteral("ui/darkTheme"), true).toBool());

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
