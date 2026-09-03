#include <catch2/catch_test_macros.hpp>

#include "rules/catalog.h"
#include "rules/findreplace.h"
#include "rules/numbering.h"
#include "rules/reports.h"
#include "symbols/librarystore.h"
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

// --------------------------------------------------------------------------
// Cablage De/Vers et rapport de composants

TEST_CASE("Le cablage De-Vers donne une ligne par liaison a tirer", "[rules][fromto]")
{
    // Deux broches sur un potentiel : une liaison. Trois broches : deux
    // liaisons, chainees — c'est ce qu'un cableur tire reellement.
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    auto *a = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 60));
    a->setDesignation(QStringLiteral("-K1"));
    a->designationLocked = true;
    auto *b = placeSymbol(project, folio, QStringLiteral("iec:breaker"), QPointF(120, 60));
    b->setDesignation(QStringLiteral("-Q1"));
    b->designationLocked = true;

    Wire *wire = drawWire(folio, { QPointF(65, 60), QPointF(115, 60) });
    wire->number = QStringLiteral("101");
    wire->numberLocked = true;
    wire->wireType = QStringLiteral("l1");

    const Netlist netlist = Netlist::build(project);
    const QVector<WireRunLine> runs = Reports::wireFromTo(project, netlist);

    REQUIRE(runs.size() == 1);
    const WireRunLine &run = runs.first();
    CHECK(run.wireNumber == QLatin1String("101"));
    // Les deux extremites, dans un sens ou dans l'autre, mais bien les deux.
    const QStringList ends{ run.fromDesignation, run.toDesignation };
    CHECK(ends.contains(QLatin1String("-K1")));
    CHECK(ends.contains(QLatin1String("-Q1")));
    CHECK(run.fromFolio == QLatin1String("1"));
    CHECK(run.toFolio == QLatin1String("1"));
    CHECK_FALSE(run.crossesFolios);
    // Le type du fil descend dans le rapport : c'est ce qu'on va chercher au
    // magasin avant de tirer le fil.
    CHECK(run.crossSection == QStringLiteral("2,5 mm²"));
    CHECK(run.colorName == QLatin1String("#7a4a2b"));
}

TEST_CASE("Un potentiel a trois broches donne deux liaisons", "[rules][fromto]")
{
    // n broches sur un potentiel donnent n-1 liaisons : le cablage est une
    // chaine, pas une etoile, sinon le rapport compte des fils qui n'existent
    // pas.
    Project project = makeProject();
    Folio *folio = project.folioAt(0);

    // Trois appareils alignes verticalement, relies par un seul bus qui ne
    // touche que leur borne de gauche : le potentiel porte trois broches, ni
    // plus ni moins.
    const QString designations[3] = { QStringLiteral("-K1"), QStringLiteral("-K2"),
                                      QStringLiteral("-K3") };
    for (int i = 0; i < 3; ++i) {
        auto *s = placeSymbol(project, folio, QStringLiteral("iec:contactor"),
                              QPointF(100.0, 50.0 + i * 40.0));
        s->setDesignation(designations[i]);
        s->designationLocked = true;
    }
    drawWire(folio, { QPointF(95, 50), QPointF(95, 130) });

    const Netlist netlist = Netlist::build(project);
    const QVector<WireRunLine> runs = Reports::wireFromTo(project, netlist);

    CHECK(runs.size() == 2);
    // Chaque appareil apparait au moins une fois dans la chaine.
    QStringList seen;
    for (const WireRunLine &run : runs) {
        seen << run.fromDesignation << run.toDesignation;
    }
    for (const QString &d : designations)
        CHECK(seen.contains(d));
}

TEST_CASE("Une broche seule ne produit aucune liaison", "[rules][fromto]")
{
    // Un fil en l'air n'est pas une liaison a cabler : il doit ressortir dans
    // les controles, pas dans le rapport de cablage.
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    auto *a = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 60));
    a->setDesignation(QStringLiteral("-K1"));
    drawWire(folio, { QPointF(65, 60), QPointF(115, 60) });

    const Netlist netlist = Netlist::build(project);
    CHECK(Reports::wireFromTo(project, netlist).isEmpty());
}

