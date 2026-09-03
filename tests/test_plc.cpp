#include <catch2/catch_test_macros.hpp>

#include "core/entities.h"
#include "core/netlist.h"
#include "io/dsnfile.h"
#include "rules/plc.h"
#include "rules/reports.h"
#include "testhelpers.h"

#include <QTemporaryDir>

using namespace dsn;
using namespace test;

namespace {

Project onePage()
{
    Project project;
    Folio *folio = project.addFolio(QStringLiteral("Automate"));
    folio->number = QStringLiteral("1");
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    folio->frame.columns = 10;
    folio->frame.rows = 6;
    return project;
}

// Pose un module d'automate comme le fait la boite d'insertion : la
// definition engendree entre dans la bibliotheque du projet, puis l'instance
// la reference. C'est tout le mecanisme, et c'est ce qui rend le module
// ordinaire pour le reste du logiciel.
SymbolInstance *placeModule(Project &project, Folio *folio, const PlcModuleDef &def, int rack,
                            int slot, int firstPoint, const QPointF &at)
{
    auto symbol = std::make_unique<SymbolInstance>();
    PlcModule::configure(*symbol, def, 0, rack, slot, firstPoint);
    project.library.insert(PlcModule::buildSymbol(def, PlcModule::points(*symbol, PlcDatabase::builtin())));
    symbol->definitionId = PlcModule::symbolId(def);
    symbol->placement.position = at;
    auto *raw = symbol.get();
    folio->addEntity(std::move(symbol));
    return raw;
}

} // namespace

TEST_CASE("La base d'automates est livree avec le logiciel", "[plc]")
{
    // Sans base embarquee, poser un automate exigerait d'abord d'installer un
    // fichier : le logiciel doit savoir le faire au premier lancement.
    const PlcDatabase database = PlcDatabase::builtin();
    CHECK(database.count() > 10);
    CHECK(database.manufacturers().size() >= 4);

    // Chaque module doit dire combien de points il porte et comment les
    // adresser : un module sans format d'adressage ne sert a rien.
    for (const PlcModuleDef &module : database.modules()) {
        CAPTURE(module.id);
        CHECK(module.points > 0);
        CHECK_FALSE(module.addressFormat.isEmpty());
        CHECK_FALSE(module.manufacturer.isEmpty());
    }
}

TEST_CASE("L'adressage suit le format du constructeur", "[plc]")
{
    // Ce sont les formats reels. Les reproduire exactement est ce qui separe
    // un dessin d'automate d'un dessin qui sert a programmer.

    // Allen-Bradley SLC : emplacement/point, le point sur deux chiffres.
    CHECK(PlcAddress::format(QStringLiteral("I:%S/%2P"), 0, 0, 3, 0) == QLatin1String("I:3/00"));
    CHECK(PlcAddress::format(QStringLiteral("I:%S/%2P"), 0, 0, 3, 12) == QLatin1String("I:3/12"));

    // Allen-Bradley ControlLogix : le point sans remplissage.
    CHECK(PlcAddress::format(QStringLiteral("Local:%S:I.Data.%P"), 0, 0, 4, 7)
          == QLatin1String("Local:4:I.Data.7"));

    // Siemens : octet.bit, groupe par huit. Le point 9 est donc le bit 1 de
    // l'octet 1 — c'est ce groupement qui fait toute la difference.
    CHECK(PlcAddress::format(QStringLiteral("%%I%B.%b"), 0, 0, 0, 0, 8) == QLatin1String("%I0.0"));
    CHECK(PlcAddress::format(QStringLiteral("%%I%B.%b"), 0, 0, 0, 7, 8) == QLatin1String("%I0.7"));
    CHECK(PlcAddress::format(QStringLiteral("%%I%B.%b"), 0, 0, 0, 8, 8) == QLatin1String("%I1.0"));
    CHECK(PlcAddress::format(QStringLiteral("%%I%B.%b"), 0, 0, 0, 9, 8) == QLatin1String("%I1.1"));

    // Omron groupe par seize, avec le bit sur deux chiffres.
    CHECK(PlcAddress::format(QStringLiteral("%B.%2b"), 0, 0, 0, 17, 16) == QLatin1String("1.01"));

    // Schneider : rack.emplacement.point.
    CHECK(PlcAddress::format(QStringLiteral("%%I%R.%S.%P"), 0, 0, 2, 5) == QLatin1String("%I0.2.5"));

    // Sans groupement, %B reste a zero et %b vaut le rang : c'est le cas des
    // constructeurs qui numerotent leurs points a la file.
    CHECK(PlcAddress::format(QStringLiteral("%B.%b"), 0, 0, 0, 12) == QLatin1String("0.12"));

    // Un jeton inconnu est recopie tel quel plutot que de disparaitre : une
    // adresse fausse et silencieuse serait pire qu'un format visiblement
    // incomplet.
    CHECK(PlcAddress::format(QStringLiteral("%Z%P"), 0, 0, 0, 3) == QLatin1String("%Z3"));
}

