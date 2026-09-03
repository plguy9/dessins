#include <catch2/catch_test_macros.hpp>

#include "core/componenttools.h"
#include "core/documentcommands.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

TEST_CASE("Ajout puis annulation laissent le folio intact", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(0, 0), QPointF(10, 0) };
    const QString id = wire->id();

    stack.push(std::make_unique<AddEntityCommand>(project, folio->id(), std::move(wire)));
    CHECK(folio->entityCount() == 1);

    stack.undo();
    CHECK(folio->entityCount() == 0);

    stack.redo();
    CHECK(folio->entityCount() == 1);
    CHECK(folio->entity(id) != nullptr);
}

TEST_CASE("La suppression restaure l'entite a son rang", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    drawWire(folio, { QPointF(0, 0), QPointF(10, 0) });
    Wire *middle = drawWire(folio, { QPointF(0, 10), QPointF(10, 10) });
    drawWire(folio, { QPointF(0, 20), QPointF(10, 20) });
    const QString id = middle->id();

    stack.push(std::make_unique<RemoveEntityCommand>(project, folio->id(), id));
    CHECK(folio->entityCount() == 2);

    stack.undo();
    REQUIRE(folio->entityCount() == 3);
    // Le rang compte : l'ordre de trace decide de ce qui passe au-dessus.
    CHECK(folio->indexOfEntity(id) == 1);
}

TEST_CASE("Les deplacements successifs fusionnent en une seule annulation", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    Wire *wire = drawWire(folio, { QPointF(0, 0), QPointF(10, 0) });
    const QStringList ids{ wire->id() };

    // Un glisser a la souris produit une commande par evenement ; l'utilisateur
    // n'en attend qu'une seule dans l'historique.
    for (int i = 0; i < 5; ++i)
        stack.push(std::make_unique<MoveEntitiesCommand>(project, folio->id(), ids, QPointF(2, 0)));

    CHECK(stack.count() == 1);
    CHECK(wire->start() == QPointF(10, 0));

    stack.undo();
    CHECK(wire->start() == QPointF(0, 0));
}

TEST_CASE("La fin d'un geste coupe la chaine de fusion", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    Wire *wire = drawWire(folio, { QPointF(0, 0), QPointF(10, 0) });
    const QStringList ids{ wire->id() };

    stack.push(std::make_unique<MoveEntitiesCommand>(project, folio->id(), ids, QPointF(5, 0)));
    stack.breakMergeChain();
    stack.push(std::make_unique<MoveEntitiesCommand>(project, folio->id(), ids, QPointF(5, 0)));

    CHECK(stack.count() == 2);
    stack.undo();
    CHECK(wire->start() == QPointF(5, 0));
}

TEST_CASE("Une macro se defait d'un seul coup", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    stack.beginMacro(QStringLiteral("Coller"));
    for (int i = 0; i < 3; ++i) {
        auto wire = std::make_unique<Wire>();
        wire->points = { QPointF(0, i * 10), QPointF(10, i * 10) };
        stack.push(std::make_unique<AddEntityCommand>(project, folio->id(), std::move(wire)));
    }
    stack.endMacro();

    CHECK(folio->entityCount() == 3);
    CHECK(stack.count() == 1);
    CHECK(stack.undoText() == QLatin1String("Coller"));

    stack.undo();
    CHECK(folio->entityCount() == 0);
    stack.redo();
    CHECK(folio->entityCount() == 3);
}

TEST_CASE("Une macro vide n'encombre pas l'historique", "[command]")
{
    Project project;
    project.addFolio();
    CommandStack stack;

    stack.beginMacro(QStringLiteral("Rien"));
    stack.endMacro();
    CHECK(stack.count() == 0);
    CHECK_FALSE(stack.canUndo());
}

TEST_CASE("Une nouvelle commande abandonne la branche defaite", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    auto first = std::make_unique<Wire>();
    first->points = { QPointF(0, 0), QPointF(10, 0) };
    stack.push(std::make_unique<AddEntityCommand>(project, folio->id(), std::move(first)));
    stack.undo();
    CHECK(stack.canRedo());

    auto second = std::make_unique<Wire>();
    second->points = { QPointF(0, 50), QPointF(10, 50) };
    stack.push(std::make_unique<AddEntityCommand>(project, folio->id(), std::move(second)));

    CHECK_FALSE(stack.canRedo());
    CHECK(stack.count() == 1);
    CHECK(folio->entityCount() == 1);
}