TEST_CASE("Le rapport de composants situe l'appareil en folio et en zone",
          "[rules][components]")
{
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    folio->frame.columns = 10;
    folio->frame.rows = 6;

    auto *k = placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 60));
    k->setDesignation(QStringLiteral("-K1"));
    k->designationLocked = true;
    k->fields.insert(QStringLiteral("manufacturer"), QStringLiteral("Schneider"));
    k->fields.insert(QStringLiteral("partNumber"), QStringLiteral("LC1D09"));

    const Netlist netlist = Netlist::build(project);
    const QVector<ComponentLine> lines = Reports::componentList(project, netlist);

    REQUIRE(lines.size() == 1);
    CHECK(lines.first().designation == QLatin1String("-K1"));
    CHECK(lines.first().folio == QLatin1String("1"));
    CHECK(lines.first().manufacturer == QLatin1String("Schneider"));
    CHECK(lines.first().partNumber == QLatin1String("LC1D09"));
    // La zone est ce qui permet de retrouver l'appareil sur la feuille.
    CHECK(lines.first().zone == folio->zoneAt(QPointF(60, 60)));
    CHECK_FALSE(lines.first().zone.isEmpty());
}

TEST_CASE("Un appareil multi-blocs ne fait qu'une ligne de composant",
          "[rules][components]")
{
    // La bobine et ses contacts sont un seul appareil : le rapport de
    // composants compte des appareils, pas des symboles poses.
    Project project = makeProject(2);
    Folio *first = project.folioAt(0);
    Folio *second = project.folioAt(1);

    auto *coil = placeSymbol(project, first, QStringLiteral("iec:contactor"), QPointF(60, 60));
    coil->setDesignation(QStringLiteral("-KM1"));
    coil->designationLocked = true;
    coil->deviceGroup = QStringLiteral("km1");
    coil->blockIndex = 0;

    auto *contact = placeSymbol(project, second, QStringLiteral("iec:contactor"), QPointF(80, 90));
    contact->setDesignation(QStringLiteral("-KM1"));
    contact->designationLocked = true;
    contact->deviceGroup = QStringLiteral("km1");
    contact->blockIndex = 1;

    const Netlist netlist = Netlist::build(project);
    const QVector<ComponentLine> lines = Reports::componentList(project, netlist);

    REQUIRE(lines.size() == 1);
    CHECK(lines.first().blockCount == 2);
    // Le bloc principal donne le folio ; les deux folios sont listes.
    CHECK(lines.first().folio == QLatin1String("1"));
    CHECK(lines.first().folios.size() == 2);
}

TEST_CASE("La portee limite un rapport au folio actif", "[rules][scope]")
{
    // Sortir le dossier complet et verifier une page sont deux gestes
    // differents : la portee est ce qui les separe.
    Project project = makeProject(2);
    placeSymbol(project, project.folioAt(0), QStringLiteral("iec:contactor"), QPointF(60, 60))
            ->setDesignation(QStringLiteral("-K1"));
    placeSymbol(project, project.folioAt(1), QStringLiteral("iec:breaker"), QPointF(60, 60))
            ->setDesignation(QStringLiteral("-Q1"));

    const Netlist netlist = Netlist::build(project);

    CHECK(Reports::componentList(project, netlist).size() == 2);

    ReportScope scope;
    scope.folioId = project.folioAt(1)->id();
    const QVector<ComponentLine> onlySecond = Reports::componentList(project, netlist, scope);
    REQUIRE(onlySecond.size() == 1);
    CHECK(onlySecond.first().designation == QLatin1String("-Q1"));

    // Le recapitulatif suit la meme portee, sinon il contredirait le rapport.
    const ReportTable summary = Reports::projectSummary(project, netlist, scope);
    REQUIRE_FALSE(summary.rows.isEmpty());
    CHECK(summary.rows.first().at(1) == QLatin1String("1")); // un seul folio
}

// --------------------------------------------------------------------------
// Format de repere et modes de reperage