TEST_CASE("L'adresse porte le noeud quand le format le demande", "[plc][noeud]")
{
    // « %N04R07S07C016 » : noeud, chassis, emplacement, canal. Le noeud est
    // ecrit en toutes lettres a cote de chaque carte sur les planches reelles,
    // et pour une raison : deux cartes de deux automates portent le meme rack
    // et le meme emplacement. Sans le noeud, deux adresses identiques
    // designent deux bornes differentes — et c'est le cableur qui le
    // decouvre, une fois le fil tire.
    const QString format = QStringLiteral("%%N%2NR%2RS%2SC%3P");
    CHECK(PlcAddress::format(format, 4, 7, 7, 16) == QLatin1String("%N04R07S07C016"));
    CHECK(PlcAddress::format(format, 4, 7, 7, 17) == QLatin1String("%N04R07S07C017"));

    // Un format sans %N n'ecrit pas le noeud, meme quand l'automate en a un :
    // le format du constructeur reste maitre de ce qui s'affiche.
    CHECK(PlcAddress::format(QStringLiteral("%%R%2RS%2SC%3P"), 4, 4, 6, 1)
          == QLatin1String("%R04S06C001"));
}

TEST_CASE("Le noeud se pose sur le module et readresse ses points", "[plc][noeud]")
{
    // Le noeud suit la meme regle que le rack et l'emplacement : il est range
    // dans les champs de l'instance, et l'adresse se RECALCULE. Changer de
    // noeud readresse les seize points d'un coup, sans risque d'en oublier un.
    const PlcDatabase database = PlcDatabase::builtin();
    const PlcModuleDef *def = database.find(QStringLiteral("generique:noeud-e-ana-16"));
    REQUIRE(def);

    Project project = onePage();
    Folio *folio = project.folioAt(0);
    auto *module = placeModule(project, folio, *def, 7, 7, 0, QPointF(80, 80));
    module->fields.insert(PlcModule::nodeKey(), QStringLiteral("4"));
    CHECK(PlcModule::node(*module) == 4);

    const QVector<PlcPoint> points = PlcModule::points(*module, database);
    REQUIRE(points.size() == 16);
    CHECK(points.first().address == QLatin1String("%N04R07S07C000"));
    CHECK(points.last().address == QLatin1String("%N04R07S07C015"));

    // Le module part sur un autre noeud : rien n'est stocke, tout suit.
    module->fields.insert(PlcModule::nodeKey(), QStringLiteral("12"));
    CHECK(PlcModule::points(*module, database).first().address
          == QLatin1String("%N12R07S07C000"));
}

TEST_CASE("Un module pose adresse ses points depuis son emplacement", "[plc]")
{
    const PlcDatabase database = PlcDatabase::builtin();
    const PlcModuleDef *def = database.find(QStringLiteral("siemens:6ES7321-1BH02"));
    REQUIRE(def);

    Project project = onePage();
    Folio *folio = project.folios().front();
    SymbolInstance *module = placeModule(project, folio, *def, 0, 0, 0, QPointF(80, 100));

    const QVector<PlcPoint> points = PlcModule::points(*module, database);
    REQUIRE(points.size() == 16);
    CHECK(points.first().address == QLatin1String("%I0.0"));
    CHECK(points.at(8).address == QLatin1String("%I1.0"));
    CHECK(points.last().address == QLatin1String("%I1.7"));

    // Deplacer le module dans l'espace d'adressage readresse tous ses points
    // d'un coup : l'adresse n'est pas stockee, elle se deduit. C'est ce qui
    // evite d'en oublier un lorsqu'on insere une carte en amont.
    module->fields.insert(PlcModule::firstPointKey(), QStringLiteral("16"));
    const QVector<PlcPoint> moved = PlcModule::points(*module, database);
    CHECK(moved.first().address == QLatin1String("%I2.0"));
    CHECK(moved.last().address == QLatin1String("%I3.7"));

    // Le repere de borne, lui, est serigraphie sur la carte : il ne bouge pas.
    CHECK(moved.first().terminal == QLatin1String("00"));
    CHECK(moved.last().terminal == QLatin1String("15"));
}

