#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/documentcommands.h"
#include "core/edittools.h"
#include "core/netlist.h"
#include "io/dsnfile.h"
#include "testhelpers.h"

#include <QTemporaryDir>

using namespace dsn;
using namespace test;
using Catch::Matchers::WithinAbs;

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

} // namespace

// --------------------------------------------------------------------------
// ECHELLE

TEST_CASE("L'echelle grossit un symbole autour du point de base", "[edition][echelle]")
{
    Project project = onePage();
    Folio *folio = project.folios().front();
    auto *symbol = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 50));

    ScaleEntitiesCommand command(project, folio->id(), { symbol->id() }, QPointF(50, 50), 2.0);
    REQUIRE(command.affectedCount() == 1);
    command.redo();

    // Le point de base ne bouge pas ; ce qui en est distant s'en eloigne du
    // facteur. C'est toute la definition d'une homothetie, et c'est ce qui
    // rend le geste previsible : on grossit « depuis » un point.
    CHECK_THAT(symbol->placement.position.x(), WithinAbs(150.0, 1e-9));
    CHECK_THAT(symbol->placement.position.y(), WithinAbs(50.0, 1e-9));
    CHECK_THAT(symbol->placement.scale, WithinAbs(2.0, 1e-9));

    command.undo();
    CHECK_THAT(symbol->placement.position.x(), WithinAbs(100.0, 1e-9));
    CHECK_THAT(symbol->placement.scale, WithinAbs(1.0, 1e-9));
}

TEST_CASE("Les broches d'un symbole grossi suivent le symbole", "[edition][echelle]")
{
    // C'est la raison d'etre du facteur de placement : le peintre et la
    // connectivite passent tous deux par la meme transformation, donc grossir
    // un appareil ne le debranche pas.
    Project project = onePage();
    Folio *folio = project.folios().front();
    auto *left = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60),
                             QStringLiteral("-K1"));
    auto *right = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(120, 60),
                              QStringLiteral("-K2"));
    drawWire(folio, { QPointF(65, 60), QPointF(115, 60) });

    const Netlist before = Netlist::build(project);
    const int connected = before.netCount();
    REQUIRE(connected > 0);

    const SymbolDefinition *definition = project.library.definition(left->definitionId);
    REQUIRE(definition);
    const QPointF pinBefore = left->placement.map(definition->pins.at(1).position);
    CHECK_THAT(pinBefore.x(), WithinAbs(65.0, 1e-9));

    // Grossir autour de la broche de droite : elle est le point fixe, donc
    // le fil qui y est raccorde le reste.
    ScaleEntitiesCommand command(project, folio->id(), { left->id() }, pinBefore, 2.0);
    command.redo();

    const QPointF pinAfter = left->placement.map(definition->pins.at(1).position);
    CHECK_THAT(pinAfter.x(), WithinAbs(65.0, 1e-6));
    CHECK_THAT(pinAfter.y(), WithinAbs(60.0, 1e-6));

    // La netlist ne perd rien : le fil touche toujours la broche.
    const Netlist after = Netlist::build(project);
    CHECK(after.netCount() == connected);
    Q_UNUSED(right);
}

TEST_CASE("L'echelle d'un texte grandit sa hauteur, pas deux fois", "[edition][echelle]")
{
    // La hauteur de capitale et le facteur de placement se multiplieraient :
    // un texte double grossirait au carre. Un seul des deux doit bouger.
    Project project = onePage();
    Folio *folio = project.folios().front();

    auto text = std::make_unique<TextItem>();
    text->text = QStringLiteral("ARMOIRE");
    text->height = 2.5;
    text->placement.position = QPointF(80, 40);
    auto *raw = text.get();
    folio->addEntity(std::move(text));

    ScaleEntitiesCommand command(project, folio->id(), { raw->id() }, QPointF(0, 0), 2.0);
    command.redo();
    CHECK_THAT(raw->height, WithinAbs(5.0, 1e-9));
    CHECK_THAT(raw->placement.scale, WithinAbs(1.0, 1e-9));
    CHECK_THAT(raw->placement.position.x(), WithinAbs(160.0, 1e-9));
}

TEST_CASE("Un facteur nul ou negatif ne fait rien", "[edition][echelle]")
{
    // Un facteur nul viderait le dessin sans rien dire. On refuse plutot que
    // de produire un symbole de taille zero, impossible a rattraper a la
    // souris.
    Project project = onePage();
    Folio *folio = project.folios().front();
    auto *symbol = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 50));

    for (const double factor : { 0.0, -2.0, 1.0 }) {
        ScaleEntitiesCommand command(project, folio->id(), { symbol->id() }, QPointF(0, 0),
                                     factor);
        CHECK(command.affectedCount() == 0);
    }
    CHECK_THAT(symbol->placement.scale, WithinAbs(1.0, 1e-9));
}