TEST_CASE("L'etat propre suit les enregistrements", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    CHECK(stack.isClean());
    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(0, 0), QPointF(10, 0) };
    stack.push(std::make_unique<AddEntityCommand>(project, folio->id(), std::move(wire)));
    CHECK_FALSE(stack.isClean());

    stack.setClean();
    CHECK(stack.isClean());
    stack.undo();
    CHECK_FALSE(stack.isClean());
    stack.redo();
    CHECK(stack.isClean());
}

TEST_CASE("La modification d'une entite se rejoue par instantane", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    Wire *wire = drawWire(folio, { QPointF(0, 0), QPointF(10, 0) });
    auto before = wire->clone();
    auto after = wire->clone();
    static_cast<Wire *>(after.get())->number = QStringLiteral("W7");

    stack.push(std::make_unique<ModifyEntityCommand>(project, folio->id(), std::move(before),
                                                     std::move(after)));
    CHECK(dynamic_cast<Wire *>(folio->entity(wire->id()))->number == QLatin1String("W7"));

    stack.undo();
    CHECK(dynamic_cast<Wire *>(folio->entity(wire->id()))->number.isEmpty());
}

TEST_CASE("Une modification garde l'entite au meme emplacement memoire", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    Wire *wire = drawWire(folio, { QPointF(0, 0), QPointF(10, 0) });
    const Wire *address = wire;

    for (int i = 0; i < 3; ++i) {
        auto before = wire->clone();
        auto after = wire->clone();
        static_cast<Wire *>(after.get())->number = QStringLiteral("W%1").arg(i);
        stack.push(std::make_unique<ModifyEntityCommand>(project, folio->id(), std::move(before),
                                                         std::move(after)));
        // Les vues et les panneaux detiennent des pointeurs vers les entites
        // qu'ils editent. Remplacer l'objet a chaque modification les
        // invaliderait, et le plantage n'arriverait qu'a la modification
        // suivante — loin de sa cause.
        CHECK(folio->entity(wire->id()) == address);
        CHECK(wire->number == QStringLiteral("W%1").arg(i));
    }

    stack.undo();
    CHECK(folio->entity(wire->id()) == address);
    CHECK(wire->number == QLatin1String("W1"));
}

TEST_CASE("La recopie d'etat refuse un type different", "[command]")
{
    Wire wire;
    wire.points = { QPointF(0, 0), QPointF(10, 0) };
    Junction junction;
    // Recopier une jonction dans un fil produirait un objet incoherent :
    // le refus doit etre explicite, pas silencieux.
    CHECK_FALSE(wire.assign(junction));
    CHECK(wire.points.size() == 2);
}

TEST_CASE("Le retrait d'un folio restaure son contenu", "[command]")
{
    Project project;
    project.addFolio(QStringLiteral("Premier"));
    Folio *second = project.addFolio(QStringLiteral("Second"));
    drawWire(second, { QPointF(0, 0), QPointF(10, 0) });
    const QString id = second->id();
    CommandStack stack;

    stack.push(std::make_unique<RemoveFolioCommand>(project, id));
    CHECK(project.folioCount() == 1);

    stack.undo();
    REQUIRE(project.folioCount() == 2);
    CHECK(project.indexOf(id) == 1);
    CHECK(project.folio(id)->entityCount() == 1);
}

TEST_CASE("La pile previent de tout changement", "[command]")
{
    Project project;
    Folio *folio = project.addFolio();
    CommandStack stack;

    int notifications = 0;
    stack.setChangedCallback([&] { ++notifications; });

    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(0, 0), QPointF(10, 0) };
    stack.push(std::make_unique<AddEntityCommand>(project, folio->id(), std::move(wire)));
    stack.undo();
    stack.redo();

    CHECK(notifications == 3);
}

// --------------------------------------------------------------------------
// ETIRER

TEST_CASE("Etirer ne deplace que les sommets pris dans la fenetre",
          "[commands][stretch]")
{
    // C'est toute la difference avec DEPLACER : le fil s'allonge au lieu de
    // se translater, et ce qui est raccorde a l'autre bout ne bouge pas.
    Project project;
    Folio *folio = project.addFolio();
    Wire *wire = drawWire(folio, { QPointF(40, 100), QPointF(200, 100) });
    const QString id = wire->id();

    // La fenetre ne prend que l'extremite droite.
    StretchEntitiesCommand command(project, folio->id(), QRectF(180, 80, 40, 40),
                                   QPointF(0, -30));
    REQUIRE(command.affectedCount() == 1);
    command.redo();

    const auto *stretched = dynamic_cast<const Wire *>(folio->entity(id));
    REQUIRE(stretched);
    CHECK(stretched->points.first() == QPointF(40, 100));   // reste en place
    CHECK(stretched->points.last() == QPointF(200, 70));    // suit la fenetre

    command.undo();
    const auto *back = dynamic_cast<const Wire *>(folio->entity(id));
    REQUIRE(back);
    CHECK(back->points.first() == QPointF(40, 100));
    CHECK(back->points.last() == QPointF(200, 100));
}

