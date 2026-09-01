#include <catch2/catch_test_macros.hpp>

#include "core/netlist.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

TEST_CASE("Un folio decoupe sa feuille en zones de reperage", "[folio]")
{
    Folio folio;
    folio.sheet = sheetFormatById(QStringLiteral("A3"));
    folio.frame.columns = 10;
    folio.frame.rows = 6;

    const QRectF frame = folio.frameRect();
    CHECK(frame.left() == folio.frame.bindingMargin);
    CHECK(frame.width() > 0.0);

    // Le premier point du cadre est en zone A1, le dernier en F10.
    CHECK(folio.zoneAt(frame.topLeft() + QPointF(1, 1)) == QLatin1String("A1"));
    CHECK(folio.zoneAt(frame.bottomRight() - QPointF(1, 1)) == QLatin1String("F10"));
    // Hors cadre : pas de zone plutot qu'une zone fausse.
    CHECK(folio.zoneAt(QPointF(-10, -10)).isEmpty());
}

TEST_CASE("Le cartouche se loge dans le coin bas droit du cadre", "[folio]")
{
    Folio folio;
    const QRectF frame = folio.frameRect();
    const QRectF block = folio.titleBlockRect();
    CHECK(block.right() == frame.right());
    CHECK(block.bottom() == frame.bottom());
    CHECK(frame.contains(block));
}

TEST_CASE("Le remplacement d'une entite garde son rang de trace", "[folio]")
{
    Folio folio;
    auto *first = drawWire(&folio, { QPointF(0, 0), QPointF(10, 0) });
    drawWire(&folio, { QPointF(0, 10), QPointF(10, 10) });
    const QString id = first->id();

    auto replacement = std::make_unique<Wire>();
    replacement->setId(id);
    replacement->points = { QPointF(0, 0), QPointF(50, 0) };
    REQUIRE(folio.replaceEntity(std::move(replacement)));

    CHECK(folio.indexOfEntity(id) == 0);
    CHECK(folio.entityCount() == 2);
    CHECK(dynamic_cast<Wire *>(folio.entity(id))->length() == 50.0);
}

TEST_CASE("Le projet retrouve une entite a travers ses folios", "[project]")
{
    Project project;
    Folio *f1 = project.addFolio(QStringLiteral("Alimentation"));
    Folio *f2 = project.addFolio(QStringLiteral("Commande"));
    Wire *wire = drawWire(f2, { QPointF(0, 0), QPointF(10, 0) });

    Folio *owner = nullptr;
    CHECK(project.findEntity(wire->id(), &owner) == wire);
    CHECK(owner == f2);
    CHECK(project.findEntity(QStringLiteral("inexistant"), &owner) == nullptr);
    CHECK(owner == nullptr);
    CHECK(f1->entityCount() == 0);
}

TEST_CASE("La renumerotation respecte les numeros voulus", "[project]")
{
    Project project;
    project.addFolio();
    Folio *f2 = project.addFolio();
    project.addFolio();
    // Un repere non numerique a ete saisi volontairement : on n'y touche pas.
    f2->number = QStringLiteral("=A1+B2/3");

    project.renumberFolios();
    CHECK(project.folioAt(0)->number == QLatin1String("1"));
    CHECK(project.folioAt(1)->number == QLatin1String("=A1+B2/3"));
    CHECK(project.folioAt(2)->number == QLatin1String("3"));
}

TEST_CASE("Le deplacement d'un folio conserve les autres", "[project]")
{
    Project project;
    Folio *a = project.addFolio(QStringLiteral("A"));
    Folio *b = project.addFolio(QStringLiteral("B"));
    Folio *c = project.addFolio(QStringLiteral("C"));

    REQUIRE(project.moveFolio(0, 2));
    CHECK(project.folioAt(0) == b);
    CHECK(project.folioAt(1) == c);
    CHECK(project.folioAt(2) == a);
    CHECK_FALSE(project.moveFolio(0, 9));
}

