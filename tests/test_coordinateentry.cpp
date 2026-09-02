#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/coordinateentry.h"

using namespace dsn;
using Catch::Matchers::WithinAbs;

TEST_CASE("Un nombre seul est une distance dans la direction visee",
          "[coordinates]")
{
    // La forme la plus utilisee de toutes : on vise a la souris, on tape la
    // cote. C'est ce qui separe dessiner de pointer.
    const QPointF from(100, 100);
    const auto point = CoordinateEntry::resolve(QStringLiteral("50"), &from,
                                                QPointF(180, 100)); // vise a droite
    REQUIRE(point);
    CHECK_THAT(point->x(), WithinAbs(150.0, 1e-9));
    CHECK_THAT(point->y(), WithinAbs(100.0, 1e-9));

    // La direction seule compte, pas la distance du curseur.
    const auto up = CoordinateEntry::resolve(QStringLiteral("25"), &from, QPointF(100, 40));
    REQUIRE(up);
    CHECK_THAT(up->x(), WithinAbs(100.0, 1e-9));
    CHECK_THAT(up->y(), WithinAbs(75.0, 1e-9));
}

TEST_CASE("Sans direction visee, une distance seule ne designe rien",
          "[coordinates]")
{
    // Inventer une direction par defaut ferait poser le point ailleurs que
    // la ou l'utilisateur regarde : mieux vaut ne rien faire.
    const QPointF from(100, 100);
    CHECK_FALSE(CoordinateEntry::resolve(QStringLiteral("50"), &from, from).has_value());
    CHECK_FALSE(CoordinateEntry::resolve(QStringLiteral("50"), nullptr,
                                         QPointF(180, 100)).has_value());
}

TEST_CASE("La forme polaire mesure depuis le point de depart", "[coordinates]")
{
    const QPointF from(100, 100);

    // 0 degre pointe a droite, comme sur un plan cote.
    auto point = CoordinateEntry::resolve(QStringLiteral("50<0"), &from, QPointF());
    REQUIRE(point);
    CHECK_THAT(point->x(), WithinAbs(150.0, 1e-9));
    CHECK_THAT(point->y(), WithinAbs(100.0, 1e-9));

    // 90 degres monte : l'angle se lit a l'endroit, l'ecran compte a l'envers.
    point = CoordinateEntry::resolve(QStringLiteral("50<90"), &from, QPointF());
    REQUIRE(point);
    CHECK_THAT(point->x(), WithinAbs(100.0, 1e-9));
    CHECK_THAT(point->y(), WithinAbs(50.0, 1e-9));

    point = CoordinateEntry::resolve(QStringLiteral("@40<180"), &from, QPointF());
    REQUIRE(point);
    CHECK_THAT(point->x(), WithinAbs(60.0, 1e-9));
    CHECK_THAT(point->y(), WithinAbs(100.0, 1e-9));
}

TEST_CASE("Une paire est relative par defaut, comme en saisie dynamique",
          "[coordinates]")
{
    const QPointF from(100, 100);

    auto point = CoordinateEntry::resolve(QStringLiteral("10,5"), &from, QPointF());
    REQUIRE(point);
    CHECK_THAT(point->x(), WithinAbs(110.0, 1e-9));
    CHECK_THAT(point->y(), WithinAbs(95.0, 1e-9));   // « 5 » monte

    // Le @ dit la meme chose, explicitement.
    const auto same = CoordinateEntry::resolve(QStringLiteral("@10,5"), &from, QPointF());
    REQUIRE(same);
    CHECK(*same == *point);

    // Le # force l'absolu : coordonnees dans le folio.
    const auto absolute = CoordinateEntry::resolve(QStringLiteral("#10,5"), &from, QPointF());
    REQUIRE(absolute);
    CHECK_THAT(absolute->x(), WithinAbs(10.0, 1e-9));
    CHECK_THAT(absolute->y(), WithinAbs(5.0, 1e-9));
}

TEST_CASE("Le point-virgule separe aussi, pour ecrire ses decimales a la virgule",
          "[coordinates]")
{
    // La virgule est le separateur de coordonnees de toute la CAO. Qui tient
    // a « 50,5 » en decimal a le point-virgule.
    const QPointF from(0, 0);
    const auto point = CoordinateEntry::resolve(QStringLiteral("10;5"), &from, QPointF());
    REQUIRE(point);
    CHECK_THAT(point->x(), WithinAbs(10.0, 1e-9));
    CHECK_THAT(point->y(), WithinAbs(-5.0, 1e-9));
}

