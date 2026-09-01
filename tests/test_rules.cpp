#include <catch2/catch_test_macros.hpp>

#include "rules/numbering.h"
#include "rules/reports.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

namespace {

// Petit projet de commande : deux appareils relies par un fil, sur un folio
// numerote, avec un cadre de dix colonnes.
Project makeProject(int folioCount = 1)
{
    Project project;
    project.library.insert(twoPinDevice(QStringLiteral("contactor"), QStringLiteral("K")));
    project.library.insert(twoPinDevice(QStringLiteral("breaker"), QStringLiteral("Q")));

    SymbolDefinition terminal = twoPinDevice(QStringLiteral("terminal"), QStringLiteral("X"));
    terminal.deviceKind = QStringLiteral("terminal");
    project.library.insert(terminal);

    for (int i = 0; i < folioCount; ++i) {
        Folio *folio = project.addFolio(QStringLiteral("Folio %1").arg(i + 1));
        folio->number = QString::number(i + 1);
    }
    return project;
}

QString wireNumber(const Project &project, const Wire *wire)
{
    const Folio *folio = project.folioAt(0);
    const auto *found = dynamic_cast<const Wire *>(folio->entity(wire->id()));
    return found ? found->number : QString();
}

} // namespace

TEST_CASE("Les designations suivent l'ordre de lecture", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    // Places dans le desordre : le repere doit suivre le folio, puis le haut
    // vers le bas, puis la gauche vers la droite.
    auto *bas = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 150));
    auto *hautDroite = placeSymbol(project, folio, QStringLiteral("iec:contactor"),
                                   QPointF(200, 50));
    auto *hautGauche = placeSymbol(project, folio, QStringLiteral("iec:contactor"),
                                   QPointF(60, 50));

    const NumberingResult result = Numbering::designateDevices(project, Profile::iec());
    CHECK(result.devicesDesignated == 3);
    CHECK(hautGauche->designation() == QLatin1String("-K1"));
    CHECK(hautDroite->designation() == QLatin1String("-K2"));
    CHECK(bas->designation() == QLatin1String("-K3"));
}

TEST_CASE("Chaque prefixe a son propre compteur", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    auto *k = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 50));
    auto *q = placeSymbol(project, folio, QStringLiteral("iec:breaker"), QPointF(120, 50));
    auto *k2 = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(180, 50));

    Numbering::designateDevices(project, Profile::iec());
    CHECK(k->designation() == QLatin1String("-K1"));
    CHECK(q->designation() == QLatin1String("-Q1"));
    CHECK(k2->designation() == QLatin1String("-K2"));
}

TEST_CASE("Une designation verrouillee n'est jamais ecrasee", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    auto *manuel = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 50));
    manuel->setDesignation(QStringLiteral("-KM7"));
    manuel->designationLocked = true;
    auto *automatique = placeSymbol(project, folio, QStringLiteral("iec:contactor"),
                                    QPointF(120, 50));

    const NumberingResult result = Numbering::designateDevices(project, Profile::iec());
    // Un automatisme qui detruit une saisie manuelle est desactive des la
    // premiere mauvaise surprise.
    CHECK(manuel->designation() == QLatin1String("-KM7"));
    CHECK(result.keptManual == 1);
    CHECK(automatique->designation() == QLatin1String("-K1"));
}

TEST_CASE("Un appareil multi-blocs partage une designation", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    // Bobine et contact auxiliaire du meme contacteur.
    auto *bobine = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 50));
    bobine->deviceGroup = QStringLiteral("KM1");
    auto *contact = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(200, 150));
    contact->deviceGroup = QStringLiteral("KM1");
    contact->blockIndex = 1;
    auto *autre = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(120, 50));

    const NumberingResult result = Numbering::designateDevices(project, Profile::iec());
    CHECK(bobine->designation() == contact->designation());
    CHECK(bobine->designation() == QLatin1String("-K1"));
    CHECK(autre->designation() == QLatin1String("-K2"));
    CHECK(result.devicesDesignated == 2); // deux appareils, trois symboles
}