TEST_CASE("Une entite entierement prise se deplace au lieu de s'etirer",
          "[commands][stretch]")
{
    // Comportement d'AutoCAD : un objet entierement compris dans la fenetre
    // n'a pas de sommet libre pour s'etirer, il se translate.
    Project project;
    Folio *folio = project.addFolio();
    Wire *wire = drawWire(folio, { QPointF(60, 100), QPointF(120, 100) });
    const QString id = wire->id();

    StretchEntitiesCommand command(project, folio->id(), QRectF(40, 80, 120, 40),
                                   QPointF(10, 10));
    command.redo();

    const auto *moved = dynamic_cast<const Wire *>(folio->entity(id));
    REQUIRE(moved);
    CHECK(moved->points.first() == QPointF(70, 110));
    CHECK(moved->points.last() == QPointF(130, 110));
}

TEST_CASE("Un symbole suit l'etirement si son point d'insertion est pris",
          "[commands][stretch]")
{
    // Un symbole n'a pas de sommet a etirer : c'est son point d'insertion qui
    // decide. Sans cela, etirer une zone laisserait les appareils sur place et
    // le schema deviendrait faux.
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();
    auto *symbol = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 60));
    const QString id = symbol->id();

    StretchEntitiesCommand inside(project, folio->id(), QRectF(90, 50, 30, 30), QPointF(0, 20));
    REQUIRE(inside.affectedCount() == 1);
    inside.redo();
    CHECK(dynamic_cast<const SymbolInstance *>(folio->entity(id))->placement.position
          == QPointF(100, 80));

    // Une fenetre qui ne contient pas le point d'insertion ne prend rien.
    StretchEntitiesCommand outside(project, folio->id(), QRectF(10, 10, 20, 20), QPointF(5, 5));
    CHECK(outside.affectedCount() == 0);
}

TEST_CASE("Etirer puis retablir redonne exactement le meme dessin",
          "[commands][stretch]")
{
    // Les sommets pris sont figes a la construction. Les recalculer au
    // retablissement les chercherait dans la geometrie deja deplacee, et le
    // dessin ne reviendrait pas au meme etat.
    Project project;
    Folio *folio = project.addFolio();
    Wire *wire = drawWire(folio, { QPointF(40, 100), QPointF(120, 100), QPointF(120, 160) });
    const QString id = wire->id();

    StretchEntitiesCommand command(project, folio->id(), QRectF(100, 80, 60, 40),
                                   QPointF(40, 0));
    command.redo();
    const QVector<QPointF> after = dynamic_cast<const Wire *>(folio->entity(id))->points;

    command.undo();
    command.redo();
    CHECK(dynamic_cast<const Wire *>(folio->entity(id))->points == after);
}

// --------------------------------------------------------------------------
// Deplacement d'un appareil, fils compris

namespace {

// Un appareil raccorde a gauche et a droite, comme sur un schema reel.
Project wiredDevice(SymbolInstance **symbolOut, Wire **leftOut, Wire **rightOut)
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();
    // Broches a -5 et +5 de l'origine du symbole.
    *symbolOut = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 60));
    *leftOut = drawWire(folio, { QPointF(40, 60), QPointF(95, 60) });
    *rightOut = drawWire(folio, { QPointF(105, 60), QPointF(180, 60) });
    return project;
}

} // namespace

TEST_CASE("Deplacer un appareil emmene les fils qui s'y raccordent",
          "[commands][component]")
{
    // Laisser les fils derriere debranche l'appareil en silence : le trace
    // reste beau, la netlist est fausse.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);
    const QString symbolId = symbol->id();
    const QString leftId = left->id();
    const QString rightId = right->id();

    MoveComponentCommand command(project, folio->id(), symbolId, QPointF(0, 30),
                                 project.library);
    REQUIRE(command.attachedCount() == 2);
    command.redo();

    CHECK(dynamic_cast<const SymbolInstance *>(folio->entity(symbolId))->placement.position
          == QPointF(100, 90));
    // L'extremite raccordee suit, l'autre reste ou elle est : le fil
    // s'allonge au lieu de se detacher.
    const auto *movedLeft = dynamic_cast<const Wire *>(folio->entity(leftId));
    CHECK(movedLeft->points.first() == QPointF(40, 60));
    CHECK(movedLeft->points.last() == QPointF(95, 90));
    const auto *movedRight = dynamic_cast<const Wire *>(folio->entity(rightId));
    CHECK(movedRight->points.first() == QPointF(105, 90));
    CHECK(movedRight->points.last() == QPointF(180, 60));

    command.undo();
    CHECK(dynamic_cast<const Wire *>(folio->entity(leftId))->points.last() == QPointF(95, 60));
}

