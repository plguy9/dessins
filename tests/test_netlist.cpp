#include <catch2/catch_test_macros.hpp>

#include "core/netlist.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

namespace {

// Renvoie le potentiel commun a deux broches, ou nullptr si elles ne sont pas
// sur le meme potentiel.
const Netlist::Net *sharedNet(const Netlist &netlist, const SymbolInstance *a, const QString &pinA,
                              const SymbolInstance *b, const QString &pinB)
{
    const Netlist::Net *na = netlist.netOfPin(a->id(), pinA);
    const Netlist::Net *nb = netlist.netOfPin(b->id(), pinB);
    if (!na || !nb || na->id != nb->id)
        return nullptr;
    return na;
}

} // namespace

TEST_CASE("Un fil relie deux broches sur le meme potentiel", "[netlist]")
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();

    auto *k1 = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(50, 50));
    auto *k2 = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(80, 50));
    drawWire(folio, { QPointF(55, 50), QPointF(75, 50) });

    const Netlist netlist = Netlist::build(project);
    CHECK(sharedNet(netlist, k1, QStringLiteral("2"), k2, QStringLiteral("1")) != nullptr);
    CHECK(sharedNet(netlist, k1, QStringLiteral("1"), k2, QStringLiteral("2")) == nullptr);
}

TEST_CASE("Un fil coude reste un seul potentiel", "[netlist]")
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();

    auto *k1 = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(50, 50));
    auto *k2 = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(50, 90));
    // Depart a droite de k1, deux coudes, arrivee a gauche de k2.
    drawWire(folio, { QPointF(55, 50), QPointF(70, 50), QPointF(70, 90), QPointF(45, 90) });

    const Netlist netlist = Netlist::build(project);
    CHECK(sharedNet(netlist, k1, QStringLiteral("2"), k2, QStringLiteral("1")) != nullptr);
}

TEST_CASE("Deux fils qui se croisent ne connectent pas sans jonction", "[netlist]")
{
    Project project;
    Folio *folio = project.addFolio();

    // Croisement franc au point (50,50), sans sommet ni jonction.
    Wire *horizontal = drawWire(folio, { QPointF(30, 50), QPointF(70, 50) });
    Wire *vertical = drawWire(folio, { QPointF(50, 30), QPointF(50, 70) });

    const Netlist netlist = Netlist::build(project);
    const Netlist::Net *nh = netlist.netOfWire(horizontal->id());
    const Netlist::Net *nv = netlist.netOfWire(vertical->id());
    REQUIRE(nh);
    REQUIRE(nv);
    CHECK(nh->id != nv->id);
}

TEST_CASE("Une jonction explicite connecte tout ce qui passe par elle", "[netlist]")
{
    Project project;
    Folio *folio = project.addFolio();

    Wire *horizontal = drawWire(folio, { QPointF(30, 50), QPointF(70, 50) });
    Wire *vertical = drawWire(folio, { QPointF(50, 30), QPointF(50, 70) });
    dropJunction(folio, QPointF(50, 50));

    const Netlist netlist = Netlist::build(project);
    const Netlist::Net *nh = netlist.netOfWire(horizontal->id());
    const Netlist::Net *nv = netlist.netOfWire(vertical->id());
    REQUIRE(nh);
    REQUIRE(nv);
    CHECK(nh->id == nv->id);
}

TEST_CASE("Un piquage en T connecte sans jonction dessinee", "[netlist]")
{
    Project project;
    Folio *folio = project.addFolio();

    // L'extremite du fil derive tombe au milieu du segment du fil principal.
    Wire *bus = drawWire(folio, { QPointF(30, 50), QPointF(70, 50) });
    Wire *tap = drawWire(folio, { QPointF(50, 50), QPointF(50, 80) });

    const Netlist netlist = Netlist::build(project);
    const Netlist::Net *nb = netlist.netOfWire(bus->id());
    const Netlist::Net *nt = netlist.netOfWire(tap->id());
    REQUIRE(nb);
    REQUIRE(nt);
    CHECK(nb->id == nt->id);
}

