#include <catch2/catch_test_macros.hpp>

#include "core/entities.h"
#include "core/netlist.h"
#include "io/dsnfile.h"
#include "rules/crossref.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

namespace {

Project twoFolios()
{
    Project project;
    for (int i = 0; i < 2; ++i) {
        Folio *folio = project.addFolio(QStringLiteral("Folio %1").arg(i + 1));
        folio->number = QString::number(i + 1);
        folio->sheet = sheetFormatById(QStringLiteral("A3"));
        folio->frame.columns = 10;
        folio->frame.rows = 6;
    }
    return project;
}

Label *dropArrow(Folio *folio, const QPointF &at, const QString &name, Label::Role role)
{
    auto label = std::make_unique<Label>();
    label->point = at;
    label->name = name;
    label->role = role;
    label->scope = Label::Scope::Project;
    auto *raw = label.get();
    folio->addEntity(std::move(label));
    return raw;
}

int countCode(const Netlist &netlist, const QString &code)
{
    int n = 0;
    for (const Netlist::Diagnostic &d : netlist.diagnostics()) {
        if (d.code == code)
            ++n;
    }
    return n;
}

} // namespace

TEST_CASE("Une source porte le renvoi vers sa destination", "[crossref]")
{
    // C'est tout l'interet de la fleche : lire ou part le signal sans
    // feuiller le dossier.
    Project project = twoFolios();
    Label *source = dropArrow(project.folioAt(0), QPointF(100, 60),
                              QStringLiteral("CMD"), Label::Role::Source);
    Label *destination = dropArrow(project.folioAt(1), QPointF(200, 150),
                                   QStringLiteral("CMD"), Label::Role::Destination);

    const Netlist netlist = Netlist::build(project);
    const QHash<QString, QString> refs = CrossReference::resolve(project, netlist);

    const QString expectedTo = CrossReference::locationOf(*project.folioAt(1), QPointF(200, 150));
    const QString expectedFrom = CrossReference::locationOf(*project.folioAt(0), QPointF(100, 60));
    REQUIRE_FALSE(expectedTo.isEmpty());

    CHECK(refs.value(source->id()) == QStringLiteral("→ ") + expectedTo);
    CHECK(refs.value(destination->id()) == QStringLiteral("← ") + expectedFrom);
    // Le repere est bien « folio/zone » : c'est ce qui permet de retrouver
    // l'autre bout sur la feuille, pas seulement la page.
    CHECK(expectedTo.startsWith(QLatin1String("2/")));
}

TEST_CASE("Une source ne renvoie pas vers une autre source", "[crossref]")
{
    // Deux sources du meme nom seraient deux origines pour un seul signal :
    // se renvoyer l'une a l'autre n'aurait aucun sens de lecture.
    Project project = twoFolios();
    Label *a = dropArrow(project.folioAt(0), QPointF(100, 60), QStringLiteral("CMD"),
                         Label::Role::Source);
    dropArrow(project.folioAt(1), QPointF(120, 60), QStringLiteral("CMD"), Label::Role::Source);

    const Netlist netlist = Netlist::build(project);
    const QHash<QString, QString> refs = CrossReference::resolve(project, netlist);
    CHECK_FALSE(refs.contains(a->id()));
}

TEST_CASE("Un renvoi simple enumere les autres pages", "[crossref]")
{
    // Un potentiel d'alimentation n'a pas de sens de lecture : son renvoi
    // enumere, sans fleche.
    Project project = twoFolios();
    auto plain = [&](Folio *folio, const QPointF &at) {
        auto label = std::make_unique<Label>();
        label->point = at;
        label->name = QStringLiteral("L1");
        label->scope = Label::Scope::Project;
        auto *raw = label.get();
        folio->addEntity(std::move(label));
        return raw;
    };
    Label *first = plain(project.folioAt(0), QPointF(100, 60));
    plain(project.folioAt(1), QPointF(100, 60));

    const Netlist netlist = Netlist::build(project);
    const QHash<QString, QString> refs = CrossReference::resolve(project, netlist);
    const QString reference = refs.value(first->id());
    CHECK_FALSE(reference.isEmpty());
    CHECK_FALSE(reference.startsWith(QStringLiteral("→")));
    CHECK(reference.startsWith(QLatin1String("2/")));
}