TEST_CASE("L'echelle survit a l'enregistrement", "[edition][echelle][io]")
{
    Project project = onePage();
    Folio *folio = project.folios().front();
    auto *symbol = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 50));
    symbol->placement.scale = 1.75;

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("echelle.arcus"));
    REQUIRE(DsnFile::save(path, project));

    Project reloaded;
    REQUIRE(DsnFile::load(path, reloaded).ok);
    const auto symbols = reloaded.folios().front()->entitiesOfType<SymbolInstance>();
    REQUIRE(symbols.size() == 1);
    CHECK_THAT(symbols.front()->placement.scale, WithinAbs(1.75, 1e-9));
}

// --------------------------------------------------------------------------
// RESEAU

TEST_CASE("Le reseau rectangulaire pose une grille de copies", "[edition][reseau]")
{
    ArraySpec spec;
    spec.columns = 3;
    spec.rows = 2;
    spec.columnSpacing = 25.0;
    spec.rowSpacing = 40.0;

    const QVector<ArrayPlacement> placements = ArrayTools::placements(spec);
    REQUIRE(placements.size() == 6);
    // L'original est le premier et ne bouge pas : le reseau s'ajoute au
    // dessin, il ne le refait pas.
    CHECK_THAT(placements.first().offset.x(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(placements.at(1).offset.x(), WithinAbs(25.0, 1e-9));
    CHECK_THAT(placements.at(3).offset.y(), WithinAbs(40.0, 1e-9));
    CHECK_THAT(placements.last().offset.x(), WithinAbs(50.0, 1e-9));
    CHECK_THAT(placements.last().offset.y(), WithinAbs(40.0, 1e-9));
}

TEST_CASE("Un reseau a pas nul est refuse", "[edition][reseau]")
{
    // Toutes les copies se poseraient au meme endroit : invisibles, et
    // impossibles a rattraper autrement qu'en annulant.
    ArraySpec spec;
    spec.columns = 4;
    spec.rows = 1;
    spec.columnSpacing = 0.0;
    CHECK_FALSE(spec.isValid());
    CHECK(ArrayTools::placements(spec).isEmpty());
}

TEST_CASE("Le reseau se defait d'une seule annulation", "[edition][reseau]")
{
    Project project = onePage();
    Folio *folio = project.folios().front();
    auto *symbol = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(60, 60));
    const int before = folio->entityCount();

    ArraySpec spec;
    spec.columns = 4;
    spec.rows = 2;
    spec.columnSpacing = 30.0;
    spec.rowSpacing = 30.0;

    ArrayEntitiesCommand command(project, folio->id(), { symbol->id() }, spec);
    CHECK(command.addedCount() == 7); // huit cases, l'original compris
    command.redo();
    CHECK(folio->entityCount() == before + 7);

    command.undo();
    CHECK(folio->entityCount() == before);

    // Le retablissement rend les memes identifiants : la selection et les
    // panneaux qui les detiennent ne pointent pas dans le vide.
    command.redo();
    CHECK(folio->entityCount() == before + 7);
}

// --------------------------------------------------------------------------
// ALIGNER

TEST_CASE("Aligner met les bords au cordeau", "[edition][aligner]")
{
    const QVector<QRectF> boxes{ QRectF(10, 10, 20, 10), QRectF(40, 25, 30, 10),
                                 QRectF(15, 50, 10, 10) };

    const QVector<QPointF> left = AlignTools::offsets(boxes, AlignMode::Left);
    REQUIRE(left.size() == 3);
    // Le bord de reference est celui de l'enveloppe : personne ne sort du
    // groupe, ce qui evite de deplacer le dessin entier par megarde.
    CHECK_THAT(left.at(0).x(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(left.at(1).x(), WithinAbs(-30.0, 1e-9));
    CHECK_THAT(left.at(2).x(), WithinAbs(-5.0, 1e-9));
    for (const QPointF &offset : left)
        CHECK_THAT(offset.y(), WithinAbs(0.0, 1e-9));

    const QVector<QPointF> top = AlignTools::offsets(boxes, AlignMode::Top);
    CHECK_THAT(top.at(1).y(), WithinAbs(-15.0, 1e-9));
    for (const QPointF &offset : top)
        CHECK_THAT(offset.x(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Repartir espace les centres a pas egal", "[edition][aligner]")
{
    // On repartit les centres et non les bords : des elements de tailles
    // differentes paraissent sinon mal espaces alors que leurs bords sont
    // parfaitement reguliers.
    const QVector<QRectF> boxes{ QRectF(0, 0, 10, 10), QRectF(12, 0, 40, 10),
                                 QRectF(100, 0, 10, 10) };
    const QVector<QPointF> spread = AlignTools::offsets(boxes, AlignMode::DistributeHorizontally);
    REQUIRE(spread.size() == 3);

    // Les extremes ne bougent pas.
    CHECK_THAT(spread.first().x(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(spread.last().x(), WithinAbs(0.0, 1e-9));
    // Celui du milieu se pose a mi-chemin des deux centres.
    const double middle = boxes.at(1).center().x() + spread.at(1).x();
    CHECK_THAT(middle, WithinAbs((5.0 + 105.0) / 2.0, 1e-9));

    // Repartir deux elements ne veut rien dire : les extremes sont les seuls
    // presents, et rien ne bouge.
    const QVector<QRectF> two{ QRectF(0, 0, 10, 10), QRectF(50, 0, 10, 10) };
    CHECK(AlignTools::offsets(two, AlignMode::DistributeHorizontally).isEmpty());
}

// --------------------------------------------------------------------------
// JOINDRE et COUPER

TEST_CASE("Deux fils colineaires bout a bout n'en font qu'un", "[edition][joindre]")
{
    Project project = onePage();
    Folio *folio = project.folios().front();
    Wire *a = drawWire(folio, { QPointF(20, 50), QPointF(60, 50) });
    Wire *b = drawWire(folio, { QPointF(60, 50), QPointF(100, 50) });

    const auto join = EditTools::joinable(*folio, a->id(), b->id());
    REQUIRE(join);
    // Le sommet commun disparait : deux fils bout a bout font un fil droit,
    // pas un fil avec un point de rebroussement invisible.
    REQUIRE(join->merged.size() == 2);
    CHECK_THAT(join->merged.first().x(), WithinAbs(20.0, 1e-9));
    CHECK_THAT(join->merged.last().x(), WithinAbs(100.0, 1e-9));
}

TEST_CASE("Un coude est conserve a la soudure", "[edition][joindre]")
{
    Project project = onePage();
    Folio *folio = project.folios().front();
    Wire *a = drawWire(folio, { QPointF(20, 50), QPointF(60, 50) });
    Wire *b = drawWire(folio, { QPointF(60, 50), QPointF(60, 90) });

    const auto join = EditTools::joinable(*folio, a->id(), b->id());
    REQUIRE(join);
    CHECK(join->merged.size() == 3);
}

TEST_CASE("Deux types de fils differents ne se soudent pas", "[edition][joindre]")
{
    // La soudure perdrait une couleur en silence — le genre de perte qu'on ne
    // remarque qu'a la relecture du dossier imprime.
    Project project = onePage();
    Folio *folio = project.folios().front();
    Wire *a = drawWire(folio, { QPointF(20, 50), QPointF(60, 50) });
    Wire *b = drawWire(folio, { QPointF(60, 50), QPointF(100, 50) });
    a->wireType = QStringLiteral("l1");
    b->wireType = QStringLiteral("n");

    CHECK_FALSE(EditTools::joinable(*folio, a->id(), b->id()));
}

TEST_CASE("Des fils qui ne se touchent pas ne se soudent pas", "[edition][joindre]")
{
    Project project = onePage();
    Folio *folio = project.folios().front();
    Wire *a = drawWire(folio, { QPointF(20, 50), QPointF(60, 50) });
    Wire *b = drawWire(folio, { QPointF(70, 50), QPointF(100, 50) });
    CHECK_FALSE(EditTools::joinable(*folio, a->id(), b->id()));
}

TEST_CASE("Couper un fil rend deux morceaux qui se rejoignent", "[edition][couper]")
{
    Project project = onePage();
    Folio *folio = project.folios().front();
    Wire *wire = drawWire(folio, { QPointF(20, 50), QPointF(100, 50) });

    const auto cut = EditTools::cut(*folio, wire->id(), QPointF(60, 50));
    REQUIRE(cut);
    CHECK(cut->before.size() == 2);
    CHECK(cut->after.size() == 2);
    CHECK_THAT(cut->before.last().x(), WithinAbs(60.0, 1e-9));
    CHECK_THAT(cut->after.first().x(), WithinAbs(60.0, 1e-9));

    // Couper sur une extremite ne produirait qu'un fil vide et un fil
    // identique : on refuse plutot que de laisser croire que c'est fait.
    CHECK_FALSE(EditTools::cut(*folio, wire->id(), QPointF(20, 50)));
    // Et hors du trace, il n'y a rien a couper.
    CHECK_FALSE(EditTools::cut(*folio, wire->id(), QPointF(60, 80)));
}
