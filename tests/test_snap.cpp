#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/snapengine.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;
using Catch::Matchers::WithinAbs;

namespace {

// Cherche parmi les candidats celui d'un mode donne, le plus proche du point
// attendu. Renvoie nullptr si le mode n'a rien propose.
const SnapHit *findMode(const QVector<SnapHit> &hits, SnapMode mode)
{
    for (const SnapHit &hit : hits) {
        if (hit.mode == mode)
            return &hit;
    }
    return nullptr;
}

} // namespace

TEST_CASE("Le milieu d'un fil est un point d'accrochage", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(20, 50), QPointF(120, 50) });

    SnapEngine engine;
    // Curseur legerement decale du milieu exact : c'est le cas reel.
    const auto hit = engine.snap(*folio, project.library, QPointF(71.5, 51.0), 6.0);
    REQUIRE(hit);
    CHECK(hit->mode == SnapMode::Midpoint);
    CHECK(hit->point == QPointF(70, 50));
    CHECK(hit->label() == QStringLiteral("Milieu"));
}

TEST_CASE("Chaque segment d'un fil coude a son propre milieu", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    // Trois segments : le milieu de chacun doit etre proposable, pas
    // seulement celui du fil entier — c'est le comportement d'AutoCAD sur
    // une polyligne, et c'est ce qu'il faut pour piquer au bon endroit.
    drawWire(folio, { QPointF(0, 0), QPointF(100, 0), QPointF(100, 60), QPointF(200, 60) });

    SnapEngine engine;
    const struct { QPointF near; QPointF expected; } cases[] = {
        { QPointF(51, 1), QPointF(50, 0) },
        { QPointF(101, 31), QPointF(100, 30) },
        { QPointF(151, 61), QPointF(150, 60) },
    };
    for (const auto &c : cases) {
        const auto hit = engine.snap(*folio, project.library, c.near, 6.0);
        REQUIRE(hit);
        CHECK(hit->mode == SnapMode::Midpoint);
        CHECK(hit->point == c.expected);
    }
}

TEST_CASE("Une extremite prime sur un milieu a distance comparable", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(0, 0), QPointF(20, 0) });

    SnapEngine engine;
    // Le curseur est a un millimetre de l'extremite : elle doit gagner.
    const auto hit = engine.snap(*folio, project.library, QPointF(1, 0.5), 8.0);
    REQUIRE(hit);
    CHECK(hit->mode == SnapMode::Endpoint);
    CHECK(hit->point == QPointF(0, 0));
}

TEST_CASE("Un milieu nettement plus proche l'emporte sur une extremite lointaine", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(0, 0), QPointF(60, 0) });

    SnapEngine engine;
    // Curseur colle au milieu (30,0), l'extremite la plus proche est a 30 mm.
    const auto hit = engine.snap(*folio, project.library, QPointF(30.2, 0.2), 40.0);
    REQUIRE(hit);
    CHECK(hit->mode == SnapMode::Midpoint);
}

TEST_CASE("Une broche est un point nodal", "[snap]")
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 100));

    SnapEngine engine;
    // La broche 1 du gabarit est a (-5,0) en local, donc (95,100).
    const auto hit = engine.snap(*folio, project.library, QPointF(95.8, 100.4), 5.0);
    REQUIRE(hit);
    CHECK(hit->mode == SnapMode::Node);
    CHECK(hit->point == QPointF(95, 100));
}

TEST_CASE("Le croisement de deux fils est un point d'intersection", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(0, 50), QPointF(100, 50) });
    drawWire(folio, { QPointF(50, 0), QPointF(50, 100) });

    SnapEngine engine;
    const auto hits = engine.candidates(*folio, project.library, QPointF(50.7, 50.7), 6.0);
    const SnapHit *intersection = findMode(hits, SnapMode::Intersection);
    REQUIRE(intersection);
    CHECK(intersection->point == QPointF(50, 50));
}

TEST_CASE("Un fil ne se croise pas lui-meme", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    // Un fil qui revient sur lui-meme : ses propres segments ne doivent pas
    // produire d'intersection, sinon chaque coude en fabriquerait une.
    drawWire(folio, { QPointF(0, 0), QPointF(100, 0), QPointF(100, 50), QPointF(0, 50) });

    SnapEngine engine;
    const auto hits = engine.candidates(*folio, project.library, QPointF(100, 0), 8.0);
    CHECK(findMode(hits, SnapMode::Intersection) == nullptr);
}