TEST_CASE("Une broche posee au milieu d'un fil est connectee", "[netlist]")
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();

    Wire *bus = drawWire(folio, { QPointF(30, 50), QPointF(90, 50) });
    // Broche 1 du symbole a (60,50), soit au milieu du fil.
    auto *k1 = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(65, 50));

    const Netlist netlist = Netlist::build(project);
    const Netlist::Net *np = netlist.netOfPin(k1->id(), QStringLiteral("1"));
    const Netlist::Net *nb = netlist.netOfWire(bus->id());
    REQUIRE(np);
    REQUIRE(nb);
    CHECK(np->id == nb->id);
}

TEST_CASE("Les etiquettes de meme nom fusionnent dans un folio", "[netlist][label]")
{
    Project project;
    Folio *folio = project.addFolio();

    Wire *a = drawWire(folio, { QPointF(10, 10), QPointF(40, 10) });
    Wire *b = drawWire(folio, { QPointF(10, 60), QPointF(40, 60) });
    dropLabel(folio, QPointF(10, 10), QStringLiteral("L1"));
    dropLabel(folio, QPointF(10, 60), QStringLiteral("L1"));

    const Netlist netlist = Netlist::build(project);
    const Netlist::Net *na = netlist.netOfWire(a->id());
    const Netlist::Net *nb = netlist.netOfWire(b->id());
    REQUIRE(na);
    REQUIRE(nb);
    CHECK(na->id == nb->id);
    CHECK(na->name == QLatin1String("L1"));
}

TEST_CASE("Une etiquette de folio ne traverse pas les folios", "[netlist][label]")
{
    Project project;
    Folio *f1 = project.addFolio();
    Folio *f2 = project.addFolio();

    Wire *a = drawWire(f1, { QPointF(10, 10), QPointF(40, 10) });
    Wire *b = drawWire(f2, { QPointF(10, 10), QPointF(40, 10) });
    dropLabel(f1, QPointF(10, 10), QStringLiteral("24V"), Label::Scope::Folio);
    dropLabel(f2, QPointF(10, 10), QStringLiteral("24V"), Label::Scope::Folio);

    const Netlist netlist = Netlist::build(project);
    REQUIRE(netlist.netOfWire(a->id()));
    REQUIRE(netlist.netOfWire(b->id()));
    CHECK(netlist.netOfWire(a->id())->id != netlist.netOfWire(b->id())->id);
}

TEST_CASE("Un renvoi de folio assure la continuite inter-folios", "[netlist][label]")
{
    Project project;
    Folio *f1 = project.addFolio();
    Folio *f2 = project.addFolio();

    Wire *a = drawWire(f1, { QPointF(10, 10), QPointF(40, 10) });
    Wire *b = drawWire(f2, { QPointF(10, 10), QPointF(40, 10) });
    dropLabel(f1, QPointF(10, 10), QStringLiteral("24V"), Label::Scope::Project);
    dropLabel(f2, QPointF(10, 10), QStringLiteral("24V"), Label::Scope::Project);

    const Netlist netlist = Netlist::build(project);
    const Netlist::Net *na = netlist.netOfWire(a->id());
    REQUIRE(na);
    REQUIRE(netlist.netOfWire(b->id()));
    CHECK(na->id == netlist.netOfWire(b->id())->id);
    CHECK(na->crossesFolios());
    CHECK(na->folioIds.size() == 2);
}