TEST_CASE("Un projet survit a l'aller-retour JSON", "[project][io]")
{
    Project source;
    source.info.title = QStringLiteral("Armoire de pompage");
    source.info.client = QStringLiteral("Ville de Sainte-Foy");
    source.info.reference = QStringLiteral("2026-014");
    source.profileId = QStringLiteral("iec");
    source.library.insert(twoPinDevice());

    Folio *folio = source.addFolio(QStringLiteral("Puissance"));
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    placeSymbol(source, folio, QStringLiteral("iec:device"), QPointF(60, 40),
                QStringLiteral("-K1"));
    drawWire(folio, { QPointF(65, 40), QPointF(100, 40) },
             { QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3") });

    Project restored;
    restored.library = source.library; // la bibliotheque est embarquee par le module io
    REQUIRE(restored.readJson(source.toJson()));

    CHECK(restored.info.title == source.info.title);
    CHECK(restored.info.client == source.info.client);
    CHECK(restored.folioCount() == 1);
    REQUIRE(restored.folioAt(0));
    CHECK(restored.folioAt(0)->title == QLatin1String("Puissance"));
    CHECK(restored.folioAt(0)->entityCount() == 2);
    CHECK(restored.resolveSymbolBounds() == 1);
    CHECK(restored.missingDefinitions().isEmpty());
}

TEST_CASE("Un document d'une version ulterieure est refuse net", "[project][io]")
{
    Project project;
    QJsonObject json = project.toJson();
    json[QStringLiteral("version")] = Project::kFormatVersion + 1;
    // Mieux vaut un refus clair qu'un document silencieusement tronque.
    CHECK_FALSE(project.readJson(json));
}

TEST_CASE("Une definition absente est signalee sans bloquer le chargement", "[project]")
{
    Project project;
    Folio *folio = project.addFolio();
    placeSymbol(project, folio, QStringLiteral("iec:fantome"), QPointF(10, 10));

    CHECK(project.missingDefinitions() == QStringList{ QStringLiteral("iec:fantome") });
    CHECK(project.resolveSymbolBounds() == 0);
}

TEST_CASE("La bibliotheque resout un symbole dans l'autre norme", "[library]")
{
    SymbolLibrary library;
    SymbolDefinition iec = twoPinDevice(QStringLiteral("contact-no"));
    library.insert(iec);

    SymbolDefinition ansi = iec;
    ansi.norm = QStringLiteral("ANSI");
    ansi.id = SymbolDefinition::makeId(ansi.norm, ansi.logicalId);
    ansi.name = QStringLiteral("Normally open contact");
    library.insert(ansi);

    REQUIRE(library.resolve(QStringLiteral("contact-no"), QStringLiteral("ANSI")));
    CHECK(library.resolve(QStringLiteral("contact-no"), QStringLiteral("ANSI"))->name
          == QLatin1String("Normally open contact"));

    // Bascule de norme depuis un identifiant complet.
    const SymbolDefinition *swapped =
            library.counterpart(QStringLiteral("iec:contact-no"), QStringLiteral("ANSI"));
    REQUIRE(swapped);
    CHECK(swapped->norm == QLatin1String("ANSI"));
}

TEST_CASE("Une norme absente se rabat sur ce qui existe", "[library]")
{
    SymbolLibrary library;
    library.insert(twoPinDevice(QStringLiteral("fuse")));
    // Un projet ANSI doit rester ouvrable meme si le symbole n'existe qu'en CEI.
    const SymbolDefinition *fallback =
            library.resolve(QStringLiteral("fuse"), QStringLiteral("ANSI"));
    REQUIRE(fallback);
    CHECK(fallback->norm == QLatin1String("IEC"));
}

TEST_CASE("La recherche dans la bibliotheque couvre nom, categorie et mots-cles", "[library]")
{
    SymbolLibrary library;
    SymbolDefinition def = twoPinDevice(QStringLiteral("contactor-coil"));
    def.name = QStringLiteral("Bobine de contacteur");
    def.category = QStringLiteral("Commande");
    def.keywords = { QStringLiteral("relais") };
    library.insert(def);

    CHECK(library.search(QStringLiteral("bobine")).size() == 1);
    CHECK(library.search(QStringLiteral("relais")).size() == 1);
    CHECK(library.search(QStringLiteral("Commande")).size() == 1);
    CHECK(library.search(QStringLiteral("transformateur")).isEmpty());
    CHECK(library.categories() == QStringList{ QStringLiteral("Commande") });
}