TEST_CASE("Le centre et les quadrants d'un cercle sont accrochables", "[snap]")
{
    Project project;
    SymbolDefinition round;
    round.logicalId = QStringLiteral("rond");
    round.norm = QStringLiteral("IEC");
    round.id = SymbolDefinition::makeId(round.norm, round.logicalId);
    round.name = QStringLiteral("Rond");
    round.graphics.append(Primitive::circle(QPointF(0, 0), 8.0));
    Pin pin;
    pin.number = QStringLiteral("1");
    pin.position = QPointF(0, -12);
    pin.direction = Direction::Up;
    round.pins.append(pin);
    project.library.insert(round);

    Folio *folio = project.addFolio();
    placeSymbol(project, folio, QStringLiteral("iec:rond"), QPointF(100, 100));

    SnapEngine engine;
    const auto centre = engine.snap(*folio, project.library, QPointF(100.4, 100.4), 4.0);
    REQUIRE(centre);
    CHECK(centre->mode == SnapMode::Center);
    CHECK(centre->point == QPointF(100, 100));

    // Quadrant a trois heures : centre + rayon en x.
    const auto hits = engine.candidates(*folio, project.library, QPointF(108.3, 100.2), 4.0);
    const SnapHit *quadrant = findMode(hits, SnapMode::Quadrant);
    REQUIRE(quadrant);
    CHECK(quadrant->point == QPointF(108, 100));
}

TEST_CASE("La perpendiculaire se calcule depuis le point d'origine", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(0, 0), QPointF(100, 0) });

    SnapEngine engine;
    const QPointF from(40, 30);
    // Depuis (40,30), le pied de la perpendiculaire au fil horizontal est
    // (40,0). Sans point d'origine, le mode ne peut rien proposer.
    const auto withOrigin =
            engine.candidates(*folio, project.library, QPointF(40.5, 0.5), 5.0, &from);
    const SnapHit *foot = findMode(withOrigin, SnapMode::Perpendicular);
    REQUIRE(foot);
    CHECK(foot->point == QPointF(40, 0));

    const auto without = engine.candidates(*folio, project.library, QPointF(40.5, 0.5), 5.0);
    CHECK(findMode(without, SnapMode::Perpendicular) == nullptr);
}

TEST_CASE("Le mode proche accroche n'importe ou sur le trait", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(0, 0), QPointF(100, 0) });

    SnapEngine engine;
    engine.setMode(SnapMode::Nearest, true);
    const auto hits = engine.candidates(*folio, project.library, QPointF(37.0, 1.2), 4.0);
    const SnapHit *nearest = findMode(hits, SnapMode::Nearest);
    REQUIRE(nearest);
    CHECK(nearest->point == QPointF(37, 0));
}

TEST_CASE("Le prolongement ne s'active qu'au-dela de l'extremite", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(0, 0), QPointF(50, 0) });

    SnapEngine engine;
    engine.setMode(SnapMode::Extension, true);

    // Au-dela de l'extremite droite, dans l'axe : prolongement propose.
    const auto beyond = engine.candidates(*folio, project.library, QPointF(70, 0.4), 5.0);
    const SnapHit *extension = findMode(beyond, SnapMode::Extension);
    REQUIRE(extension);
    CHECK_THAT(extension->point.x(), WithinAbs(70.0, 0.5));
    CHECK(extension->hasOrigin);

    // A l'interieur du fil, ce n'est pas un prolongement.
    const auto inside = engine.candidates(*folio, project.library, QPointF(25, 0.4), 5.0);
    CHECK(findMode(inside, SnapMode::Extension) == nullptr);
}

TEST_CASE("Un mode desactive ne propose rien", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(0, 0), QPointF(100, 0) });

    SnapEngine engine;
    engine.setMode(SnapMode::Midpoint, false);
    const auto hits = engine.candidates(*folio, project.library, QPointF(50, 0.3), 5.0);
    CHECK(findMode(hits, SnapMode::Midpoint) == nullptr);
}

TEST_CASE("L'accrochage aux objets coupe tombe sur la grille", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(0, 0), QPointF(100, 0) });

    SnapEngine engine;
    engine.setObjectSnapEnabled(false);
    engine.setGridStep(2.5);
    const auto hit = engine.snap(*folio, project.library, QPointF(51.2, 0.4), 5.0);
    REQUIRE(hit);
    CHECK(hit->mode == SnapMode::Grid);
    // 51,2 tombe sur le pas 50 : 51,25 serait un demi-pas, donc hors grille.
    CHECK(hit->point == QPointF(50, 0));
}

