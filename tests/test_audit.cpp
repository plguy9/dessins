#include <catch2/catch_test_macros.hpp>

#include "core/entities.h"
#include "core/netlist.h"
#include "rules/audit.h"
#include "symbols/librarystore.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

namespace {

Project onePage()
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio(QStringLiteral("Commande"));
    folio->number = QStringLiteral("1");
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    folio->frame.columns = 10;
    folio->frame.rows = 6;
    return project;
}

bool hasCode(const QVector<AuditFinding> &findings, const QString &code)
{
    for (const AuditFinding &finding : findings) {
        if (finding.code == code)
            return true;
    }
    return false;
}

const AuditFinding *findByCode(const QVector<AuditFinding> &findings, const QString &code)
{
    for (const AuditFinding &finding : findings) {
        if (finding.code == code)
            return &finding;
    }
    return nullptr;
}

QVector<AuditFinding> audit(const Project &project)
{
    return Audit::run(project, Netlist::build(project), PlcDatabase::builtin());
}

} // namespace

TEST_CASE("Un dossier sain ne remonte rien de bloquant", "[audit]")
{
    // Le controle doit rester silencieux quand tout va bien : un audit qui
    // crie a chaque enregistrement finit par ne plus etre lu.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto *a = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                          QStringLiteral("-K1"));
    auto *b = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(120, 60),
                          QStringLiteral("-K2"));
    a->fields.insert(QStringLiteral("partNumber"), QStringLiteral("LC1D09"));
    b->fields.insert(QStringLiteral("partNumber"), QStringLiteral("LC1D09"));
    drawWire(folio, { QPointF(65, 60), QPointF(115, 60) });

    const QVector<AuditFinding> findings = audit(project);
    for (const AuditFinding &finding : findings) {
        CAPTURE(finding.code, finding.message);
        CHECK(finding.severity != AuditFinding::Severity::Error);
    }
}

TEST_CASE("Deux appareils ne peuvent pas porter le meme repere", "[audit]")
{
    // Le cableur ne saurait plus lequel des deux -K1 est celui du folio 3 :
    // c'est une erreur, pas un avertissement.
    Project project = onePage();
    Folio *folio = project.folios().front();
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                QStringLiteral("-K1"));
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(120, 60),
                QStringLiteral("-K1"));

    const QVector<AuditFinding> findings = audit(project);
    const AuditFinding *duplicate = findByCode(findings, QStringLiteral("tag.duplicate"));
    REQUIRE(duplicate);
    CHECK(duplicate->severity == AuditFinding::Severity::Error);
    // Le constat doit dire ou : un message sans lieu coute plus de temps
    // qu'il n'en fait gagner.
    CHECK_FALSE(duplicate->folioId.isEmpty());
    CHECK_FALSE(duplicate->entityId.isEmpty());
    CHECK_FALSE(duplicate->zone.isEmpty());
}

TEST_CASE("Les blocs d'un meme appareil partagent leur repere sans conflit", "[audit]")
{
    // Une bobine et ses contacts auxiliaires portent le meme repere : c'est
    // le fonctionnement normal des appareils multi-blocs, pas un doublon.
    Project project = onePage();
    Folio *folio = project.folios().front();
    auto *coil = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                             QStringLiteral("-KM1"));
    auto *contact = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(120, 60),
                                QStringLiteral("-KM1"));
    coil->deviceGroup = QStringLiteral("km1");
    contact->deviceGroup = QStringLiteral("km1");

    CHECK_FALSE(hasCode(audit(project), QStringLiteral("tag.duplicate")));
}