TEST_CASE("Le profil ANSI supprime le tiret et remplit les zeros", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    auto *device = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 50));

    Numbering::designateDevices(project, Profile::ansi());
    CHECK(device->designation() == QLatin1String("K1"));
}

TEST_CASE("Le repere de fil combine folio et colonne", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    folio->number = QStringLiteral("3");
    folio->frame.columns = 10;

    // Colonne 1 du cadre : le repere attendu est 301.
    const QRectF frame = folio->frameRect();
    Wire *wire = drawWire(folio, { frame.topLeft() + QPointF(2, 20),
                                   frame.topLeft() + QPointF(2, 60) });

    const Netlist netlist = Netlist::build(project);
    const NumberingResult result = Numbering::numberWires(project, netlist, Profile::iec());
    CHECK(result.wiresNumbered == 1);
    CHECK(wireNumber(project, wire) == QLatin1String("301"));
}

TEST_CASE("Le meme potentiel porte le meme repere sur tous ses fils", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    // Deux fils raccordes par une jonction : un seul potentiel, donc un seul
    // repere. Le meme fil electrique ne peut pas en porter deux.
    Wire *a = drawWire(folio, { QPointF(60, 60), QPointF(120, 60) });
    Wire *b = drawWire(folio, { QPointF(120, 60), QPointF(120, 120) });

    const Netlist netlist = Netlist::build(project);
    Numbering::numberWires(project, netlist, Profile::iec());

    const QString numberA = dynamic_cast<const Wire *>(folio->entity(a->id()))->number;
    const QString numberB = dynamic_cast<const Wire *>(folio->entity(b->id()))->number;
    CHECK_FALSE(numberA.isEmpty());
    CHECK(numberA == numberB);
}

TEST_CASE("Un repere verrouille gouverne tout son potentiel", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    Wire *a = drawWire(folio, { QPointF(60, 60), QPointF(120, 60) });
    Wire *b = drawWire(folio, { QPointF(120, 60), QPointF(120, 120) });
    a->number = QStringLiteral("L1");
    a->numberLocked = true;

    const Netlist netlist = Netlist::build(project);
    const NumberingResult result = Numbering::numberWires(project, netlist, Profile::iec());

    CHECK(dynamic_cast<const Wire *>(folio->entity(a->id()))->number == QLatin1String("L1"));
    CHECK(dynamic_cast<const Wire *>(folio->entity(b->id()))->number == QLatin1String("L1"));
    CHECK(result.keptManual == 1);
}

TEST_CASE("Deux potentiels dans la meme colonne sont departages", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    folio->number = QStringLiteral("2");

    const QRectF frame = folio->frameRect();
    Wire *a = drawWire(folio, { frame.topLeft() + QPointF(2, 20),
                                frame.topLeft() + QPointF(2, 40) });
    Wire *b = drawWire(folio, { frame.topLeft() + QPointF(6, 80),
                                frame.topLeft() + QPointF(6, 100) });

    const Netlist netlist = Netlist::build(project);
    Numbering::numberWires(project, netlist, Profile::iec());

    const QString na = dynamic_cast<const Wire *>(folio->entity(a->id()))->number;
    const QString nb = dynamic_cast<const Wire *>(folio->entity(b->id()))->number;
    CHECK(na == QLatin1String("201"));
    // Un suffixe alphabetique plutot qu'un increment, qui empieterait sur la
    // colonne suivante.
    CHECK(nb == QLatin1String("201A"));
}

TEST_CASE("La strategie par nom de potentiel utilise l'etiquette", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    Wire *wire = drawWire(folio, { QPointF(60, 60), QPointF(120, 60) });
    dropLabel(folio, QPointF(60, 60), QStringLiteral("24V"));

    Profile profile = Profile::iec();
    profile.wireNumbering.strategy = WireNumberingRule::Strategy::PotentialName;

    const Netlist netlist = Netlist::build(project);
    Numbering::numberWires(project, netlist, profile);
    CHECK(dynamic_cast<const Wire *>(folio->entity(wire->id()))->number == QLatin1String("24V"));
}

