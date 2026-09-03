#include <catch2/catch_test_macros.hpp>

#include "core/entities.h"
#include "core/netlist.h"
#include "rules/audit.h"
#include "rules/circuitcopy.h"
#include "rules/numbering.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

namespace {

Project onePage()
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio(QStringLiteral("Puissance"));
    folio->number = QStringLiteral("1");
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    folio->frame.columns = 10;
    folio->frame.rows = 6;
    return project;
}

// Le geste que la commande sert : copier ce qui est selectionne, le poser
// ailleurs. Les copies portent deja leur identifiant et leur position, comme
// dans FolioView::pasteClipboard.
std::vector<EntityPtr> copyOf(const Folio &folio, const QStringList &ids, const QPointF &offset)
{
    std::vector<EntityPtr> copies;
    for (const QString &id : ids) {
        const Entity *source = folio.entity(id);
        REQUIRE(source != nullptr);
        EntityPtr copy = source->clone();
        copy->setId(newId());
        copy->translate(offset);
        copies.push_back(std::move(copy));
    }
    return copies;
}

void place(Project &project, Folio *folio, std::vector<EntityPtr> &copies)
{
    for (EntityPtr &copy : copies)
        folio->addEntity(std::move(copy));
    Q_UNUSED(project);
}

CircuitCopyResult retag(std::vector<EntityPtr> &copies, const Project &project,
                        const Folio *destination)
{
    return CircuitCopy::retag(copies, project, Profile::byId(QStringLiteral("iec")),
                              PlcDatabase(), destination);
}

} // namespace

TEST_CASE("Un depart moteur colle ne reprend pas les reperes de l'original", "[circuitcopy]")
{
    // C'est la raison d'etre de la commande : huit departs identiques se
    // dessinent une fois et se collent sept fois, et la nomenclature doit
    // rester juste sans que personne n'ait a la relire.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto *km = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                           QStringLiteral("K1"));
    auto *ka = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 90),
                           QStringLiteral("K2"));

    std::vector<EntityPtr> copies =
            copyOf(*folio, { km->id(), ka->id() }, QPointF(40.0, 0.0));
    const CircuitCopyResult result = retag(copies, project, folio);

    REQUIRE(result.devicesRetagged == 2);
    QStringList fresh;
    for (const EntityPtr &copy : copies) {
        const auto *symbol = dynamic_cast<const SymbolInstance *>(copy.get());
        REQUIRE(symbol != nullptr);
        fresh << symbol->designation();
    }
    REQUIRE(!fresh.contains(QStringLiteral("K1")));
    REQUIRE(!fresh.contains(QStringLiteral("K2")));
    REQUIRE(fresh.at(0) != fresh.at(1));
}

TEST_CASE("Le collage re-repere ne fait remonter aucun doublon a l'audit", "[circuitcopy]")
{
    // Le test qui compte : c'est l'audit, et non le detail des reperes, qui
    // dit si le collage a fait son travail. Sans re-reperage il signale
    // « repere porte par deux appareils differents » pour chaque copie.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto *km = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                           QStringLiteral("K1"));
    Wire *wire = drawWire(folio, { QPointF(40, 60), QPointF(55, 60) });
    wire->number = QStringLiteral("101");

    std::vector<EntityPtr> copies = copyOf(*folio, { km->id(), wire->id() }, QPointF(0.0, 40.0));
    retag(copies, project, folio);
    place(project, folio, copies);

    const QVector<AuditFinding> findings = Audit::run(project, Netlist::build(project),
                                                      PlcDatabase());
    for (const AuditFinding &finding : findings)
        REQUIRE(finding.code != QStringLiteral("tag.duplicate"));
}

TEST_CASE("Sans re-reperage, le meme collage fait remonter le doublon", "[circuitcopy]")
{
    // Le contre-essai du precedent. Sans lui, le test « aucun doublon » ne
    // prouverait rien : il passerait aussi si l'audit avait cesse de voir les
    // doublons. C'est aussi ce que fait « Coller a l'identique », et c'est
    // pourquoi cette commande n'est pas le raccourci par defaut.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto *km = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                           QStringLiteral("K1"));

    std::vector<EntityPtr> copies = copyOf(*folio, { km->id() }, QPointF(0.0, 40.0));
    place(project, folio, copies); // pose sans passer par CircuitCopy::retag

    const QVector<AuditFinding> findings = Audit::run(project, Netlist::build(project),
                                                      PlcDatabase());
    bool duplicate = false;
    for (const AuditFinding &finding : findings)
        duplicate = duplicate || finding.code == QStringLiteral("tag.duplicate");
    REQUIRE(duplicate);
}