TEST_CASE("Le format de repere remplace ses parametres", "[rules][tagformat]")
{
    // Le format est une convention de bureau d'etudes : il doit pouvoir dire
    // autre chose que « famille + numero » sans toucher au code.
    DesignationRule rule;
    rule.leadingDash = true;

    DesignationContext context;
    context.family = QStringLiteral("K");
    context.number = 7;
    context.sheet = QStringLiteral("2");
    context.lineReference = QStringLiteral("204");
    context.installation = QStringLiteral("A1");
    context.location = QStringLiteral("ARM");

    CHECK(rule.format(context) == QLatin1String("-K7"));   // defaut %F%N

    rule.tagFormat = QStringLiteral("%S%F%N");
    CHECK(rule.format(context) == QLatin1String("-2K7"));

    rule.tagFormat = QStringLiteral("+%L-%F%N");
    CHECK(rule.format(context) == QLatin1String("-+ARM-K7"));

    rule.tagFormat = QStringLiteral("=%I+%L-%F%N");
    CHECK(rule.format(context) == QLatin1String("-=A1+ARM-K7"));

    rule.tagFormat = QStringLiteral("%X%F");
    CHECK(rule.format(context) == QLatin1String("-204K"));

    // Un pour cent litteral, et un jeton inconnu recopie tel quel : un format
    // mal saisi doit rester lisible, pas disparaitre du repere.
    rule.tagFormat = QStringLiteral("%F%%%Z");
    CHECK(rule.format(context) == QLatin1String("-K%%Z"));
}

TEST_CASE("Le remplissage a gauche s'applique au numero du format",
          "[rules][tagformat]")
{
    DesignationRule rule;
    rule.leadingDash = false;
    rule.padding = 3;
    DesignationContext context;
    context.family = QStringLiteral("K");
    context.number = 7;
    CHECK(rule.format(context) == QLatin1String("K007"));
}

TEST_CASE("Le reperage par reference de ligne place l'appareil",
          "[rules][numbering][lineref]")
{
    // C'est l'interet du mode : le repere dit ou trouver l'appareil sur le
    // schema, sans passer par la nomenclature.
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    folio->frame.columns = 10;

    Profile profile = Profile::iec();
    profile.designation.mode = DesignationRule::Mode::LineReference;

    const QRectF frame = folio->frameRect();
    // Un appareil au milieu de la 4e colonne du cadre.
    const double columnWidth = frame.width() / folio->frame.columns;
    const QPointF at(frame.left() + columnWidth * 3.5, frame.top() + 40.0);
    placeSymbol(project, folio, QStringLiteral("iec:contactor"), at);

    Numbering::designateDevices(project, profile);

    const auto symbols = folio->entitiesOfType<SymbolInstance>();
    REQUIRE(symbols.size() == 1);
    CHECK(folio->columnAt(at) == 4);
    CHECK(symbols.front()->designation() == QLatin1String("-104K"));
}

TEST_CASE("Deux appareils sur la meme reference se departagent par une lettre",
          "[rules][numbering][lineref]")
{
    // Un contact et sa bobine cote a cote tombent sur la meme reference de
    // ligne : sans suffixe ils porteraient le meme repere, ce qui est faux.
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    folio->frame.columns = 10;

    Profile profile = Profile::iec();
    profile.designation.mode = DesignationRule::Mode::LineReference;

    const QRectF frame = folio->frameRect();
    const double columnWidth = frame.width() / folio->frame.columns;
    const double x = frame.left() + columnWidth * 3.5;
    placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(x, frame.top() + 30.0));
    placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(x, frame.top() + 60.0));

    Numbering::designateDevices(project, profile);

    QStringList designations;
    for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>())
        designations << symbol->designation();
    designations.sort();
    CHECK(designations == QStringList{ QStringLiteral("-104K"), QStringLiteral("-104KA") });
}

TEST_CASE("Le reperage par reference de ligne reste reproductible",
          "[rules][numbering][lineref]")
{
    // Meme exigence que pour le mode sequentiel : relance sur un dessin
    // inchange, le reperage doit redonner exactement les memes reperes.
    Project project = makeProject();
    Folio *folio = project.folioAt(0);
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    folio->frame.columns = 10;

    Profile profile = Profile::iec();
    profile.designation.mode = DesignationRule::Mode::LineReference;

    const QRectF frame = folio->frameRect();
    for (int i = 0; i < 5; ++i) {
        placeSymbol(project, folio, QStringLiteral("iec:contactor"),
                    QPointF(frame.left() + 20.0 + i * 25.0, frame.top() + 30.0 + i * 12.0));
    }

    Numbering::designateDevices(project, profile);
    QStringList first;
    for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>())
        first << symbol->designation();

    Numbering::designateDevices(project, profile);
    QStringList second;
    for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>())
        second << symbol->designation();

    CHECK(first == second);
}