TEST_CASE("Une saisie incomprehensible ne designe rien", "[coordinates]")
{
    // Mieux vaut ne rien faire qu'interpreter de travers une cote : un point
    // pose au mauvais endroit se remarque bien plus tard.
    const QPointF from(100, 100);
    for (const char *bad : { "", "   ", "abc", "10,", ",5", "50<", "<45", "#50", "10,5,7x" })
        CHECK_FALSE(CoordinateEntry::resolve(QString::fromLatin1(bad), &from,
                                             QPointF(180, 100)).has_value());
}

TEST_CASE("Les nombres negatifs et decimaux passent", "[coordinates]")
{
    const QPointF from(100, 100);
    const auto point = CoordinateEntry::resolve(QStringLiteral("@-12.5,-7.5"), &from, QPointF());
    REQUIRE(point);
    CHECK_THAT(point->x(), WithinAbs(87.5, 1e-9));
    CHECK_THAT(point->y(), WithinAbs(107.5, 1e-9));
}

TEST_CASE("Seuls certains caracteres ouvrent une saisie", "[coordinates]")
{
    // C'est ce qui decide si une frappe ouvre le champ ou reste un raccourci :
    // « R » doit continuer a faire pivoter, « 5 » doit ouvrir la cote.
    CHECK(CoordinateEntry::startsEntry(QStringLiteral("5")));
    CHECK(CoordinateEntry::startsEntry(QStringLiteral("@")));
    CHECK(CoordinateEntry::startsEntry(QStringLiteral("#")));
    CHECK(CoordinateEntry::startsEntry(QStringLiteral("-")));
    CHECK(CoordinateEntry::startsEntry(QStringLiteral(".")));
    CHECK_FALSE(CoordinateEntry::startsEntry(QStringLiteral("R")));
    CHECK_FALSE(CoordinateEntry::startsEntry(QStringLiteral("w")));
    CHECK_FALSE(CoordinateEntry::startsEntry(QString()));
}

TEST_CASE("L'angle ecran se lit a l'endroit", "[coordinates]")
{
    // L'axe des ordonnees descend a l'ecran ; l'angle doit malgre tout se lire
    // comme sur un plan cote, sens trigonometrique, origine a trois heures.
    CHECK_THAT(CoordinateEntry::screenAngle(QPointF(10, 0)), WithinAbs(0.0, 1e-9));
    CHECK_THAT(CoordinateEntry::screenAngle(QPointF(0, -10)), WithinAbs(90.0, 1e-9));
    CHECK_THAT(CoordinateEntry::screenAngle(QPointF(-10, 0)), WithinAbs(180.0, 1e-9));
    CHECK_THAT(CoordinateEntry::screenAngle(QPointF(0, 10)), WithinAbs(270.0, 1e-9));
}

TEST_CASE("Une cote peut porter son unité", "[coordonnees][unites]")
{
    // Le dessin se compte en millimètres, mais on ne pense pas un chemin de
    // câbles en millimètres : on dit « dix mètres ». C'est le geste d'AutoCAD,
    // signalé à l'usage — *« nous pouvons écrire 10 m et il sera à l'échelle
    // comparé à 3 cm »*. Sans suffixe, c'est le millimètre : le cas courant ne
    // doit rien coûter à écrire.
    const QPointF origine(0.0, 0.0);
    const QPointF vise(10.0, 0.0); // le curseur, vers la droite

    const auto distance = [&](const QString &texte) {
        const auto p = CoordinateEntry::resolve(texte, &origine, vise);
        REQUIRE(p.has_value());
        return p->x();
    };

    CHECK(distance(QStringLiteral("150")) == 150.0);    // millimètres par défaut
    CHECK(distance(QStringLiteral("150mm")) == 150.0);
    CHECK(distance(QStringLiteral("3cm")) == 30.0);
    CHECK(distance(QStringLiteral("10m")) == 10000.0);
    CHECK(distance(QStringLiteral("0.5m")) == 500.0);
    // La casse et l'espace sont indifférentes : on ne vérifie pas sa touche
    // Majuscule en cotant.
    CHECK(distance(QStringLiteral("10 M")) == 10000.0);
    // Le pouce, pour les catalogues américains.
    CHECK(distance(QStringLiteral("2in")) == 50.8);

    // « mm » doit être essayé avant « m », sinon « 10mm » se lirait « 10 m ».
    CHECK(distance(QStringLiteral("10mm")) == 10.0);

    // Une unité seule ne veut rien dire : on ne pose rien plutôt que d'inventer.
    CHECK_FALSE(CoordinateEntry::resolve(QStringLiteral("m"), &origine, vise).has_value());
}
