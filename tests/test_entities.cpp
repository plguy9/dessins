#include <catch2/catch_test_macros.hpp>

#include <catch2/catch_approx.hpp>
#include "core/entities.h"
#include "core/symboldef.h"

using namespace dsn;

namespace {

// Un aller-retour JSON doit rendre exactement le meme document : c'est la
// garantie minimale d'un format de fichier qu'on ose appeler natif.
template <typename T>
std::unique_ptr<T> roundTrip(const T &source)
{
    const QJsonObject json = source.toJson();
    EntityPtr restored = createEntity(json.value(QStringLiteral("type")).toString());
    REQUIRE(restored);
    REQUIRE(restored->readJson(json));
    auto *typed = dynamic_cast<T *>(restored.get());
    REQUIRE(typed);
    restored.release();
    return std::unique_ptr<T>(typed);
}

} // namespace

TEST_CASE("Une instance de symbole survit a l'aller-retour JSON", "[entities][io]")
{
    SymbolInstance source;
    source.definitionId = QStringLiteral("iec:contactor-coil");
    source.placement.position = QPointF(123.45, 67.89);
    source.placement.orientation = Orientation::R270;
    source.placement.mirrored = true;
    source.setDesignation(QStringLiteral("-K1"));
    source.fields.insert(QStringLiteral("value"), QStringLiteral("24 VDC"));
    source.deviceGroup = QStringLiteral("groupe-1");
    source.blockIndex = 2;
    source.designationLocked = true;

    const auto restored = roundTrip(source);
    CHECK(restored->id() == source.id());
    CHECK(restored->definitionId == source.definitionId);
    CHECK(restored->placement.position == source.placement.position);
    CHECK(restored->placement.orientation == Orientation::R270);
    CHECK(restored->placement.mirrored);
    CHECK(restored->designation() == QLatin1String("-K1"));
    CHECK(restored->fields.value(QStringLiteral("value")) == QLatin1String("24 VDC"));
    CHECK(restored->deviceGroup == QLatin1String("groupe-1"));
    CHECK(restored->blockIndex == 2);
    CHECK(restored->designationLocked);
}

TEST_CASE("Un fil multi-conducteurs survit a l'aller-retour JSON", "[entities][io]")
{
    Wire source;
    source.points = { QPointF(0, 0), QPointF(10, 0), QPointF(10, 20) };
    source.conductors = { QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3"),
                          QStringLiteral("N"), QStringLiteral("PE") };
    source.number = QStringLiteral("W12");
    source.numberLocked = true;

    const auto restored = roundTrip(source);
    CHECK(restored->points == source.points);
    CHECK(restored->conductors == source.conductors);
    CHECK(restored->conductorCount() == 5);
    CHECK(restored->number == QLatin1String("W12"));
    CHECK(restored->numberLocked);
}

TEST_CASE("Un fil sans conducteur nomme en compte un seul", "[entities]")
{
    Wire wire;
    wire.points = { QPointF(0, 0), QPointF(10, 0) };
    CHECK(wire.conductorCount() == 1);
    CHECK(wire.conductorName(0).isEmpty());
}

TEST_CASE("La longueur d'un fil suit ses coudes", "[entities]")
{
    Wire wire;
    wire.points = { QPointF(0, 0), QPointF(30, 0), QPointF(30, 40) };
    CHECK(wire.length() == 70.0);
    CHECK_FALSE(wire.isDegenerate());

    Wire flat;
    flat.points = { QPointF(5, 5), QPointF(5, 5) };
    CHECK(flat.isDegenerate());
}

TEST_CASE("Les primitives survivent a l'aller-retour JSON", "[entities][io]")
{
    GraphicItem source;
    source.shape = Primitive::arc(QPointF(10, 10), 5.0, 30.0, 120.0, 0.5);

    const auto restored = roundTrip(source);
    CHECK(restored->shape.kind == Primitive::Kind::Arc);
    CHECK(restored->shape.radius == 5.0);
    CHECK(restored->shape.startAngle == 30.0);
    CHECK(restored->shape.spanAngle == 120.0);
    CHECK(restored->shape.lineWidth == 0.5);
}