// --------------------------------------------------------------------------
// Catalogue fabricant

TEST_CASE("Le catalogue livre est embarque dans le binaire", "[rules][catalog]")
{
    // Le logiciel doit proposer des references des le premier lancement, sur
    // un poste ou rien n'est installe a cote.
    const Catalog catalog = Catalog::builtin();
    CHECK(catalog.count() > 20);
    CHECK(catalog.deviceKinds().contains(QStringLiteral("contactor")));
    CHECK(catalog.deviceKinds().contains(QStringLiteral("terminal")));
}

TEST_CASE("Le catalogue filtre par famille d'appareil", "[rules][catalog]")
{
    // La boite du composant part de la famille du symbole : proposer un
    // moteur pour une borne ferait perdre plus de temps qu'il n'en gagne.
    const Catalog catalog = Catalog::builtin();
    const auto contactors = catalog.forDeviceKind(QStringLiteral("contactor"));
    REQUIRE_FALSE(contactors.isEmpty());
    for (const CatalogItem &item : contactors)
        CHECK(item.deviceKind == QLatin1String("contactor"));

    // Une famille vide rend tout : un catalogue reel ne colle jamais
    // parfaitement a nos familles, il faut pouvoir en sortir.
    CHECK(catalog.forDeviceKind(QString()).size() == catalog.count());
}

TEST_CASE("La recherche du catalogue est insensible a la casse", "[rules][catalog]")
{
    const Catalog catalog = Catalog::builtin();
    const auto found = catalog.search(QStringLiteral("schneider"));
    REQUIRE_FALSE(found.isEmpty());
    for (const CatalogItem &item : found)
        CHECK(item.searchText().contains(QStringLiteral("Schneider")));

    // La recherche porte sur toutes les colonnes, pas seulement la reference.
    CHECK_FALSE(catalog.search(QStringLiteral("thermique")).isEmpty());
    CHECK(catalog.search(QStringLiteral("introuvable-xyz")).isEmpty());
}

TEST_CASE("Un article charge deux fois ne compte qu'une", "[rules][catalog]")
{
    // Le catalogue du poste complete celui du logiciel : recharger le meme
    // article ne doit pas le faire apparaitre en double dans la liste.
    Catalog catalog;
    CatalogItem item;
    item.deviceKind = QStringLiteral("contactor");
    item.manufacturer = QStringLiteral("Schneider");
    item.partNumber = QStringLiteral("LC1D09B7");
    catalog.insert(item);

    item.description = QStringLiteral("Description corrigée");
    catalog.insert(item);

    REQUIRE(catalog.count() == 1);
    CHECK(catalog.items().first().description == QStringLiteral("Description corrigée"));

    // Un article sans reference n'est pas un article.
    catalog.insert(CatalogItem());
    CHECK(catalog.count() == 1);
}

TEST_CASE("Le catalogue survit a l'aller-retour JSON", "[rules][catalog]")
{
    const Catalog source = Catalog::builtin();
    Catalog restored;
    QString error;
    REQUIRE(restored.readJson(source.toJson(), &error));
    CHECK(restored.count() == source.count());

    // Un fichier abime est refuse avec un message, pas devine.
    Catalog broken;
    CHECK_FALSE(broken.readJson(QByteArray("ceci n'est pas du JSON"), &error));
    CHECK_FALSE(error.isEmpty());
    CHECK(broken.isEmpty());
}