TEST_CASE("Un potentiel ne peut pas porter deux reperes de fil", "[audit]")
{
    // C'est le meme conducteur : il ne peut pas etre a la fois 101 et 102.
    Project project = onePage();
    Folio *folio = project.folios().front();
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                QStringLiteral("-K1"));
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(120, 60),
                QStringLiteral("-K2"));

    Wire *first = drawWire(folio, { QPointF(65, 60), QPointF(90, 60) });
    Wire *second = drawWire(folio, { QPointF(90, 60), QPointF(115, 60) });
    first->number = QStringLiteral("101");
    second->number = QStringLiteral("102");

    const AuditFinding *conflict =
            findByCode(audit(project), QStringLiteral("wire.conflictingNumbers"));
    REQUIRE(conflict);
    CHECK(conflict->severity == AuditFinding::Severity::Error);
    CHECK(conflict->message.contains(QStringLiteral("101")));
    CHECK(conflict->message.contains(QStringLiteral("102")));
}

TEST_CASE("Deux cartes d'automate ne peuvent pas occuper la meme adresse", "[audit][plc]")
{
    // L'erreur d'automate par excellence : elle ne se voit pas sur le folio,
    // elle se voit a la mise en service.
    const PlcDatabase database = PlcDatabase::builtin();
    const PlcModuleDef *def = database.find(QStringLiteral("siemens:6ES7321-1BH02"));
    REQUIRE(def);

    Project project = onePage();
    Folio *folio = project.folios().front();

    auto place = [&](int firstPoint, const QString &tag, const QPointF &at) {
        auto module = std::make_unique<SymbolInstance>();
        PlcModule::configure(*module, *def, 0, 0, 0, firstPoint);
        project.library.insert(
                PlcModule::buildSymbol(*def, PlcModule::points(*module, database)));
        module->definitionId = PlcModule::symbolId(*def);
        module->setDesignation(tag);
        module->placement.position = at;
        folio->addEntity(std::move(module));
    };

    // Deux cartes de seize points a huit points d'intervalle : leurs espaces
    // d'adressage se chevauchent sur huit points.
    place(0, QStringLiteral("-A1"), QPointF(60, 100));
    place(8, QStringLiteral("-A2"), QPointF(160, 100));

    const QVector<AuditFinding> findings = audit(project);
    const AuditFinding *overlap = findByCode(findings, QStringLiteral("plc.addressOverlap"));
    REQUIRE(overlap);
    CHECK(overlap->severity == AuditFinding::Severity::Error);
    CHECK(overlap->category == QStringLiteral("Automates"));

    // Un seul constat par carte : seize lignes identiques pour un meme
    // chevauchement noieraient le rapport.
    int count = 0;
    for (const AuditFinding &finding : findings) {
        if (finding.code == QStringLiteral("plc.addressOverlap"))
            ++count;
    }
    CHECK(count == 1);

    // Decalees de seize points, elles ne se chevauchent plus.
    Project apart = onePage();
    Folio *other = apart.folios().front();
    {
        auto module = std::make_unique<SymbolInstance>();
        PlcModule::configure(*module, *def, 0, 0, 0, 0);
        apart.library.insert(
                PlcModule::buildSymbol(*def, PlcModule::points(*module, database)));
        module->definitionId = PlcModule::symbolId(*def);
        module->setDesignation(QStringLiteral("-A1"));
        module->placement.position = QPointF(60, 100);
        other->addEntity(std::move(module));

        auto second = std::make_unique<SymbolInstance>();
        PlcModule::configure(*second, *def, 0, 0, 0, 16);
        second->definitionId = PlcModule::symbolId(*def);
        second->setDesignation(QStringLiteral("-A2"));
        second->placement.position = QPointF(160, 100);
        other->addEntity(std::move(second));
    }
    CHECK_FALSE(hasCode(audit(apart), QStringLiteral("plc.addressOverlap")));
}