TEST_CASE("Le reperage est reproductible sur un dessin inchange", "[rules][numbering]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    for (int i = 0; i < 6; ++i) {
        placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60 + i * 30, 50));
        drawWire(folio, { QPointF(60 + i * 30, 80), QPointF(60 + i * 30, 120) });
    }

    Numbering::renumberAll(project, Profile::iec());
    QStringList first;
    for (const SymbolInstance *s : folio->entitiesOfType<SymbolInstance>())
        first.append(s->designation());
    for (const Wire *wire : folio->entitiesOfType<Wire>())
        first.append(wire->number);

    Numbering::renumberAll(project, Profile::iec());
    QStringList second;
    for (const SymbolInstance *s : folio->entitiesOfType<SymbolInstance>())
        second.append(s->designation());
    for (const Wire *wire : folio->entitiesOfType<Wire>())
        second.append(wire->number);

    // Sans stabilite, chaque regeneration reimprime un dossier different.
    CHECK(first == second);
}

TEST_CASE("La nomenclature regroupe par reference d'article", "[rules][reports]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    for (int i = 0; i < 3; ++i) {
        auto *s = placeSymbol(project, folio, QStringLiteral("iec:contactor"),
                              QPointF(60 + i * 30, 50));
        s->fields.insert(QStringLiteral("partNumber"), QStringLiteral("LC1D09"));
        s->fields.insert(QStringLiteral("manufacturer"), QStringLiteral("Schneider"));
    }
    auto *autre = placeSymbol(project, folio, QStringLiteral("iec:breaker"), QPointF(200, 50));
    autre->fields.insert(QStringLiteral("partNumber"), QStringLiteral("A9F74210"));

    Numbering::designateDevices(project, Profile::iec());
    const QVector<BomLine> bom = Reports::billOfMaterials(project);

    REQUIRE(bom.size() == 2);
    const BomLine *contacteurs = nullptr;
    for (const BomLine &line : bom) {
        if (line.partNumber == QLatin1String("LC1D09"))
            contacteurs = &line;
    }
    REQUIRE(contacteurs);
    CHECK(contacteurs->quantity == 3);
    CHECK(contacteurs->manufacturer == QLatin1String("Schneider"));
    CHECK(contacteurs->designations.size() == 3);
}

TEST_CASE("Un appareil multi-blocs ne compte qu'une fois dans la nomenclature",
          "[rules][reports]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    auto *bobine = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 50));
    bobine->deviceGroup = QStringLiteral("KM1");
    bobine->fields.insert(QStringLiteral("partNumber"), QStringLiteral("LC1D09"));
    auto *contact = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(120, 50));
    contact->deviceGroup = QStringLiteral("KM1");
    contact->fields.insert(QStringLiteral("partNumber"), QStringLiteral("LC1D09"));

    Numbering::designateDevices(project, Profile::iec());
    const QVector<BomLine> bom = Reports::billOfMaterials(project);
    REQUIRE(bom.size() == 1);
    // On commande un contacteur, pas deux.
    CHECK(bom.first().quantity == 1);
}

TEST_CASE("Un appareil multi-blocs sans reference ne compte qu'une fois",
          "[rules][reports]")
{
    Project project = makeProject(2);

    // Le contacteur de puissance est sur le folio 1, sa bobine sur le folio 2 :
    // c'est un seul article a commander, et c'est le bloc principal qui doit
    // le nommer.
    SymbolDefinition puissance = twoPinDevice(QStringLiteral("contactor-power"),
                                              QStringLiteral("K"));
    puissance.name = QStringLiteral("Contacteur de puissance");
    project.library.insert(puissance);
    SymbolDefinition bobine = twoPinDevice(QStringLiteral("coil"), QStringLiteral("K"));
    bobine.name = QStringLiteral("Bobine");
    project.library.insert(bobine);

    auto *bloc0 = placeSymbol(project, project.folioAt(0),
                              QStringLiteral("iec:contactor-power"), QPointF(60, 50));
    bloc0->deviceGroup = QStringLiteral("KM1");
    auto *bloc1 = placeSymbol(project, project.folioAt(1), QStringLiteral("iec:coil"),
                              QPointF(60, 50));
    bloc1->deviceGroup = QStringLiteral("KM1");
    bloc1->blockIndex = 1;

    Numbering::designateDevices(project, Profile::iec());
    const QVector<BomLine> bom = Reports::billOfMaterials(project);

    REQUIRE(bom.size() == 1);
    CHECK(bom.first().quantity == 1);
    CHECK(bom.first().name == QStringLiteral("Contacteur de puissance"));
    // L'appareil doit etre cite sur les deux folios ou il apparait.
    CHECK(bom.first().folios == QStringList{ QStringLiteral("1"), QStringLiteral("2") });
}