TEST_CASE("Un bornier est partagé, ses bornes sont numérotées", "[rules][bornier]")
{
    // UNE BORNE N'EST PAS UN APPAREIL : c'est une place dans un bornier.
    // Traitée comme un appareil, chaque borne recevait sa propre désignation —
    // trois bornes côte à côte donnaient -X1, -X2, -X3, soit trois borniers
    // d'une borne chacun. Sur un dossier de soixante bornes, cela faisait
    // soixante borniers, et l'éditeur de borniers devenait inutilisable.
    // C'est l'essai de dossier qui l'a montré.
    Project project;
    LibraryStore::loadBuiltin(project.library);
    Folio *folio = project.addFolio(QStringLiteral("Bornier"));
    folio->number = QStringLiteral("1");

    auto poser = [&](double y) {
        auto borne = std::make_unique<SymbolInstance>();
        borne->definitionId = QStringLiteral("iec:terminal");
        borne->placement.position = QPointF(80.0, y);
        return static_cast<SymbolInstance *>(folio->addEntity(std::move(borne)));
    };
    auto *b1 = poser(40.0);
    auto *b2 = poser(60.0);
    auto *b3 = poser(80.0);

    Profile profile = Profile::iec();
    const NumberingResult resultat = Numbering::designateDevices(project, profile);

    // Un seul bornier pour les trois.
    CHECK(b1->designation() == b2->designation());
    CHECK(b2->designation() == b3->designation());
    CHECK_FALSE(b1->designation().isEmpty());

    // Et trois numéros distincts, dans l'ordre de lecture.
    const auto numero = [](const SymbolInstance *s) {
        return s->fields.value(QStringLiteral("terminal"));
    };
    CHECK(numero(b1) == QStringLiteral("1"));
    CHECK(numero(b2) == QStringLiteral("2"));
    CHECK(numero(b3) == QStringLiteral("3"));
    CHECK(resultat.terminalsNumbered == 3);
}

TEST_CASE("Le bornier se remplit, il ne se renumérote pas", "[rules][bornier]")
{
    // Renuméroter d'office un bornier déjà câblé est une faute, pas un
    // service : le câbleur a le plan de l'an dernier dans les mains. Une borne
    // neuve prend donc le premier numéro LIBRE, et celles qui portent déjà un
    // numéro ne bougent pas. Renuméroter reste possible, mais c'est le bouton
    // explicite de l'éditeur de borniers.
    Project project;
    LibraryStore::loadBuiltin(project.library);
    Folio *folio = project.addFolio(QStringLiteral("Bornier"));
    folio->number = QStringLiteral("1");

    auto poser = [&](double y, const QString &bloc, const QString &num) {
        auto borne = std::make_unique<SymbolInstance>();
        borne->definitionId = QStringLiteral("iec:terminal");
        borne->placement.position = QPointF(80.0, y);
        if (!bloc.isEmpty())
            borne->setDesignation(bloc);
        if (!num.isEmpty())
            borne->fields.insert(QStringLiteral("terminal"), num);
        return static_cast<SymbolInstance *>(folio->addEntity(std::move(borne)));
    };
    // Un bornier déjà câblé : X1 avec les bornes 1 et 3. La borne 2 a sauté.
    auto *pose1 = poser(40.0, QStringLiteral("-X1"), QStringLiteral("1"));
    auto *pose3 = poser(80.0, QStringLiteral("-X1"), QStringLiteral("3"));
    // Une borne neuve intercalée entre les deux.
    auto *neuve = poser(60.0, QString(), QString());

    Profile profile = Profile::iec();
    Numbering::designateDevices(project, profile);

    const auto numero = [](const SymbolInstance *s) {
        return s->fields.value(QStringLiteral("terminal"));
    };
    // Les câblées n'ont pas bougé.
    CHECK(numero(pose1) == QStringLiteral("1"));
    CHECK(numero(pose3) == QStringLiteral("3"));
    // La neuve a rejoint leur bornier et pris le premier numéro libre.
    CHECK(neuve->designation() == QStringLiteral("-X1"));
    CHECK(numero(neuve) == QStringLiteral("2"));

    // Relancé sur un dessin inchangé, le repérage redonne exactement la même
    // chose : c'est l'invariant de reproductibilité.
    Numbering::designateDevices(project, profile);
    CHECK(numero(pose1) == QStringLiteral("1"));
    CHECK(numero(neuve) == QStringLiteral("2"));
    CHECK(numero(pose3) == QStringLiteral("3"));
}

// --------------------------------------------------------------------------
// Rechercher / remplacer dans tout le dossier