TEST_CASE("Une bobine et ses contacts copies ensemble restent un seul appareil", "[circuitcopy]")
{
    // Un appareil multi-blocs partage un groupe. La copie doit garder le
    // partage et changer le groupe : sans nouveau groupe, les contacts copies
    // rejoindraient la bobine d'origine et reprendraient sa designation.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto *coil = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                             QStringLiteral("K1"));
    auto *contact = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 90),
                                QStringLiteral("K1"));
    coil->deviceGroup = QStringLiteral("g1");
    contact->deviceGroup = QStringLiteral("g1");
    contact->blockIndex = 1;

    std::vector<EntityPtr> copies =
            copyOf(*folio, { coil->id(), contact->id() }, QPointF(40.0, 0.0));
    retag(copies, project, folio);

    const auto *a = dynamic_cast<const SymbolInstance *>(copies.at(0).get());
    const auto *b = dynamic_cast<const SymbolInstance *>(copies.at(1).get());
    REQUIRE(a->deviceGroup == b->deviceGroup);
    REQUIRE(a->deviceGroup != QStringLiteral("g1"));
    REQUIRE(a->designation() == b->designation());
    REQUIRE(a->designation() != QStringLiteral("K1"));
}

TEST_CASE("Une etiquette de potentiel copiee garde son nom", "[circuitcopy]")
{
    // Huit departs moteur se branchent tous sur L1 : renommer l'etiquette de
    // la copie debrancherait le circuit qu'on vient de copier. C'est la seule
    // chose que le re-reperage doit laisser tranquille.
    Project project = onePage();
    Folio *folio = project.folios().front();

    Label *l1 = dropLabel(folio, QPointF(40, 40), QStringLiteral("L1"));
    std::vector<EntityPtr> copies = copyOf(*folio, { l1->id() }, QPointF(0.0, 40.0));
    const CircuitCopyResult result = retag(copies, project, folio);

    REQUIRE(result.signalsRenamed == 0);
    REQUIRE(dynamic_cast<const Label *>(copies.front().get())->name == QStringLiteral("L1"));
}

TEST_CASE("Une paire de renvois copiee ensemble recoit un seul nouveau code", "[circuitcopy]")
{
    // Deux sources du meme code sont une faute, pas un raccourci : les
    // fleches de signal sont donc renommees. Mais les deux bouts d'un meme
    // renvoi doivent recevoir le MEME nouveau code, sinon la copie perd son
    // renvoi — c'est le piege de cette regle.
    Project project = onePage();
    Folio *folio = project.folios().front();

    Label *source = dropLabel(folio, QPointF(40, 40), QStringLiteral("DEMARRAGE"),
                              Label::Scope::Project);
    source->role = Label::Role::Source;
    Label *destination = dropLabel(folio, QPointF(140, 40), QStringLiteral("DEMARRAGE"),
                                   Label::Scope::Project);
    destination->role = Label::Role::Destination;

    std::vector<EntityPtr> copies =
            copyOf(*folio, { source->id(), destination->id() }, QPointF(0.0, 40.0));
    const CircuitCopyResult result = retag(copies, project, folio);

    REQUIRE(result.signalsRenamed == 2);
    const auto *a = dynamic_cast<const Label *>(copies.at(0).get());
    const auto *b = dynamic_cast<const Label *>(copies.at(1).get());
    REQUIRE(a->name == b->name);
    REQUIRE(a->name != QStringLiteral("DEMARRAGE"));
    REQUIRE(a->role == Label::Role::Source);
    REQUIRE(b->role == Label::Role::Destination);
}

TEST_CASE("Un troisieme collage ne empile pas les suffixes de renvoi", "[circuitcopy]")
{
    // DEMARRAGE_2 puis DEMARRAGE_3, jamais DEMARRAGE_2_2 : un code de renvoi
    // se lit sur le folio, et un nom qui s'allonge a chaque collage devient
    // illisible avant la quatrieme copie.
    QSet<QString> taken{ QStringLiteral("DEMARRAGE"), QStringLiteral("DEMARRAGE_2") };
    const QString third = CircuitCopy::nextSignalName(QStringLiteral("DEMARRAGE_2"), taken);
    REQUIRE(third == QStringLiteral("DEMARRAGE_3"));
}