TEST_CASE("Le tri de la nomenclature est naturel", "[rules][reports]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    for (int i = 0; i < 12; ++i)
        placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60 + i * 25, 50));

    Numbering::designateDevices(project, Profile::iec());
    const QVector<BomLine> bom = Reports::billOfMaterials(project);
    REQUIRE_FALSE(bom.isEmpty());
    const QStringList designations = bom.first().designations;
    REQUIRE(designations.size() == 12);
    // -K2 avant -K10 : un tri alphabetique donnerait l'inverse, et cela se
    // voit immediatement sur une nomenclature imprimee.
    CHECK(designations.at(1) == QLatin1String("-K2"));
    CHECK(designations.last() == QLatin1String("-K12"));
}

TEST_CASE("Le bornier relie borne, fil et appareil", "[rules][reports]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    auto *borne = placeSymbol(project, folio, QStringLiteral("iec:terminal"), QPointF(100, 60));
    borne->setDesignation(QStringLiteral("-X1"));
    borne->designationLocked = true;
    borne->fields.insert(QStringLiteral("terminal"), QStringLiteral("1"));

    auto *appareil = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(160, 60));
    appareil->setDesignation(QStringLiteral("-K1"));
    appareil->designationLocked = true;

    Wire *wire = drawWire(folio, { QPointF(105, 60), QPointF(155, 60) });
    wire->number = QStringLiteral("101");
    wire->numberLocked = true;

    const Netlist netlist = Netlist::build(project);
    const QVector<TerminalLine> terminals = Reports::terminalList(project, netlist);

    REQUIRE(terminals.size() == 1);
    CHECK(terminals.first().block == QLatin1String("-X1"));
    CHECK(terminals.first().terminal == QLatin1String("1"));
    CHECK(terminals.first().wireNumber == QLatin1String("101"));
    CHECK(terminals.first().target == QLatin1String("-K1"));
}

TEST_CASE("La liste des fils compte la longueur une fois par liaison",
          "[rules][reports]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    // Liaison triphasee : trois potentiels sur un seul trait de 50 mm. La
    // longueur de cable ne doit pas etre comptee trois fois.
    drawWire(folio, { QPointF(60, 60), QPointF(110, 60) },
             { QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3") });

    const Netlist netlist = Netlist::build(project);
    const QVector<WireLine> wires = Reports::wireList(project, netlist);

    REQUIRE(wires.size() == 3);
    double total = 0.0;
    for (const WireLine &line : wires)
        total += line.length;
    CHECK(total == 50.0);
    CHECK(wires.first().conductorCount == 3);
}

TEST_CASE("Le recapitulatif compte ce qui est sur les folios", "[rules][reports]")
{
    Project project = makeProject(2);
    Folio *folio = project.folioAt(0);
    placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 50));
    drawWire(folio, { QPointF(60, 80), QPointF(60, 180) }); // 100 mm

    const Netlist netlist = Netlist::build(project);
    const ReportTable table = Reports::projectSummary(project, netlist);

    CHECK(table.rows.at(0) == QStringList{ QStringLiteral("Folios"), QStringLiteral("2") });
    CHECK(table.rows.at(1) == QStringList{ QStringLiteral("Appareils"), QStringLiteral("1") });
    // rules/ produit une valeur independante de la locale ; la francisation du
    // separateur decimal est la responsabilite de la couche qui affiche ou
    // exporte (voir CsvOptions::decimalSeparator).
    CHECK(table.rows.at(3).at(1) == QStringLiteral("0.10 m"));
}