TEST_CASE("Une etiquette conserve sa portee", "[entities][io]")
{
    Label source;
    source.point = QPointF(20, 30);
    source.name = QStringLiteral("24V");
    source.scope = Label::Scope::Project;
    source.direction = Direction::Up;

    const auto restored = roundTrip(source);
    CHECK(restored->name == QLatin1String("24V"));
    CHECK(restored->scope == Label::Scope::Project);
    CHECK(restored->direction == Direction::Up);
}

TEST_CASE("Une balise d'entite inconnue ne fabrique rien", "[entities][io]")
{
    // Un document ecrit par une version ulterieure perd l'entite inconnue mais
    // reste ouvrable : c'est le compromis voulu.
    CHECK(createEntity(QStringLiteral("hologramme")) == nullptr);
}

TEST_CASE("La racine d'une broche se deduit de son sens et de sa longueur", "[symboldef]")
{
    Pin pin;
    pin.position = QPointF(10, 0);
    pin.direction = Direction::Right;
    pin.length = 2.5;
    CHECK(pin.root() == QPointF(7.5, 0));

    pin.direction = Direction::Up;
    pin.position = QPointF(0, -10);
    CHECK(pin.root() == QPointF(0, -7.5));
}

TEST_CASE("La boite d'un symbole englobe le corps et les broches", "[symboldef]")
{
    SymbolDefinition def;
    def.graphics.append(Primitive::rect(QRectF(-2.5, -2.5, 5, 5), 0.0));
    Pin pin;
    pin.number = QStringLiteral("1");
    pin.position = QPointF(-8, 0);
    pin.direction = Direction::Left;
    pin.length = 5.5;
    def.pins.append(pin);

    const QRectF bounds = def.bounds();
    CHECK(bounds.left() <= -8.0);
    CHECK(bounds.right() >= 2.5);
    CHECK(def.pin(QStringLiteral("1")) != nullptr);
    CHECK(def.pin(QStringLiteral("99")) == nullptr);
}

TEST_CASE("Une definition survit a l'aller-retour JSON", "[symboldef][io]")
{
    SymbolDefinition source;
    source.logicalId = QStringLiteral("contactor-coil");
    source.norm = QStringLiteral("IEC");
    source.id = SymbolDefinition::makeId(source.norm, source.logicalId);
    source.name = QStringLiteral("Bobine de contacteur");
    source.category = QStringLiteral("Commande");
    source.designationPrefix = QStringLiteral("K");
    source.keywords = { QStringLiteral("bobine"), QStringLiteral("relais") };
    source.graphics.append(Primitive::rect(QRectF(-5, -2.5, 10, 5)));
    Pin a1;
    a1.number = QStringLiteral("A1");
    a1.position = QPointF(0, -5);
    a1.direction = Direction::Up;
    source.pins.append(a1);

    const SymbolDefinition restored = SymbolDefinition::fromJson(source.toJson());
    CHECK(restored.id == QLatin1String("iec:contactor-coil"));
    CHECK(restored.name == source.name);
    CHECK(restored.designationPrefix == QLatin1String("K"));
    CHECK(restored.keywords == source.keywords);
    CHECK(restored.graphics.size() == 1);
    REQUIRE(restored.pins.size() == 1);
    CHECK(restored.pins.first().number == QLatin1String("A1"));
    CHECK(restored.pins.first().direction == Direction::Up);
}

TEST_CASE("Grossir une forme change ses dimensions, pas son trait", "[entities][echelle]")
{
    // Demande utilisateur : « je ne veux pas grossir (épaissir) les fils, je
    // veux juste des dimensions plus grosses ». Une épaisseur de trait n'est
    // pas une dimension : c'est la plume, et elle ne change pas parce qu'on
    // dessine plus grand. Sur un schéma elle porte en plus un sens — puissance
    // ou commande — qu'un agrandissement n'a pas à modifier.
    Primitive circle = Primitive::circle(QPointF(10, 10), 4.0);
    circle.lineWidth = 0.35;
    circle.textHeight = 2.5;

    circle.scale(QPointF(0, 0), 3.0);

    CHECK(circle.radius == Catch::Approx(12.0));
    CHECK(circle.textHeight == Catch::Approx(7.5));
    CHECK(circle.lineWidth == Catch::Approx(0.35));
}