TEST_CASE("La recherche traverse le dossier et dit où", "[rules][findreplace]")
{
    // Le cas réel : l'affaire change de numéro, et le texte est écrit sur
    // plusieurs folios. Une liste qui dit « 3 occurrences » sans dire où coûte
    // plus de temps qu'elle n'en fait gagner — d'où le folio et la zone.
    Project project = makeProject(2);
    auto *k1 = placeSymbol(project, project.folioAt(0), QStringLiteral("iec:contactor"),
                           QPointF(60, 60), QStringLiteral("-KM1"));
    k1->fields.insert(QStringLiteral("value"), QStringLiteral("KM1 24 V"));
    placeSymbol(project, project.folioAt(1), QStringLiteral("iec:breaker"), QPointF(60, 60),
                QStringLiteral("-KM10"));

    FindQuery query;
    query.needle = QStringLiteral("KM1");
    const auto hits = FindReplace::find(project, query);
    // -KM1, son champ valeur, et -KM10 du second folio.
    REQUIRE(hits.size() == 3);
    CHECK_FALSE(hits.at(0).zone.isEmpty());
    CHECK(hits.at(0).folioLabel.startsWith(QStringLiteral("1")));
    CHECK(hits.at(2).folioLabel.startsWith(QStringLiteral("2")));

    // La portée est celle des rapports : un seul point de filtrage.
    query.scope.folioId = project.folioAt(0)->id();
    CHECK(FindReplace::find(project, query).size() == 2);
}

TEST_CASE("« Mot entier » ne prend pas -KM10 pour -KM1", "[rules][findreplace]")
{
    // Sans cette option, renommer KM1 en KM7 transforme KM10 en KM70 : le
    // dossier devient faux d'un seul clic, et l'erreur ne se voit nulle part.
    FindQuery query;
    query.needle = QStringLiteral("KM1");
    query.replacement = QStringLiteral("KM7");
    CHECK(FindReplace::replaced(QStringLiteral("-KM10"), query) == QStringLiteral("-KM70"));

    query.wholeWord = true;
    CHECK(FindReplace::replaced(QStringLiteral("-KM10"), query) == QStringLiteral("-KM10"));
    CHECK(FindReplace::replaced(QStringLiteral("-KM1"), query) == QStringLiteral("-KM7"));
}

TEST_CASE("La recherche ne prend pas le point pour un joker", "[rules][findreplace]")
{
    // « Mot entier » passe par une expression régulière : le motif doit être
    // échappé, sinon un repère qui contient un point se met à tout attraper.
    FindQuery query;
    query.needle = QStringLiteral("A.1");
    query.wholeWord = true;
    CHECK(FindReplace::matches(QStringLiteral("A.1"), query));
    CHECK_FALSE(FindReplace::matches(QStringLiteral("AB1"), query));
}

TEST_CASE("Chercher une chaîne vide ne renvoie pas tout le dossier",
          "[rules][findreplace]")
{
    // Ce ne serait pas une recherche mais un inventaire, et personne ne l'a
    // demandé — surtout suivi d'un « Remplacer tout ».
    Project project = makeProject(1);
    placeSymbol(project, project.folioAt(0), QStringLiteral("iec:contactor"), QPointF(60, 60),
                QStringLiteral("-KM1"));
    FindQuery query;
    CHECK(FindReplace::find(project, query).isEmpty());
}

TEST_CASE("On ne cherche que dans les gisements demandés", "[rules][findreplace]")
{
    // Renommer tous les « M1 » d'un dossier ne doit pas toucher la phrase
    // « alimentation M1 » d'une annotation si on ne l'a pas demandé.
    Project project = makeProject(1);
    auto *k1 = placeSymbol(project, project.folioAt(0), QStringLiteral("iec:contactor"),
                           QPointF(60, 60), QStringLiteral("-KM1"));
    k1->fields.insert(QStringLiteral("value"), QStringLiteral("bobine KM1"));

    FindQuery query;
    query.needle = QStringLiteral("KM1");
    query.inFields = false;
    const auto hits = FindReplace::find(project, query);
    REQUIRE(hits.size() == 1);
    CHECK(hits.at(0).where == QStringLiteral("Repere"));
}