TEST_CASE("Un appareil isole se deplace seul", "[commands][component]")
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();
    auto *symbol = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 60));

    MoveComponentCommand command(project, folio->id(), symbol->id(), QPointF(20, 0),
                                 project.library);
    CHECK(command.attachedCount() == 0);
    command.redo();
    CHECK(symbol->placement.position == QPointF(120, 60));
}

TEST_CASE("Un fil qui croise une broche sans y finir n'est pas emmene",
          "[commands][component]")
{
    // Un fil qui passe au milieu d'une broche la croise, il n'y est pas
    // raccorde : le tirer avec l'appareil deformerait le schema.
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();
    auto *symbol = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 60));
    // Ce fil traverse la broche de gauche (95,60) sans s'y arreter.
    Wire *crossing = drawWire(folio, { QPointF(95, 20), QPointF(95, 120) });
    const QString crossingId = crossing->id();

    MoveComponentCommand command(project, folio->id(), symbol->id(), QPointF(0, 40),
                                 project.library);
    CHECK(command.attachedCount() == 0);
    command.redo();
    const auto *untouched = dynamic_cast<const Wire *>(folio->entity(crossingId));
    CHECK(untouched->points.first() == QPointF(95, 20));
    CHECK(untouched->points.last() == QPointF(95, 120));
}

TEST_CASE("L'axe de glissement suit le fil raccorde", "[commands][scoot]")
{
    // Scoot ne bouge un appareil que le long de son fil : c'est ce qui
    // l'empeche de le detacher de son circuit.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);

    const auto axis = ComponentTools::scootAxis(*folio, project.library, *symbol);
    REQUIRE(axis);
    CHECK(std::abs(axis->x()) > 0.99);   // horizontal, comme ses deux fils
    CHECK(std::abs(axis->y()) < 0.01);

    // Un deplacement quelconque est ramene sur l'axe.
    const QPointF constrained = ComponentTools::constrainToAxis(QPointF(20, 35), *axis);
    CHECK(constrained.x() == 20.0);
    CHECK(constrained.y() == 0.0);
}

TEST_CASE("Deux fils tirant dans des axes differents ne donnent pas d'axe",
          "[commands][scoot]")
{
    // Glisser un appareil raccorde a l'horizontale et a la verticale n'a pas
    // de direction unique : mieux vaut ne rien contraindre.
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();
    auto *symbol = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 60));
    drawWire(folio, { QPointF(40, 60), QPointF(95, 60) });    // arrive a l'horizontale
    drawWire(folio, { QPointF(105, 60), QPointF(105, 120) }); // repart a la verticale

    CHECK_FALSE(ComponentTools::scootAxis(*folio, project.library, *symbol).has_value());
}

TEST_CASE("Retirer un appareil referme le fil", "[commands][component][heal]")
{
    // L'insertion coupe et rebranche ; la suppression doit recoudre. Sans
    // cela, effacer un contact laisse deux fils qui pointent vers du vide :
    // le dessin paraît juste et le circuit est ouvert — l'erreur ne se voit
    // qu'au câblage.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);

    const auto heal = ComponentTools::healOnRemoval(*folio, project.library, *symbol);
    REQUIRE(heal);
    // Les deux morceaux redeviennent un fil qui va d'un bout à l'autre...
    CHECK(heal->points.first() == QPointF(40, 60));
    CHECK(heal->points.last() == QPointF(180, 60));
    // ...et sans sommet inutile : les deux points de broche étaient alignés
    // avec leurs voisins, un coude fantôme se verrait au premier déplacement.
    CHECK(heal->points.size() == 2);
    CHECK(((heal->keepWireId == left->id() && heal->removeWireId == right->id())
           || (heal->keepWireId == right->id() && heal->removeWireId == left->id())));
}

TEST_CASE("Le fil recousu garde le repère saisi à la main", "[commands][component][heal]")
{
    // Le fil recousu est le même conducteur qu'avant la pose de l'appareil :
    // il doit en garder le nom, et c'est le nom saisi à la main qui compte.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);
    right->number = QStringLiteral("L1");
    right->numberLocked = true;

    const auto heal = ComponentTools::healOnRemoval(*folio, project.library, *symbol);
    REQUIRE(heal);
    CHECK(heal->keepWireId == right->id());
}

