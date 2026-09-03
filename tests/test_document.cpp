#include <catch2/catch_test_macros.hpp>

#include "core/netlist.h"
#include "core/titleblock.h"
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

TEST_CASE("Un folio copié garde ses tables de cartouche", "[document][cartouche]")
{
    // La copie d'un folio est écrite à la main — les entités se clonent une à
    // une. Un champ ajouté au folio et oublié là ne casse rien visiblement :
    // il se perd à la première copie (une annulation, un collage, un aperçu),
    // et on cherche longtemps pourquoi. Payé sur les tables du cartouche.
    Folio source;
    source.number = QStringLiteral("3");
    source.tables.insert(QStringLiteral("revisions"),
                         { { QStringLiteral("0"), QStringLiteral("2026-09-03"),
                             QStringLiteral("CONCEPTION INITIALE") } });

    const Folio copie(source);
    CHECK(copie.tables.value(QStringLiteral("revisions")).size() == 1);

    Folio affecte;
    affecte = source;
    REQUIRE(affecte.tables.value(QStringLiteral("revisions")).size() == 1);
    CHECK(affecte.tables.value(QStringLiteral("revisions")).first().at(2)
          == QStringLiteral("CONCEPTION INITIALE"));
}

TEST_CASE("Le cartouche traverse le fichier, images comprises",
          "[document][cartouche][io]")
{
    // Le gabarit voyage avec le dossier, comme la bibliothèque et les types
    // de fils : un dossier rouvert ailleurs garde SON cartouche, même si le
    // poste ne connaît pas le gabarit du bureau qui l'a tiré. Et le logo est
    // embarqué, pas pointé sur le disque — une image référencée disparaît au
    // premier changement de poste, sans que personne ne le remarque.
    Project source;
    source.titleBlock = TitleBlock::loopSheet();
    TitleBlockCell maison;
    maison.kind = TitleBlockCell::Kind::Field;
    maison.rect = QRectF(10, 10, 30, 6);
    maison.label = QStringLiteral("Atelier");
    maison.key = QStringLiteral("atelier");
    source.titleBlock.cells.append(maison);
    source.images.insert(QStringLiteral("logo"), QByteArray("\x89PNG-faux-logo"));
    Folio *folio = source.addFolio(QStringLiteral("Essai"));
    folio->tables.insert(QStringLiteral("revisions"),
                         { { QStringLiteral("0"), {}, QStringLiteral("2026-09-03"),
                             QStringLiteral("ÉMISSION") } });

    Project relu;
    REQUIRE(relu.readJson(source.toJson()));
    CHECK(relu.titleBlock.id == QStringLiteral("boucle"));
    CHECK(relu.titleBlock.cells.size() == source.titleBlock.cells.size());
    CHECK(relu.titleBlock.cells.last().label == QStringLiteral("Atelier"));
    CHECK(relu.images.value(QStringLiteral("logo")) == QByteArray("\x89PNG-faux-logo"));
    REQUIRE(relu.folioCount() == 1);
    CHECK(relu.folioAt(0)->tables.value(QStringLiteral("revisions")).size() == 1);
}

TEST_CASE("Le champ du folio l'emporte sur celui du projet", "[document][cartouche]")
{
    // Un champ posé sur une planche est plus précis que le réglage du
    // dossier : c'est la planche qu'on a sous les yeux quand on le saisit.
    Project project;
    project.info.author = QStringLiteral("Bureau");
    project.info.extra.insert(QStringLiteral("sector"), QStringLiteral("000"));
    Folio *folio = project.addFolio(QStringLiteral("Commande"));
    folio->titleBlock.insert(QStringLiteral("sector"), QStringLiteral("039"));

    const QMap<QString, QString> v = TitleBlock::values(project, *folio);
    CHECK(v.value(QStringLiteral("sector")) == QStringLiteral("039"));
    CHECK(v.value(QStringLiteral("author")) == QStringLiteral("Bureau"));
    // Une clef inconnue n'a pas de valeur — surtout pas son propre nom.
    CHECK(v.value(QStringLiteral("nexistePas")).isEmpty());
    CHECK_FALSE(v.contains(QStringLiteral("nexistePas")));
}

TEST_CASE("Une bande de localisation dit où se trouve ce qu'elle contient",
          "[folio][bandes]")
{
    // C'est tout l'intérêt de la bande : elle n'est pas décorative, elle est
    // la LOCALISATION de ce qu'elle contient — donc les colonnes « de » et
    // « vers » du rapport de câblage. L'appartenance se déduit de l'abscisse,
    // comme la zone : rien n'est stocké sur l'entité, donc rien ne peut se
    // désynchroniser quand on la déplace.
    Folio folio;
    folio.sheet = sheetFormatById(QStringLiteral("A3"));
    folio.bands = { { QStringLiteral("CHAMP"), 200.0 },
                    { QStringLiteral("CABINET 037BJ0151"), 100.0 } };

    const QRectF fr = folio.frameRect();
    CHECK(folio.bandAt(fr.topLeft() + QPointF(50, 50)) == QStringLiteral("CHAMP"));
    CHECK(folio.bandAt(fr.topLeft() + QPointF(250, 50))
          == QStringLiteral("CABINET 037BJ0151"));
    // La dernière s'étire jusqu'au bord : sans cela un changement de format
    // laisserait une lisière sans nom, et une entité posée dedans n'aurait
    // pas de localisation.
    CHECK(folio.bandAt(QPointF(fr.right() - 1.0, fr.center().y()))
          == QStringLiteral("CABINET 037BJ0151"));
    CHECK(folio.bandAt(QPointF(fr.right() + 20.0, fr.center().y())).isEmpty());
    CHECK(folio.bandRect(0).width() == 200.0);
}

TEST_CASE("Le sens du repérage change les renvois, pas seulement l'affichage",
          "[folio][bandes]")
{
    // Deux bureaux d'études à seize ans d'écart numérotent de droite à gauche
    // et de bas en haut, la zone A du côté du cartouche. Tous les renvois du
    // dossier en dépendent : une planche reproduite avec l'autre sens renvoie
    // vers la mauvaise case, et rien ne le signale.
    Folio folio;
    folio.sheet = sheetFormatById(QStringLiteral("A3"));
    folio.frame.columns = 10;
    folio.frame.rows = 6;

    const QRectF fr = folio.frameRect();
    const QPointF hautGauche = fr.topLeft() + QPointF(fr.width() * 0.05, fr.height() * 0.08);
    CHECK(folio.zoneAt(hautGauche) == QStringLiteral("A1"));

    folio.frame.columnsRightToLeft = true;
    folio.frame.rowsBottomToTop = true;
    // Le même point est maintenant en F10 : dernière colonne, dernière ligne.
    CHECK(folio.zoneAt(hautGauche) == QStringLiteral("F10"));
}