TEST_CASE("Une etiquette locale ne renvoie nulle part", "[crossref]")
{
    // Une etiquette de folio ne relie que sa page : lui donner un renvoi
    // ferait croire a une continuite qui n'existe pas.
    Project project = twoFolios();
    Label *local = dropLabel(project.folioAt(0), QPointF(100, 60), QStringLiteral("A"));
    local->scope = Label::Scope::Folio;
    dropLabel(project.folioAt(1), QPointF(100, 60), QStringLiteral("A"))->scope =
            Label::Scope::Folio;

    const Netlist netlist = Netlist::build(project);
    CHECK_FALSE(CrossReference::resolve(project, netlist).contains(local->id()));
}

TEST_CASE("Une source sans destination est signalee", "[crossref][audit]")
{
    // Un signal qui part et ne revient nulle part se lit comme un schema
    // complet alors qu'il manque une page. C'est le controle que fait
    // l'audit electrique d'AutoCAD.
    Project project = twoFolios();
    dropArrow(project.folioAt(0), QPointF(100, 60), QStringLiteral("CMD"), Label::Role::Source);

    const Netlist netlist = Netlist::build(project);
    CHECK(countCode(netlist, QStringLiteral("signal.noDestination")) == 1);
    CHECK(countCode(netlist, QStringLiteral("signal.noSource")) == 0);
}

TEST_CASE("Une destination sans source est signalee", "[crossref][audit]")
{
    Project project = twoFolios();
    dropArrow(project.folioAt(1), QPointF(100, 60), QStringLiteral("CMD"),
              Label::Role::Destination);

    const Netlist netlist = Netlist::build(project);
    CHECK(countCode(netlist, QStringLiteral("signal.noSource")) == 1);
}

TEST_CASE("Une paire source-destination ne declenche aucune anomalie",
          "[crossref][audit]")
{
    Project project = twoFolios();
    dropArrow(project.folioAt(0), QPointF(100, 60), QStringLiteral("CMD"), Label::Role::Source);
    dropArrow(project.folioAt(1), QPointF(100, 60), QStringLiteral("CMD"),
              Label::Role::Destination);

    const Netlist netlist = Netlist::build(project);
    CHECK(countCode(netlist, QStringLiteral("signal.noSource")) == 0);
    CHECK(countCode(netlist, QStringLiteral("signal.noDestination")) == 0);
}

TEST_CASE("Le role d'une fleche survit a l'aller-retour .dsn", "[crossref][io]")
{
    Project project = twoFolios();
    dropArrow(project.folioAt(0), QPointF(100, 60), QStringLiteral("CMD"), Label::Role::Source);

    Project restored;
    REQUIRE(DsnFile::fromArchive(DsnFile::toArchive(project), restored).ok);

    const auto labels = restored.folioAt(0)->entitiesOfType<Label>();
    REQUIRE(labels.size() == 1);
    CHECK(labels.front()->role == Label::Role::Source);
    // Une fleche est inter-folios par construction : la portee suit toujours.
    CHECK(labels.front()->scope == Label::Scope::Project);
}

TEST_CASE("Un signal relie bien les deux folios en un seul potentiel",
          "[crossref][netlist]")
{
    // La fleche n'est pas qu'un graphisme : les deux reseaux deviennent un
    // seul potentiel, comme le fait AutoCAD Electrical.
    Project project = twoFolios();
    drawWire(project.folioAt(0), { QPointF(100, 60), QPointF(160, 60) });
    dropArrow(project.folioAt(0), QPointF(100, 60), QStringLiteral("CMD"), Label::Role::Source);

    drawWire(project.folioAt(1), { QPointF(100, 60), QPointF(160, 60) });
    dropArrow(project.folioAt(1), QPointF(100, 60), QStringLiteral("CMD"),
              Label::Role::Destination);

    const Netlist netlist = Netlist::build(project);
    int crossing = 0;
    for (const Netlist::Net &net : netlist.nets()) {
        if (net.crossesFolios())
            ++crossing;
    }
    CHECK(crossing == 1);
}