TEST_CASE("On ne recoud pas deux types de fils différents", "[commands][component][heal]")
{
    // Même règle que JOINDRE : souder deux types ferait disparaître une
    // couleur — donc une section — sans que rien ne le dise.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);
    right->wireType = QStringLiteral("commande");

    CHECK_FALSE(ComponentTools::healOnRemoval(*folio, project.library, *symbol).has_value());
}

TEST_CASE("Effacer tout un départ ne recoud rien", "[commands][component][heal]")
{
    // Effacer l'appareil ET ses fils dans le même geste : recoudre des fils
    // qu'on est en train d'effacer produirait un fil ressuscité.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);

    const QSet<QString> partent = { symbol->id(), left->id(), right->id() };
    CHECK_FALSE(ComponentTools::healOnRemoval(*folio, project.library, *symbol, partent)
                        .has_value());
}

namespace {

// Un second appareil deux bornes, aux broches ailleurs : c'est ce décalage
// qui rend le test lisible — si les fils ne suivent pas, ils restent où ils
// étaient et l'appareil est débranché en silence.
SymbolDefinition otherTwoPinDevice(const QString &logicalId, double dx,
                                   const QString &firstNumber = QStringLiteral("1"),
                                   const QString &secondNumber = QStringLiteral("2"))
{
    SymbolDefinition def = twoPinDevice(logicalId, QStringLiteral("K"));
    def.pins[0].position = QPointF(-dx, 0.0);
    def.pins[0].number = firstNumber;
    def.pins[1].position = QPointF(dx, 0.0);
    def.pins[1].number = secondNumber;
    return def;
}

} // namespace

TEST_CASE("Remplacer un symbole fait suivre les fils", "[commands][component][swap]")
{
    // Sans ce geste il faut effacer, reposer, retaper le repère et refaire les
    // fils — et c'est en refaisant les fils qu'on débranche un circuit sans
    // le voir. Les extrémités doivent donc suivre les broches neuves.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);
    project.library.insert(otherTwoPinDevice(QStringLiteral("autre"), 8.0));

    const auto plan = ComponentTools::planSwap(*folio, project.library, *symbol,
                                               QStringLiteral("iec:autre"));
    REQUIRE(plan.valid);
    CHECK(plan.orphaned == 0);
    REQUIRE(plan.moves.size() == 2);

    // Broches à ±8 au lieu de ±5 : les deux extrémités se déplacent de 3 mm.
    QSet<double> cibles;
    for (const auto &move : plan.moves)
        cibles.insert(move.to.x());
    CHECK(cibles.contains(92.0));
    CHECK(cibles.contains(108.0));
}

TEST_CASE("Les broches s'apparient par numéro, pas par distance",
          "[commands][component][swap]")
{
    // Un 13/14 reste un 13/14 quel que soit le dessin du symbole : c'est le
    // seul appariement qui ait un sens électrique. Ici les numéros sont
    // inversés dans l'espace — apparier par distance laisserait chaque fil sur
    // place et raccorderait la borne 1 sur la 2 sans que rien ne le dise.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);
    project.library.insert(otherTwoPinDevice(QStringLiteral("inverse"), 5.0,
                                             QStringLiteral("2"), QStringLiteral("1")));

    const auto plan = ComponentTools::planSwap(*folio, project.library, *symbol,
                                               QStringLiteral("iec:inverse"));
    REQUIRE(plan.valid);
    REQUIRE(plan.moves.size() == 2);
    // Le fil de gauche tenait à la borne « 1 », qui est maintenant à droite.
    for (const auto &move : plan.moves) {
        if (move.wireId == left->id())
            CHECK(move.to.x() == 105.0);
        else
            CHECK(move.to.x() == 95.0);
    }
}

TEST_CASE("Une extrémité sans broche correspondante est comptée",
          "[commands][component][swap]")
{
    // Un fil qu'aucune broche neuve ne reprend reste où il est. Le compter
    // permet de le DIRE : un fil en l'air ne se voit pas sur le tracé, il se
    // découvre au câblage.
    SymbolInstance *symbol = nullptr;
    Wire *left = nullptr;
    Wire *right = nullptr;
    Project project = wiredDevice(&symbol, &left, &right);
    Folio *folio = project.folioAt(0);

    SymbolDefinition seule = twoPinDevice(QStringLiteral("borne"), QStringLiteral("X"));
    seule.pins.remove(1); // une seule borne
    project.library.insert(seule);

    const auto plan = ComponentTools::planSwap(*folio, project.library, *symbol,
                                               QStringLiteral("iec:borne"));
    REQUIRE(plan.valid);
    CHECK(plan.orphaned == 1);
}