TEST_CASE("Une borne copiee garde son bornier et change de numero", "[circuitcopy]")
{
    // Copier cinq bornes de X1 veut dire « cinq bornes de plus dans X1 »,
    // jamais « un bornier par borne ». C'est l'exception a la regle generale
    // — la copie est un nouvel appareil — et elle vient de l'usage.
    Project project = onePage();
    SymbolDefinition terminal = twoPinDevice(QStringLiteral("terminal"), QStringLiteral("X"));
    project.library.insert(terminal);
    Folio *folio = project.folios().front();

    QStringList ids;
    for (int i = 0; i < 3; ++i) {
        auto *symbol = placeSymbol(project, folio, terminal.id, QPointF(60, 40 + 10 * i),
                                   QStringLiteral("X1"));
        symbol->fields.insert(QStringLiteral("terminal"), QString::number(i + 1));
        ids << symbol->id();
    }

    std::vector<EntityPtr> copies = copyOf(*folio, ids, QPointF(40.0, 0.0));
    const CircuitCopyResult result = retag(copies, project, folio);

    REQUIRE(result.terminalsRenumbered == 3);
    QStringList numbers;
    for (const EntityPtr &copy : copies) {
        const auto *symbol = dynamic_cast<const SymbolInstance *>(copy.get());
        REQUIRE(symbol->designation() == QStringLiteral("X1"));
        numbers << symbol->fields.value(QStringLiteral("terminal"));
    }
    REQUIRE(numbers == QStringList{ QStringLiteral("4"), QStringLiteral("5"),
                                    QStringLiteral("6") });
}

TEST_CASE("Le repere d'un fil colle est libere, pas recopie", "[circuitcopy]")
{
    // Le repere depend du potentiel, donc de la netlist, donc du dessin une
    // fois les copies posees. Le recopier serait faux des le premier fil qui
    // touche la copie ; le vider dit la verite — ce fil reste a numeroter.
    Project project = onePage();
    Folio *folio = project.folios().front();

    Wire *wire = drawWire(folio, { QPointF(40, 60), QPointF(120, 60) });
    wire->number = QStringLiteral("101");
    wire->numberLocked = true;

    std::vector<EntityPtr> copies = copyOf(*folio, { wire->id() }, QPointF(0.0, 20.0));
    const CircuitCopyResult result = retag(copies, project, folio);

    REQUIRE(result.wiresReleased == 1);
    const auto *copy = dynamic_cast<const Wire *>(copies.front().get());
    REQUIRE(copy->number.isEmpty());
    REQUIRE_FALSE(copy->numberLocked);
}

TEST_CASE("Coller a l'identique laisse tout en place", "[circuitcopy]")
{
    // La commande jumelle sert au geste inverse : deplacer un circuit d'un
    // folio a l'autre, ou l'appareil doit garder son identite. Elle
    // n'appelle simplement pas le re-reperage — ce test tient la promesse que
    // le clone est fidele, verrou compris.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto *km = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                           QStringLiteral("K1"));
    km->designationLocked = true;

    std::vector<EntityPtr> copies = copyOf(*folio, { km->id() }, QPointF(40.0, 0.0));
    const auto *copy = dynamic_cast<const SymbolInstance *>(copies.front().get());
    REQUIRE(copy->designation() == QStringLiteral("K1"));
    REQUIRE(copy->designationLocked);
    REQUIRE(copy->id() != km->id());
}

TEST_CASE("Un repere verrouille du projet n'est jamais repris par une copie", "[circuitcopy]")
{
    // La designation de lot doit eviter TOUS les reperes portes, pas seulement
    // ceux qui sont verrouilles : elle n'a le droit de bousculer personne.
    Project project = onePage();
    Folio *folio = project.folios().front();

    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                QStringLiteral("K1"));
    auto *k2 = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 60),
                           QStringLiteral("K2"));
    auto *k3 = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(140, 60),
                           QStringLiteral("K3"));

    std::vector<EntityPtr> copies = copyOf(*folio, { k2->id(), k3->id() }, QPointF(0.0, 40.0));
    retag(copies, project, folio);

    const QSet<QString> existing = Numbering::allDesignations(project);
    for (const EntityPtr &copy : copies) {
        const auto *symbol = dynamic_cast<const SymbolInstance *>(copy.get());
        REQUIRE_FALSE(existing.contains(symbol->designation()));
    }
}