TEST_CASE("L'audit trie les erreurs devant et compte par categorie", "[audit]")
{
    // On corrige ce qui bloque avant ce qui gene : l'ordre du rapport est
    // l'ordre du depannage.
    Project project = onePage();
    Folio *folio = project.folios().front();
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                QStringLiteral("-K1"));
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(120, 60),
                QStringLiteral("-K1"));
    placeSymbol(project, folio, QStringLiteral("iec:inconnu"), QPointF(180, 60),
                QStringLiteral("-K3"));

    const QVector<AuditFinding> findings = audit(project);
    REQUIRE_FALSE(findings.isEmpty());
    CHECK(findings.first().severity == AuditFinding::Severity::Error);
    for (int i = 1; i < findings.size(); ++i)
        CHECK(findings.at(i - 1).severity >= findings.at(i).severity);

    CHECK(hasCode(findings, QStringLiteral("symbol.missingDefinition")));

    const QMap<QString, int> counts = Audit::countByCategory(findings);
    // Toutes les categories sont presentes, meme a zero : les onglets de la
    // fenetre ne doivent pas apparaitre et disparaitre au fil des controles.
    CHECK(counts.size() == Audit::categories().size());
    CHECK(counts.value(QStringLiteral("Symboles")) >= 1);
    CHECK(counts.value(QStringLiteral("Repères")) >= 1);

    const ReportTable table = Audit::toTable(findings);
    CHECK(table.rowCount() == findings.size());
    CHECK(table.headers.contains(QStringLiteral("Zone")));
}

TEST_CASE("La reference fabricant manquante est une information, pas une erreur", "[audit]")
{
    // Un schema se dessine avant de se chiffrer. Rappeler a chaque
    // enregistrement ce qui reste a referencer ferait ignorer tout le reste
    // de l'audit.
    Project project = onePage();
    Folio *folio = project.folios().front();
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                QStringLiteral("-K1"));

    const AuditFinding *finding =
            findByCode(audit(project), QStringLiteral("catalog.noPartNumber"));
    REQUIRE(finding);
    CHECK(finding->severity == AuditFinding::Severity::Info);

    // Un appareil multi-blocs ne le dit qu'une fois : trois lignes pour un
    // seul contacteur a commander seraient trompeuses.
    Project grouped = onePage();
    Folio *page = grouped.folios().front();
    for (int i = 0; i < 3; ++i) {
        auto *block = placeSymbol(grouped, page, QStringLiteral("iec:device"),
                                  QPointF(40 + 40 * i, 60), QStringLiteral("-KM1"));
        block->deviceGroup = QStringLiteral("km1");
        block->blockIndex = i;
    }
    int count = 0;
    for (const AuditFinding &finding2 : audit(grouped)) {
        if (finding2.code == QStringLiteral("catalog.noPartNumber"))
            ++count;
    }
    CHECK(count == 1);
}

TEST_CASE("Dix bornes d'un bornier ne sont pas dix repères en double", "[audit][bornier]")
{
    // Corollaire immédiat du bornier partagé : pour l'audit, trois bornes qui
    // portent -X1 étaient trois appareils se disputant un repère. Pour une
    // borne, l'identité c'est le bornier ET le numéro — le doublon, c'est
    // deux fois -X1:4, et celui-là est une vraie faute : le câbleur ne sait
    // pas où visser son fil.
    Project project;
    LibraryStore::loadBuiltin(project.library);
    Folio *folio = project.addFolio(QStringLiteral("Bornier"));
    folio->number = QStringLiteral("1");

    auto poser = [&](double y, const QString &numero) {
        auto borne = std::make_unique<SymbolInstance>();
        borne->definitionId = QStringLiteral("iec:terminal");
        borne->placement.position = QPointF(80.0, y);
        borne->setDesignation(QStringLiteral("-X1"));
        borne->fields.insert(QStringLiteral("terminal"), numero);
        folio->addEntity(std::move(borne));
    };
    poser(40.0, QStringLiteral("1"));
    poser(60.0, QStringLiteral("2"));
    poser(80.0, QStringLiteral("3"));

    const auto compter = [&] {
        int doublons = 0;
        for (const AuditFinding &f : Audit::run(project, Netlist::build(project),
                                                PlcDatabase::builtin()))
            if (f.code == QLatin1String("tag.duplicate"))
                ++doublons;
        return doublons;
    };
    CHECK(compter() == 0);

    // Deux fois la borne 2 : là, c'est une faute.
    poser(100.0, QStringLiteral("2"));
    CHECK(compter() == 1);
}
