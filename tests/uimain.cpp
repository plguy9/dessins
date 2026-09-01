// Point d'entree des tests d'interface.
//
// Ils construisent de vraies fenetres sur la plateforme « offscreen ». Un
// binaire qui demarre ne prouve pas qu'une fenetre se monte : la moitie des
// regressions d'interface sont des panneaux qui n'apparaissent plus ou des
// signaux debranches, et rien de tout cela ne se voit sans monter la fenetre.
#include <catch2/catch_session.hpp>

#include <QApplication>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Arcus"));
    QCoreApplication::setApplicationName(QStringLiteral("ArcusTests"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    return Catch::Session().run(argc, argv);
}
