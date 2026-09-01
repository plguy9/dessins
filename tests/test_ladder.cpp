#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/entities.h"
#include "core/folio.h"
#include "rules/ladder.h"

using namespace dsn;
using Catch::Matchers::WithinAbs;

namespace {

int countOf(const std::vector<EntityPtr> &entities, EntityType type)
{
    int n = 0;
    for (const EntityPtr &entity : entities) {
        if (entity->type() == type)
            ++n;
    }
    return n;
}

} // namespace

TEST_CASE("Une echelle pose deux rails et leurs etiquettes", "[ladder]")
{
    LadderSpec spec;
    spec.origin = QPointF(40, 40);
    spec.width = 150.0;
    spec.rungs = 10;
    spec.rungSpacing = 18.0;
    spec.drawRungs = false;

    const auto entities = LadderBuilder::build(spec);
    CHECK(countOf(entities, EntityType::Wire) == 2);
    CHECK(countOf(entities, EntityType::Label) == 2);
    // Un numero par ligne.
    CHECK(countOf(entities, EntityType::Text) == 10);
}

TEST_CASE("Les rails portent un repere verrouille", "[ladder]")
{
    LadderSpec spec;
    spec.leftRailName = QStringLiteral("L1");
    spec.rightRailName = QStringLiteral("N");

    const auto entities = LadderBuilder::build(spec);
    QStringList numbers;
    for (const EntityPtr &entity : entities) {
        if (const auto *wire = dynamic_cast<const Wire *>(entity.get())) {
            numbers.append(wire->number);
            // Le nom du rail gouverne tout le rail : le reperage automatique
            // ne doit pas le remplacer par un numero de colonne.
            CHECK(wire->numberLocked);
        }
    }
    numbers.sort();
    CHECK(numbers == QStringList{ QStringLiteral("L1"), QStringLiteral("N") });
}

TEST_CASE("Les etiquettes de rail traversent le projet", "[ladder]")
{
    const auto entities = LadderBuilder::build(LadderSpec());
    for (const EntityPtr &entity : entities) {
        if (const auto *label = dynamic_cast<const Label *>(entity.get())) {
            // Un rail d'alimentation traverse tout le dossier : sa portee
            // doit etre le projet, pas le folio.
            CHECK(label->scope == Label::Scope::Project);
        }
    }
}

TEST_CASE("Les rails vont d'un bout a l'autre de l'echelle", "[ladder]")
{
    LadderSpec spec;
    spec.origin = QPointF(50, 30);
    spec.width = 120.0;
    spec.rungs = 5;
    spec.rungSpacing = 20.0; // hauteur = 4 x 20 = 80

    const auto entities = LadderBuilder::build(spec);
    QVector<const Wire *> rails;
    for (const EntityPtr &entity : entities) {
        if (const auto *wire = dynamic_cast<const Wire *>(entity.get()))
            rails.append(wire);
    }
    REQUIRE(rails.size() == 2);
    CHECK(rails.at(0)->points.first() == QPointF(50, 30));
    CHECK(rails.at(0)->points.last() == QPointF(50, 110));
    CHECK(rails.at(1)->points.first() == QPointF(170, 30));
    CHECK(rails.at(1)->points.last() == QPointF(170, 110));
}

TEST_CASE("Les barreaux sont optionnels", "[ladder]")
{
    LadderSpec spec;
    spec.rungs = 6;
    spec.drawRungs = true;

    const auto entities = LadderBuilder::build(spec);
    // Deux rails plus six barreaux.
    CHECK(countOf(entities, EntityType::Wire) == 8);
}

TEST_CASE("L'increment de numerotation laisse de la place", "[ladder]")
{
    LadderSpec spec;
    spec.rungs = 4;
    spec.firstRungNumber = 100;
    spec.rungNumberStep = 10;

    QStringList numbers;
    for (const EntityPtr &entity : LadderBuilder::build(spec)) {
        if (const auto *text = dynamic_cast<const TextItem *>(entity.get()))
            numbers.append(text->text);
    }
    // Un pas de dix laisse de la place pour intercaler des lignes plus tard.
    CHECK(numbers == QStringList{ QStringLiteral("100"), QStringLiteral("110"),
                                  QStringLiteral("120"), QStringLiteral("130") });
}

TEST_CASE("Un debordement du cadre est annonce avant l'insertion", "[ladder]")
{
    Folio folio;
    const QRectF frame = folio.frameRect();

    LadderSpec ok;
    ok.origin = frame.topLeft() + QPointF(20, 20);
    ok.width = 120.0;
    ok.rungs = 5;
    ok.rungSpacing = 15.0;
    CHECK(LadderBuilder::fitWarning(ok, frame).isEmpty());

    // Cent lignes ne tiennent sur aucun format : mieux vaut le dire avant de
    // poser deux cents entites hors cadre.
    LadderSpec tooTall = ok;
    tooTall.rungs = 100;
    const QString warning = LadderBuilder::fitWarning(tooTall, frame);
    CHECK_FALSE(warning.isEmpty());
    CHECK(warning.contains(QStringLiteral("100")));
}

TEST_CASE("Une echelle vide ne produit rien", "[ladder]")
{
    LadderSpec spec;
    spec.rungs = 0;
    CHECK(LadderBuilder::build(spec).empty());

    spec.rungs = 5;
    spec.width = 0.0;
    CHECK(LadderBuilder::build(spec).empty());
}
