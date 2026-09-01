#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/wiretools.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;
using Catch::Matchers::WithinAbs;

TEST_CASE("Une portion encadree par deux croisements laisse deux morceaux", "[wiretools][trim]")
{
    Project project;
    Folio *folio = project.addFolio();
    // Un fil horizontal traverse par deux verticales : ajuster au milieu doit
    // retirer la portion centrale et laisser les deux bouts.
    Wire *bus = drawWire(folio, { QPointF(0, 50), QPointF(200, 50) });
    drawWire(folio, { QPointF(60, 20), QPointF(60, 80) });
    drawWire(folio, { QPointF(140, 20), QPointF(140, 80) });

    const auto result = WireTools::trim(*folio, project.library, bus->id(), QPointF(100, 50));
    REQUIRE(result);
    REQUIRE(result->pieces.size() == 2);
    CHECK(result->pieces.at(0).first() == QPointF(0, 50));
    CHECK(result->pieces.at(0).last() == QPointF(60, 50));
    CHECK(result->pieces.at(1).first() == QPointF(140, 50));
    CHECK(result->pieces.at(1).last() == QPointF(200, 50));
}

TEST_CASE("Une portion en bout de fil ne laisse qu'un morceau", "[wiretools][trim]")
{
    Project project;
    Folio *folio = project.addFolio();
    Wire *bus = drawWire(folio, { QPointF(0, 50), QPointF(200, 50) });
    drawWire(folio, { QPointF(60, 20), QPointF(60, 80) });

    // Clic apres l'unique croisement : la queue s'en va, la tete reste.
    const auto result = WireTools::trim(*folio, project.library, bus->id(), QPointF(120, 50));
    REQUIRE(result);
    REQUIRE(result->pieces.size() == 1);
    CHECK(result->pieces.first().first() == QPointF(0, 50));
    CHECK(result->pieces.first().last() == QPointF(60, 50));
}

TEST_CASE("Un fil sans croisement disparait entierement", "[wiretools][trim]")
{
    Project project;
    Folio *folio = project.addFolio();
    Wire *lonely = drawWire(folio, { QPointF(0, 50), QPointF(200, 50) });

    // Rien pour le couper : le fil entier s'en va. C'est previsible, et
    // c'est le comportement d'AutoCAD.
    const auto result = WireTools::trim(*folio, project.library, lonely->id(), QPointF(100, 50));
    REQUIRE(result);
    CHECK(result->pieces.isEmpty());
}

TEST_CASE("Un fil coude ajuste garde ses coudes", "[wiretools][trim]")
{
    Project project;
    Folio *folio = project.addFolio();
    Wire *bent = drawWire(folio, { QPointF(0, 0), QPointF(100, 0), QPointF(100, 100),
                                   QPointF(200, 100) });
    // Deux coupes : une avant le premier coude, une apres le second.
    drawWire(folio, { QPointF(40, -20), QPointF(40, 20) });
    drawWire(folio, { QPointF(160, 80), QPointF(160, 120) });

    const auto result = WireTools::trim(*folio, project.library, bent->id(), QPointF(100, 50));
    REQUIRE(result);
    REQUIRE(result->pieces.size() == 2);
    // Le premier morceau va du depart a la premiere coupe, en ligne droite.
    CHECK(result->pieces.at(0).first() == QPointF(0, 0));
    CHECK(result->pieces.at(0).last() == QPointF(40, 0));
    // Le second part de la seconde coupe et rejoint la fin.
    CHECK(result->pieces.at(1).first() == QPointF(160, 100));
    CHECK(result->pieces.at(1).last() == QPointF(200, 100));
}

TEST_CASE("La decoupe conserve les sommets interieurs", "[wiretools]")
{
    const QVector<QPointF> points{ QPointF(0, 0), QPointF(100, 0), QPointF(100, 100) };
    CHECK_THAT(WireTools::polylineLength(points), WithinAbs(200.0, 1e-9));
    CHECK(WireTools::pointAtLength(points, 150.0) == QPointF(100, 50));

    // Une portion qui enjambe le coude doit garder ce coude.
    const QVector<QPointF> part = WireTools::subPolyline(points, 50.0, 150.0);
    REQUIRE(part.size() == 3);
    CHECK(part.at(0) == QPointF(50, 0));
    CHECK(part.at(1) == QPointF(100, 0));
    CHECK(part.at(2) == QPointF(100, 50));
}

TEST_CASE("Un fil se prolonge jusqu'au premier obstacle", "[wiretools][extend]")
{
    Project project;
    Folio *folio = project.addFolio();
    Wire *stub = drawWire(folio, { QPointF(0, 50), QPointF(80, 50) });
    drawWire(folio, { QPointF(120, 20), QPointF(120, 80) });
    // Un second obstacle plus loin : c'est le plus proche qui doit gagner.
    drawWire(folio, { QPointF(180, 20), QPointF(180, 80) });

    const auto target = WireTools::extend(*folio, project.library, stub->id(), true);
    REQUIRE(target);
    CHECK(*target == QPointF(120, 50));
}

TEST_CASE("Un prolongement sans cible ne renvoie rien", "[wiretools][extend]")
{
    Project project;
    Folio *folio = project.addFolio();
    Wire *stub = drawWire(folio, { QPointF(0, 50), QPointF(80, 50) });

    CHECK_FALSE(WireTools::extend(*folio, project.library, stub->id(), true).has_value());
}

TEST_CASE("Le prolongement atteint la broche d'un appareil", "[wiretools][extend]")
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();
    // Broche 1 du gabarit : de (-5,0) a (-2,5) en local, donc de (95,50)
    // a (97,5;50) une fois pose en (100,50).
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 50));
    Wire *stub = drawWire(folio, { QPointF(0, 50), QPointF(60, 50) });

    const auto target = WireTools::extend(*folio, project.library, stub->id(), true);
    REQUIRE(target);
    // Une broche est un obstacle legitime — c'est meme la cible la plus
    // frequente d'un prolongement sur un schema.
    CHECK_THAT(target->x(), WithinAbs(95.0, 0.01));
    CHECK_THAT(target->y(), WithinAbs(50.0, 0.01));
}

TEST_CASE("Le prolongement part aussi de la premiere extremite", "[wiretools][extend]")
{
    Project project;
    Folio *folio = project.addFolio();
    Wire *stub = drawWire(folio, { QPointF(80, 50), QPointF(160, 50) });
    drawWire(folio, { QPointF(20, 20), QPointF(20, 80) });

    const auto target = WireTools::extend(*folio, project.library, stub->id(), false);
    REQUIRE(target);
    CHECK(*target == QPointF(20, 50));
}