TEST_CASE("Le symbole d'un module a une broche par point", "[plc]")
{
    const PlcDatabase database = PlcDatabase::builtin();
    const PlcModuleDef *input = database.find(QStringLiteral("ab:1746-IA16"));
    const PlcModuleDef *output = database.find(QStringLiteral("ab:1746-OW16"));
    REQUIRE(input);
    REQUIRE(output);

    Project project = onePage();
    Folio *folio = project.folios().front();
    SymbolInstance *symbol = placeModule(project, folio, *input, 0, 3, 0, QPointF(80, 100));

    const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
    REQUIRE(definition);
    CHECK(definition->pins.size() == 16);
    // Sans broches, aucun potentiel ne peut etre calcule : un module qui ne
    // se cable pas n'est qu'un dessin.
    CHECK(definition->pin(QStringLiteral("00")));
    CHECK(definition->pin(QStringLiteral("15")));

    // Le corps grandit avec le nombre de points : une carte de quatre voies
    // n'est pas le meme dessin qu'une carte de trente-deux.
    const PlcModuleDef *analog = database.find(QStringLiteral("ab:1746-NI4"));
    REQUIRE(analog);
    SymbolInstance probe;
    PlcModule::configure(probe, *analog, 0, 0, 1, 0);
    const SymbolDefinition small =
            PlcModule::buildSymbol(*analog, PlcModule::points(probe, database));
    CHECK(small.bounds().height() < definition->bounds().height());

    // Les entrees se cablent a gauche, les sorties a droite : c'est le sens
    // de lecture d'un folio, de l'amont vers l'aval.
    SymbolInstance out;
    PlcModule::configure(out, *output, 0, 0, 4, 0);
    const SymbolDefinition outDef =
            PlcModule::buildSymbol(*output, PlcModule::points(out, database));
    CHECK(definition->pins.first().direction == Direction::Left);
    CHECK(outDef.pins.first().direction == Direction::Right);
}

TEST_CASE("Le rapport d'entrees-sorties dit a quoi chaque point est raccorde", "[plc][rapport]")
{
    const PlcDatabase database = PlcDatabase::builtin();
    const PlcModuleDef *def = database.find(QStringLiteral("ab:1746-IA16"));
    REQUIRE(def);

    Project project = onePage();
    Folio *folio = project.folios().front();
    project.library.insert(twoPinDevice());

    SymbolInstance *module = placeModule(project, folio, *def, 0, 3, 0, QPointF(60, 60));
    module->setDesignation(QStringLiteral("-A1"));
    PlcModule::setDescription(*module, 0, QStringLiteral("Marche pompe P1"));

    // Un bouton cable sur le premier point du module.
    const SymbolDefinition *definition = project.library.definition(module->definitionId);
    REQUIRE(definition);
    const Pin *first = definition->pin(QStringLiteral("00"));
    REQUIRE(first);
    const QPointF pinPoint = module->placement.position + first->position;

    auto *button = placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(20, 60));
    button->setDesignation(QStringLiteral("-S1"));
    const SymbolDefinition *buttonDef = project.library.definition(button->definitionId);
    REQUIRE(buttonDef);
    const QPointF buttonPin = button->placement.position + buttonDef->pins.at(1).position;
    // Le fil monte a la hauteur du point puis l'aborde par la gauche. Passer
    // d'abord a l'aplomb du module croiserait les broches intermediaires — et
    // un fil qui traverse une broche s'y raccorde, ce qui est correct.
    drawWire(folio, { buttonPin, QPointF(buttonPin.x(), pinPoint.y()), pinPoint });

    const Netlist netlist = Netlist::build(project);
    const QVector<PlcIoLine> lines = Reports::plcIoList(project, netlist, database);

    // Seize points, seize lignes : un point non cable reste au rapport, avec
    // ses colonnes vides — c'est justement ce qu'on veut voir.
    REQUIRE(lines.size() == 16);
    CHECK(lines.first().address == QLatin1String("I:3/00"));
    CHECK(lines.first().description == QLatin1String("Marche pompe P1"));
    CHECK(lines.first().target == QLatin1String("-S1"));
    CHECK(lines.first().designation == QLatin1String("-A1"));
    CHECK(lines.at(1).target.isEmpty());

    const ReportTable table = Reports::toTable(lines);
    CHECK(table.rowCount() == 16);
    CHECK(table.headers.contains(QStringLiteral("Adresse")));
}

