// Point d'entree de la suite de tests.
//
// Le rendu a besoin d'une QGuiApplication pour la gestion des polices. La
// plateforme « offscreen » permet de peindre sans ecran, donc de verifier le
// rendu en integration continue au lieu de le laisser hors couverture.
#include <catch2/catch_session.hpp>

#include <QGuiApplication>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Arcus"));
    QCoreApplication::setApplicationName(QStringLiteral("Arcus"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    return Catch::Session().run(argc, argv);
}
