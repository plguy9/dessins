#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/geometry.h"

using namespace dsn;
using Catch::Matchers::WithinAbs;

TEST_CASE("Les quarts de tour tombent juste", "[geometry]")
{
    // Une derive de 1e-17 sur cos(pi/2) finit par decaler un accrochage de
    // broche : les rotations droites doivent etre exactes.
    const Transform2D r90 = Transform2D::rotation(90.0);
    CHECK(r90.map(QPointF(1, 0)) == QPointF(0, 1));
    CHECK(r90.map(QPointF(0, 1)) == QPointF(-1, 0));

    const Transform2D r180 = Transform2D::rotation(180.0);
    CHECK(r180.map(QPointF(3, 4)) == QPointF(-3, -4));

    const Transform2D r270 = Transform2D::rotation(270.0);
    CHECK(r270.map(QPointF(1, 0)) == QPointF(0, -1));
}

TEST_CASE("La composition applique les transformations dans l'ordre", "[geometry]")
{
    const Transform2D t = Transform2D::rotation(90.0).then(Transform2D::translation(10, 0));
    CHECK(t.map(QPointF(1, 0)) == QPointF(10, 1));
}

TEST_CASE("L'inversion annule la transformation", "[geometry]")
{
    const Transform2D t = Transform2D::scaling(-1, 1)
                                  .then(Transform2D::rotation(90))
                                  .then(Transform2D::translation(7, -3));
    bool ok = false;
    const Transform2D inv = t.inverted(&ok);
    REQUIRE(ok);
    const QPointF p(4.5, -2.25);
    const QPointF round = inv.map(t.map(p));
    CHECK_THAT(round.x(), WithinAbs(p.x(), 1e-9));
    CHECK_THAT(round.y(), WithinAbs(p.y(), 1e-9));
}

TEST_CASE("Le placement enchaine miroir, rotation puis translation", "[geometry]")
{
    Placement p;
    p.position = QPointF(100, 50);
    p.orientation = Orientation::R90;
    p.mirrored = true;

    // (1,0) --miroir--> (-1,0) --rot 90--> (0,-1) --translation--> (100,49)
    CHECK(p.map(QPointF(1, 0)) == QPointF(100, 49));
}

TEST_CASE("La rotation d'une broche suit le symbole", "[geometry]")
{
    CHECK(rotatedBy(Direction::Right, Orientation::R90, false) == Direction::Down);
    CHECK(rotatedBy(Direction::Right, Orientation::R0, true) == Direction::Left);
    CHECK(rotatedBy(Direction::Up, Orientation::R180, false) == Direction::Down);
}

TEST_CASE("L'orientation se normalise sur les angles hors bornes", "[geometry]")
{
    CHECK(orientationFromDegrees(450) == Orientation::R90);
    CHECK(orientationFromDegrees(-90) == Orientation::R270);
    CHECK(rotateCw(Orientation::R270) == Orientation::R0);
    CHECK(rotateCcw(Orientation::R0) == Orientation::R270);
}

TEST_CASE("L'accrochage a la grille arrondit au pas le plus proche", "[geometry]")
{
    CHECK_THAT(snapToGrid(2.4, 1.0), WithinAbs(2.0, 1e-9));
    CHECK_THAT(snapToGrid(2.6, 1.0), WithinAbs(3.0, 1e-9));
    CHECK_THAT(snapToGrid(-2.6, 1.0), WithinAbs(-3.0, 1e-9));
    // Un pas nul ne doit pas diviser par zero.
    CHECK_THAT(snapToGrid(2.6, 0.0), WithinAbs(2.6, 1e-9));
}

TEST_CASE("Le trace orthogonal garde le plus grand deplacement", "[geometry]")
{
    CHECK(orthogonalize(QPointF(0, 0), QPointF(10, 3)) == QPointF(10, 0));
    CHECK(orthogonalize(QPointF(0, 0), QPointF(3, 10)) == QPointF(0, 10));
}

TEST_CASE("Un point sur un segment est detecte a la tolerance de connexion", "[geometry]")
{
    const QPointF a(0, 0), b(10, 0);
    CHECK(pointOnSegment(QPointF(5, 0), a, b));
    CHECK(pointOnSegment(QPointF(5, 0.005), a, b));
    CHECK_FALSE(pointOnSegment(QPointF(5, 0.5), a, b));
    CHECK_FALSE(pointOnSegment(QPointF(15, 0), a, b));
}

TEST_CASE("L'intersection de segments ignore les paralleles", "[geometry]")
{
    const auto hit = segmentIntersection(QPointF(0, 0), QPointF(10, 0), QPointF(5, -5),
                                         QPointF(5, 5));
    REQUIRE(hit.has_value());
    CHECK(*hit == QPointF(5, 0));

    CHECK_FALSE(segmentIntersection(QPointF(0, 0), QPointF(10, 0), QPointF(0, 1),
                                    QPointF(10, 1)).has_value());
}

TEST_CASE("Les formats de feuille couvrent l'ISO et l'ANSI", "[geometry]")
{
    const SheetFormat a3 = sheetFormatById(QStringLiteral("A3"));
    CHECK_THAT(a3.width, WithinAbs(420.0, 1e-9));
    CHECK_THAT(a3.height, WithinAbs(297.0, 1e-9));

    const SheetFormat ansiD = sheetFormatById(QStringLiteral("ANSI_D"));
    CHECK_THAT(ansiD.width, WithinAbs(mmFromInch(34.0), 1e-9));

    // Un identifiant inconnu doit degrader vers un format utilisable, pas
    // faire echouer un chargement de fichier.
    CHECK(sheetFormatById(QStringLiteral("inexistant")).id == QLatin1String("A3"));

    CHECK_THAT(sheetFormatById(QStringLiteral("A4")).portrait().width, WithinAbs(210.0, 1e-9));
}