TEST_CASE("L'entite exclue ne s'accroche pas a elle-meme", "[snap]")
{
    Project project;
    Folio *folio = project.addFolio();
    Wire *wire = drawWire(folio, { QPointF(0, 0), QPointF(100, 0) });

    SnapEngine engine;
    engine.setGridSnapEnabled(false);
    // Pendant le trace d'un fil, il ne doit pas s'accrocher a ses propres
    // points : sinon le curseur reste colle a l'origine du geste.
    const auto hits = engine.candidates(*folio, project.library, QPointF(50, 0.2), 5.0, nullptr,
                                        wire->id());
    CHECK(hits.isEmpty());
}

TEST_CASE("Le mode ortho contraint aux quarts de tour", "[snap][ortho]")
{
    SnapEngine engine;
    engine.setOrthoEnabled(true);

    const QPointF from(0, 0);
    // Deplacement majoritairement horizontal : la verticale est ecrasee.
    CHECK(engine.constrain(from, QPointF(100, 20)) == QPointF(100, 0));
    CHECK(engine.constrain(from, QPointF(20, 100)) == QPointF(0, 100));
    CHECK(engine.constrain(from, QPointF(-80, 10)) == QPointF(-80, 0));
}

TEST_CASE("Le suivi polaire n'accroche qu'aux abords de ses angles", "[snap][polar]")
{
    SnapEngine engine;
    engine.setOrthoEnabled(false);
    engine.setPolarEnabled(true);
    engine.setPolarIncrement(45.0);

    const QPointF from(0, 0);
    // A un demi-degre de la diagonale : la contrainte doit mordre.
    const auto angle = engine.constrainedAngle(from, QPointF(100, 99));
    REQUIRE(angle);
    CHECK_THAT(*angle, WithinAbs(45.0, 1e-9));

    // A vingt degres : l'utilisateur vise clairement autre chose.
    CHECK_FALSE(engine.constrainedAngle(from, QPointF(100, 36)).has_value());
}

TEST_CASE("Ortho prime sur le suivi polaire", "[snap][ortho]")
{
    SnapEngine engine;
    engine.setOrthoEnabled(true);
    engine.setPolarEnabled(true);
    engine.setPolarIncrement(45.0);

    // Sans ortho, ce geste tomberait sur la diagonale a 45 degres.
    CHECK(engine.constrain(QPointF(0, 0), QPointF(100, 100)) == QPointF(100, 0));
}

TEST_CASE("La contrainte garde la longueur projetee", "[snap][polar]")
{
    SnapEngine engine;
    engine.setOrthoEnabled(true);

    // Le curseur decide de la longueur, la contrainte seulement du cap.
    const QPointF result = engine.constrain(QPointF(10, 10), QPointF(60, 12));
    CHECK(result == QPointF(60, 10));
}

TEST_CASE("Les modes par defaut couvrent les points remarquables", "[snap]")
{
    const SnapModes modes = SnapEngine::defaultModes();
    CHECK(modes.testFlag(SnapMode::Endpoint));
    CHECK(modes.testFlag(SnapMode::Midpoint));
    CHECK(modes.testFlag(SnapMode::Node));
    CHECK(modes.testFlag(SnapMode::Intersection));
    // « Proche » accroche partout : allume par defaut, il masquerait les
    // modes precis. AutoCAD le laisse eteint pour la meme raison.
    CHECK_FALSE(modes.testFlag(SnapMode::Nearest));
    CHECK_FALSE(modes.testFlag(SnapMode::Extension));
}

TEST_CASE("Chaque mode a un libelle et une cle stable", "[snap]")
{
    const QList<SnapMode> all = SnapEngine::allModes();
    CHECK(all.size() == 11);
    for (SnapMode mode : all) {
        INFO("mode " << int(mode));
        CHECK_FALSE(snapModeName(mode).isEmpty());
        CHECK_FALSE(snapModeTag(mode).isEmpty());
        // La cle sert au fichier de reglages : elle doit faire l'aller-retour.
        CHECK(snapModeFromTag(snapModeTag(mode)) == mode);
    }
}