TEST_CASE("Deux collages de suite ne se marchent pas dessus", "[circuitcopy]")
{
    // Le second collage doit voir ce que le premier a pose : c'est ce que
    // garantit le passage par le projet plutot que par un compteur retenu
    // entre deux appels.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto *km = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                           QStringLiteral("K1"));

    std::vector<EntityPtr> first = copyOf(*folio, { km->id() }, QPointF(0.0, 30.0));
    retag(first, project, folio);
    const QString firstTag =
            dynamic_cast<const SymbolInstance *>(first.front().get())->designation();
    place(project, folio, first);

    std::vector<EntityPtr> second = copyOf(*folio, { km->id() }, QPointF(0.0, 60.0));
    retag(second, project, folio);
    const QString secondTag =
            dynamic_cast<const SymbolInstance *>(second.front().get())->designation();

    REQUIRE(firstTag != secondTag);
    REQUIRE(firstTag != QStringLiteral("K1"));
    REQUIRE(secondTag != QStringLiteral("K1"));
}

TEST_CASE("Designer un lot ne touche a rien d'autre dans le projet", "[circuitcopy]")
{
    // La difference avec la regeneration globale : celle-ci a le droit de
    // reattribuer ce qui n'est pas verrouille, la designation de lot n'a le
    // droit de bousculer personne — sinon coller renumeroterait le dossier.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto *a = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                          QStringLiteral("K7"));
    auto *b = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 60),
                          QStringLiteral("K9"));

    std::vector<EntityPtr> copies = copyOf(*folio, { a->id() }, QPointF(0.0, 40.0));
    retag(copies, project, folio);

    REQUIRE(a->designation() == QStringLiteral("K7"));
    REQUIRE(b->designation() == QStringLiteral("K9"));
}

TEST_CASE("Une carte d'automate collee ne reprend pas les adresses de l'original",
          "[circuitcopy][plc]")
{
    // Deux cartes a la meme adresse est l'erreur d'automate par excellence :
    // elle ne se voit pas sur le folio, elle se voit a la mise en service.
    // Le collage doit la rendre impossible, pas la signaler apres coup.
    const PlcDatabase database = PlcDatabase::builtin();
    const PlcModuleDef *def = database.find(QStringLiteral("ab:1746-IA16"));
    REQUIRE(def != nullptr);

    Project project = onePage();
    Folio *folio = project.folios().front();

    auto symbol = std::make_unique<SymbolInstance>();
    PlcModule::configure(*symbol, *def, 0, 0, 2, 0);
    project.library.insert(
            PlcModule::buildSymbol(*def, PlcModule::points(*symbol, database)));
    symbol->definitionId = PlcModule::symbolId(*def);
    symbol->placement.position = QPointF(80, 60);
    auto *original = symbol.get();
    folio->addEntity(std::move(symbol));

    std::vector<EntityPtr> copies = copyOf(*folio, { original->id() }, QPointF(60.0, 0.0));
    const CircuitCopyResult result =
            CircuitCopy::retag(copies, project, Profile::byId(QStringLiteral("iec")), database,
                               folio);

    REQUIRE(result.modulesMoved == 1);
    const auto *copy = dynamic_cast<const SymbolInstance *>(copies.front().get());
    REQUIRE(copy != nullptr);

    QSet<QString> before;
    for (const PlcPoint &point : PlcModule::points(*original, database))
        before.insert(point.address);
    for (const PlcPoint &point : PlcModule::points(*copy, database))
        REQUIRE_FALSE(before.contains(point.address));
}

TEST_CASE("Un symbole dont la definition manque garde son repere", "[circuitcopy]")
{
    // La designation saute les symboles sans definition — les vider avant
    // l'appel les rendrait anonymes. Un dessin incomplet doit rester lisible :
    // c'est justement quand une bibliotheque manque qu'on a besoin du repere.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto orphan = std::make_unique<SymbolInstance>();
    orphan->definitionId = QStringLiteral("iec:inconnu");
    orphan->placement.position = QPointF(60, 60);
    orphan->setDesignation(QStringLiteral("K9"));
    auto *raw = orphan.get();
    folio->addEntity(std::move(orphan));

    std::vector<EntityPtr> copies = copyOf(*folio, { raw->id() }, QPointF(40.0, 0.0));
    retag(copies, project, folio);

    REQUIRE(dynamic_cast<const SymbolInstance *>(copies.front().get())->designation()
            == QStringLiteral("K9"));
}
