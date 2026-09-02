#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>

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

// --------------------------------------------------------------------------
// Decalage parallele et bus multi-conducteurs.

TEST_CASE("Le decalage parallele garde les jambes d'un coude paralleles", "[wiretools][bus]")
{
    // Le piege que la fonction existe pour eviter : translater la polyligne
    // entiere garde sa forme, mais le sommet part de biais et les deux jambes
    // ne sont plus a la bonne distance de l'original. Sur un coude en L, le
    // sommet decale doit rester un coude droit.
    const QVector<QPointF> path{ { 0, 0 }, { 100, 0 }, { 100, 60 } };
    const QVector<QPointF> shifted = WireTools::offsetPolyline(path, 10.0);

    REQUIRE(shifted.size() == 3);
    // Vers le bas pour la jambe horizontale tracee de gauche a droite.
    CHECK(shifted.at(0).y() == Catch::Approx(10.0));
    CHECK(shifted.at(0).x() == Catch::Approx(0.0));
    // Le sommet est l'intersection des deux droites decalees : (90, 10).
    CHECK(shifted.at(1).x() == Catch::Approx(90.0));
    CHECK(shifted.at(1).y() == Catch::Approx(10.0));
    CHECK(shifted.at(2).x() == Catch::Approx(90.0));
    CHECK(shifted.at(2).y() == Catch::Approx(60.0));
}

TEST_CASE("Le decalage d'une droite est une simple translation", "[wiretools][bus]")
{
    const QVector<QPointF> path{ { 10, 20 }, { 90, 20 } };
    const QVector<QPointF> shifted = WireTools::offsetPolyline(path, 5.0);
    REQUIRE(shifted.size() == 2);
    CHECK(shifted.at(0) == QPointF(10, 25));
    CHECK(shifted.at(1) == QPointF(90, 25));
}

TEST_CASE("Le cote d'un bus ne depend pas du sens du trace", "[wiretools][bus]")
{
    // Le meme geste doit donner le meme dessin. Tire de droite a gauche, un
    // bus dont le cote suivrait la normale du trace partirait vers le haut :
    // deux dessins pour un seul geste, et le dessinateur ne sait plus lequel
    // il obtiendra.
    BusSpec spec;
    spec.count = 3;
    spec.spacing = 5.0;

    const auto rightward = WireTools::busPaths({ { 0, 50 }, { 100, 50 } }, spec);
    const auto leftward = WireTools::busPaths({ { 100, 50 }, { 0, 50 } }, spec);

    REQUIRE(rightward.size() == 3);
    REQUIRE(leftward.size() == 3);
    CHECK(rightward.at(2).at(0).y() == Catch::Approx(60.0));
    CHECK(leftward.at(2).at(0).y() == Catch::Approx(60.0));
}

TEST_CASE("Un bus vertical s'ecarte vers la droite", "[wiretools][bus]")
{
    // Pour la meme raison, et parce que c'est la ou un folio met les
    // conducteurs suivants : une echelle de commande descend a gauche.
    BusSpec spec;
    spec.count = 2;
    spec.spacing = 4.0;

    const auto down = WireTools::busPaths({ { 50, 0 }, { 50, 80 } }, spec);
    const auto up = WireTools::busPaths({ { 50, 80 }, { 50, 0 } }, spec);

    REQUIRE(down.size() == 2);
    REQUIRE(up.size() == 2);
    CHECK(down.at(1).at(0).x() == Catch::Approx(54.0));
    CHECK(up.at(1).at(0).x() == Catch::Approx(54.0));
}

TEST_CASE("Un bus refuse un reglage sans effet", "[wiretools][bus]")
{
    // Un seul conducteur n'est pas un bus, et un pas nul empilerait les fils
    // les uns sur les autres — invisible a l'ecran, faux dans la netlist.
    BusSpec spec;
    spec.count = 1;
    CHECK_FALSE(spec.isValid());
    CHECK(WireTools::busPaths({ { 0, 0 }, { 10, 0 } }, spec).isEmpty());

    spec.count = 3;
    spec.spacing = 0.0;
    CHECK_FALSE(spec.isValid());
}

TEST_CASE("Les conducteurs d'un bus restent equidistants sur tout le parcours",
          "[wiretools][bus]")
{
    // Trois conducteurs a pas constant : le troisieme est deux pas plus loin
    // que le premier, sommet par sommet. C'est ce qui fait qu'un bus reste
    // lisible apres deux coudes.
    BusSpec spec;
    spec.count = 3;
    spec.spacing = 6.0;

    const auto paths = WireTools::busPaths({ { 0, 0 }, { 80, 0 }, { 80, 50 }, { 140, 50 } },
                                           spec);
    REQUIRE(paths.size() == 3);
    for (const QVector<QPointF> &path : paths)
        REQUIRE(path.size() == 4);

    for (int vertex = 0; vertex < 4; ++vertex) {
        const QPointF first = paths.at(0).at(vertex);
        const QPointF second = paths.at(1).at(vertex);
        const QPointF third = paths.at(2).at(vertex);
        CHECK((third - first).x() == Catch::Approx(2.0 * (second - first).x()));
        CHECK((third - first).y() == Catch::Approx(2.0 * (second - first).y()));
    }
}