TEST_CASE("Un module survit a l'enregistrement du projet", "[plc][io]")
{
    // La definition engendree voyage dans le fichier avec le reste de la
    // bibliotheque : sans cela, un dossier rouvert sur un autre poste
    // perdrait le dessin de ses cartes.
    const PlcDatabase database = PlcDatabase::builtin();
    const PlcModuleDef *def = database.find(QStringLiteral("schneider:BMXDDI1602"));
    REQUIRE(def);

    Project project = onePage();
    Folio *folio = project.folios().front();
    SymbolInstance *module = placeModule(project, folio, *def, 0, 2, 0, QPointF(90, 90));
    module->setDesignation(QStringLiteral("-A2"));
    PlcModule::setDescription(*module, 3, QStringLiteral("Défaut variateur"));

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("automate.arcus"));
    REQUIRE(DsnFile::save(path, project));

    Project reloaded;
    REQUIRE(DsnFile::load(path, reloaded).ok);
    const Folio *back = reloaded.folios().front();
    const auto modules = back->entitiesOfType<SymbolInstance>();
    REQUIRE(modules.size() == 1);

    CHECK(PlcModule::isModule(*modules.front()));
    CHECK(PlcModule::moduleId(*modules.front()) == def->id);
    CHECK(PlcModule::description(*modules.front(), 3) == QStringLiteral("Défaut variateur"));
    CHECK(reloaded.library.definition(PlcModule::symbolId(*def)));

    const QVector<PlcPoint> points = PlcModule::points(*modules.front(), database);
    REQUIRE(points.size() == 16);
    CHECK(points.at(5).address == QLatin1String("%I0.2.5"));
}

TEST_CASE("Le titre d'une carte tient dans son bandeau", "[plc]")
{
    // Les references constructeur vont jusqu'a vingt caracteres. Ecrites a
    // taille fixe, elles debordent de la carte et se superposent aux fils
    // voisins. La hauteur du texte se reduit donc pour tenir — c'est la seule
    // facon de garder le meme dessin pour toutes les references.
    const PlcDatabase database = PlcDatabase::builtin();

    auto titleHeight = [](const SymbolDefinition &symbol) {
        for (const Primitive &graphic : symbol.graphics) {
            if (graphic.kind == Primitive::Kind::Text && graphic.text == symbol.name)
                return graphic.textHeight;
        }
        return 0.0;
    };

    for (const PlcModuleDef &def : database.modules()) {
        SymbolInstance probe;
        PlcModule::configure(probe, def, 0, 0, 0, 0);
        const SymbolDefinition symbol =
                PlcModule::buildSymbol(def, PlcModule::points(probe, database));
        CAPTURE(def.id, symbol.name);

        const double height = titleHeight(symbol);
        REQUIRE(height > 0.0);
        // Largeur estimee du titre contre la largeur du corps. Le corps est
        // le rectangle du dessin : c'est lui qui doit contenir le texte.
        const double bodyWidth = symbol.bodyBounds().width();
        CHECK(symbol.name.size() * height * 0.85 <= bodyWidth);
        // Mais elle ne descend jamais au point d'etre illisible sur papier.
        CHECK(height >= 1.2);
    }
}