TEST_CASE("Les conducteurs d'une liaison multiple restent distincts", "[netlist][multi]")
{
    Project project;
    Folio *folio = project.addFolio();

    const QStringList triphase{ QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3") };
    Wire *depart = drawWire(folio, { QPointF(10, 10), QPointF(50, 10) }, triphase);

    const Netlist netlist = Netlist::build(project);
    const Netlist::Net *l1 = netlist.netOfWire(depart->id(), 0);
    const Netlist::Net *l2 = netlist.netOfWire(depart->id(), 1);
    const Netlist::Net *l3 = netlist.netOfWire(depart->id(), 2);
    REQUIRE(l1);
    REQUIRE(l2);
    REQUIRE(l3);
    // Un trait unique, trois potentiels : c'est tout l'enjeu de l'unifilaire.
    CHECK(l1->id != l2->id);
    CHECK(l2->id != l3->id);
    CHECK(netlist.netCount() >= 3);
}

TEST_CASE("Deux liaisons multiples apparient leurs conducteurs par nom", "[netlist][multi]")
{
    Project project;
    Folio *folio = project.addFolio();

    // Le second cable declare ses conducteurs dans l'ordre inverse : c'est le
    // nom qui doit primer sur le rang de saisie.
    Wire *amont = drawWire(folio, { QPointF(10, 10), QPointF(50, 10) },
                           { QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3") });
    Wire *aval = drawWire(folio, { QPointF(50, 10), QPointF(90, 10) },
                          { QStringLiteral("L3"), QStringLiteral("L2"), QStringLiteral("L1") });

    const Netlist netlist = Netlist::build(project);
    // L1 amont (rang 0) doit rejoindre L1 aval (rang 2).
    const Netlist::Net *amontL1 = netlist.netOfWire(amont->id(), 0);
    const Netlist::Net *avalL1 = netlist.netOfWire(aval->id(), 2);
    const Netlist::Net *avalL3 = netlist.netOfWire(aval->id(), 0);
    REQUIRE(amontL1);
    REQUIRE(avalL1);
    REQUIRE(avalL3);
    CHECK(amontL1->id == avalL1->id);
    CHECK(amontL1->id != avalL3->id);
}

TEST_CASE("Deux liaisons multiples anonymes apparient par rang", "[netlist][multi]")
{
    Project project;
    Folio *folio = project.addFolio();

    Wire *amont = drawWire(folio, { QPointF(10, 10), QPointF(50, 10) });
    Wire *aval = drawWire(folio, { QPointF(50, 10), QPointF(90, 10) });

    const Netlist netlist = Netlist::build(project);
    REQUIRE(netlist.netOfWire(amont->id(), 0));
    REQUIRE(netlist.netOfWire(aval->id(), 0));
    CHECK(netlist.netOfWire(amont->id(), 0)->id == netlist.netOfWire(aval->id(), 0)->id);
}

TEST_CASE("Une definition de symbole manquante remonte un diagnostic", "[netlist][diagnostic]")
{
    Project project;
    Folio *folio = project.addFolio();
    placeSymbol(project, folio, QStringLiteral("iec:inexistant"), QPointF(50, 50));

    const Netlist netlist = Netlist::build(project);
    bool found = false;
    for (const auto &d : netlist.diagnostics()) {
        if (d.code == QLatin1String("symbol.missingDefinition"))
            found = true;
    }
    CHECK(found);
}

TEST_CASE("Un fil en l'air est signale", "[netlist][diagnostic]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(10, 10), QPointF(40, 10) });

    const Netlist netlist = Netlist::build(project);
    CHECK_FALSE(netlist.danglingNets().isEmpty());
}

TEST_CASE("La rotation d'un symbole deplace ses broches", "[netlist]")
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();

    // Symbole tourne d'un quart de tour : la broche 2, a droite en local,
    // se retrouve en dessous.
    auto instance = std::make_unique<SymbolInstance>();
    instance->definitionId = QStringLiteral("iec:device");
    instance->placement.position = QPointF(50, 50);
    instance->placement.orientation = Orientation::R90;
    auto *k1 = instance.get();
    folio->addEntity(std::move(instance));

    Wire *wire = drawWire(folio, { QPointF(50, 55), QPointF(50, 80) });

    const Netlist netlist = Netlist::build(project);
    const Netlist::Net *pin2 = netlist.netOfPin(k1->id(), QStringLiteral("2"));
    REQUIRE(pin2);
    REQUIRE(netlist.netOfWire(wire->id()));
    CHECK(pin2->id == netlist.netOfWire(wire->id())->id);
}
