#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QApplication>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QLineEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QAbstractButton>
#include <QToolButton>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QFontMetricsF>
#include <QHash>
#include <QSet>
#include <QDockWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QTextBlock>
#include <QFrame>
#include <QTemporaryDir>

#include <cmath>

#include "core/documentcommands.h"
#include "symbols/librarystore.h"
#include "ui/dockrail.h"
#include "ui/findreplacedialog.h"
#include "ui/titleblockeditor.h"
#include "ui/pagesetupdialog.h"
#include "core/titleblock.h"
#include "ui/docktitle.h"
#include "ui/document.h"
#include "ui/folioview.h"
#include "ui/symboleditor.h"
#include "ui/appearance.h"
#include "ui/commandline.h"
#include "ui/commandpalette.h"
#include "ui/componentdialog.h"
#include "ui/mainwindow.h"
#include "ui/draftingsettingsdialog.h"
#include "ui/pagesetupdialog.h"
#include "ui/reportpanel.h"
#include "ui/ribbon.h"
#include "ui/startpage.h"
#include "ui/surferdialog.h"
#include "ui/terminalstripdialog.h"
#include "ui/symbolpalette.h"
#include "ui/theme.h"
#include "rules/numbering.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;
using Catch::Matchers::WithinAbs;

namespace {

SymbolLibrary builtinLibrary()
{
    SymbolLibrary library;
    LibraryStore::loadBuiltin(library);
    return library;
}

// Designer un point a la souris. Defini plus bas, declare ici : les tests de
// commandes en ont besoin avant, et un namespace anonyme rouvert reste le
// meme namespace.
void clickScene(FolioView &view, const QPointF &scene);

// Une capture non vide prouve que le widget s'est reellement peint, pas
// seulement qu'il s'est construit sans planter.
bool hasVisibleContent(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return false;
    const QImage image = pixmap.toImage();
    const QRgb first = image.pixel(0, 0);
    for (int y = 0; y < image.height(); y += 3) {
        for (int x = 0; x < image.width(); x += 3) {
            if (image.pixel(x, y) != first)
                return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("Le document part sur un projet neuf utilisable", "[ui][document]")
{
    Document document;
    document.newProject(builtinLibrary());

    CHECK(document.folioCount() == 1);
    REQUIRE(document.currentFolio());
    CHECK_FALSE(document.isModified());
    CHECK(document.project().library.count() > 60);
    // Une feuille par defaut est indispensable : sans elle le canevas n'a
    // rien a peindre au premier lancement.
    CHECK(document.currentFolio()->sheet.width > 100.0);
}

TEST_CASE("Le document signale ses modifications et son etat propre", "[ui][document]")
{
    Document document;
    document.newProject(builtinLibrary());

    int changes = 0;
    QObject::connect(&document, &Document::changed, [&changes] { ++changes; });

    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(20, 20), QPointF(80, 20) };
    document.push(std::make_unique<AddEntityCommand>(document.project(),
                                                     document.currentFolio()->id(),
                                                     std::move(wire)));
    CHECK(changes == 1);
    CHECK(document.isModified());

    document.undo();
    CHECK_FALSE(document.isModified());
}

TEST_CASE("La netlist du document se recalcule apres modification", "[ui][document]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    CHECK(document.netlist().netCount() == 0);

    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(20, 20), QPointF(80, 20) };
    document.push(std::make_unique<AddEntityCommand>(document.project(), folio->id(),
                                                     std::move(wire)));
    // Une netlist restee en cache donnerait des rapports faux sans qu'aucune
    // erreur ne se declare.
    CHECK(document.netlist().netCount() == 1);
}

TEST_CASE("Un projet fait l'aller-retour par le document", "[ui][document]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    Document source;
    source.newProject(builtinLibrary());
    source.project().info.title = QStringLiteral("Essai d'interface");
    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(20, 20), QPointF(80, 20) };
    source.push(std::make_unique<AddEntityCommand>(source.project(),
                                                   source.currentFolio()->id(),
                                                   std::move(wire)));

    const QString path = dir.filePath(QStringLiteral("essai.dsn"));
    QString error;
    REQUIRE(source.save(path, &error));
    CHECK(error.isEmpty());
    CHECK_FALSE(source.isModified());

    Document restored;
    restored.newProject(builtinLibrary());
    REQUIRE(restored.load(path, &error));
    CHECK(restored.project().info.title == QStringLiteral("Essai d'interface"));
    CHECK(restored.currentFolio()->entityCount() == 1);
}

namespace {

// Un clic reel, aux pixels que la vue attend : les tests d'outil doivent
// passer par le meme chemin que la main, sinon ils prouvent que la fonction
// interne marche et rien de plus.
void clickAt(FolioView &view, const QPointF &mm)
{
    const QPointF p = view.mapFromScene(mm);
    QMouseEvent press(QEvent::MouseButtonPress, p, view.mapToGlobal(p.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, p, view.mapToGlobal(p.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &release);
}

} // namespace

TEST_CASE("Le canevas se peint avec son contenu", "[ui][view]")
{
    Document document;
    document.newProject(builtinLibrary());
    placeSymbol(document.project(), document.currentFolio(), QStringLiteral("iec:coil"),
                QPointF(120, 90), QStringLiteral("-K1"));

    FolioView view(&document);
    view.resize(900, 640);
    view.zoomToFit();
    CHECK(hasVisibleContent(view.grab()));
    // L'ajustement doit donner un zoom exploitable, pas un folio microscopique.
    CHECK(view.zoom() > 0.5);
}

TEST_CASE("Le canevas selectionne et supprime", "[ui][view]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *wire = drawWire(folio, { QPointF(20, 20), QPointF(80, 20) });

    FolioView view(&document);
    view.resize(900, 640);
    view.selectAll();
    CHECK(view.selection().size() == 1);

    view.deleteSelection();
    CHECK(folio->entityCount() == 0);
    CHECK(view.selection().isEmpty());

    document.undo();
    CHECK(folio->entityCount() == 1);
    CHECK(folio->entity(wire->id()) != nullptr);
}

TEST_CASE("Effacer un appareil referme le fil, en une seule annulation",
          "[ui][view][heal]")
{
    // Le geste réel : sélectionner l'appareil, Suppr. Ce que le dessinateur
    // attend, c'est le symétrique de l'insertion — poser le contact avait
    // coupé le fil, l'effacer doit le refermer. Et en UNE annulation : deux
    // Ctrl+Z pour défaire une suppression seraient une surprise.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    auto *contact = placeSymbol(document.project(), folio, QStringLiteral("iec:contact-no"),
                                QPointF(100, 60), QStringLiteral("-K1"));
    REQUIRE(contact);
    const SymbolDefinition *def = document.project().library.definition(contact->definitionId);
    REQUIRE(def);
    REQUIRE(def->pins.size() == 2);
    const QPointF gauche = contact->placement.map(def->pins.at(0).position);
    const QPointF droite = contact->placement.map(def->pins.at(1).position);
    drawWire(folio, { QPointF(40, gauche.y()), gauche });
    drawWire(folio, { droite, QPointF(180, droite.y()) });
    REQUIRE(folio->entityCount() == 3);

    FolioView view(&document);
    view.resize(900, 640);
    view.setSelection({ contact->id() });
    view.deleteSelection();

    // Un seul fil reste, et il va d'un bout à l'autre : le circuit est refermé.
    const auto fils = folio->entitiesOfType<Wire>();
    REQUIRE(fils.size() == 1);
    CHECK(fils.front()->points.first().x() == 40.0);
    CHECK(fils.front()->points.last().x() == 180.0);

    document.undo();
    CHECK(folio->entityCount() == 3);
    CHECK(folio->entity(contact->id()) != nullptr);
}

TEST_CASE("Remplacer un symbole garde repère, position et fils",
          "[ui][view][swap]")
{
    // Le geste que cherche un dessinateur venu d'AutoCAD : un contact NO
    // devient un contact NF. Trois choses doivent survivre, et ce sont les
    // trois qu'on perdrait en effaçant puis reposant.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    auto *contact = placeSymbol(document.project(), folio, QStringLiteral("iec:contact-no"),
                                QPointF(100, 60), QStringLiteral("-K1"));
    REQUIRE(contact);
    const SymbolDefinition *def = document.project().library.definition(contact->definitionId);
    REQUIRE(def);
    const QPointF gauche = contact->placement.map(def->pins.at(0).position);
    Wire *fil = drawWire(folio, { QPointF(40, gauche.y()), gauche });
    const QPointF avant = contact->placement.position;

    FolioView view(&document);
    view.resize(900, 640);
    const int orphelins = view.swapSymbol(contact->id(), QStringLiteral("iec:contact-nc"));
    CHECK(orphelins == 0);

    const auto *apres =
            dynamic_cast<const SymbolInstance *>(folio->entity(contact->id()));
    REQUIRE(apres);
    CHECK(apres->definitionId == QStringLiteral("iec:contact-nc"));
    CHECK(apres->designation() == QStringLiteral("-K1")); // le repère survit
    CHECK(apres->placement.position == avant);            // la position aussi

    // Et le fil tient toujours à une broche : c'est le point qui décide si le
    // circuit est encore raccordé.
    const SymbolDefinition *neuf = document.project().library.definition(apres->definitionId);
    REQUIRE(neuf);
    bool raccorde = false;
    for (const Pin &pin : neuf->pins) {
        if (samePoint(apres->placement.map(pin.position), fil->points.last()))
            raccorde = true;
    }
    CHECK(raccorde);

    // Une seule annulation défait tout : le symbole ET le fil qui l'a suivi.
    document.undo();
    const auto *revenu = dynamic_cast<const SymbolInstance *>(folio->entity(contact->id()));
    REQUIRE(revenu);
    CHECK(revenu->definitionId == QStringLiteral("iec:contact-no"));
    CHECK(fil->points.last() == gauche);
}

TEST_CASE("Remplacer dans tout le dossier tient dans une annulation",
          "[ui][findreplace]")
{
    // C'est ce qui rend le geste sans risque : on essaie sur quarante
    // occurrences, on regarde, Ctrl+Z. Deux annulations pour défaire un
    // remplacement seraient un piège.
    Document document;
    document.newProject(builtinLibrary());
    Folio *premier = document.currentFolio();
    premier->number = QStringLiteral("1");
    auto *k1 = placeSymbol(document.project(), premier, QStringLiteral("iec:coil"),
                           QPointF(60, 60), QStringLiteral("-KM1"));
    k1->fields.insert(QStringLiteral("value"), QStringLiteral("bobine KM1"));
    Folio *second = document.project().addFolio(QStringLiteral("Commande"));
    second->number = QStringLiteral("2");
    placeSymbol(document.project(), second, QStringLiteral("iec:coil"), QPointF(60, 60),
                QStringLiteral("-KM1"));

    FindReplaceDialog dialog(&document);
    dialog.setNeedle(QStringLiteral("KM1"));
    CHECK(dialog.runSearch() == 3); // deux repères et un champ, sur deux folios

    // Sans texte de remplacement, la boîte ne fait que chercher.
    CHECK(dialog.runReplaceAll() == 0);

    dialog.findChild<QLineEdit *>()->setText(QStringLiteral("KM1"));
    const auto champs = dialog.findChildren<QLineEdit *>();
    REQUIRE(champs.size() >= 2);
    champs.at(1)->setText(QStringLiteral("KM9"));
    CHECK(dialog.runReplaceAll() == 3);

    const auto *apres = dynamic_cast<const SymbolInstance *>(premier->entity(k1->id()));
    REQUIRE(apres);
    CHECK(apres->designation() == QStringLiteral("-KM9"));
    CHECK(apres->fields.value(QStringLiteral("value")) == QStringLiteral("bobine KM9"));
    // Le repère remplacé est verrouillé : sans cela la prochaine régénération
    // le recalcule et le remplacement disparaît sans un mot.
    CHECK(apres->designationLocked);

    document.undo();
    const auto *revenu = dynamic_cast<const SymbolInstance *>(premier->entity(k1->id()));
    REQUIRE(revenu);
    CHECK(revenu->designation() == QStringLiteral("-KM1"));
    CHECK(revenu->fields.value(QStringLiteral("value")) == QStringLiteral("bobine KM1"));
}

TEST_CASE("Coter tient en trois clics, et la mesure suit le dessin",
          "[ui][view][cote]")
{
    // Le geste : deux points à mesurer, puis la place de la ligne de cote.
    // Et ce qui fait une cote plutôt qu'un texte posé à côté : étirer une
    // attache change la valeur écrite, parce qu'elle est mesurée.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.zoomToFit();
    view.setDimensionKind(DimensionItem::Kind::Horizontal);
    view.setTool(FolioView::Tool::Dimension);
    // Les vrais clics, aux vrais pixels : c'est le chemin que prend la main.
    clickAt(view, QPointF(50, 100));
    clickAt(view, QPointF(200, 100));
    CHECK(folio->entityCount() == 0); // rien tant que la ligne n'est pas posée
    clickAt(view, QPointF(120, 130));

    REQUIRE(folio->entityCount() == 1);
    auto *cote = dynamic_cast<DimensionItem *>(folio->entities().front().get());
    REQUIRE(cote);
    CHECK(cote->measure() == 150.0);
    CHECK(cote->geometry().lineStart.y() == 130.0);

    // L'outil reste armé : on cote rarement une seule distance.
    CHECK(view.tool() == FolioView::Tool::Dimension);

    // Déplacer une attache change le nombre — c'est tout l'intérêt.
    cote->first = QPointF(80, 100);
    CHECK(cote->displayText() == QStringLiteral("120"));

    // Et elle se sélectionne au clic sur son trait, pas partout dans la bande
    // qu'elle mesure : sinon elle avalerait les clics du dessin qu'elle cote.
    view.setSelection({});
    view.selectAll();
    CHECK(view.selection().size() == 1);

    document.undo();
    CHECK(folio->entityCount() == 0);
}

TEST_CASE("Une cote refuse de se poser sur deux points confondus",
          "[ui][view][cote]")
{
    // Elle serait invisible et impossible à rattraper au clic : le dire vaut
    // mieux que de la poser.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.zoomToFit();
    view.setTool(FolioView::Tool::Dimension);
    clickAt(view, QPointF(100, 100));
    clickAt(view, QPointF(100, 100));
    clickAt(view, QPointF(120, 130));
    CHECK(folio->entityCount() == 0);
}

TEST_CASE("Composer un cartouche s'applique au dossier et s'annule",
          "[ui][cartouche]")
{
    // « Nous devons pouvoir créer une cartouche perso. » Le geste complet :
    // partir d'un gabarit livré, ajouter une case, appliquer — et Ctrl+Z si
    // on s'est trompé sur un dossier de quarante planches.
    Document document;
    document.newProject(builtinLibrary());
    document.project().addFolio(QStringLiteral("Second"));
    const double avant = document.currentFolio()->frame.titleBlockWidth;

    TitleBlockEditor editor(&document);
    editor.setTemplate(TitleBlock::loopSheet());
    const int cases = int(editor.edited().cells.size());
    editor.apply();

    CHECK(document.project().titleBlock.id == QStringLiteral("boucle"));
    CHECK(int(document.project().titleBlock.cells.size()) == cases);
    // La taille réservée sur CHAQUE folio suit le gabarit : les séparer
    // laisserait un cartouche de 330 mm serré dans un cadre qui n'en réserve
    // que 180, et le peintre devrait le rétrécir sans que rien ne le dise.
    for (const Folio *folio : document.project().folios()) {
        CHECK(folio->frame.titleBlockWidth == document.project().titleBlock.width);
        CHECK(folio->frame.titleBlockHeight == document.project().titleBlock.height);
    }

    document.undo();
    CHECK(document.project().titleBlock.isEmpty());
    CHECK(document.currentFolio()->frame.titleBlockWidth == avant);
}

TEST_CASE("Une case ajoutée au cartouche se retrouve dans le dossier",
          "[ui][cartouche]")
{
    // Ce que veut dire « perso » : ajouter SON champ, pas choisir parmi les
    // nôtres. Une clef libre est acceptée — la refuser figerait le cartouche
    // une seconde fois.
    Document document;
    document.newProject(builtinLibrary());

    TitleBlockEditor editor(&document);
    editor.setTemplate(TitleBlock::standard());
    const int avant = int(editor.edited().cells.size());
    // On ajoute une case par le même chemin que le bouton « + Champ ».
    TitleBlockTemplate gabarit = editor.edited();
    TitleBlockCell maison;
    maison.rect = QRectF(4, 30, 60, 6);
    maison.label = QStringLiteral("Atelier");
    maison.key = QStringLiteral("atelier");
    gabarit.cells.append(maison);
    editor.setTemplate(gabarit);
    editor.apply();

    REQUIRE(int(document.project().titleBlock.cells.size()) == avant + 1);
    CHECK(document.project().titleBlock.cells.last().key == QStringLiteral("atelier"));

    // Et la valeur se renseigne par folio, comme tout champ de cartouche.
    document.currentFolio()->titleBlock.insert(QStringLiteral("atelier"),
                                               QStringLiteral("Montage 3"));
    const QMap<QString, QString> v =
            TitleBlock::values(document.project(), *document.currentFolio());
    CHECK(v.value(QStringLiteral("atelier")) == QStringLiteral("Montage 3"));
}

TEST_CASE("Les bandes se saisissent en « nom = largeur »", "[ui][bandes]")
{
    // Le format tient sur une ligne parce qu'un jeu de bandes se relit d'un
    // coup d'œil ; un tableau à deux colonnes demanderait trois clics pour
    // ajouter une bande.
    const QVector<FolioBand> bandes = PageSetupDialog::parseBands(
            QStringLiteral("CHAMP = 200\nCABINET 037BJ0151 = 120\n"));
    REQUIRE(bandes.size() == 2);
    CHECK(bandes.at(0).title == QStringLiteral("CHAMP"));
    CHECK(bandes.at(0).width == 200.0);
    CHECK(bandes.at(1).title == QStringLiteral("CABINET 037BJ0151"));

    // Une ligne sans « = » garde son nom et prend la largeur par défaut :
    // refuser la ligne perdrait ce qui vient d'être tapé.
    const QVector<FolioBand> sansLargeur =
            PageSetupDialog::parseBands(QStringLiteral("PROCEDE"));
    REQUIRE(sansLargeur.size() == 1);
    CHECK(sansLargeur.at(0).title == QStringLiteral("PROCEDE"));
    CHECK(sansLargeur.at(0).width > 0.0);

    // Le nom peut contenir un « = » : c'est le DERNIER qui sépare.
    const QVector<FolioBand> avecEgal =
            PageSetupDialog::parseBands(QStringLiteral("REPERE = X = 80"));
    REQUIRE(avecEgal.size() == 1);
    CHECK(avecEgal.at(0).title == QStringLiteral("REPERE = X"));
    CHECK(avecEgal.at(0).width == 80.0);
}

TEST_CASE("La rotation depuis le canevas est annulable", "[ui][view]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    auto *symbol = placeSymbol(document.project(), folio, QStringLiteral("iec:coil"),
                               QPointF(100, 100));

    FolioView view(&document);
    view.resize(900, 640);
    view.setSelection({ symbol->id() });
    view.rotateSelection(true);

    const auto *rotated = dynamic_cast<const SymbolInstance *>(folio->entity(symbol->id()));
    REQUIRE(rotated);
    CHECK(rotated->placement.orientation == Orientation::R90);

    document.undo();
    const auto *restored = dynamic_cast<const SymbolInstance *>(folio->entity(symbol->id()));
    REQUIRE(restored);
    CHECK(restored->placement.orientation == Orientation::R0);
}

TEST_CASE("Le copier-coller decale la copie et lui donne un identifiant", "[ui][view]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    auto *symbol = placeSymbol(document.project(), folio, QStringLiteral("iec:coil"),
                               QPointF(100, 100));

    FolioView view(&document);
    view.resize(900, 640);
    view.setSelection({ symbol->id() });
    view.copySelection();
    view.pasteClipboard();

    CHECK(folio->entityCount() == 2);
    // Deux entites de meme identifiant rendraient la connectivite ambigue.
    const auto symbols = folio->entitiesOfType<SymbolInstance>();
    REQUIRE(symbols.size() == 2);
    CHECK(symbols.at(0)->id() != symbols.at(1)->id());
}

TEST_CASE("Un circuit colle depuis le canevas est re-repere en une annulation",
          "[ui][view][circuitcopy]")
{
    // Le collage construit ses copies, les re-repere, puis les pose : tout
    // tient dans une macro. Une annulation qui ne rendrait que la moitie du
    // travail laisserait le dossier dans un etat que personne n'a demande.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    auto *coil = placeSymbol(document.project(), folio, QStringLiteral("iec:coil"),
                             QPointF(100, 100));
    coil->setDesignation(QStringLiteral("KM1"));
    Wire *wire = drawWire(folio, { QPointF(60, 100), QPointF(95, 100) });
    wire->number = QStringLiteral("101");

    FolioView view(&document);
    view.resize(900, 640);
    view.setSelection({ coil->id(), wire->id() });
    view.copySelection();
    view.pasteClipboard();

    REQUIRE(folio->entityCount() == 4);
    for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
        if (symbol->id() == coil->id())
            continue;
        CHECK(symbol->designation() != QStringLiteral("KM1"));
        CHECK_FALSE(symbol->designation().isEmpty());
    }
    for (const Wire *pasted : folio->entitiesOfType<Wire>()) {
        if (pasted->id() != wire->id())
            CHECK(pasted->number.isEmpty());
    }

    document.undo();
    CHECK(folio->entityCount() == 2);
    CHECK(coil->designation() == QStringLiteral("KM1"));
    CHECK(wire->number == QStringLiteral("101"));
}

TEST_CASE("Coller a l'identique conserve les reperes", "[ui][view][circuitcopy]")
{
    // La commande jumelle sert a deplacer un circuit d'un folio a l'autre :
    // l'appareil doit y garder son identite. Elle cree deliberement un
    // doublon tant que l'original n'est pas efface — c'est pour cela qu'elle
    // est seconde, et pas le raccourci par defaut.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    auto *coil = placeSymbol(document.project(), folio, QStringLiteral("iec:coil"),
                             QPointF(100, 100));
    coil->setDesignation(QStringLiteral("KM1"));

    FolioView view(&document);
    view.resize(900, 640);
    view.setSelection({ coil->id() });
    view.copySelection();
    view.pasteClipboard(true);

    const auto symbols = folio->entitiesOfType<SymbolInstance>();
    REQUIRE(symbols.size() == 2);
    CHECK(symbols.at(0)->designation() == QStringLiteral("KM1"));
    CHECK(symbols.at(1)->designation() == QStringLiteral("KM1"));
    CHECK(symbols.at(0)->id() != symbols.at(1)->id());
}

TEST_CASE("La palette liste, filtre et rend ses apercus", "[ui][palette]")
{
    SymbolLibrary library = builtinLibrary();
    SymbolPalette palette;
    palette.setLibrary(&library);
    palette.setNorm(QStringLiteral("IEC"));
    palette.resize(320, 600);

    CHECK(hasVisibleContent(palette.grab()));

    // L'apercu doit etre le vrai trace du symbole, pas une icone vide.
    const SymbolDefinition *coil = library.definition(QStringLiteral("iec:coil"));
    REQUIRE(coil);
    const QIcon icon = SymbolPalette::renderIcon(*coil, 48, RenderStyle::screen());
    CHECK(hasVisibleContent(icon.pixmap(48, 48)));
}

TEST_CASE("La grille montre autant de symboles que la liste", "[ui][palette]")
{
    // La grille est une autre vue, pas un autre contenu : si elle en montrait
    // moins, on chercherait un symbole qui existe et on ne le trouverait pas.
    QSettings().remove(QStringLiteral("ui/recentSymbols"));
    SymbolLibrary library = builtinLibrary();
    SymbolPalette palette;
    palette.setLibrary(&library);
    palette.setNorm(QStringLiteral("IEC"));
    palette.resize(250, 600);

    palette.setGridMode(true);
    const int inGrid = palette.visibleCount();
    palette.setGridMode(false);
    const int inList = palette.visibleCount();

    CHECK(inGrid > 0);
    CHECK(inGrid == inList);
}

TEST_CASE("Les récents remontent ce qui a été posé, dans l'ordre", "[ui][palette]")
{
    // Sur un départ moteur on repose les mêmes cinq symboles : les rechercher
    // à chaque fois est le vrai coût du panneau. L'ordre compte autant que la
    // liste — le dernier posé est celui qu'on repose.
    QSettings().remove(QStringLiteral("ui/recentSymbols"));
    SymbolLibrary library = builtinLibrary();
    SymbolPalette palette;
    palette.setLibrary(&library);
    palette.setNorm(QStringLiteral("IEC"));
    palette.resize(250, 600);

    palette.noteUsed(QStringLiteral("iec:coil"));
    palette.noteUsed(QStringLiteral("iec:contact-no"));
    palette.noteUsed(QStringLiteral("iec:coil")); // repose : il repasse devant

    CHECK(palette.recent().size() == 2);
    CHECK(palette.recent().first() == QStringLiteral("iec:coil"));

    // Un symbole seulement survolé dans la palette n'entre pas : seul ce qui
    // est réellement posé compte.
    palette.noteUsed(QString());
    CHECK(palette.recent().size() == 2);

    QSettings().remove(QStringLiteral("ui/recentSymbols"));
}

TEST_CASE("La recherche de la palette réduit ce qui est montré", "[ui][palette]")
{
    SymbolLibrary library = builtinLibrary();
    SymbolPalette palette;
    palette.setLibrary(&library);
    palette.setNorm(QStringLiteral("IEC"));
    palette.resize(250, 600);

    const int total = palette.visibleCount();
    REQUIRE(total > 10);

    auto *search = palette.findChild<QLineEdit *>();
    REQUIRE(search);
    search->setText(QStringLiteral("bobine"));
    const int filtered = palette.visibleCount();

    CHECK(filtered > 0);
    CHECK(filtered < total);
}

TEST_CASE("L'editeur de symboles ouvre, modifie et enregistre", "[ui][symboleditor]")
{
    SymbolLibrary library = builtinLibrary();
    SymbolEditor editor(&library);
    editor.resize(1000, 680);
    // Un symbole integre est duplique plutot que modifie en place.
    editor.editDefinition(QStringLiteral("iec:coil"), true);

    CHECK(hasVisibleContent(editor.grab()));
}

TEST_CASE("Le canevas de l'editeur cree, deplace et annule", "[ui][symboleditor]")
{
    SymbolCanvas canvas;
    canvas.resize(600, 480);

    SymbolDefinition definition;
    definition.norm = QStringLiteral("IEC");
    definition.logicalId = QStringLiteral("essai");
    definition.id = SymbolDefinition::makeId(definition.norm, definition.logicalId);
    definition.name = QStringLiteral("Essai");
    canvas.setDefinition(definition);

    canvas.modify(QStringLiteral("Ajouter"), [](SymbolDefinition &d) {
        d.graphics.append(Primitive::rect(QRectF(-5, -5, 10, 10)));
        Pin pin;
        pin.number = QStringLiteral("1");
        pin.position = QPointF(-10, 0);
        pin.direction = Direction::Left;
        d.pins.append(pin);
    });
    CHECK(canvas.definition().graphics.size() == 1);
    CHECK(canvas.definition().pins.size() == 1);
    CHECK(hasVisibleContent(canvas.grab()));

    canvas.commands().undo();
    CHECK(canvas.definition().graphics.isEmpty());
    canvas.commands().redo();
    CHECK(canvas.definition().pins.size() == 1);
}

TEST_CASE("Les icones du theme sont reellement dessinees", "[ui][theme]")
{
    // Une icone vide passe inapercue a la relecture du code et saute aux yeux
    // dans la barre d'outils.
    const QVector<Icons::Glyph> glyphs{
        Icons::Glyph::New,    Icons::Glyph::Open,     Icons::Glyph::Save,
        Icons::Glyph::Print,  Icons::Glyph::Undo,     Icons::Glyph::Redo,
        Icons::Glyph::Select, Icons::Glyph::Wire,     Icons::Glyph::Junction,
        Icons::Glyph::LabelTag, Icons::Glyph::Text,   Icons::Glyph::Rotate,
        Icons::Glyph::Mirror, Icons::Glyph::ZoomIn,   Icons::Glyph::ZoomOut,
        Icons::Glyph::ZoomFit, Icons::Glyph::Grid,    Icons::Glyph::Snap,
        Icons::Glyph::Renumber, Icons::Glyph::Check,  Icons::Glyph::Edit,
    };
    for (Icons::Glyph glyph : glyphs) {
        const QIcon icon = Icons::icon(glyph, QColor(255, 255, 255));
        const QPixmap pixmap = icon.pixmap(24, 24);
        INFO("glyphe " << int(glyph));
        REQUIRE_FALSE(pixmap.isNull());

        // L'icone est tracee dans une boite de 24 unites : si le facteur de
        // densite etait applique deux fois, le quart inferieur droit serait
        // systematiquement vide.
        const QImage image = pixmap.toImage();
        CHECK(image.width() >= 24);
    }
}

TEST_CASE("Le theme couvre les deux modes", "[ui][theme]")
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    REQUIRE(app);

    Theme::apply(*app, true);
    CHECK(Theme::isDark());
    const QColor darkText = Theme::colors().text;

    Theme::apply(*app, false);
    CHECK_FALSE(Theme::isDark());
    const QColor lightText = Theme::colors().text;

    // Les deux thèmes doivent vraiment differer, sinon l'un des deux est
    // illisible sur son propre fond.
    CHECK(darkText.lightness() > lightText.lightness());
    CHECK_FALSE(Theme::colors().window.isValid() == false);

    Theme::apply(*app, true);
}

TEST_CASE("Règle 6 : aucun plan de chrome n'est blanc pur", "[ui][theme][papier]")
{
    // Le papier est le seul blanc pur du logiciel. C'est ce qui le fait
    // flotter en clair comme en sombre — un panneau de la couleur d'une
    // feuille fait s'effondrer la hiérarchie au dernier centimètre, là où
    // l'œil travaille. Et c'est cette règle qui permet au dessin de n'avoir
    // qu'UNE palette : puisque le papier est blanc des deux côtés, l'encre
    // est la même des deux côtés.
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    REQUIRE(app);

    for (bool dark : { true, false }) {
        Theme::apply(*app, dark);
        const ThemeColors &c = Theme::colors();
        for (const QColor &plan : { c.canvas, c.window, c.surface, c.elevated })
            CHECK(plan != QColor(Qt::white));
        CHECK(c.paper == QColor(Qt::white));
        // Le vide reste plus profond que le chrome, dans les deux thèmes :
        // c'est la règle 1, et elle ne s'inverse pas. En clair elle était
        // cassée — le canevas était plus CLAIR que la fenêtre.
        CHECK(c.canvas.lightness() < c.window.lightness());
        // Des filets, pas des boîtes : encore faut-il que le filet se voie.
        // Deux pour cent d'écart ne font pas une séparation.
        CHECK(std::abs(c.border.lightness() - c.surface.lightness()) >= 8);
    }

    Theme::apply(*app, true);
}

TEST_CASE("La vignette, le canevas et le PDF s'accordent sur le papier",
          "[ui][theme][papier]")
{
    // Le défaut que cette étape corrige était visible à l'écran : la vignette
    // d'un folio était peinte avec `print()` et le canevas avec `screen()` —
    // la même page, deux couleurs, à quinze centimètres l'une de l'autre.
    // Tout ce qui rend à l'écran passe maintenant par un seul endroit.
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    REQUIRE(app);

    QSettings().remove(QStringLiteral("display/darkSheet"));
    for (bool dark : { true, false }) {
        Theme::apply(*app, dark);
        const RenderStyle ecran = MainWindow::buildRenderStyle();
        const RenderStyle papier = RenderStyle::print();
        CHECK(ecran.sheet == Theme::colors().paper);
        CHECK(ecran.sheet == papier.sheet);
        CHECK(ecran.symbol == Theme::colors().ink);
        // Le vide autour vient du thème, lui : c'est le seul endroit où
        // l'interface a de la profondeur.
        CHECK(ecran.pageBackground == Theme::colors().canvas);
        // ET LE CADRE SUIT L'ENCRE. C'est l'assertion qui tient réellement la
        // dérivation : `screen()` porte déjà un papier blanc, donc les trois
        // lignes précédentes passaient AUSSI sans dérivation — vérifié en la
        // retirant. Le cadre, lui, valait #222624 dans le préréglage et vaut
        // l'encre du thème une fois dérivé : lui seul distingue les deux.
        CHECK(ecran.frame == Theme::colors().ink);
    }

    // Et le fond sombre reste atteignable — mais par un réglage explicite,
    // plus par le thème d'interface. Les quatre combinaisons existent.
    Appearance::setDarkSheet(true);
    Theme::apply(*app, false);
    CHECK(MainWindow::buildRenderStyle().sheet != Theme::colors().paper);
    Appearance::setDarkSheet(false);
    Theme::apply(*app, true);
    CHECK(MainWindow::buildRenderStyle().sheet == Theme::colors().paper);

    QSettings().remove(QStringLiteral("display/darkSheet"));
}

TEST_CASE("Basculer le fond de dessin oublie la couleur épinglée",
          "[ui][theme][papier]")
{
    // Le piège que l'étape ne voyait pas. `Appearance::save` écrit la couleur
    // de la feuille à CHAQUE validation de la boîte de paramètres, même si
    // l'utilisateur n'y a pas touché ; et `load` passe en dernier, puisque le
    // réglage explicite gagne sur le thème. Sans oubli, cocher « fond de
    // dessin sombre » n'aurait plus aucun effet visible dès la première visite
    // dans les paramètres — et rien ne l'aurait dit.
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    REQUIRE(app);
    Theme::apply(*app, true);
    QSettings().remove(QStringLiteral("display/darkSheet"));

    // L'utilisateur valide la boîte : la couleur du papier est épinglée.
    RenderStyle epingle = MainWindow::buildRenderStyle();
    Appearance::save(epingle, true);
    CHECK(MainWindow::buildRenderStyle().sheet == Theme::colors().paper);

    // Il coche « fond de dessin sombre » : la feuille doit vraiment noircir.
    Appearance::setDarkSheet(true);
    CHECK(MainWindow::buildRenderStyle().sheet != Theme::colors().paper);

    // Et le retour marche aussi.
    Appearance::setDarkSheet(false);
    CHECK(MainWindow::buildRenderStyle().sheet == Theme::colors().paper);

    Appearance::reset(true);
    QSettings().remove(QStringLiteral("display/darkSheet"));
}

TEST_CASE("Le canevas expose son moteur d'accrochage", "[ui][snap]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    drawWire(folio, { QPointF(40, 60), QPointF(160, 60) });

    FolioView view(&document);
    view.resize(900, 640);
    view.zoomToFit();

    // Les bascules de la barre d'etat agissent sur ce moteur : si la vue ne
    // l'exposait plus, les touches de fonction deviendraient muettes sans
    // qu'aucune compilation n'echoue.
    CHECK(view.snapEngine().objectSnapEnabled());
    view.snapEngine().setOrthoEnabled(true);
    CHECK(view.snapEngine().orthoEnabled());
    view.snapSettingsTouched();
}

TEST_CASE("Le survol d'un milieu de fil est rendu visible", "[ui][snap]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    drawWire(folio, { QPointF(40, 60), QPointF(160, 60) });

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();

    // Sans survol, aucun marqueur ; apres survol du milieu, le rendu change.
    const QPixmap before = view.grab();

    const QPointF target = view.mapFromScene(QPointF(100.4, 60.3));
    QMouseEvent move(QEvent::MouseMove, target, view.mapToGlobal(target), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &move);
    const QPixmap after = view.grab();

    CHECK(before.toImage() != after.toImage());
}

TEST_CASE("La boite de parametres de dessin rend les reglages", "[ui][snap]")
{
    SnapEngine engine;
    engine.setMode(SnapMode::Nearest, true);
    engine.setOrthoEnabled(true);
    engine.setPolarIncrement(30.0);

    RenderStyle style = RenderStyle::screen();
    style.gridStep = 2.5;
    style.showGrid = true;
    style.gridStyle = GridStyle::Lines;
    style.crosshairPercent = 40.0;

    DraftingSettingsDialog dialog(engine, style);
    dialog.resize(580, 560);
    CHECK(hasVisibleContent(dialog.grab()));

    // Les reglages ressortent tels qu'ils sont entres : la boite ne doit rien
    // perdre en route, sinon l'utilisateur croit avoir regle et n'a rien fait.
    const SnapEngine out = dialog.engine();
    CHECK(out.hasMode(SnapMode::Nearest));
    CHECK(out.orthoEnabled());
    CHECK(out.polarIncrement() == 30.0);
    CHECK(dialog.gridStep() == 2.5);
    CHECK(dialog.gridVisible());

    // L'onglet Affichage rend lui aussi ce qu'on lui a donne — y compris ce
    // qu'on n'a pas touche, qui doit ressortir inchange.
    const RenderStyle back = dialog.style();
    CHECK(back.gridStyle == GridStyle::Lines);
    CHECK(back.crosshairPercent == 40.0);
    CHECK(back.crosshair == style.crosshair);
    CHECK(back.sheet == style.sheet);
    CHECK_FALSE(dialog.resetRequested());
}

TEST_CASE("La mise en page change le format sans toucher au contenu", "[ui][page]")
{
    Project project;
    project.library = builtinLibrary();
    Folio *folio = project.addFolio(QStringLiteral("Essai"));
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    drawWire(folio, { QPointF(40, 60), QPointF(160, 60) });

    PageSetupDialog dialog(project, *folio);
    dialog.resize(940, 640);
    CHECK(hasVisibleContent(dialog.grab()));

    const Folio configured = dialog.result();
    // Le folio ressort avec la meme identite et le meme contenu : seule la
    // mise en page est en jeu.
    CHECK(configured.id() == folio->id());
    CHECK(configured.entityCount() == folio->entityCount());
    CHECK(configured.sheet.width == folio->sheet.width);
    CHECK_FALSE(dialog.applyToAllFolios());
}

TEST_CASE("La mise en page s'annule comme toute modification", "[ui][page]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    const double before = folio->sheet.width;

    SheetFormat a4 = sheetFormatById(QStringLiteral("A4"));
    SheetFrame frame = folio->frame;
    frame.columns = 6;

    document.push(std::make_unique<ChangeFolioLayoutCommand>(document.project(), folio->id(),
                                                             a4, frame, folio->bands,
                                                             folio->bandHeaderHeight));
    CHECK(folio->sheet.width == a4.width);
    CHECK(folio->frame.columns == 6);

    document.undo();
    CHECK(folio->sheet.width == before);
}

TEST_CASE("La selection fenetre ne prend que ce qu'elle contient", "[ui][selection]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *inside = drawWire(folio, { QPointF(60, 60), QPointF(90, 60) });
    Wire *straddling = drawWire(folio, { QPointF(80, 80), QPointF(200, 80) });

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();

    // Geste de gauche a droite : fenetre. Le fil qui depasse du rectangle
    // n'est pas pris, meme s'il le traverse.
    const QPointF start = view.mapFromScene(QPointF(50, 50));
    const QPointF end = view.mapFromScene(QPointF(120, 100));
    QMouseEvent press(QEvent::MouseButtonPress, start, view.mapToGlobal(start),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, end, view.mapToGlobal(end), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.mapToGlobal(end),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &move);
    QApplication::sendEvent(&view, &release);

    CHECK(view.selection().contains(inside->id()));
    CHECK_FALSE(view.selection().contains(straddling->id()));
}

TEST_CASE("La selection par capture prend ce qu'elle effleure", "[ui][selection]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *inside = drawWire(folio, { QPointF(60, 60), QPointF(90, 60) });
    Wire *straddling = drawWire(folio, { QPointF(80, 80), QPointF(200, 80) });

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();

    // Geste de droite a gauche : capture. Le fil qui traverse le rectangle
    // est pris, meme s'il en sort largement.
    const QPointF start = view.mapFromScene(QPointF(120, 100));
    const QPointF end = view.mapFromScene(QPointF(50, 50));
    QMouseEvent press(QEvent::MouseButtonPress, start, view.mapToGlobal(start),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, end, view.mapToGlobal(end), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.mapToGlobal(end),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &move);
    QApplication::sendEvent(&view, &release);

    CHECK(view.selection().contains(inside->id()));
    CHECK(view.selection().contains(straddling->id()));
}

TEST_CASE("Une poignee deplace un sommet de fil et s'annule", "[ui][grips]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *wire = drawWire(folio, { QPointF(60, 60), QPointF(160, 60) });
    const QString id = wire->id();

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.setSelection({ id });

    // On saisit la poignee de l'extremite droite et on la tire vers le bas.
    const QPointF grip = view.mapFromScene(QPointF(160, 60));
    const QPointF target = view.mapFromScene(QPointF(160, 100));
    QMouseEvent press(QEvent::MouseButtonPress, grip, view.mapToGlobal(grip), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, target, view.mapToGlobal(target), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, target, view.mapToGlobal(target),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &move);
    QApplication::sendEvent(&view, &release);

    const auto *edited = dynamic_cast<const Wire *>(folio->entity(id));
    REQUIRE(edited);
    CHECK(edited->points.last().y() > 90.0);
    // Le premier sommet ne bouge pas : seule la poignee saisie agit.
    CHECK(edited->points.first() == QPointF(60, 60));

    document.undo();
    const auto *restored = dynamic_cast<const Wire *>(folio->entity(id));
    REQUIRE(restored);
    CHECK(restored->points.last() == QPointF(160, 60));
}

TEST_CASE("Un fil selectionne porte une poignee par sommet et par segment",
          "[ui][grips]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    // Trois sommets, donc deux segments : cinq poignees attendues.
    Wire *wire = drawWire(folio, { QPointF(40, 40), QPointF(120, 40), QPointF(120, 110) });

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();

    const QPixmap plain = view.grab();
    view.setSelection({ wire->id() });
    const QPixmap gripped = view.grab();

    // Les poignees doivent se voir : sans retour visuel, personne ne devine
    // qu'on peut tirer dessus.
    CHECK(plain.toImage() != gripped.toImage());
}

TEST_CASE("La ligne de commande execute par nom et par alias", "[ui][command]")
{
    CommandLine line;
    int calls = 0;
    QStringList received;
    line.registerCommand({ QStringLiteral("LIGNE"),
                           { QStringLiteral("L"), QStringLiteral("FIL") },
                           QStringLiteral("Tracer un fil"),
                           [&](const QStringList &args) { ++calls; received = args; } });

    CHECK(line.execute(QStringLiteral("LIGNE")));
    CHECK(line.execute(QStringLiteral("l")));       // insensible a la casse
    CHECK(line.execute(QStringLiteral("  FIL  "))); // espaces ignores
    CHECK(calls == 3);

    // Les arguments arrivent au gestionnaire, sans le nom de la commande.
    CHECK(line.execute(QStringLiteral("LIGNE 10 20")));
    CHECK(received == QStringList{ QStringLiteral("10"), QStringLiteral("20") });
}

TEST_CASE("La ligne de commande marche au clavier, pas seulement par appel",
          "[ui][command]")
{
    // execute() prouve que le repertoire repond ; il ne prouve pas que le
    // champ y est branche. C'est pourtant le seul chemin qu'emprunte
    // l'utilisateur : taper, puis Entrée.
    CommandLine line;
    int calls = 0;
    line.registerCommand({ QStringLiteral("ZOOM"),
                           { QStringLiteral("Z") },
                           QStringLiteral("Zoom"),
                           [&](const QStringList &) { ++calls; } });

    auto *input = line.findChild<QLineEdit *>();
    REQUIRE(input);

    input->setText(QStringLiteral("Z"));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(input, &enter);
    CHECK(calls == 1);
    CHECK(input->text().isEmpty()); // le champ se vide, prêt pour la suivante

    // Entrée sur une ligne vide relance la dernière commande : le réflexe
    // d'AutoCAD, et ce qui fait gagner du temps quand on répète un geste.
    QApplication::sendEvent(input, &enter);
    CHECK(calls == 2);
    // C'est le nom canonique qui est retenu, pas l'alias tapé : relancer
    // « ZOOM » reste juste même si l'alias vient à changer.
    CHECK(line.lastCommand() == QStringLiteral("ZOOM"));

    // La flèche haut rappelle ce qu'on a tapé.
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(input, &up);
    CHECK(input->text() == QStringLiteral("Z"));
}

TEST_CASE("L'historique de la ligne de commande ne répète pas l'invite", "[ui][command]")
{
    // Un utilisateur a signalé « deux fois une ligne de commande » : l'invite
    // du champ et un message d'accueil identique dans l'historique, l'un
    // au-dessus de l'autre, tous deux dessinés comme des champs de saisie.
    // L'historique part donc vide, et il n'est plus stylé comme un champ.
    CommandLine line;
    auto *history = line.findChild<QPlainTextEdit *>();
    auto *input = line.findChild<QLineEdit *>();
    REQUIRE(history);
    REQUIRE(input);

    CHECK(history->toPlainText().isEmpty());
    CHECK_FALSE(input->placeholderText().isEmpty());
    CHECK(history->property("commandHistory").toBool());
    // Vide, il ne prend pas de place : le bandeau vaut la hauteur du seul
    // champ. Sinon il réserve en permanence trois lignes qui ne viendront
    // peut-être jamais.
    CHECK(history->isHidden());

    // Il se montre dès qu'il a quelque chose à dire — c'est bien pour cela
    // qu'il existe.
    line.execute(QStringLiteral("COMMANDEQUINEXISTEPAS"));
    CHECK_FALSE(history->toPlainText().isEmpty());
    CHECK_FALSE(history->isHidden());
}

TEST_CASE("Une commande inconnue est refusee sans rien casser", "[ui][command]")
{
    CommandLine line;
    line.registerCommand({ QStringLiteral("ZOOM"), {}, QStringLiteral("Zoom"), nullptr });
    CHECK_FALSE(line.execute(QStringLiteral("HOLOGRAMME")));
    CHECK(line.execute(QStringLiteral("ZOOM")));
    CHECK(hasVisibleContent(line.grab()));
}

TEST_CASE("La derniere commande est memorisee pour etre relancee", "[ui][command]")
{
    CommandLine line;
    int calls = 0;
    line.registerCommand({ QStringLiteral("PIVOTER"), { QStringLiteral("RO") },
                           QStringLiteral("Pivoter"), [&](const QStringList &) { ++calls; } });

    line.execute(QStringLiteral("ro"));
    // La derniere commande est retenue sous son nom canonique, pas sous
    // l'alias tape : c'est ce que reaffiche AutoCAD quand on relance.
    CHECK(line.lastCommand() == QStringLiteral("PIVOTER"));

    line.execute(line.lastCommand());
    CHECK(calls == 2);
}

TEST_CASE("Le point d'interrogation liste les commandes", "[ui][command]")
{
    CommandLine line;
    line.registerCommand({ QStringLiteral("LIGNE"), { QStringLiteral("L") },
                           QStringLiteral("Tracer un fil"), nullptr });
    line.registerCommand({ QStringLiteral("ZOOM"), {}, QStringLiteral("Zoom"), nullptr });

    // Sans decouvrabilite, une ligne de commande est inutilisable a qui ne
    // connait pas deja le repertoire.
    CHECK(line.execute(QStringLiteral("?")));
    CHECK(line.commands().size() == 2);
}

TEST_CASE("La fenetre principale enregistre un repertoire consequent", "[ui][command]")
{
    // La fenetre construit tout : palette, canevas, rapports, ligne de
    // commande. Ce test attrape aussi bien une commande oubliee qu'un
    // panneau qui ne se monte plus.
    MainWindow window;
    window.resize(1400, 900);
    CHECK(hasVisibleContent(window.grab()));
}

TEST_CASE("L'outil ajuster coupe un fil au croisement", "[ui][trim]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    drawWire(folio, { QPointF(40, 100), QPointF(240, 100) });
    drawWire(folio, { QPointF(90, 60), QPointF(90, 140) });
    drawWire(folio, { QPointF(190, 60), QPointF(190, 140) });
    const int before = int(folio->entityCount());

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setTool(FolioView::Tool::Trim);

    const QPointF click = view.mapFromScene(QPointF(140, 100));
    QMouseEvent press(QEvent::MouseButtonPress, click, view.mapToGlobal(click), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    // Le fil coupe devient deux morceaux : une entite de plus au total.
    CHECK(int(folio->entityCount()) == before + 1);

    // Et l'ajustement s'annule d'un seul coup, malgre ses trois operations.
    document.undo();
    CHECK(int(folio->entityCount()) == before);
}

TEST_CASE("L'outil prolonger allonge jusqu'a l'obstacle", "[ui][extend]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *stub = drawWire(folio, { QPointF(40, 100), QPointF(120, 100) });
    drawWire(folio, { QPointF(190, 60), QPointF(190, 140) });
    const QString id = stub->id();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setTool(FolioView::Tool::Extend);

    // On vise pres de l'extremite droite : c'est ce bout qui doit s'allonger.
    const QPointF click = view.mapFromScene(QPointF(115, 100));
    QMouseEvent press(QEvent::MouseButtonPress, click, view.mapToGlobal(click), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    const auto *extended = dynamic_cast<const Wire *>(folio->entity(id));
    REQUIRE(extended);
    CHECK(extended->points.last() == QPointF(190, 100));
    CHECK(extended->points.first() == QPointF(40, 100));
}

TEST_CASE("Le zoom precedent remonte la pile des vues", "[ui][zoom]")
{
    Document document;
    document.newProject(builtinLibrary());

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    const double fitted = view.zoom();

    view.zoomToRect(QRectF(60, 60, 40, 30));
    CHECK(view.zoom() > fitted);
    CHECK(view.canZoomPrevious());

    // Le filet de securite : on revient exactement d'ou l'on vient.
    view.zoomPrevious();
    CHECK(view.zoom() == fitted);
}

TEST_CASE("Le decalage copie le fil du cote clique", "[ui][offset]")
{
    // DECALER sert a doubler un depart : la copie doit se poser du cote ou
    // l'on montre, sinon il faut la reprendre a chaque fois.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *source = drawWire(folio, { QPointF(40, 100), QPointF(200, 100) });
    const QString sourceId = source->id();
    const int before = int(folio->entityCount());

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setSelection({ sourceId });
    view.beginOffset(10.0);
    REQUIRE(view.hasPendingGesture());

    // On clique au-dessus du fil : la copie doit s'y trouver, pas en dessous.
    const QPointF click = view.mapFromScene(QPointF(120, 70));
    QMouseEvent press(QEvent::MouseButtonPress, click, view.mapToGlobal(click), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    CHECK_FALSE(view.hasPendingGesture());
    REQUIRE(int(folio->entityCount()) == before + 1);

    const Wire *copy = nullptr;
    for (const EntityPtr &entity : folio->entities()) {
        if (entity->id() != sourceId) {
            if (const auto *wire = dynamic_cast<const Wire *>(entity.get()))
                copy = wire;
        }
    }
    REQUIRE(copy);
    CHECK(copy->points.first() == QPointF(40, 90));
    CHECK(copy->points.last() == QPointF(200, 90));
    // Le fil d'origine ne bouge pas : decaler copie, il ne deplace pas.
    const auto *original = dynamic_cast<const Wire *>(folio->entity(sourceId));
    REQUIRE(original);
    CHECK(original->points.first() == QPointF(40, 100));

    // Un decalage se defait d'une seule annulation.
    document.undo();
    CHECK(int(folio->entityCount()) == before);
}

TEST_CASE("Le decalage ne recopie pas le repere du fil", "[ui][offset]")
{
    // Deux conducteurs distincts portant le meme repere, c'est un schema
    // faux : le fil decale doit repartir sans repere pour etre renumerote.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *source = drawWire(folio, { QPointF(40, 100), QPointF(200, 100) });
    source->number = QStringLiteral("101");
    source->numberLocked = true;
    const QString sourceId = source->id();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setSelection({ sourceId });
    view.beginOffset(10.0);

    const QPointF click = view.mapFromScene(QPointF(120, 130));
    QMouseEvent press(QEvent::MouseButtonPress, click, view.mapToGlobal(click), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    for (const EntityPtr &entity : folio->entities()) {
        if (entity->id() == sourceId)
            continue;
        const auto *copy = dynamic_cast<const Wire *>(entity.get());
        REQUIRE(copy);
        CHECK(copy->number.isEmpty());
        CHECK_FALSE(copy->numberLocked);
    }
}

TEST_CASE("Le deplacement en deux points suit la distance montree", "[ui][move]")
{
    // DEPLACER existe a cote du glisser parce que ses deux points
    // s'accrochent au dessin : c'est ainsi qu'on deplace d'une distance juste.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *wire = drawWire(folio, { QPointF(40, 100), QPointF(200, 100) });
    const QString id = wire->id();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setSelection({ id });
    view.beginMoveSelection();
    REQUIRE(view.hasPendingGesture());

    auto click = [&](const QPointF &scene) {
        const QPointF at = view.mapFromScene(scene);
        QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &press);
    };

    click(QPointF(40, 100));   // point de base : l'extremite du fil
    CHECK(view.hasPendingGesture());
    click(QPointF(40, 60));    // point d'arrivee, 40 mm plus haut

    CHECK_FALSE(view.hasPendingGesture());
    const auto *moved = dynamic_cast<const Wire *>(folio->entity(id));
    REQUIRE(moved);
    CHECK(moved->points.first() == QPointF(40, 60));
    CHECK(moved->points.last() == QPointF(200, 60));

    document.undo();
    const auto *back = dynamic_cast<const Wire *>(folio->entity(id));
    REQUIRE(back);
    CHECK(back->points.first() == QPointF(40, 100));
}

TEST_CASE("Un fil neuf recoit le type de fil arme", "[ui][wiretype]")
{
    // Le type courant s'arme une fois puis vaut pour la suite du trace :
    // c'est ce qui evite de rehabiller chaque fil apres coup.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setTool(FolioView::Tool::Wire);
    view.setCurrentWireType(QStringLiteral("l1"));

    auto click = [&](const QPointF &scene, Qt::MouseButton button) {
        const QPointF at = view.mapFromScene(scene);
        QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), button, button,
                          Qt::NoModifier);
        QApplication::sendEvent(&view, &press);
    };

    click(QPointF(50, 100), Qt::LeftButton);
    click(QPointF(150, 100), Qt::LeftButton);
    click(QPointF(150, 100), Qt::RightButton); // le clic droit termine le trace

    const Wire *drawn = nullptr;
    for (const EntityPtr &entity : folio->entities()) {
        if (const auto *wire = dynamic_cast<const Wire *>(entity.get()))
            drawn = wire;
    }
    REQUIRE(drawn);
    CHECK(drawn->wireType == QLatin1String("l1"));
}

TEST_CASE("Etirer prend ses sommets a la fenetre puis les deplace", "[ui][stretch]")
{
    // Le geste complet d'AutoCAD : une fenêtre de capture, un point de base,
    // un point d'arrivee. C'est ainsi qu'on rallonge un barreau d'echelle
    // sans detacher ce qui y est raccorde.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *left = drawWire(folio, { QPointF(40, 100), QPointF(160, 100) });
    Wire *right = drawWire(folio, { QPointF(40, 140), QPointF(60, 140) });
    const QString leftId = left->id();
    const QString rightId = right->id();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.beginStretch();

    auto at = [&](const QPointF &scene) { return view.mapFromScene(scene); };
    auto send = [&](QEvent::Type type, const QPointF &scene, Qt::MouseButton button) {
        QMouseEvent event(type, at(scene), view.mapToGlobal(at(scene)), button,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : button,
                          Qt::NoModifier);
        QApplication::sendEvent(&view, &event);
    };

    // Fenêtre de capture autour de la seule extremite droite du premier fil.
    send(QEvent::MouseButtonPress, QPointF(140, 80), Qt::LeftButton);
    send(QEvent::MouseMove, QPointF(200, 120), Qt::LeftButton);
    send(QEvent::MouseButtonRelease, QPointF(200, 120), Qt::LeftButton);
    REQUIRE(view.hasPendingGesture());

    send(QEvent::MouseButtonPress, QPointF(160, 100), Qt::LeftButton); // point de base
    send(QEvent::MouseButtonPress, QPointF(200, 100), Qt::LeftButton); // point d'arrivee
    CHECK_FALSE(view.hasPendingGesture());

    const auto *stretched = dynamic_cast<const Wire *>(folio->entity(leftId));
    REQUIRE(stretched);
    CHECK(stretched->points.first() == QPointF(40, 100)); // l'autre bout ne bouge pas
    CHECK(stretched->points.last() == QPointF(200, 100)); // le bout pris suit

    // Le fil hors de la fenetre est intact.
    const auto *untouched = dynamic_cast<const Wire *>(folio->entity(rightId));
    REQUIRE(untouched);
    CHECK(untouched->points.first() == QPointF(40, 140));

    document.undo();
    CHECK(dynamic_cast<const Wire *>(folio->entity(leftId))->points.last() == QPointF(160, 100));
}

TEST_CASE("Le panneau des rapports sort les onglets de cablage et de composants",
          "[ui][reports]")
{
    // Les rapports d'AutoCAD Electrical les plus utilises doivent etre la,
    // remplis, et suivre la portee choisie.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    folio->number = QStringLiteral("1");

    auto *a = placeSymbol(document.project(), folio, QStringLiteral("iec:contactor-power-3p"),
                          QPointF(100, 60));
    a->setDesignation(QStringLiteral("-KM1"));
    a->designationLocked = true;
    auto *b = placeSymbol(document.project(), folio, QStringLiteral("iec:motor-3ph"),
                          QPointF(100, 140));
    b->setDesignation(QStringLiteral("-M1"));
    b->designationLocked = true;
    document.invalidateNetlist();

    ReportPanel panel(&document);
    panel.resize(900, 500);
    panel.show();
    panel.refresh();

    // Les onglets attendus existent, dans l'ordre annonce.
    const QStringList expected{ QStringLiteral("Récapitulatif"), QStringLiteral("Nomenclature"),
                                QStringLiteral("Composants"),    QStringLiteral("Bornier"),
                                QStringLiteral("Fils"),          QStringLiteral("Câbles"),
                                QStringLiteral("Câblage De/Vers") };
    auto *tabs = panel.findChild<QTabWidget *>();
    REQUIRE(tabs);
    for (int i = 0; i < expected.size(); ++i)
        CHECK(tabs->tabText(i) == expected.at(i));

    // Le rapport de composants voit les deux appareils poses.
    tabs->setCurrentIndex(2);
    const ReportTable components = panel.currentTable();
    CHECK(components.rowCount() >= 2);

    // La portee par defaut est le projet entier.
    CHECK(panel.scope().isProject());
}

TEST_CASE("La boite du composant reunit repere, catalogue et rattachement",
          "[ui][component]")
{
    // C'est la boite la plus utilisee d'AutoCAD Electrical : tout ce qui
    // identifie un appareil doit y tenir en un ecran.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    folio->number = QStringLiteral("1");

    auto *symbol = placeSymbol(document.project(), folio,
                               QStringLiteral("iec:contactor-power-3p"), QPointF(120, 80));
    symbol->setDesignation(QStringLiteral("-KM1"));
    symbol->designationLocked = true;
    symbol->deviceGroup = QStringLiteral("km1");
    symbol->fields.insert(QStringLiteral("manufacturer"), QStringLiteral("Schneider"));
    symbol->fields.insert(QStringLiteral("partNumber"), QStringLiteral("LC1D09B7"));

    // Un second bloc du meme appareil, sur un autre folio.
    Folio *second = document.project().addFolio(QStringLiteral("Commande"));
    second->number = QStringLiteral("2");
    auto *contact = placeSymbol(document.project(), second, QStringLiteral("iec:contact-no"),
                                QPointF(90, 150));
    contact->setDesignation(QStringLiteral("-KM1"));
    contact->deviceGroup = QStringLiteral("km1");

    const Catalog catalog = Catalog::builtin();
    ComponentDialog dialog(document.project(), *symbol, catalog, false);
    dialog.resize(620, 560);
    dialog.show();

    // Sans rien toucher, la boite doit rendre exactement ce qu'elle a recu :
    // ouvrir puis valider ne doit jamais modifier un appareil.
    const SymbolInstance back = dialog.result();
    CHECK(back.designation() == QLatin1String("-KM1"));
    CHECK(back.designationLocked);
    CHECK(back.deviceGroup == QLatin1String("km1"));
    CHECK(back.fields.value(QStringLiteral("partNumber")) == QLatin1String("LC1D09B7"));
    CHECK(back.definitionId == symbol->definitionId);
    CHECK(back.placement.position == symbol->placement.position);
}

TEST_CASE("Le catalogue s'ouvre sur la famille du symbole", "[ui][component]")
{
    const Catalog catalog = Catalog::builtin();
    CatalogDialog dialog(catalog, QStringLiteral("contactor"));
    dialog.resize(760, 460);
    dialog.show();

    auto *table = dialog.findChild<QTableWidget *>();
    REQUIRE(table);
    CHECK(table->rowCount() == catalog.forDeviceKind(QStringLiteral("contactor")).size());
    CHECK(table->rowCount() > 0);
    CHECK(table->rowCount() < catalog.count()); // la famille filtre vraiment
}

TEST_CASE("Le reperage aligne un fil neuf sur le milieu d'un autre", "[ui][tracking]")
{
    // Le geste demande : retenir le milieu d'un fil existant, puis tracer un
    // fil qui s'arrete exactement a son aplomb, loin de toute geometrie.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    // Fil horizontal de 40 a 200 : son milieu est en x = 120.
    drawWire(folio, { QPointF(40, 60), QPointF(200, 60) });

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.snapEngine().setTrackingEnabled(true);
    view.snapEngine().setOrthoEnabled(true);

    // L'outil se choisit d'abord : changer d'outil relache les reperes, comme
    // la fin d'une commande chez AutoCAD.
    view.setTool(FolioView::Tool::Wire);
    // Acquisition directe : le survol prolonge est un detail d'interface, la
    // regle testee ici est ce que le repere fait une fois retenu.
    view.snapEngine().acquire(QPointF(120, 60), SnapMode::Midpoint);
    REQUIRE(view.snapEngine().isTracked(QPointF(120, 60)));

    auto click = [&](const QPointF &scene) {
        const QPointF at = view.mapFromScene(scene);
        QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &press);
    };

    click(QPointF(40, 160));   // depart, a gauche et plus bas
    // Le second point vise a peu pres l'aplomb du milieu : le reperage doit
    // le ramener exactement dessus.
    click(QPointF(122, 161));

    const QPointF right = view.mapFromScene(QPointF(122, 161));
    QMouseEvent stop(QEvent::MouseButtonPress, right, view.mapToGlobal(right), Qt::RightButton,
                     Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &stop);

    const Wire *drawn = nullptr;
    for (const EntityPtr &entity : folio->entities()) {
        const auto *wire = dynamic_cast<const Wire *>(entity.get());
        if (wire && wire->points.first() != QPointF(40, 60))
            drawn = wire;
    }
    REQUIRE(drawn);
    CHECK_THAT(drawn->points.last().x(), WithinAbs(120.0, 1e-9)); // pile a l'aplomb du milieu
    CHECK_THAT(drawn->points.last().y(), WithinAbs(160.0, 1e-9)); // sur l'horizontale du trace

    // Les reperes appartiennent a la commande : elle finie, ils sont oublies.
    CHECK(view.snapEngine().trackedPoints().isEmpty());
}

TEST_CASE("Sans repere acquis, le trace n'est pas devie", "[ui][tracking]")
{
    // Le reperage ne doit rien changer tant qu'aucun point n'est retenu :
    // un alignement surgi de nulle part serait pire que pas d'alignement.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    drawWire(folio, { QPointF(40, 60), QPointF(200, 60) });

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.snapEngine().setTrackingEnabled(true);
    view.snapEngine().setGridSnapEnabled(false);

    auto click = [&](const QPointF &scene, Qt::MouseButton button) {
        const QPointF at = view.mapFromScene(scene);
        QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), button, button,
                          Qt::NoModifier);
        QApplication::sendEvent(&view, &press);
    };

    view.setTool(FolioView::Tool::Wire);
    click(QPointF(40, 160), Qt::LeftButton);
    click(QPointF(122, 160), Qt::LeftButton);
    click(QPointF(122, 160), Qt::RightButton);

    const Wire *drawn = nullptr;
    for (const EntityPtr &entity : folio->entities()) {
        const auto *wire = dynamic_cast<const Wire *>(entity.get());
        if (wire && wire->points.first() != QPointF(40, 60))
            drawn = wire;
    }
    REQUIRE(drawn);
    CHECK_THAT(drawn->points.last().x(), WithinAbs(122.0, 1e-9)); // le curseur garde la main
}


TEST_CASE("Taper une cote pose le point a la distance voulue", "[ui][entry]")
{
    // Le geste d'AutoCAD : clic, on vise la direction, on tape la longueur.
    // Sans lui, toute cote exacte passe par la grille.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setTool(FolioView::Tool::Wire);

    auto click = [&](const QPointF &scene, Qt::MouseButton button) {
        const QPointF at = view.mapFromScene(scene);
        QMouseEvent e(QEvent::MouseButtonPress, at, view.mapToGlobal(at), button, button,
                      Qt::NoModifier);
        QApplication::sendEvent(&view, &e);
    };
    auto move = [&](const QPointF &scene) {
        const QPointF at = view.mapFromScene(scene);
        QMouseEvent e(QEvent::MouseMove, at, view.mapToGlobal(at), Qt::NoButton, Qt::NoButton,
                      Qt::NoModifier);
        QApplication::sendEvent(&view, &e);
    };
    auto type = [&](const QString &text) {
        for (const QChar c : text) {
            QKeyEvent e(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
            QApplication::sendEvent(&view, &e);
        }
    };
    auto enter = [&] {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&view, &e);
    };

    click(QPointF(60, 100), Qt::LeftButton);
    move(QPointF(140, 100));   // on vise a droite, distance quelconque
    type(QStringLiteral("50"));
    CHECK(view.typing());
    CHECK(view.typedText() == QLatin1String("50"));
    enter();
    CHECK_FALSE(view.typing());
    click(QPointF(0, 0), Qt::RightButton);   // termine le trace

    const Wire *drawn = nullptr;
    for (const EntityPtr &entity : folio->entities()) {
        if (const auto *wire = dynamic_cast<const Wire *>(entity.get()))
            drawn = wire;
    }
    REQUIRE(drawn);
    CHECK_THAT(drawn->points.first().x(), WithinAbs(60.0, 1e-9));
    // Exactement 50 mm a droite, quelle que soit la position du curseur.
    CHECK_THAT(drawn->points.last().x(), WithinAbs(110.0, 1e-9));
    CHECK_THAT(drawn->points.last().y(), WithinAbs(100.0, 1e-9));
}

TEST_CASE("Une cote relative tapee place le point au decalage voulu", "[ui][entry]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setTool(FolioView::Tool::Wire);

    auto click = [&](const QPointF &scene, Qt::MouseButton button) {
        const QPointF at = view.mapFromScene(scene);
        QMouseEvent e(QEvent::MouseButtonPress, at, view.mapToGlobal(at), button, button,
                      Qt::NoModifier);
        QApplication::sendEvent(&view, &e);
    };
    auto type = [&](const QString &text) {
        for (const QChar c : text) {
            QKeyEvent e(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
            QApplication::sendEvent(&view, &e);
        }
        QKeyEvent done(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&view, &done);
    };

    click(QPointF(60, 100), Qt::LeftButton);
    type(QStringLiteral("@40,30"));   // 40 a droite, 30 vers le haut
    click(QPointF(0, 0), Qt::RightButton);

    const Wire *drawn = nullptr;
    for (const EntityPtr &entity : folio->entities()) {
        if (const auto *wire = dynamic_cast<const Wire *>(entity.get()))
            drawn = wire;
    }
    REQUIRE(drawn);
    CHECK_THAT(drawn->points.last().x(), WithinAbs(100.0, 1e-9));
    CHECK_THAT(drawn->points.last().y(), WithinAbs(70.0, 1e-9));
}

TEST_CASE("Echap abandonne la cote, pas le trace", "[ui][entry]")
{
    // On s'est trompe de chiffre : on ne veut pas recommencer tout le fil.
    Document document;
    document.newProject(builtinLibrary());

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setTool(FolioView::Tool::Wire);

    const QPointF at = view.mapFromScene(QPointF(60, 100));
    QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    QKeyEvent five(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral("5"));
    QApplication::sendEvent(&view, &five);
    CHECK(view.typing());

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &escape);
    CHECK_FALSE(view.typing());
    // Le trace est toujours en cours : un second Echap seulement l'annule.
    CHECK(view.tool() == FolioView::Tool::Wire);
    QApplication::sendEvent(&view, &escape);
}

TEST_CASE("Hors commande, un chiffre reste libre", "[ui][entry]")
{
    // Sans geste en cours, taper un chiffre ne doit pas ouvrir un champ de
    // cote : il n'y a pas de point a poser.
    Document document;
    document.newProject(builtinLibrary());

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setTool(FolioView::Tool::Select);

    QKeyEvent five(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral("5"));
    QApplication::sendEvent(&view, &five);
    CHECK_FALSE(view.typing());
}


TEST_CASE("Le Surfer liste les autres blocs et les appareils raccordes",
          "[ui][surfer]")
{
    // Un appareil pose sur trois folios, un fil qui repart deux pages plus
    // loin : sans outil, les retrouver demande de feuilleter le dossier.
    Document document;
    document.newProject(builtinLibrary());
    Folio *first = document.currentFolio();
    first->number = QStringLiteral("1");
    Folio *second = document.project().addFolio(QStringLiteral("Commande"));
    second->number = QStringLiteral("2");

    auto *coil = placeSymbol(document.project(), first,
                             QStringLiteral("iec:contactor-power-3p"), QPointF(100, 60));
    coil->setDesignation(QStringLiteral("-KM1"));
    coil->deviceGroup = QStringLiteral("km1");
    auto *contact = placeSymbol(document.project(), second, QStringLiteral("iec:contact-no"),
                                QPointF(120, 140));
    contact->setDesignation(QStringLiteral("-KM1"));
    contact->deviceGroup = QStringLiteral("km1");
    document.invalidateNetlist();

    const auto sites = SurferDialog::sitesFor(document.project(), document.netlist(),
                                              coil->id());
    REQUIRE(sites.size() >= 1);
    // Le contact de l'autre folio est bien propose, avec son folio.
    bool foundContact = false;
    for (const auto &site : sites) {
        if (site.entityId == contact->id()) {
            foundContact = true;
            CHECK(site.folioId == second->id());
            CHECK(site.title.contains(QStringLiteral("-KM1")));
        }
    }
    CHECK(foundContact);

    // On ne se propose jamais soi-meme : sauter sur place n'aide personne.
    for (const auto &site : sites)
        CHECK(site.entityId != coil->id());
}

TEST_CASE("Le Surfer relie les deux bouts d'un signal", "[ui][surfer]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *first = document.currentFolio();
    first->number = QStringLiteral("1");
    Folio *second = document.project().addFolio();
    second->number = QStringLiteral("2");

    auto arrow = [&](Folio *folio, const QPointF &at, Label::Role role) {
        auto label = std::make_unique<Label>();
        label->point = at;
        label->name = QStringLiteral("CMD");
        label->role = role;
        label->scope = Label::Scope::Project;
        auto *raw = label.get();
        folio->addEntity(std::move(label));
        return raw;
    };
    Label *source = arrow(first, QPointF(100, 60), Label::Role::Source);
    Label *destination = arrow(second, QPointF(140, 90), Label::Role::Destination);
    document.invalidateNetlist();

    const auto sites = SurferDialog::sitesFor(document.project(), document.netlist(),
                                              source->id());
    REQUIRE(sites.size() == 1);
    CHECK(sites.first().entityId == destination->id());
    CHECK(sites.first().folioId == second->id());
    CHECK(sites.first().title.contains(QStringLiteral("destination")));
}

TEST_CASE("Glisser deplace l'appareil le long de son fil seulement",
          "[ui][scoot]")
{
    // C'est la garantie de Scoot : l'appareil ne quitte jamais sa ligne, donc
    // ne se detache jamais de son circuit.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    auto *symbol = placeSymbol(document.project(), folio, QStringLiteral("iec:fuse"),
                               QPointF(120, 100));
    const QString id = symbol->id();
    // Deux fils horizontaux qui arrivent sur ses broches.
    const SymbolDefinition *definition =
            document.project().library.definition(symbol->definitionId);
    REQUIRE(definition);
    REQUIRE(definition->pins.size() >= 2);
    const QPointF pinA = symbol->placement.map(definition->pins.at(0).position);
    const QPointF pinB = symbol->placement.map(definition->pins.at(1).position);
    Wire *up = drawWire(folio, { QPointF(pinA.x(), pinA.y() - 50.0), pinA });
    drawWire(folio, { pinB, QPointF(pinB.x(), pinB.y() + 50.0) });
    const QString upId = up->id();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setSelection({ id });
    view.beginScoot();
    REQUIRE(view.hasPendingGesture());

    // On vise en biais : seule la composante sur l'axe doit compter.
    const QPointF target(symbol->placement.position.x() + 40.0,
                         symbol->placement.position.y() + 30.0);
    const QPointF at = view.mapFromScene(target);
    QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    const auto *moved = dynamic_cast<const SymbolInstance *>(folio->entity(id));
    REQUIRE(moved);
    // Les fils sont verticaux : l'appareil n'a bouge qu'en y.
    CHECK_THAT(moved->placement.position.x(), WithinAbs(120.0, 1e-6));
    CHECK(moved->placement.position.y() > 100.0);

    // Et le fil du haut s'est allonge au lieu de se detacher.
    const auto *stretched = dynamic_cast<const Wire *>(folio->entity(upId));
    REQUIRE(stretched);
    CHECK_THAT(stretched->points.first().y(), WithinAbs(pinA.y() - 50.0, 1e-6));
    CHECK(stretched->points.last().y() > pinA.y());
}

TEST_CASE("Poser un appareil sur un fil le branche en coupant", "[ui][insert]")
{
    // Poser une borne sur un fil doit la brancher, pas la poser par-dessus en
    // laissant le fil passer derriere : le schema serait faux sans que rien
    // ne le montre.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    // Le fusible a ses broches a la verticale : le fil doit l'etre aussi,
    // sinon l'appareil serait en travers et le couper le laisserait en l'air.
    drawWire(folio, { QPointF(120, 40), QPointF(120, 200) });
    const int before = int(folio->entityCount());

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setPendingSymbol(QStringLiteral("iec:fuse"));

    const QPointF at = view.mapFromScene(QPointF(120, 120));
    QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    // Un fil de plus (coupe en deux) et le symbole : trois entites au lieu
    // d'une.
    CHECK(int(folio->entityCount()) == before + 2);

    QVector<const Wire *> wires;
    for (const EntityPtr &entity : folio->entities()) {
        if (const auto *wire = dynamic_cast<const Wire *>(entity.get()))
            wires.append(wire);
    }
    REQUIRE(wires.size() == 2);
    // Aucun morceau ne va plus d'un bout a l'autre : le fil est reellement
    // coupe, pas seulement recouvert.
    for (const Wire *wire : wires) {
        const bool spansWhole = wire->points.first() == QPointF(120, 40)
                && wire->points.last() == QPointF(120, 200);
        CHECK_FALSE(spansWhole);
    }

    // Et tout se defait d'une seule annulation.
    document.undo();
    CHECK(int(folio->entityCount()) == before);
}

TEST_CASE("Un appareil pose a cote du fil ne le coupe pas", "[ui][insert]")
{
    // La coupure ne doit se declencher que si l'appareil est vraiment sur le
    // fil : sinon poser un symbole quelque part casserait un fil voisin.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    drawWire(folio, { QPointF(120, 40), QPointF(120, 200) });
    const int before = int(folio->entityCount());

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();
    view.setPendingSymbol(QStringLiteral("iec:fuse"));

    const QPointF at = view.mapFromScene(QPointF(180, 120)); // bien a l'ecart
    QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    CHECK(int(folio->entityCount()) == before + 1); // seulement le symbole
}

TEST_CASE("L'editeur de borniers rassemble les bornes dans l'ordre du dossier",
          "[ui][terminals]")
{
    // Un bornier ne se lit pas sur le schema : ses bornes sont dispersees sur
    // plusieurs folios, et c'est leur ordre dans l'armoire qui compte.
    Document document;
    document.newProject(builtinLibrary());
    Folio *first = document.currentFolio();
    first->number = QStringLiteral("1");
    Folio *second = document.project().addFolio();
    second->number = QStringLiteral("2");

    auto terminal = [&](Folio *folio, const QPointF &at, const QString &block,
                        const QString &number) {
        auto *symbol = placeSymbol(document.project(), folio, QStringLiteral("iec:terminal"), at);
        symbol->setDesignation(block);
        symbol->designationLocked = true;
        symbol->fields.insert(QStringLiteral("terminal"), number);
        return symbol;
    };
    // Posees dans le desordre, sur deux folios et deux borniers.
    terminal(second, QPointF(100, 60), QStringLiteral("-X1"), QStringLiteral("3"));
    terminal(first, QPointF(100, 140), QStringLiteral("-X1"), QStringLiteral("2"));
    terminal(first, QPointF(100, 60), QStringLiteral("-X1"), QStringLiteral("1"));
    terminal(first, QPointF(160, 60), QStringLiteral("-X2"), QStringLiteral("1"));
    document.invalidateNetlist();

    const QStringList blocks = TerminalStripDialog::blocksOf(document.project());
    CHECK(blocks == QStringList{ QStringLiteral("-X1"), QStringLiteral("-X2") });

    const auto strip = TerminalStripDialog::terminalsOf(document.project(), document.netlist(),
                                                        QStringLiteral("-X1"));
    REQUIRE(strip.size() == 3);
    // Ordre de lecture : folio 1 de haut en bas, puis folio 2.
    CHECK(strip.at(0).folio == QLatin1String("1"));
    CHECK(strip.at(0).number == QLatin1String("1"));
    CHECK(strip.at(1).folio == QLatin1String("1"));
    CHECK(strip.at(1).number == QLatin1String("2"));
    CHECK(strip.at(2).folio == QLatin1String("2"));
    CHECK(strip.at(2).number == QLatin1String("3"));

    // Le second bornier n'est pas mele au premier.
    CHECK(TerminalStripDialog::terminalsOf(document.project(), document.netlist(),
                                           QStringLiteral("-X2")).size() == 1);
}

TEST_CASE("L'editeur de borniers montre le fil et l'appareil raccordes",
          "[ui][terminals]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    folio->number = QStringLiteral("1");

    auto *borne = placeSymbol(document.project(), folio, QStringLiteral("iec:terminal"),
                              QPointF(100, 60));
    borne->setDesignation(QStringLiteral("-X1"));
    borne->designationLocked = true;
    borne->fields.insert(QStringLiteral("terminal"), QStringLiteral("1"));

    auto *appareil = placeSymbol(document.project(), folio, QStringLiteral("iec:fuse"),
                                 QPointF(100, 140));
    appareil->setDesignation(QStringLiteral("-F1"));
    appareil->designationLocked = true;

    // Un fil entre la borne et l'appareil.
    const SymbolDefinition *terminalDef =
            document.project().library.definition(borne->definitionId);
    const SymbolDefinition *fuseDef =
            document.project().library.definition(appareil->definitionId);
    REQUIRE(terminalDef);
    REQUIRE(fuseDef);
    Wire *wire = drawWire(folio, { borne->placement.map(terminalDef->pins.last().position),
                                   appareil->placement.map(fuseDef->pins.first().position) });
    wire->number = QStringLiteral("101");
    wire->numberLocked = true;
    document.invalidateNetlist();

    const auto strip = TerminalStripDialog::terminalsOf(document.project(), document.netlist(),
                                                        QStringLiteral("-X1"));
    REQUIRE(strip.size() == 1);
    CHECK(strip.first().wireNumber == QLatin1String("101"));
    CHECK(strip.first().target == QLatin1String("-F1"));
    CHECK_FALSE(strip.first().zone.isEmpty());
}


TEST_CASE("La palette de commandes trouve par lettres non contigues",
          "[ui][palette]")
{
    // C'est ce qui la rend rapide : on tape les lettres qui viennent, sans
    // se souvenir du libelle exact ni de l'endroit ou la commande est rangee.
    CHECK(CommandPalette::matches(QStringLiteral("psrap"),
                                  QStringLiteral("Poser le rapport dans le dessin")));
    CHECK(CommandPalette::matches(QStringLiteral("bornier"),
                                  QStringLiteral("Éditeur de borniers")));
    // Les accents ne doivent pas etre un peage : personne ne doit taper
    // « Repérage » avec son accent pour trouver la commande.
    CHECK(CommandPalette::matches(QStringLiteral("reperage"),
                                  QStringLiteral("Repérage d'accrochage")));
    CHECK(CommandPalette::matches(QStringLiteral("ÉDIT"), QStringLiteral("éditeur")));
    CHECK_FALSE(CommandPalette::matches(QStringLiteral("zzz"),
                                        QStringLiteral("Poser le rapport")));
}

TEST_CASE("Le classement met devant ce qu'on cherchait", "[ui][palette]")
{
    CommandPalette::Entry offset;
    offset.title = QStringLiteral("Décaler…");
    offset.keywords = { QStringLiteral("DECALER"), QStringLiteral("DC") };

    CommandPalette::Entry other;
    other.title = QStringLiteral("Éditeur de borniers");
    other.detail = QStringLiteral("Rassembler les bornes, dont celles décalées");

    // Un titre qui commence par ce qu'on tape passe devant tout.
    CHECK(CommandPalette::score(QStringLiteral("déca"), offset)
          < CommandPalette::score(QStringLiteral("déca"), other));
    // Un alias exact aussi : qui tape « DC » veut DÉCALER.
    CHECK(CommandPalette::score(QStringLiteral("DC"), offset) <= 1);
    // Ce qui ne correspond pas est ecarte, pas classe dernier.
    CHECK(CommandPalette::score(QStringLiteral("zzzz"), offset) < 0);
}

TEST_CASE("La palette expose tout ce que les menus offrent", "[ui][palette]")
{
    // La promesse : rien ne se cache. Si une commande existe dans un menu,
    // elle doit se trouver a la palette sans savoir dans quel menu regarder.
    MainWindow window;
    window.resize(1400, 900);
    window.show();

    auto *palette = window.findChild<CommandPalette *>();
    // La palette est construite paresseusement : on l'ouvre pour la remplir.
    if (!palette) {
        QMetaObject::invokeMethod(&window, "openCommandPalette");
        palette = window.findChild<CommandPalette *>();
    }
    REQUIRE(palette);

    const auto entries = palette->visibleEntries();
    CHECK(entries.size() > 60);

    // Quelques commandes qui doivent absolument s'y trouver.
    auto has = [&](const QString &needle) {
        for (const auto &entry : entries) {
            if (entry.title.contains(needle, Qt::CaseInsensitive))
                return true;
        }
        return false;
    };
    CHECK(has(QStringLiteral("bornier")));
    CHECK(has(QStringLiteral("types de fils")));
    CHECK(has(QStringLiteral("surfer")));
    CHECK(has(QStringLiteral("étirer")));

    // Chaque entree sait ce qu'elle fait : une entree sans action serait un
    // mensonge dans la liste.
    for (const auto &entry : entries)
        CHECK(entry.run != nullptr);

    palette->close();
}

// --------------------------------------------------------------------------
// Identite visuelle

TEST_CASE("Les plans du theme vont du plus profond au plus clair", "[ui][theme]")
{
    // Le fond du dessin passe sous le chrome, et le chrome sous les panneaux :
    // c'est cet ordre, et lui seul, qui fait flotter la feuille au lieu de la
    // poser a plat sur un gris etranger. Quelqu'un retouchera une couleur un
    // jour ; le test dit dans quel ordre elles doivent rester.
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    REQUIRE(app);

    for (const bool dark : { true, false }) {
        Theme::apply(*app, dark);
        const ThemeColors &c = Theme::colors();
        CAPTURE(dark);
        CHECK(c.dark == dark);

        if (dark) {
            CHECK(c.canvas.lightness() < c.window.lightness());
            CHECK(c.window.lightness() < c.surface.lightness());
            CHECK(c.surface.lightness() <= c.elevated.lightness());
        } else {
            // En clair l'echelle est renversee : la feuille est ce qu'il y a
            // de plus clair, et le fond du canevas ce qu'il y a de plus fonce.
            CHECK(c.canvas.lightness() < c.window.lightness());
            CHECK(c.window.lightness() < c.surface.lightness());
        }

        // Trois niveaux d'encre, dans l'ordre : le texte porte, le retrait
        // accompagne, l'etiquette gravee s'efface. Deux niveaux qui se
        // rejoindraient rendraient l'un des deux inutile.
        const int surface = c.surface.lightness();
        CHECK(std::abs(c.text.lightness() - surface) > 120);
        CHECK(std::abs(c.textMuted.lightness() - surface)
              > std::abs(c.textFaint.lightness() - surface));
    }
    Theme::apply(*app, true);
}

TEST_CASE("L'echelle d'espacement n'a qu'un pas", "[ui][theme]")
{
    // Toutes les marges du logiciel sortent d'un seul pas de quatre pixels :
    // c'est ce qui donne le meme rythme d'une boite de dialogue a l'autre.
    CHECK(Theme::space() == 4);
    CHECK(Theme::space(2) == 8);
    CHECK(Theme::space(5) == 20);
    CHECK(Theme::gap() == Theme::space(2));
}

TEST_CASE("L'etiquette gravee est en capitales et espacee", "[ui][theme]")
{
    // Qt ne sait pas mettre un texte en capitales depuis une feuille de style :
    // la mise en capitales passe donc par la fonte, et c'est elle qui doit la
    // porter — sans quoi chaque appelant devrait y penser.
    const QFont engraved = Theme::engravedFont();
    CHECK(engraved.capitalization() == QFont::AllUppercase);
    CHECK(engraved.letterSpacing() > 0.0);
    CHECK(engraved.pointSizeF() < Theme::uiFont(10).pointSizeF());

    // Et la chasse fixe est bien fixe : une coordonnee qui change ne doit pas
    // faire danser celles d'a cote.
    const QFontMetricsF metrics(Theme::monoFont());
    CHECK_THAT(metrics.horizontalAdvance(QStringLiteral("0")),
               WithinAbs(metrics.horizontalAdvance(QStringLiteral("8")), 0.01));
}

TEST_CASE("Aucun nom ni alias de commande n'est enregistre deux fois", "[ui][commandline]")
{
    // Un doublon ne casse rien visiblement : la seconde inscription masque la
    // premiere, et une commande devient injoignable sans que personne ne le
    // remarque. C'est arrive une fois — « ECHELLE » designait a la fois
    // l'homothetie et l'echelle de commande, et « SU » a la fois supprimer et
    // le Surfer.
    MainWindow window;
    CommandLine *line = window.findChild<CommandLine *>();
    REQUIRE(line);

    QSet<QString> seen;
    for (const CommandDefinition &command : line->commands()) {
        QStringList tokens{ command.name };
        tokens += command.aliases;
        for (const QString &token : tokens) {
            const QString key = token.toUpper();
            CAPTURE(command.name, key);
            CHECK_FALSE(seen.contains(key));
            seen.insert(key);
        }
    }
}

TEST_CASE("La fonte gravee ne descend pas dans le contenu des panneaux", "[ui][theme]")
{
    // Qt ne propage a un enfant que les attributs poses sur sa fonte. Poser
    // la fonte gravee sur un panneau et la fonte d'interface sur son contenu
    // ne suffisait pas : la mise en capitales, que uiFont ne mentionnait pas,
    // continuait de descendre — et toute la liste des symboles se lisait en
    // majuscules.
    MainWindow window;
    const QList<QDockWidget *> docks = window.findChildren<QDockWidget *>();
    REQUIRE_FALSE(docks.isEmpty());

    for (QDockWidget *dock : docks) {
        CAPTURE(dock->objectName());
        // Les panneaux portent maintenant leur propre barre de titre, avec le
        // bouton qui les tasse : c'est son etiquette qui est gravee, et non le
        // panneau entier. Le piege reste le meme — ce qui est grave ne doit
        // pas descendre dans le contenu.
        if (auto *title = qobject_cast<DockTitle *>(dock->titleBarWidget())) {
            auto *label = title->findChild<QLabel *>();
            REQUIRE(label);
            CHECK(label->font().capitalization() == QFont::AllUppercase);
        }
        if (QWidget *content = dock->widget())
            CHECK(content->font().capitalization() == QFont::MixedCase);
    }
}

TEST_CASE("Une commande sans sélection demande au lieu de refuser", "[ui][commandes]")
{
    // Le défaut central du logiciel, signalé par l'utilisateur : « je ne peux
    // même pas cliquer sur l'outil couper un fil ». Il n'était pas cassé — il
    // exigeait une sélection préalable et refusait sinon, en écrivant dans la
    // barre d'état, à l'opposé du regard de qui vient de cliquer dans le ruban.
    //
    // AutoCAD fait l'inverse : on lance la commande, ELLE demande ce qu'il lui
    // faut. C'est ce que ce test exige, pour les six commandes du canevas.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    drawWire(folio, { QPointF(40, 60), QPointF(160, 60) });
    placeSymbol(document.project(), folio, QStringLiteral("iec:coil"), QPointF(100, 100));

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();

    struct Cas {
        const char *nom;
        std::function<void(FolioView &)> lancer;
    };
    const std::vector<Cas> cas{
        { "Déplacer", [](FolioView &v) { v.beginMoveSelection(); } },
        { "Décaler", [](FolioView &v) { v.beginOffset(5.0); } },
        { "Échelle", [](FolioView &v) { v.beginScale(); } },
        { "Couper un fil", [](FolioView &v) { v.beginCut(); } },
        { "Glisser le long du fil", [](FolioView &v) { v.beginScoot(); } },
        { "Déplacer l'appareil", [](FolioView &v) { v.beginMoveComponent(); } },
    };

    for (const Cas &c : cas) {
        INFO("commande : " << c.nom);
        view.clearSelection();
        c.lancer(view);
        // Elle ne s'est pas contentée d'un refus : elle attend qu'on désigne.
        CHECK(view.isPicking());
        // Et Échap rend la main proprement.
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&view, &escape);
        CHECK_FALSE(view.isPicking());
    }
}

TEST_CASE("La désignation filtre, valide et rend la main à la commande",
          "[ui][commandes]")
{
    // Le cycle complet de « Select objects: ». Le filtre compte autant que le
    // reste : désigner un appareil quand la commande attend un fil doit être
    // refusé et dit, pas ignoré — un clic ignoré passe pour un clic raté.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *wire = drawWire(folio, { QPointF(40, 60), QPointF(160, 60) });
    auto *coil = placeSymbol(document.project(), folio, QStringLiteral("iec:coil"),
                             QPointF(100, 120));

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);

    view.beginCut();
    REQUIRE(view.isPicking());

    // L'appareil ne convient pas : il n'entre pas dans la désignation.
    clickScene(view, coil->placement.position);
    CHECK(view.selection().isEmpty());
    CHECK(view.isPicking());

    // Le fil convient.
    clickScene(view, QPointF(100, 60));
    CHECK(view.selection().contains(wire->id()));

    // Entrée valide et la commande reprend : elle attend maintenant le point
    // de coupure, ce qui n'est plus une désignation.
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&view, &enter);
    CHECK_FALSE(view.isPicking());
    CHECK(view.hasPendingGesture());

    // Et le geste va jusqu'au bout : le fil est coupé en deux.
    clickScene(view, QPointF(100, 60));
    CHECK(folio->entitiesOfType<Wire>().size() == 2);
}

TEST_CASE("Une commande part directement si la sélection convient déjà",
          "[ui][commandes]")
{
    // AutoCAD accepte les deux sens — sélectionner puis agir, ou agir puis
    // désigner. Perdre le premier en gagnant le second serait un mauvais
    // échange : c'est le geste de l'habitué.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    Wire *wire = drawWire(folio, { QPointF(40, 60), QPointF(160, 60) });

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();

    view.setSelection({ wire->id() });
    view.beginCut();
    CHECK_FALSE(view.isPicking());   // rien à demander
    CHECK(view.hasPendingGesture()); // elle attend déjà le point de coupure
}

TEST_CASE("Une commande qui ne peut rien faire est grisée", "[ui][commandes]")
{
    // Avant le bloc A, 62 actions sur 66 restaient noires et cliquables quoi
    // qu'il arrive : elles écrivaient un refus dans la barre d'état, à
    // l'opposé du regard de qui vient de cliquer dans le ruban. Un bouton qui
    // a l'air disponible et ne répond pas est un bouton cassé.
    //
    // La règle du grisage est « impossible », pas « rien de sélectionné » :
    // une commande qui a besoin d'objets les demande.
    MainWindow window;
    window.resize(1400, 900);

    auto byLabel = [&](const QString &label) -> QAction * {
        for (QAction *action : window.findChildren<QAction *>()) {
            if (action->text().remove(QLatin1Char('&')) == label)
                return action;
        }
        return nullptr;
    };

    QAction *cut = byLabel(QStringLiteral("Couper un fil"));
    QAction *join = byLabel(QStringLiteral("Joindre les fils"));
    QAction *scoot = byLabel(QStringLiteral("Glisser le long du fil"));
    QAction *zoomFit = byLabel(QStringLiteral("Ajuster au folio"));
    REQUIRE(cut);
    REQUIRE(join);
    REQUIRE(scoot);
    REQUIRE(zoomFit);

    // Folio vide : rien à couper, rien à joindre, aucun appareil à glisser.
    CHECK_FALSE(cut->isEnabled());
    CHECK_FALSE(join->isEnabled());
    CHECK_FALSE(scoot->isEnabled());
    // Mais ce qui ne dépend de rien reste disponible.
    CHECK(zoomFit->isEnabled());

    Document *document = window.findChild<Document *>();
    REQUIRE(document);
    Folio *folio = document->currentFolio();
    REQUIRE(folio);

    // On passe par une vraie commande : le grisage doit se rafraîchir tout
    // seul quand le dessin change, sinon il ment jusqu'au prochain clic.
    auto addWire = [&](const QPointF &from, const QPointF &to) {
        auto wire = std::make_unique<Wire>();
        wire->points = { from, to };
        document->push(std::make_unique<AddEntityCommand>(document->project(), folio->id(),
                                                          std::move(wire),
                                                          QStringLiteral("Tracer")));
    };

    addWire(QPointF(40, 60), QPointF(120, 60));
    CHECK(cut->isEnabled());
    CHECK_FALSE(join->isEnabled()); // il en faut deux
    CHECK_FALSE(scoot->isEnabled());

    addWire(QPointF(120, 60), QPointF(200, 60));
    CHECK(join->isEnabled());

    // Un appareil : glisser le long du fil devient possible.
    auto coil = std::make_unique<SymbolInstance>();
    coil->definitionId = QStringLiteral("iec:coil");
    coil->placement.position = QPointF(100, 100);
    document->push(std::make_unique<AddEntityCommand>(document->project(), folio->id(),
                                                      std::move(coil),
                                                      QStringLiteral("Poser")));
    CHECK(scoot->isEnabled());

    // Et tout redevient impossible si l'on annule jusqu'au folio vide.
    while (document->commands().canUndo())
        document->undo();
    CHECK_FALSE(cut->isEnabled());
    CHECK_FALSE(join->isEnabled());
    CHECK_FALSE(scoot->isEnabled());
}

TEST_CASE("Un panneau se tasse par son bouton et revient par sa commande",
          "[ui][panneaux]")
{
    // Demande utilisateur : un bouton visible pour rendre la place au dessin.
    // La croix que Qt dessine est minuscule et la feuille de style du theme
    // l'efface — il n'y avait donc aucun moyen visible de fermer un panneau.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();

    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("dock.symbols"));
    REQUIRE(dock);
    auto *title = qobject_cast<DockTitle *>(dock->titleBarWidget());
    REQUIRE(title);
    REQUIRE(dock->isVisible());

    auto *button = title->findChild<QToolButton *>();
    REQUIRE(button);
    // Le bouton dit comment revenir : un panneau qui disparait sans laisser
    // de chemin de retour coute plus cher qu'il ne rend.
    CHECK(button->toolTip().contains(QStringLiteral("Ctrl+3")));

    // L'icone doit se VOIR, pas seulement exister. Le padding general des
    // QToolButton l'ecrasait a deux pixels de large dans un bouton de vingt :
    // le bouton repondait au clic et restait invisible. Un test qui ne
    // verifie que sa presence ne l'aurait jamais releve.
    CHECK_FALSE(button->icon().isNull());
    const QImage painted = button->grab().toImage();
    int inked = 0;
    for (int y = 0; y < painted.height(); ++y) {
        for (int x = 0; x < painted.width(); ++x) {
            if (painted.pixel(x, y) != painted.pixel(0, 0))
                ++inked;
        }
    }
    CHECK(inked > 20);

    button->click();
    CHECK_FALSE(dock->isVisible());

    // Et la commande le ramene — cochee ou decochee, elle bascule.
    QAction *toggle = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(QLatin1Char('&')) == QStringLiteral("Palette de symboles"))
            toggle = action;
    }
    REQUIRE(toggle);
    CHECK(toggle->isCheckable());
    CHECK_FALSE(toggle->isChecked());

    toggle->trigger();
    CHECK(dock->isVisible());
    CHECK(toggle->isChecked());
}

TEST_CASE("Toutes les barres d'outils tiennent dans la fenetre", "[ui][theme]")
{
    // Une barre qui deborde ne le dit pas : Qt masque simplement la fin, et
    // des commandes deviennent introuvables sans que rien ne le signale.
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();

    for (QToolBar *bar : window.findChildren<QToolBar *>()) {
        CAPTURE(bar->objectName());
        int hidden = 0;
        for (QAction *action : bar->actions()) {
            const QWidget *button = bar->widgetForAction(action);
            if (button && !button->isVisible())
                ++hidden;
        }
        CHECK(hidden == 0);
    }
}

// --------------------------------------------------------------------------
// Dessin de formes

namespace {

// Un clic complet a un point du dessin. Les tests de trace en enchainent
// plusieurs : une forme se donne en deux ou trois points.
void doubleClickScene(FolioView &view, const QPointF &scene)
{
    const QPointF widget = view.mapFromScene(scene);
    QMouseEvent event(QEvent::MouseButtonDblClick, widget, view.mapToGlobal(widget),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &event);
}

void clickScene(FolioView &view, const QPointF &scene)
{
    const QPointF widget = view.mapFromScene(scene);
    QMouseEvent move(QEvent::MouseMove, widget, view.mapToGlobal(widget), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QMouseEvent press(QEvent::MouseButtonPress, widget, view.mapToGlobal(widget),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, widget, view.mapToGlobal(widget),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &move);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &release);
}

const GraphicItem *soleGraphic(const Folio &folio)
{
    const auto graphics = folio.entitiesOfType<GraphicItem>();
    return graphics.size() == 1 ? graphics.front() : nullptr;
}

} // namespace

TEST_CASE("Le rectangle se trace en deux clics", "[ui][dessin]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);
    view.setTool(FolioView::Tool::Rectangle);

    clickScene(view, QPointF(60, 60));
    clickScene(view, QPointF(140, 110));

    const GraphicItem *shape = soleGraphic(*folio);
    REQUIRE(shape);
    CHECK(shape->shape.kind == Primitive::Kind::Rect);
    const QRectF box = shape->boundingBox();
    CHECK_THAT(box.width(), WithinAbs(80.0, 0.6));
    CHECK_THAT(box.height(), WithinAbs(50.0, 0.6));

    // L'outil reste arme : on trace rarement un seul encadre.
    CHECK(view.tool() == FolioView::Tool::Rectangle);
}

TEST_CASE("Le cercle prend son centre puis son rayon", "[ui][dessin]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);
    view.setTool(FolioView::Tool::Circle);

    clickScene(view, QPointF(100, 100));
    clickScene(view, QPointF(130, 100));

    const GraphicItem *shape = soleGraphic(*folio);
    REQUIRE(shape);
    CHECK(shape->shape.kind == Primitive::Kind::Circle);
    CHECK_THAT(shape->shape.radius, WithinAbs(30.0, 0.6));
}

TEST_CASE("L'arc passe par ses trois points", "[ui][dessin]")
{
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);
    view.setTool(FolioView::Tool::Arc);

    // Demi-cercle de rayon 40 centre en (100,100) : depart a droite, sommet
    // en haut, arrivee a gauche.
    clickScene(view, QPointF(140, 100));
    clickScene(view, QPointF(100, 60));
    clickScene(view, QPointF(60, 100));

    const GraphicItem *shape = soleGraphic(*folio);
    REQUIRE(shape);
    CHECK(shape->shape.kind == Primitive::Kind::Arc);
    CHECK_THAT(shape->shape.radius, WithinAbs(40.0, 0.8));
    // Le sens retenu est celui qui passe par le point du milieu. Sans ce
    // choix, un arc sur deux part du mauvais cote du cercle.
    CHECK(shape->shape.spanAngle > 0.0);
    CHECK_THAT(std::abs(shape->shape.spanAngle), WithinAbs(180.0, 2.0));
}

TEST_CASE("Trois points alignes ne font pas un arc", "[ui][dessin]")
{
    // Poser un segment a la place laisserait croire que c'est un arc.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);
    view.setTool(FolioView::Tool::Arc);

    clickScene(view, QPointF(60, 100));
    clickScene(view, QPointF(100, 100));
    clickScene(view, QPointF(140, 100));

    CHECK(folio->entitiesOfType<GraphicItem>().empty());
}

TEST_CASE("Une forme creuse se designe par son contour", "[ui][dessin][selection]")
{
    // Sans cette regle, un encadre de zone avalerait tous les clics de la
    // zone qu'il encadre — et il est justement fait pour en encadrer une.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    auto frame = std::make_unique<GraphicItem>();
    frame->shape = Primitive::rect(QRectF(60, 60, 100, 60));
    const QString frameId = frame->id();
    folio->addEntity(std::move(frame));

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);

    // Au milieu du rectangle : rien n'est pris.
    clickScene(view, QPointF(110, 90));
    CHECK(view.selection().isEmpty());

    // Sur son bord : il est pris.
    clickScene(view, QPointF(110, 60));
    CHECK(view.selection().contains(frameId));
}

TEST_CASE("Ortho ne s'applique pas aux formes qu'il aplatirait", "[ui][dessin]")
{
    // Ortho est allume par defaut. Applique au coin oppose d'un rectangle il
    // l'aplatit, au rayon d'un cercle il le force sur un axe, aux points d'un
    // arc il les aligne — trois outils sur six seraient inutilisables au
    // premier essai. La contrainte ne vaut donc que la ou une droite est le
    // sujet : fil, ligne, polyligne, deplacement.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);
    REQUIRE(view.snapEngine().orthoEnabled());

    view.setTool(FolioView::Tool::Rectangle);
    clickScene(view, QPointF(60, 60));
    clickScene(view, QPointF(140, 110));

    const GraphicItem *shape = soleGraphic(*folio);
    REQUIRE(shape);
    CHECK(shape->boundingBox().height() > 40.0);

    // Mais la ligne, elle, reste bien contrainte : c'est tout l'interet
    // d'ortho, et un trait de rappel se trace droit.
    view.setTool(FolioView::Tool::Line);
    clickScene(view, QPointF(60, 200));
    clickScene(view, QPointF(140, 215));

    const auto graphics = folio->entitiesOfType<GraphicItem>();
    REQUIRE(graphics.size() == 2);
    const Primitive &line = graphics.back()->shape;
    REQUIRE(line.points.size() == 2);
    CHECK_THAT(line.points.first().y(), WithinAbs(line.points.last().y(), 1e-6));
}

TEST_CASE("Le fil multiple pose ses conducteurs en une annulation", "[ui][bus]")
{
    // Trois conducteurs poses et trois annulations pour les retirer serait un
    // piege : le geste est unique, l'annulation doit l'etre aussi.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);

    BusSpec spec;
    spec.count = 3;
    spec.spacing = 5.0;
    view.setBus(spec);
    REQUIRE(view.tool() == FolioView::Tool::Wire);

    clickScene(view, QPointF(60, 60));
    clickScene(view, QPointF(160, 60));
    QKeyEvent done(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&view, &done);

    const auto wires = folio->entitiesOfType<Wire>();
    REQUIRE(wires.size() == 3);
    QStringList names;
    for (const Wire *wire : wires)
        names << wire->conductorName(0);
    std::sort(names.begin(), names.end());
    CHECK(names == QStringList{ QStringLiteral("L1"), QStringLiteral("L2"),
                                QStringLiteral("L3") });

    document.undo();
    CHECK(folio->entitiesOfType<Wire>().empty());
}

TEST_CASE("Un bus se raccorde par nom, jamais par voisinage", "[ui][bus][netlist]")
{
    // La raison d'etre du nom de conducteur : deux bus qui se croisent ou se
    // touchent doivent joindre L1 a L1 et laisser L2 tranquille. Sans le nom,
    // la netlist apparie par rang, et le rang depend de l'ordre de saisie.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();
    view.snapEngine().setGridSnapEnabled(false);

    BusSpec spec;
    spec.count = 3;
    spec.spacing = 10.0;
    view.setBus(spec);

    clickScene(view, QPointF(60, 60));
    clickScene(view, QPointF(160, 60));
    QKeyEvent done(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&view, &done);
    REQUIRE(folio->entitiesOfType<Wire>().size() == 3);

    // Un second bus dans le prolongement du premier : les extremites se
    // touchent conducteur par conducteur.
    view.setBus(spec);
    clickScene(view, QPointF(160, 60));
    clickScene(view, QPointF(240, 60));
    QKeyEvent done2(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&view, &done2);
    REQUIRE(folio->entitiesOfType<Wire>().size() == 6);

    // Six fils, mais trois potentiels : L1 avec L1, L2 avec L2, L3 avec L3.
    const Netlist &netlist = document.netlist();
    int connected = 0;
    for (const Netlist::Net &net : netlist.nets()) {
        if (net.wires.size() >= 2)
            ++connected;
    }
    CHECK(connected == 3);
}

TEST_CASE("Le bus tombe des qu'on quitte l'outil fil", "[ui][bus]")
{
    // Un bus qui survivrait a un changement d'outil ferait tracer trois
    // conducteurs a qui n'en demandait qu'un — et l'erreur ne se voit qu'a la
    // netlist, longtemps apres.
    Document document;
    document.newProject(builtinLibrary());

    FolioView view(&document);
    view.resize(900, 640);

    BusSpec spec;
    spec.count = 3;
    view.setBus(spec);
    CHECK(view.busArmed());

    view.setTool(FolioView::Tool::Select);
    CHECK_FALSE(view.busArmed());

    view.setTool(FolioView::Tool::Wire);
    CHECK_FALSE(view.busArmed());
}

TEST_CASE("Le type de fil s'applique a une selection deja tracee", "[ui][wiretype]")
{
    // Le selecteur du ruban n'arme que le trace a venir : sans cette commande,
    // changer le type d'un depart deja dessine demande de le retracer.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    Wire *a = drawWire(folio, { QPointF(40, 60), QPointF(120, 60) });
    Wire *b = drawWire(folio, { QPointF(40, 80), QPointF(120, 80) });
    b->wireType = QStringLiteral("commande");

    FolioView view(&document);
    view.resize(900, 640);
    view.setSelection({ a->id(), b->id() });

    CHECK(view.applyWireTypeToSelection(QStringLiteral("commande")) == 1);
    CHECK(a->wireType == QStringLiteral("commande"));

    document.undo();
    CHECK(a->wireType.isEmpty());
}

TEST_CASE("Fixer un repere vide ne fixe rien", "[ui][reperes]")
{
    // Un verrou sans repere est une promesse que personne ne tient : la
    // renumerotation l'ignore, et le cadenas affiche mentirait.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    Wire *tagged = drawWire(folio, { QPointF(40, 60), QPointF(120, 60) });
    tagged->number = QStringLiteral("101");
    Wire *bare = drawWire(folio, { QPointF(40, 80), QPointF(120, 80) });

    FolioView view(&document);
    view.resize(900, 640);
    view.setSelection({ tagged->id(), bare->id() });

    CHECK(view.setSelectionTagsLocked(true) == 1);
    CHECK(tagged->numberLocked);
    CHECK_FALSE(bare->numberLocked);
}

TEST_CASE("Un repere fixe survit a la renumerotation, un repere libere non",
          "[ui][reperes]")
{
    // Ce que la commande sert a preparer : on fixe ce qu'on a saisi, puis on
    // relance Ctrl+R sans craindre de le perdre.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    auto *coil = placeSymbol(document.project(), folio, QStringLiteral("iec:coil"),
                             QPointF(100, 100));
    coil->setDesignation(QStringLiteral("KM-SPECIAL"));

    FolioView view(&document);
    view.resize(900, 640);
    view.setSelection({ coil->id() });
    REQUIRE(view.setSelectionTagsLocked(true) == 1);

    Numbering::renumberAll(document.project(), document.profile());
    CHECK(coil->designation() == QStringLiteral("KM-SPECIAL"));

    REQUIRE(view.setSelectionTagsLocked(false) == 1);
    Numbering::renumberAll(document.project(), document.profile());
    CHECK(coil->designation() != QStringLiteral("KM-SPECIAL"));
}

TEST_CASE("Le conseil du folio vide est calé au centre de la feuille", "[ui][accueil]")
{
    // Decision utilisateur (2026-09-02) : le conseil etait ancre a la fenetre,
    // donc il se decalait des qu'un panneau s'ouvrait et ne tombait au milieu
    // de la feuille dans aucune configuration. Il suit maintenant la feuille —
    // et ce test le verifie a deux tailles de vue, parce que c'est justement
    // le changement de taille qui le mettait de travers.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.show();

    for (const QSize &size : { QSize(900, 640), QSize(1400, 500) }) {
        view.resize(size);
        view.zoomToFit();
        REQUIRE(view.showsEmptyHint());

        const QPointF sheetCentre = view.mapFromScene(folio->sheetRect().center());
        const QPointF hintCentre = view.emptyHintRect().center();
        CHECK_THAT(hintCentre.x(), WithinAbs(sheetCentre.x(), 1.0));
        CHECK_THAT(hintCentre.y(), WithinAbs(sheetCentre.y(), 1.0));
    }

    // Et il disparait des le premier element pose : un conseil qui reste
    // par-dessus le dessin devient une gene.
    drawWire(folio, { QPointF(40, 60), QPointF(120, 60) });
    CHECK_FALSE(view.showsEmptyHint());
    CHECK(view.emptyHintRect().isNull());
}

TEST_CASE("Le conseil garde la même proportion à tous les zooms", "[ui][accueil]")
{
    // Demande utilisateur : « je ne veux pas que le message grossisse en
    // zoomant dézoomant, je veux qu'il reste au milieu de la feuille, pas très
    // gros, 1/10 de la feuille ». Composé à taille de pixel constante, il
    // paraissait énorme sur une feuille dézoomée et minuscule sur une feuille
    // zoomée. Lié à la feuille, son rapport à elle ne bouge plus.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();

    auto ratio = [&] {
        const QRectF sheet = QRectF(view.mapFromScene(folio->sheetRect().topLeft()),
                                    view.mapFromScene(folio->sheetRect().bottomRight()))
                                     .normalized();
        REQUIRE(sheet.height() > 1.0);
        return view.emptyHintRect().height() / sheet.height();
    };

    view.zoomToFit();
    const double fitted = ratio();
    CHECK_THAT(fitted, WithinAbs(FolioView::kHintSheetFraction, 0.01));

    // Deux fois plus près : la feuille double dans la vue, le conseil aussi.
    view.setZoom(view.zoom() * 2.0);
    CHECK_THAT(ratio(), WithinAbs(fitted, 0.01));

    // Et deux fois plus loin.
    view.setZoom(view.zoom() / 4.0);
    CHECK_THAT(ratio(), WithinAbs(fitted, 0.01));
}

TEST_CASE("Le conseil suit la feuille quand elle se deplace", "[ui][accueil]")
{
    // C'est la promesse exacte : « collé au centre de la feuille ». Zoomer
    // deplace la feuille dans la vue, et le conseil doit partir avec elle.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();

    FolioView view(&document);
    view.resize(1000, 700);
    view.show();
    view.zoomToFit();

    const QPointF before = view.emptyHintRect().center();
    view.zoomToRect(QRectF(folio->sheetRect().left(), folio->sheetRect().top(),
                           folio->sheetRect().width() * 0.6,
                           folio->sheetRect().height() * 0.6));
    const QPointF after = view.emptyHintRect().center();

    // La vue a change : si le conseil etait ancre a la fenetre, il n'aurait
    // pas bouge d'un pixel.
    CHECK((before - after).manhattanLength() > 1.0);
}

TEST_CASE("Le double-clic dans le vide demande les proprietes du folio", "[ui][proprietes]")
{
    // Decision utilisateur (2026-09-02) : le panneau ancre a droite disparait,
    // et le double-clic ouvre la meme fiche la ou l'on regarde deja.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    auto *coil = placeSymbol(document.project(), folio, QStringLiteral("iec:coil"),
                             QPointF(100, 100));

    FolioView view(&document);
    view.resize(900, 640);
    view.show();
    view.zoomToFit();

    int folioAsked = 0;
    QString entityAsked;
    QObject::connect(&view, &FolioView::folioActivated, [&folioAsked] { ++folioAsked; });
    QObject::connect(&view, &FolioView::entityActivated,
                     [&entityAsked](const QString &id) { entityAsked = id; });

    doubleClickScene(view, QPointF(250, 200)); // loin de tout
    CHECK(folioAsked == 1);
    CHECK(entityAsked.isEmpty());

    doubleClickScene(view, coil->placement.position);
    CHECK(entityAsked == coil->id());
    CHECK(folioAsked == 1); // l'entite gagne : elle est plus precise que le folio
}

TEST_CASE("La fenetre n'a plus de panneau de proprietes ancre", "[ui][proprietes]")
{
    // Il prenait la place du dessin en permanence pour un reglage qu'on ne
    // fait que par moments. La commande et le raccourci restent — ce sont eux
    // qu'on a dans les doigts — mais ils ouvrent une fiche.
    MainWindow window;
    window.resize(1400, 900);

    CHECK(window.findChild<QDockWidget *>(QStringLiteral("dock.properties")) == nullptr);

    bool found = false;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(QLatin1Char('&')) == QStringLiteral("Propriétés…")) {
            found = true;
            CHECK(action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_1));
        }
    }
    CHECK(found);
}

TEST_CASE("Les réglages d'affichage survivent au changement de thème", "[ui][apparence]")
{
    // La regle qui gouverne ui/appearance.* : le theme fournit les defauts, le
    // reglage explicite gagne. Applique dans l'autre ordre, changer de theme
    // effacerait ce que le dessinateur a reglé pour son confort.
    Appearance::reset(true);
    Appearance::reset(false);

    RenderStyle chosen = RenderStyle::screenDark();
    chosen.gridStyle = GridStyle::Lines;
    chosen.crosshairPercent = 35.0;
    chosen.pickBoxPixels = 14.0;
    chosen.crosshair = QColor(0xE0, 0xA5, 0x4A);
    Appearance::save(chosen, true);

    // On repart du style brut du theme, comme le fait applyTheme.
    RenderStyle fresh = RenderStyle::screenDark();
    REQUIRE(fresh.gridStyle == GridStyle::Dots);
    Appearance::load(fresh, true);

    CHECK(fresh.gridStyle == GridStyle::Lines);
    CHECK(fresh.crosshairPercent == 35.0);
    CHECK(fresh.pickBoxPixels == 14.0);
    CHECK(fresh.crosshair == QColor(0xE0, 0xA5, 0x4A));

    // La forme est commune aux deux themes, la couleur ne l'est pas : une
    // teinte lisible sur fond noir ne l'est pas sur blanc.
    RenderStyle light = RenderStyle::screen();
    const QColor themeCrosshair = light.crosshair;
    Appearance::load(light, false);
    CHECK(light.gridStyle == GridStyle::Lines);
    CHECK(light.crosshair == themeCrosshair);

    Appearance::reset(true);
    RenderStyle back = RenderStyle::screenDark();
    Appearance::load(back, true);
    CHECK(back.gridStyle == GridStyle::Dots);
    CHECK(back.crosshairPercent == RenderStyle::screenDark().crosshairPercent);
}

TEST_CASE("Aucun glyphe n'est vide ni ne repete un autre", "[ui][theme][icones]")
{
    // La regle du projet : deux commandes differentes ne doivent jamais
    // partager un glyphe, sinon la barre d'outils devient illisible. Elle
    // n'etait tenue que par l'oeil — les trois exports (PDF, DXF, CSV) ont
    // partage le meme dessin pendant tout ce temps.
    dsn::Theme::apply(*qobject_cast<QApplication *>(QCoreApplication::instance()), true);

    QHash<QByteArray, int> seen;
    for (int i = 0; i < int(dsn::Icons::Glyph::Count); ++i) {
        const auto glyph = dsn::Icons::Glyph(i);
        CAPTURE(i);

        // Rendu a seize pixels : c'est la taille ou une icone sera lue dans
        // une grille de petits boutons, donc celle qui doit rester lisible.
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        dsn::Icons::icon(glyph, QColor(Qt::white)).paint(&painter, QRect(0, 0, 16, 16));
        painter.end();

        const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        int inked = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) > 24)
                    ++inked;
            }
        }
        // Un glyphe qui ne dessine rien est un bouton vide dans la barre.
        CHECK(inked > 6);

        const QByteArray key(reinterpret_cast<const char *>(image.constBits()),
                             int(image.sizeInBytes()));
        const auto known = seen.constFind(key);
        if (known != seen.constEnd()) {
            CAPTURE(known.value());
            FAIL("le glyphe " << i << " est identique au glyphe " << known.value());
        }
        seen.insert(key, i);
    }
    CHECK(seen.size() == int(dsn::Icons::Glyph::Count));
}

TEST_CASE("Toute commande de menu porte une icone", "[ui][theme][icones]")
{
    // Le ruban presente les memes QAction que les menus. Une action sans
    // icone s'y affiche en toutes lettres au milieu d'icones : le panneau
    // perd son alignement et sa largeur triple. C'est arrive aux quatre
    // sortes d'etiquette, creees sans icone parce qu'elles n'allaient qu'au
    // menu.
    MainWindow window;
    QStringList naked;
    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu,
                                                             const QString &path) {
        for (QAction *action : menu->actions()) {
            if (action->isSeparator())
                continue;
            const QString label = QString(action->text()).remove(QLatin1Char('&'));
            if (QMenu *sub = action->menu()) {
                walk(sub, path + QStringLiteral(" > ") + label);
                continue;
            }
            // Les entrees engendrees a l'execution — fichiers recents, bascules
            // de panneaux ancrables, profils metier — n'ont pas de glyphe
            // propre et n'ont pas de place au ruban.
            if (action->icon().isNull())
                naked.append(path + QStringLiteral(" > ") + label);
        }
    };
    for (QAction *m : window.menuBar()->actions()) {
        if (QMenu *menu = m->menu())
            walk(menu, QString(m->text()).remove(QLatin1Char('&')));
    }

    // Les seules exceptions admises, nommement.
    const QStringList allowed{ QStringLiteral("Fichier"), QStringLiteral("Affichage"),
                               QStringLiteral("Projet > Profil métier"),
                               QStringLiteral("Symboles"), QStringLiteral("Aide") };
    QStringList unexpected;
    for (const QString &entry : naked) {
        bool excused = false;
        for (const QString &prefix : allowed)
            excused = excused || entry.startsWith(prefix);
        if (!excused)
            unexpected.append(entry);
    }
    CAPTURE(unexpected.join(QStringLiteral(" | ")));
    CHECK(unexpected.isEmpty());
}

// --------------------------------------------------------------------------
// Ruban

TEST_CASE("Tout ce qui est au ruban est aussi au menu", "[ui][ruban]")
{
    // C'est l'invariant qui gouverne le ruban. La palette de commandes se
    // remplit en parcourant les menus : une commande qui ne serait QU'au
    // ruban serait introuvable a la palette et sur l'ecran d'accueil. Le
    // ruban est une seconde vue sur le repertoire des menus, jamais une
    // troisieme source.
    MainWindow window;
    Ribbon *ribbon = window.findChild<Ribbon *>();
    REQUIRE(ribbon);
    CHECK(ribbon->pageCount() >= 4);

    QSet<QAction *> inMenus;
    std::function<void(QMenu *)> walk = [&](QMenu *menu) {
        for (QAction *action : menu->actions()) {
            if (action->isSeparator())
                continue;
            if (QMenu *sub = action->menu())
                walk(sub);
            else
                inMenus.insert(action);
        }
    };
    QMenuBar *bar = window.findChild<QMenuBar *>();
    REQUIRE(bar);
    for (QAction *m : bar->actions()) {
        if (QMenu *menu = m->menu())
            walk(menu);
    }
    REQUIRE(inMenus.size() > 60);

    int placed = 0;
    for (int i = 0; i < ribbon->pageCount(); ++i) {
        RibbonPage *page = ribbon->page(i);
        REQUIRE(page);
        // Un onglet sans panneau nomme n'est qu'une rangee d'icones : c'est
        // exactement ce qu'on remplace.
        CHECK_FALSE(page->panels().isEmpty());
        for (RibbonPanel *panel : page->panels()) {
            CHECK_FALSE(panel->title().isEmpty());
            for (QAction *action : panel->ribbonActions()) {
                CAPTURE(action->text());
                CHECK(inMenus.contains(action));
                ++placed;
            }
        }
    }
    for (QAction *action : ribbon->quickActions()) {
        CAPTURE(action->text());
        CHECK(inMenus.contains(action));
    }
    // Assez de commandes pour que le ruban serve vraiment.
    CHECK(placed > 60);
}

TEST_CASE("Deux commandes d'un meme panneau ne partagent pas une icone",
          "[ui][ruban][icones]")
{
    // La regle « deux commandes ne partagent jamais un glyphe » etait tenue
    // par un test qui compare les glyphes entre eux — il dit qu'ils sont
    // distincts, pas qu'on ne les a pas ASSIGNES deux fois. Or c'est
    // l'assignation qui se voit : deux icones identiques cote a cote dans le
    // meme panneau, et on clique au hasard. Le panneau est la bonne maille —
    // c'est la que les icones se lisent ensemble.
    MainWindow window;
    window.resize(1600, 1000);

    Ribbon *ribbon = window.findChild<Ribbon *>();
    REQUIRE(ribbon);

    for (int i = 0; i < ribbon->pageCount(); ++i) {
        RibbonPage *page = ribbon->page(i);
        REQUIRE(page);
        for (RibbonPanel *panel : page->panels()) {
            QHash<QString, QString> ownerOfIcon; // signature d'image -> commande
            for (QAction *action : panel->ribbonActions()) {
                const QIcon icon = action->icon();
                if (icon.isNull())
                    continue;
                QImage image = icon.pixmap(QSize(20, 20)).toImage()
                                       .convertToFormat(QImage::Format_ARGB32);
                const QByteArray signature(reinterpret_cast<const char *>(image.constBits()),
                                           int(image.sizeInBytes()));
                const QString key = QString::fromLatin1(signature.toHex());
                const auto known = ownerOfIcon.constFind(key);
                INFO("panneau : " << panel->title().toStdString());
                INFO("commande : " << action->text().toStdString());
                if (known != ownerOfIcon.constEnd())
                    INFO("déjà portée par : " << known.value().toStdString());
                CHECK(known == ownerOfIcon.constEnd());
                ownerOfIcon.insert(key, action->text());
            }
        }
    }
}

TEST_CASE("Deux commandes différentes ne partagent pas une icône",
          "[ui][icones][menus]")
{
    // Le test frère tient la règle PAR PANNEAU DU RUBAN — c'est là que deux
    // icônes se lisent côte à côte. Mais l'inventaire complet des menus en
    // comptait dix-huit groupes en collision : une commande qui emprunte
    // l'icône d'une autre se reconnaît mal partout, pas seulement au ruban.
    //
    // Trois doublons restent, et ils sont VOULUS : ce sont les mêmes
    // commandes atteintes par deux chemins. Leur donner deux dessins serait
    // pire que le doublon — l'utilisateur croirait à deux commandes.
    const QStringList voulus = {
        QStringLiteral("Cotation|Cote alignée"),
        QStringLiteral("Étiquette|Étiquette de potentiel"),
        QStringLiteral("Palette de commandes…|Toutes les commandes…"),
    };

    MainWindow window;
    window.resize(1600, 1000);

    QHash<QString, QStringList> parIcone;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().isEmpty() || action->menu() || action->icon().isNull())
            continue;
        QImage image = action->icon().pixmap(QSize(20, 20)).toImage()
                               .convertToFormat(QImage::Format_ARGB32);
        const QByteArray signature(reinterpret_cast<const char *>(image.constBits()),
                                   int(image.sizeInBytes()));
        QString nom = action->text();
        nom.remove(QLatin1Char('&'));
        parIcone[QString::fromLatin1(signature.toHex())].append(nom);
    }

    for (auto it = parIcone.cbegin(); it != parIcone.cend(); ++it) {
        if (it.value().size() < 2)
            continue;
        QStringList noms = it.value();
        noms.sort();
        QStringList attendus;
        for (const QString &couple : voulus) {
            QStringList parts = couple.split(QLatin1Char('|'));
            parts.sort();
            if (parts == noms)
                attendus = parts;
        }
        INFO("icône partagée par : " << noms.join(QStringLiteral(", ")).toStdString());
        CHECK(attendus == noms);
    }
}

TEST_CASE("La palette de commandes survit au ruban", "[ui][ruban][palette]")
{
    // setMenuWidget() detache la barre de menus de QMainWindow : menuBar() en
    // fabrique ensuite une neuve et vide. La palette, qui parcourt les menus,
    // s'est retrouvee sans une seule commande — un test l'a rattrape avant
    // qu'on le voie a l'usage.
    MainWindow window;
    window.resize(1400, 900);
    window.show();

    auto *palette = window.findChild<CommandPalette *>();
    if (!palette) {
        QMetaObject::invokeMethod(&window, "openCommandPalette");
        palette = window.findChild<CommandPalette *>();
    }
    REQUIRE(palette);

    const auto entries = palette->visibleEntries();
    int fromMenus = 0;
    for (const auto &entry : entries) {
        // Les entrees venues des menus portent le nom du menu en groupe ;
        // celles de la ligne de commande portent « Ligne de commande ».
        if (!entry.group.startsWith(QStringLiteral("Ligne de commande")))
            ++fromMenus;
    }
    CHECK(fromMenus > 60);
}

TEST_CASE("Les deux collages se grisent tant que le presse-papiers est vide",
          "[ui][ruban][circuitcopy]")
{
    // Une commande de collage active sur un presse-papiers vide ne fait rien
    // et ne dit rien : l'utilisateur croit avoir rate le raccourci. Le grisage
    // n'est juste que si quelque chose le reveille — d'ou le signal du
    // canevas, que ce test verifie du meme coup.
    MainWindow window;
    window.resize(1400, 900);

    QAction *paste = nullptr;
    QAction *pasteKeep = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        const QString label = action->text().remove(QLatin1Char('&'));
        if (label == QStringLiteral("Coller"))
            paste = action;
        else if (label == QStringLiteral("Coller à l'identique"))
            pasteKeep = action;
    }
    REQUIRE(paste);
    REQUIRE(pasteKeep);
    CHECK_FALSE(paste->isEnabled());
    CHECK_FALSE(pasteKeep->isEnabled());

    FolioView *view = window.findChild<FolioView *>();
    Document *document = window.findChild<Document *>();
    REQUIRE(view);
    REQUIRE(document);
    Folio *folio = document->currentFolio();
    REQUIRE(folio);
    auto *symbol = placeSymbol(document->project(), folio, QStringLiteral("iec:coil"),
                               QPointF(100, 100));
    view->setSelection({ symbol->id() });
    view->copySelection();

    CHECK(paste->isEnabled());
    CHECK(pasteKeep->isEnabled());
}

TEST_CASE("Le ruban se replie et rend la place au dessin", "[ui][ruban]")
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    Ribbon *ribbon = window.findChild<Ribbon *>();
    REQUIRE(ribbon);

    const int expanded = ribbon->sizeHint().height();
    ribbon->setCollapsed(true);
    const int collapsed = ribbon->sizeHint().height();
    CHECK(collapsed < expanded);
    // Les onglets restent : replier ne doit pas rendre le ruban introuvable.
    CHECK(collapsed >= Ribbon::kTabHeight);
    ribbon->setCollapsed(false);
    CHECK(ribbon->sizeHint().height() == expanded);
}

// --------------------------------------------------------------------------
// Bloc A4 — la ligne de commande conduit le geste

TEST_CASE("L'invite dit ce que le geste attend, et tombe avec lui", "[ui][invite]")
{
    // C'est la correction de fond du bloc A4 : avant, une invite etait poussee
    // dans la barre d'etat depuis soixante-quinze endroits et s'y effacait au
    // bout de six secondes — au milieu du geste qu'elle decrivait. Deduite de
    // l'etat, elle ne peut ni manquer a l'appel ni survivre a son geste.
    Document document;
    document.newProject(builtinLibrary());
    FolioView view(&document);
    view.resize(900, 700);

    // Au repos, le logiciel n'attend rien : il ne dit rien.
    view.setTool(FolioView::Tool::Select);
    CHECK(view.currentPrompt().isEmpty());

    view.setTool(FolioView::Tool::Wire);
    const QString armed = view.currentPrompt();
    CHECK_FALSE(armed.isEmpty());

    // Un trace commence n'attend pas la meme chose qu'un trace a poser.
    clickScene(view, QPointF(40, 40));
    const QString drawing = view.currentPrompt();
    CHECK_FALSE(drawing.isEmpty());
    CHECK(drawing != armed);

    // Echap rend la main : plus de geste, plus d'invite.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &escape);
    view.setTool(FolioView::Tool::Select);
    CHECK(view.currentPrompt().isEmpty());
}

TEST_CASE("L'invite nomme le symbole armé", "[ui][invite]")
{
    // « Cliquez pour poser le symbole » ne dit pas lequel. Quand on vient
    // d'en choisir un dans une grille de cent trois vignettes, c'est
    // exactement ce qu'on veut relire.
    Document document;
    document.newProject(builtinLibrary());
    FolioView view(&document);
    view.resize(900, 700);

    view.setPendingSymbol(QStringLiteral("iec:coil"));
    const SymbolDefinition *definition =
            document.project().library.definition(QStringLiteral("iec:coil"));
    REQUIRE(definition);
    CHECK(view.currentPrompt().contains(definition->name));
}

TEST_CASE("Une désignation en cours dit combien d'objets sont pris", "[ui][invite]")
{
    // Le compte fait partie de l'invite : sans lui, on ne sait pas si le clic
    // a porte, et on reclique sur le meme objet — ce qui le retire.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    REQUIRE(folio);
    auto *wire = drawWire(folio, { QPointF(20, 20), QPointF(80, 20) });

    FolioView view(&document);
    view.resize(900, 700);
    bool ran = false;
    view.requestSelection(QStringLiteral("Décaler : désignez le fil"),
                          FolioView::PickFilter::Wires, 1, [&ran] { ran = true; });
    CHECK(view.currentPrompt().contains(QStringLiteral("Décaler")));

    view.setSelection(QSet<QString>{ wire->id() });
    CHECK(view.currentPrompt().contains(QStringLiteral("1")));
    CHECK_FALSE(ran);
}

TEST_CASE("La barre d'état ne porte plus de dialogue", "[ui][invite]")
{
    // Un compte rendu qui s'efface tout seul dans le coin bas de la fenetre
    // n'est pas lu, et quand il l'est, il est deja parti. Tout ce que le
    // logiciel a a dire va dans l'historique de la ligne de commande, qui le
    // garde. La barre d'etat ne tient plus que ses etats permanents.
    MainWindow window;
    window.resize(1400, 900);
    window.show();

    CommandLine *command = window.findChild<CommandLine *>();
    FolioView *view = window.findChild<FolioView *>();
    REQUIRE(command);
    REQUIRE(view);

    QPlainTextEdit *history = nullptr;
    for (QPlainTextEdit *edit : command->findChildren<QPlainTextEdit *>()) {
        if (edit->property("commandHistory").toBool())
            history = edit;
    }
    REQUIRE(history);

    const QString message = QStringLiteral("Trois fils décalés de 2,5 mm.");
    Q_EMIT view->statusMessage(message);
    CHECK(history->toPlainText().contains(message));
    CHECK(window.statusBar()->currentMessage().isEmpty());
}

TEST_CASE("L'invite déduite arrive dans la ligne de commande", "[ui][invite]")
{
    // Le rendu est le point de synchronisation : tout changement d'etat
    // appelle update(). Le test rend donc la vue, comme l'ecran le ferait.
    MainWindow window;
    window.resize(1400, 900);
    window.show();

    CommandLine *command = window.findChild<CommandLine *>();
    FolioView *view = window.findChild<FolioView *>();
    REQUIRE(command);
    REQUIRE(view);

    view->setTool(FolioView::Tool::Wire);
    QPixmap canvas(view->size());
    view->render(&canvas);
    CHECK(command->prompt() == view->currentPrompt());
    CHECK_FALSE(command->prompt().isEmpty());

    view->setTool(FolioView::Tool::Select);
    view->render(&canvas);
    CHECK(command->prompt().isEmpty());
}

TEST_CASE("Une commande lancée à la souris s'écrit dans la ligne de commande",
          "[ui][invite]")
{
    // La ligne de commande devient le journal de la seance, quel que soit le
    // chemin pris. On y relit ce qu'on vient de faire, et on y apprend le nom
    // de la commande — un bouton clique enseigne ce qu'il faudra taper.
    MainWindow window;
    window.resize(1400, 900);
    window.show();

    CommandLine *command = window.findChild<CommandLine *>();
    REQUIRE(command);
    QPlainTextEdit *history = nullptr;
    for (QPlainTextEdit *edit : command->findChildren<QPlainTextEdit *>()) {
        if (edit->property("commandHistory").toBool())
            history = edit;
    }
    REQUIRE(history);

    QAction *zoomExtents = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(QLatin1Char('&')) == QStringLiteral("Ajuster au folio"))
            zoomExtents = action;
    }
    REQUIRE(zoomExtents);
    zoomExtents->trigger();
    CHECK(history->toPlainText().contains(QStringLiteral("Ajuster au folio")));
}

TEST_CASE("Échap et le clic droit abandonnent tout, et le disent", "[ui][invite]")
{
    // Les deux sorties avaient chacune leur code, et il ne couvrait pas les
    // memes etats : Echap laissait le panoramique arme et quittait le zoom
    // fenetre sans un mot. Une commande abandonnee doit l'etre entierement.
    Document document;
    document.newProject(builtinLibrary());
    FolioView view(&document);
    view.resize(900, 700);

    QStringList said;
    QObject::connect(&view, &FolioView::statusMessage, &view,
                     [&said](const QString &m) { said.append(m); });

    view.beginPan();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &escape);
    CHECK_FALSE(said.isEmpty());
    CHECK(view.currentPrompt().isEmpty());

    said.clear();
    view.beginZoomWindow();
    CHECK_FALSE(view.currentPrompt().isEmpty());
    QApplication::sendEvent(&view, &escape);
    CHECK_FALSE(said.isEmpty());
    CHECK(view.currentPrompt().isEmpty());
}

TEST_CASE("Le clic droit sur une sélection ouvre le menu, il ne la vide pas",
          "[ui][invite][menu]")
{
    // L'abandon unique defait couche par couche ; le clic droit doit s'arreter
    // a l'outil. Aller jusqu'a la selection viderait ce sur quoi le menu
    // contextuel allait porter — le menu ne servirait plus a rien.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    REQUIRE(folio);
    auto *symbol = placeSymbol(document.project(), folio, QStringLiteral("iec:coil"),
                               QPointF(100, 100));

    FolioView view(&document);
    view.resize(900, 700);
    view.zoomToFit();
    view.setSelection(QSet<QString>{ symbol->id() });

    int menus = 0;
    QObject::connect(&view, &FolioView::contextMenuRequested, &view,
                     [&menus](const QPoint &) { ++menus; });

    const QPointF widget = view.mapFromScene(QPointF(100, 100));
    QMouseEvent press(QEvent::MouseButtonPress, widget, view.mapToGlobal(widget.toPoint()),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);

    CHECK(menus == 1);
    CHECK(view.selection().contains(symbol->id()));
}


// --------------------------------------------------------------------------
// Ce que l'essai de reproduction d'un vrai schéma a fait remonter.

TEST_CASE("Le style de trait s'arme et s'applique à la sélection", "[ui][trait]")
{
    // Même mécanique que le type de fil : le style choisi vaut pour ce qu'on
    // va tracer ET s'applique tout de suite à ce qui est désigné. Sans le
    // second point il faudrait retracer un cadre qu'on vient de poser.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    REQUIRE(folio);
    FolioView view(&document);
    view.resize(900, 700);

    auto forme = std::make_unique<GraphicItem>();
    forme->shape = Primitive::rect(QRectF(20, 20, 60, 40));
    auto *pose = static_cast<GraphicItem *>(folio->addEntity(std::move(forme)));
    REQUIRE(pose);
    CHECK(pose->shape.stroke == Primitive::Stroke::Solid);

    view.setSelection(QSet<QString>{ pose->id() });
    view.setShapeStroke(Primitive::Stroke::Dashed);
    CHECK(pose->shape.stroke == Primitive::Stroke::Dashed);
    CHECK(view.shapeStroke() == Primitive::Stroke::Dashed);

    // Une seule annulation défait le changement : c'est une macro.
    document.undo();
    CHECK(pose->shape.stroke == Primitive::Stroke::Solid);

    // Et le style reste armé pour la forme suivante.
    view.clearSelection();
    view.setTool(FolioView::Tool::Rectangle);
    clickScene(view, QPointF(100, 100));
    clickScene(view, QPointF(160, 140));
    const auto formes = folio->entitiesOfType<GraphicItem>();
    REQUIRE(formes.size() == 2);
    CHECK(formes.back()->shape.stroke == Primitive::Stroke::Dashed);
}

TEST_CASE("Un texte se pose là où on clique, hors résolution", "[ui][texte]")
{
    // LA RÉSOLUTION TIENT LE COURANT, PAS L'ANNOTATION. Un sommet de fil
    // tombe sur le pas de 2,5 mm — c'est ce qui aligne un schéma tout seul.
    // Un texte, non : le forcer sur ce pas interdit d'écrire deux lignes de
    // 1,7 mm l'une sous l'autre, ce que le renvoi d'une voie d'automate
    // demande à chaque ligne. C'est l'essai de reproduction qui l'a montré :
    // les deux lignes du repère d'entrée se superposaient.
    Document document;
    document.newProject(builtinLibrary());
    FolioView view(&document);
    view.resize(900, 700);
    view.zoomToFit();
    REQUIRE(view.snapEngine().gridSnapEnabled());

    // Un point volontairement entre deux pas de grille.
    const QPointF vise(63.2, 41.7);
    CHECK(view.snapAnnotation(vise) == vise);
    // Le même point, pour un fil, tombe sur la grille.
    CHECK(view.snapEngine().snapToGridPoint(vise) != vise);
}

TEST_CASE("Un texte se tape sur le dessin, sans boîte modale", "[ui][texte]")
{
    // Poser un texte ouvrait une boîte modale, une par texte : l'essai de
    // reproduction d'un vrai schéma en a compté QUARANTE-HUIT pour un seul
    // folio. La boîte coupe le dessin en deux, s'ouvre loin du point visé et
    // cache justement l'endroit où le texte va se poser.
    Document document;
    document.newProject(builtinLibrary());
    Folio *folio = document.currentFolio();
    REQUIRE(folio);
    FolioView view(&document);
    view.resize(900, 700);
    view.zoomToFit();

    view.setTool(FolioView::Tool::Text);
    CHECK_FALSE(view.isTypingText());
    clickScene(view, QPointF(60, 40));
    CHECK(view.isTypingText());

    const auto tape = [&view](const QString &texte) {
        for (const QChar c : texte) {
            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
            QApplication::sendEvent(&view, &press);
        }
    };
    tape(QStringLiteral("BINARY OUTPUT"));
    CHECK(view.textBeingTyped() == QStringLiteral("BINARY OUTPUT"));

    // Échap abandonne sans rien poser.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &escape);
    CHECK_FALSE(view.isTypingText());
    CHECK(folio->entitiesOfType<TextItem>().empty());

    // La hauteur retenue s'applique au texte suivant : on écrit rarement une
    // seule ligne dans une taille donnée.
    view.setTextHeight(1.8);
    clickScene(view, QPointF(60, 50));
    tape(QStringLiteral("12 (CH5)"));
    QKeyEvent entree(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&view, &entree);

    const auto textes = folio->entitiesOfType<TextItem>();
    REQUIRE(textes.size() == 1);
    CHECK(textes.front()->text == QStringLiteral("12 (CH5)"));
    CHECK(textes.front()->height == 1.8);
    CHECK_FALSE(view.isTypingText());
}

TEST_CASE("Les raccourcis d'une lettre ne mangent pas le texte tapé",
          "[ui][texte][raccourci]")
{
    // LE PIÈGE QUE LA BOÎTE MODALE CACHAIT. Les outils portent des raccourcis
    // d'une lettre à portée application — S sélection, L étiquette, T texte,
    // O décaler. Une boîte modale les bloquait ; en tapant sur le dessin,
    // « RELAIS OMRON » déclenchait Décaler au O et Sélection au S, le texte
    // n'arrivait jamais, et des commandes partaient toutes seules.
    //
    // Qt::ShortcutOverride est la réponse exacte, et ce test est la seule
    // façon de la vérifier : il faut une vraie fenêtre, car les raccourcis
    // vivent sur ses actions, et de vrais événements, car c'est
    // QApplication::notify qui arbitre entre raccourci et frappe.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    // Sans ce tour de boucle la fenêtre n'est pas active, et une portée
    // application ne déclenche rien : le test passerait sans rien prouver.
    QApplication::processEvents();
    FolioView *view = window.findChild<FolioView *>();
    Document *document = window.findChild<Document *>();
    REQUIRE(view);
    REQUIRE(document);

    view->setTool(FolioView::Tool::Text);
    view->setFocus();
    clickScene(*view, QPointF(60, 40));
    REQUIRE(view->isTypingText());

    // Chaque lettre de ce mot est un raccourci d'outil ou de commande.
    const QString piege = QStringLiteral("RELAIS OMRON");
    for (const QChar c : piege) {
        const int code = c == QLatin1Char(' ') ? int(Qt::Key_Space)
                                               : int(c.toUpper().unicode());
        QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier, QString(c));
        QApplication::sendEvent(view, &press);
        QApplication::processEvents();
    }

    CHECK(view->textBeingTyped() == piege);
    // L'outil n'a pas bougé, et aucune boîte ne s'est ouverte.
    CHECK(view->tool() == FolioView::Tool::Text);
    CHECK(QApplication::activeModalWidget() == nullptr);

    QKeyEvent entree(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(view, &entree);
    const auto textes = document->currentFolio()->entitiesOfType<TextItem>();
    REQUIRE(textes.size() == 1);
    CHECK(textes.front()->text == piege);
}

TEST_CASE("Le dernier symbole posé reste sous la main", "[ui][insertion]")
{
    // Changer d'outil désarme le symbole : poser un texte au milieu d'une
    // série de bornes obligeait à retourner dans la palette, la rechercher et
    // la reprendre. C'est l'INSERT d'AutoCAD qui a raison — il propose
    // toujours le dernier bloc inséré. L'essai de reproduction faisait cet
    // aller-retour à chaque annotation intercalée.
    Document document;
    document.newProject(builtinLibrary());
    FolioView view(&document);
    view.resize(900, 700);
    view.zoomToFit();

    // Rien n'a encore été posé : il n'y a rien à reprendre.
    CHECK_FALSE(view.rearmLastSymbol());

    view.setPendingSymbol(QStringLiteral("iec:terminal"));
    view.rotateSelection(true); // un quart de tour sur le symbole armé
    clickScene(view, QPointF(60, 40));
    REQUIRE(document.currentFolio()->entitiesOfType<SymbolInstance>().size() == 1);

    // Un texte au milieu de la série : l'outil change, le symbole tombe.
    view.setTool(FolioView::Tool::Text);
    CHECK(view.pendingSymbol().isEmpty());

    // Revenir à l'outil Symbole le reprend, AVEC son orientation : l'avoir
    // fait pivoter ne doit pas être à refaire.
    view.setTool(FolioView::Tool::Symbol);
    CHECK(view.pendingSymbol() == QStringLiteral("iec:terminal"));
    clickScene(view, QPointF(60, 60));
    const auto poses = document.currentFolio()->entitiesOfType<SymbolInstance>();
    REQUIRE(poses.size() == 2);
    CHECK(poses.back()->placement.orientation == poses.front()->placement.orientation);
}

TEST_CASE("Un projet neuf garde ses symboles", "[ui][document][symboles]")
{
    // LE BUG DES CADRES BARRÉS ROUGES, trouvé après trois signalements.
    //
    //     m_document->newProject(m_document->project().library);
    //
    // La bibliothèque passée EST celle du projet. newProject appelle
    // Project::clear(), qui la vide — puis l'affecte à elle-même. Le projet
    // se retrouve donc sans un seul symbole, et tout ce qu'on pose ensuite
    // référence une définition introuvable : un cadre barré rouge.
    //
    // La palette, elle, garde les vignettes déjà construites : elle continue
    // d'offrir cent trois symboles qui n'existent plus. D'où le geste qui le
    // déclenche — écran d'accueil, « Nouveau projet », poser un symbole — et
    // d'où le fait qu'il ne se voyait jamais dans un test qui ne repartait
    // pas d'un projet neuf.
    MainWindow window;
    window.resize(1200, 800);
    Document *document = window.findChild<Document *>();
    REQUIRE(document);

    const int avant = document->project().library.count();
    REQUIRE(avant > 60);

    // Le geste : Fichier ▸ Nouveau projet, ou la carte de l'écran d'accueil.
    QAction *nouveau = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(QLatin1Char('&')).startsWith(QStringLiteral("Nouveau projet")))
            nouveau = action;
    }
    REQUIRE(nouveau);
    nouveau->trigger();

    CHECK(document->project().library.count() == avant);
    // Et un symbole posé après doit se dessiner, pas se barrer.
    CHECK(document->project().library.definition(QStringLiteral("iec:opamp")) != nullptr);
}

TEST_CASE("La flèche reste sur le bord pour rouvrir un panneau tassé", "[ui][docks][rail]")
{
    // « Sur la section à gauche avec les symboles pour la faire disparaître :
    // la flèche doit rester pour pouvoir la rouvrir ? » — signalé à l'usage,
    // 2026-09-02. Elle ne restait pas : le chevron vit dans la barre de titre
    // du panneau, donc il part avec lui, et le seul retour était un menu ou un
    // raccourci — rien à l'endroit même où le panneau venait de disparaître.
    //
    // Le rail tient la promesse : rien tant que tout est ouvert, un onglet dès
    // qu'un panneau est tassé, et cet onglet le ramène avec sa largeur.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QApplication::processEvents();

    auto *rail = window.findChild<DockRail *>();
    REQUIRE(rail);
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("dock.symbols"));
    REQUIRE(dock);

    // Tout est ouvert : le rail ne coûte pas un pixel de dessin.
    CHECK(rail->tabs().isEmpty());
    CHECK(rail->isHidden());

    // Le geste réel : le chevron de la barre de titre du panneau.
    auto *titre = qobject_cast<DockTitle *>(dock->titleBarWidget());
    REQUIRE(titre);
    auto *chevron = titre->findChild<QToolButton *>();
    REQUIRE(chevron);
    chevron->click();
    QApplication::processEvents();

    REQUIRE(dock->isHidden());
    // La flèche est restée, et elle dit lequel des deux panneaux elle ramène.
    CHECK_FALSE(rail->isHidden());
    CHECK(rail->tabs().contains(QStringLiteral("SYMBOLES")));

    // Et elle le ramène — avec de la place, comme la commande d'affichage :
    // les deux chemins passent par setDockVisible, sinon l'un des deux
    // oublierait la largeur.
    QAbstractButton *onglet = nullptr;
    for (QAbstractButton *bouton : rail->findChildren<QAbstractButton *>()) {
        if (bouton->isVisible())
            onglet = bouton;
    }
    REQUIRE(onglet);
    onglet->click();
    QApplication::processEvents();

    CHECK_FALSE(dock->isHidden());
    CHECK(dock->width() > 40);
    // Le rail se retire quand il n'a plus rien à offrir.
    CHECK(rail->tabs().isEmpty());
    CHECK(rail->isHidden());
}

TEST_CASE("Un panneau tassé revient avec de la largeur", "[ui][docks]")
{
    // Le chevron de la barre de titre cache le panneau ; la commande
    // d'affichage doit le ramener. Elle le rendait « visible » mais large de
    // zéro pixel : Qt restaure un dock caché avec la largeur qu'il avait au
    // moment où on l'a caché, et le canevas avait pris toute la place. Du
    // point de vue du dessinateur, le panneau ne revenait jamais.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QApplication::processEvents();

    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("dock.symbols"));
    REQUIRE(dock);
    CHECK(dock->isVisible());
    const int largeurInitiale = dock->width();
    REQUIRE(largeurInitiale > 0);

    QAction *bascule = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(QLatin1Char('&')).startsWith(QStringLiteral("Palette de symboles")))
            bascule = action;
    }
    REQUIRE(bascule);

    bascule->trigger(); // tasser
    QApplication::processEvents();
    CHECK_FALSE(dock->isVisible());

    bascule->trigger(); // ramener
    QApplication::processEvents();
    CHECK(dock->isVisible());
    // C'est CE point qui manquait : visible ne suffit pas, il faut de la place.
    CHECK(dock->width() > 40);
}

namespace {

// Compte les pixels d'une zone qui S'ECARTENT d'une couleur de reference.
// Mesurer l'encre deposee est la seule verification qui distingue un aplat
// d'un filet : la presence d'une regle dans la feuille de style ne prouve
// rien — Qt ignore en silence une declaration qu'il ne comprend pas.
int pixelsHorsFond(const QImage &image, const QColor &fond, const QRect &zone,
                   int tolerance = 14)
{
    int compte = 0;
    for (int y = zone.top(); y <= zone.bottom(); ++y) {
        if (y < 0 || y >= image.height())
            continue;
        for (int x = zone.left(); x <= zone.right(); ++x) {
            if (x < 0 || x >= image.width())
                continue;
            const QColor c = image.pixelColor(x, y);
            if (std::abs(c.red() - fond.red()) > tolerance
                || std::abs(c.green() - fond.green()) > tolerance
                || std::abs(c.blue() - fond.blue()) > tolerance)
                ++compte;
        }
    }
    return compte;
}

// Compte les pixels PROCHES d'une couleur donnee — l'inverse du precedent.
int pixelsProches(const QImage &image, const QColor &cible, const QRect &zone,
                  int tolerance = 14)
{
    int compte = 0;
    for (int y = zone.top(); y <= zone.bottom(); ++y) {
        if (y < 0 || y >= image.height())
            continue;
        for (int x = zone.left(); x <= zone.right(); ++x) {
            if (x < 0 || x >= image.width())
                continue;
            const QColor c = image.pixelColor(x, y);
            if (std::abs(c.red() - cible.red()) <= tolerance
                && std::abs(c.green() - cible.green()) <= tolerance
                && std::abs(c.blue() - cible.blue()) <= tolerance)
                ++compte;
        }
    }
    return compte;
}

// L'epaisseur du filet du bas : le nombre de rangees consecutives, en partant
// du bas, dont le milieu porte la couleur cherchee.
int epaisseurDuFilet(const QImage &image, const QColor &cible, const QRect &zone,
                     int tolerance = 14)
{
    const int x = zone.center().x();
    int epaisseur = 0;
    for (int y = zone.bottom(); y >= zone.top(); --y) {
        if (y < 0 || y >= image.height() || x < 0 || x >= image.width())
            break;
        const QColor c = image.pixelColor(x, y);
        if (std::abs(c.red() - cible.red()) > tolerance
            || std::abs(c.green() - cible.green()) > tolerance
            || std::abs(c.blue() - cible.blue()) > tolerance)
            break;
        ++epaisseur;
    }
    return epaisseur;
}

// L'etendue horizontale de l'encre dans une zone : la premiere et la derniere
// colonne qui portent autre chose que le fond. C'est ce qui mesure vraiment un
// texte dessine — une taille de widget ne dit rien de ce qui a ete peint
// dedans, et c'est precisement la ou le piege du font-weight se cache.
QPair<int, int> etendueDeLEncre(const QImage &image, const QColor &fond, const QRect &zone,
                                int tolerance = 14)
{
    int gauche = -1;
    int droite = -1;
    for (int x = zone.left(); x <= zone.right(); ++x) {
        if (x < 0 || x >= image.width())
            continue;
        bool encre = false;
        for (int y = zone.top(); y <= zone.bottom() && !encre; ++y) {
            if (y < 0 || y >= image.height())
                continue;
            const QColor c = image.pixelColor(x, y);
            encre = std::abs(c.red() - fond.red()) > tolerance
                    || std::abs(c.green() - fond.green()) > tolerance
                    || std::abs(c.blue() - fond.blue()) > tolerance;
        }
        if (!encre)
            continue;
        if (gauche < 0)
            gauche = x;
        droite = x;
    }
    return { gauche, droite };
}

QToolButton *bascule(QStatusBar *bar, const QString &nom)
{
    for (QToolButton *button : bar->findChildren<QToolButton *>()) {
        if (button->property("statusToggle").toBool() && button->text() == nom)
            return button;
    }
    return nullptr;
}

QLabel *valeurDeCase(QStatusBar *bar, const QString &libelle)
{
    // Le libelle precede immediatement sa valeur : c'est l'ordre de pose.
    QLabel *precedent = nullptr;
    for (QWidget *w : bar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        auto *label = qobject_cast<QLabel *>(w);
        if (!label)
            continue;
        if (precedent && precedent->text() == libelle && label->property("cellValue").toBool())
            return label;
        precedent = label;
    }
    return nullptr;
}

} // namespace

namespace {

// La couleur reellement posee sur une ligne de l'historique. On la lit dans le
// document, pas dans la feuille de style : c'est la seule facon de savoir ce
// qui a ete ecrit — Qt ne range dans un document que des teintes.
QColor couleurDeLaLigne(const QPlainTextEdit *history, int index)
{
    QTextBlock block = history->document()->findBlockByNumber(index);
    if (!block.isValid())
        return QColor();
    QTextBlock::iterator it = block.begin();
    if (it == block.end())
        return QColor();
    return it.fragment().charFormat().foreground().color();
}

// L'indice de la ligne qui porte ce texte. L'historique porte deja ce que le
// demarrage a dit : chercher par le texte plutot que par le rang est la seule
// facon de viser la ligne qu'on vient d'ecrire.
int ligneQuiPorte(const QPlainTextEdit *history, const QString &texte)
{
    for (QTextBlock block = history->document()->begin(); block.isValid();
         block = block.next()) {
        if (block.text().contains(texte))
            return block.blockNumber();
    }
    return -1;
}

QPlainTextEdit *historiqueDe(CommandLine *command)
{
    for (QPlainTextEdit *edit : command->findChildren<QPlainTextEdit *>()) {
        if (edit->property("commandHistory").toBool())
            return edit;
    }
    return nullptr;
}

} // namespace

TEST_CASE("Les quatre voix de la ligne de commande ne se confondent pas",
          "[ui][ligne-de-commande]")
{
    // `success`, `warning` et `danger` etaient dans les jetons du theme et ne
    // se voyaient NULLE PART : tout partait dans la meme encre, et un compte
    // rendu d'automatisme avait l'air d'une erreur. Il fallait lire la phrase
    // entiere pour savoir si le logiciel se felicitait ou se plaignait.
    //
    // Le test lit la couleur POSEE DANS LE DOCUMENT. Verifier que la fonction
    // existe ne prouverait rien : c'est la teinte ecrite qui distingue les
    // voix, et elle est ecrite une seule fois, au moment de la ligne.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();

    CommandLine *command = window.findChild<CommandLine *>();
    REQUIRE(command);
    QPlainTextEdit *history = historiqueDe(command);
    REQUIRE(history);

    command->writeSource(QStringLiteral("REPERAGE-source"));
    command->write(QStringLiteral("fil-normal"));
    command->writeOk(QStringLiteral("abouti"));
    command->writeWarning(QStringLiteral("a-regarder"));
    command->writeError(QStringLiteral("echoue"));

    const ThemeColors &c = Theme::colors();
    const QStringList reperes = { QStringLiteral("REPERAGE-source"),
                                  QStringLiteral("fil-normal"), QStringLiteral("abouti"),
                                  QStringLiteral("a-regarder"), QStringLiteral("echoue") };
    const QList<QColor> attendues = { c.textFaint, c.textMuted, c.success, c.warning,
                                      c.danger };
    QSet<QRgb> teintes;
    for (int i = 0; i < reperes.size(); ++i) {
        CAPTURE(reperes.at(i));
        const int ligne = ligneQuiPorte(history, reperes.at(i));
        REQUIRE(ligne >= 0);
        CHECK(couleurDeLaLigne(history, ligne) == attendues.at(i));
        teintes.insert(couleurDeLaLigne(history, ligne).rgb());
    }
    // Et elles sont bien cinq couleurs distinctes : deux voix de la meme
    // teinte ne sont pas deux voix.
    CHECK(teintes.size() == 5);
}

TEST_CASE("L'historique se retrace au changement de thème", "[ui][ligne-de-commande]")
{
    // Qt ne range dans un document QUE des teintes, jamais des jetons : une
    // erreur ecrite en thème sombre garderait son rouge sombre sur fond clair.
    // C'est le seul endroit du logiciel ou une couleur du thème est FIGEE au
    // moment ou on l'ecrit — donc le seul qui demande un retracage.
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    REQUIRE(app);

    MainWindow window;
    window.resize(1400, 900);
    window.show();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();

    CommandLine *command = window.findChild<CommandLine *>();
    REQUIRE(command);
    QPlainTextEdit *history = historiqueDe(command);
    REQUIRE(history);

    // Le basculement passe par la COMMANDE DE MENU, pas par Theme::apply : on
    // veut tenir la chaine entiere. Si MainWindow oubliait de prevenir la
    // ligne de commande, le test devrait le voir.
    QAction *sombre = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(QLatin1Char('&')) == QStringLiteral("Thème sombre"))
            sombre = action;
    }
    REQUIRE(sombre);
    if (!sombre->isChecked())
        sombre->trigger();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();

    command->writeError(QStringLiteral("echec-temoin"));
    command->writeOk(QStringLiteral("reussite-temoin"));
    const QColor rougeSombre =
            couleurDeLaLigne(history, ligneQuiPorte(history, QStringLiteral("echec-temoin")));
    CHECK(rougeSombre == Theme::colors().danger);

    sombre->trigger();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();

    // Les deux thèmes doivent vraiment differer, sinon le test ne prouve rien.
    REQUIRE(Theme::colors().danger != rougeSombre);
    const int echec = ligneQuiPorte(history, QStringLiteral("echec-temoin"));
    const int reussite = ligneQuiPorte(history, QStringLiteral("reussite-temoin"));
    // Et le texte n'a pas bouge en chemin : le retracage reecrit tout.
    REQUIRE(echec >= 0);
    REQUIRE(reussite >= 0);
    CHECK(couleurDeLaLigne(history, echec) == Theme::colors().danger);
    CHECK(couleurDeLaLigne(history, reussite) == Theme::colors().success);

    if (!sombre->isChecked())
        sombre->trigger();
    Theme::apply(*app, true);
}

TEST_CASE("Le filet d'accent s'allume avec l'invite et s'éteint avec elle",
          "[ui][ligne-de-commande]")
{
    // Le seul signal « le logiciel t'attend » de toute la fenêtre. Il s'allume
    // avec l'invite et s'eteint avec elle : il ne peut donc pas mentir, ce
    // qu'un message d'etat pousse a la main ne peut pas promettre.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();

    CommandLine *command = window.findChild<CommandLine *>();
    REQUIRE(command);
    QFrame *filet = nullptr;
    for (QFrame *frame : command->findChildren<QFrame *>()) {
        if (frame->property("commandWaiting").toBool())
            filet = frame;
    }
    REQUIRE(filet);
    CHECK_FALSE(filet->isVisible());

    command->setPrompt(QStringLiteral("Point suivant ou [Cote / Annuler] :"));
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();
    REQUIRE(filet->isVisible());
    CHECK(filet->width() == 2);

    // Et il est vraiment encre a l'accent : une regle de feuille de style
    // qu'un jeton mal ecrit ferait sauter passerait sans cela.
    const QImage image = command->grab().toImage();
    const QPoint milieu(filet->x() + filet->width() / 2,
                        filet->y() + filet->height() / 2);
    CHECK(image.pixelColor(milieu) == Theme::colors().accent);

    command->setPrompt(QString());
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();
    CHECK_FALSE(filet->isVisible());
}

TEST_CASE("Le bandeau de commande ne change pas de hauteur en plein geste",
          "[ui][ligne-de-commande]")
{
    // Depuis que l'en-tête est parti, le bandeau vaut sa taille naturelle — et
    // l'invite, elle, va et vient avec le geste. Masquée quand rien n'attend,
    // elle fait GRANDIR le bandeau de dix-neuf pixels au premier geste (43 px
    // mesurés au repos contre 62 avec l'invite) : la feuille recule au moment
    // précis où l'on vise un point. L'invite garde donc sa rangée, vide ; seul
    // le filet bascule.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();

    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("dock.command"));
    FolioView *view = window.findChild<FolioView *>();
    REQUIRE(dock);
    REQUIRE(view);

    const int repos = dock->height();
    CHECK(repos > 40);

    Q_EMIT view->promptChanged(QStringLiteral("Point suivant ou [Cote / Annuler] :"));
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();
    CAPTURE(repos, dock->height());
    CHECK(dock->height() == repos);

    Q_EMIT view->promptChanged(QString());
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();
    CHECK(dock->height() == repos);
}

TEST_CASE("Le bandeau de commande n'a plus d'en-tête, et Ctrl+9 le ramène",
          "[ui][ligne-de-commande]")
{
    // « LIGNE DE COMMANDE » gravé au-dessus d'un champ où l'on tape dépensait
    // une rangée entière à nommer l'évidence. Le chevron de repli part avec la
    // barre de titre : la commande d'affichage devient donc le seul chemin de
    // retour, et elle doit ramener le bandeau AVEC SA HAUTEUR.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();

    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("dock.command"));
    REQUIRE(dock);
    // Une barre de titre posee explicitement, donc vide : c'est ainsi que Qt
    // retire la sienne.
    REQUIRE(dock->titleBarWidget() != nullptr);
    CHECK(dock->titleBarWidget()->findChildren<QLabel *>().isEmpty());

    QAction *bascule = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(QLatin1Char('&')) == QStringLiteral("Ligne de commande"))
            bascule = action;
    }
    REQUIRE(bascule);
    // Cochee : le ruban et le menu disent d'un coup d'oeil qu'il est la.
    CHECK(bascule->isCheckable());
    CHECK(bascule->isChecked());

    const int hauteur = dock->height();
    REQUIRE(hauteur > 40);

    bascule->trigger();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();
    CHECK_FALSE(dock->isVisible());
    CHECK_FALSE(bascule->isChecked());

    bascule->trigger();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();
    CHECK(dock->isVisible());
    CHECK(bascule->isChecked());
    // ET AVEC SA HAUTEUR. Un panneau du bas se mesure en hauteur, pas en
    // largeur : retenir 1560 px et les reposer comme hauteur le ramenait
    // plaque au plafond de 320 px. Il revenait, mais pas a sa taille.
    // Il revient A LA TAILLE DE SON CONTENU. Avant le correctif, setDockVisible
    // retenait `dock->width()` — 1400 px pour un bandeau qui court sur toute
    // la fenêtre — et la reposait comme HAUTEUR : le bandeau revenait plaqué
    // au plafond de 320 px et mangeait le tiers du dessin.
    CAPTURE(hauteur, dock->height(), dock->widget()->minimumSizeHint().height());
    CHECK(dock->height() <= dock->widget()->minimumSizeHint().height() + 4);
    CHECK(dock->height() < 150);
}

TEST_CASE("Une bascule allumée porte un filet, pas un aplat", "[ui][barre-etat]")
{
    // LE PIRE DEFAUT DU CHROME, et il tenait en une regle de feuille de style.
    // Six bascules portaient un aplat d'accent EN PERMANENCE, dans le coin le
    // plus visible de la fenetre. La regle 3 du theme dit que l'accent ne
    // designe que ce qui est actif ; ici il disait « actif » sans
    // interruption, donc il n'informait plus de rien et il criait la ou rien
    // d'important ne se passe.
    //
    // Le test compte l'encre. Verifier que la feuille de style contient la
    // bonne regle ne prouverait rien : Qt ignore sans un mot une declaration
    // qu'il ne comprend pas — un jeton mal ecrit (%ACCENT_HOVER% au lieu de
    // %ACCENTHOVER%) passerait le test et pas l'oeil.
    MainWindow window;
    window.resize(1560, 980);
    window.show();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();

    QStatusBar *bar = window.statusBar();
    QToolButton *ortho = bascule(bar, QStringLiteral("ORTHO"));
    REQUIRE(ortho);
    // Invariant 9 : ortho est allume par defaut, un schema se trace droit.
    REQUIRE(ortho->isChecked());

    const QImage image = bar->grab().toImage();
    const QRect zone = ortho->geometry();
    REQUIRE(zone.width() > 20);

    const QColor fond = Theme::colors().window;
    const QRect corps(zone.x(), zone.y(), zone.width(), zone.height() - 3);
    const QRect filet(zone.x(), zone.bottom() - 1, zone.width(), 2);

    // Le filet court sur toute la largeur du bouton.
    CHECK(pixelsProches(image, Theme::colors().accent, filet) > zone.width());
    // Et le corps ne porte que du texte : sans le correctif, l'aplat couvrait
    // les neuf dixiemes de la surface.
    const int encre = pixelsHorsFond(image, fond, corps);
    CAPTURE(encre, corps.width() * corps.height());
    CHECK(encre < corps.width() * corps.height() / 3);
}

TEST_CASE("Le filet d'une bascule est celui de l'onglet de ruban actif", "[ui][barre-etat]")
{
    // Un seul motif a apprendre pour deux endroits : le meme trait, de la
    // meme epaisseur, de la meme couleur, dit « ceci est actif ». Deux
    // marques differentes pour la meme idee obligent a apprendre deux fois.
    MainWindow window;
    window.resize(1560, 980);
    window.show();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();

    QStatusBar *bar = window.statusBar();
    QToolButton *ortho = bascule(bar, QStringLiteral("ORTHO"));
    REQUIRE(ortho);
    REQUIRE(ortho->isChecked());

    QTabBar *onglets = nullptr;
    for (QTabBar *candidate : window.findChildren<QTabBar *>()) {
        if (candidate->property("ribbon").toBool())
            onglets = candidate;
    }
    REQUIRE(onglets);
    REQUIRE(onglets->count() > 1);

    const QColor accent = Theme::colors().accent;
    const QImage barre = bar->grab().toImage();
    const QImage ruban = onglets->grab().toImage();
    const QRect ongletActif = onglets->tabRect(onglets->currentIndex());

    const int filetBascule = epaisseurDuFilet(barre, accent, ortho->geometry());
    const int filetOnglet = epaisseurDuFilet(ruban, accent, ongletActif);
    CAPTURE(filetBascule, filetOnglet);
    CHECK(filetBascule == 2);
    CHECK(filetOnglet == 2);
    CHECK(filetBascule == filetOnglet);
}

TEST_CASE("Cocher une bascule ne rogne pas son texte", "[ui][barre-etat]")
{
    // Le piege paye trois fois dans ce depot : un font-weight pose sur l'etat
    // coche fait grossir le texte, mais Qt a calcule la taille du bouton dans
    // son etat NORMAL — le texte se retrouve rogne des qu'on l'allume. La
    // distinction se fait par le filet et par la couleur, jamais par la
    // graisse.
    MainWindow window;
    window.resize(1560, 980);
    window.show();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();

    QStatusBar *bar = window.statusBar();
    const QColor fond = Theme::colors().window;

    for (QToolButton *button : bar->findChildren<QToolButton *>()) {
        if (!button->property("statusToggle").toBool())
            continue;
        CAPTURE(button->text());

        // Le corps, sans les deux rangees du filet : elles sont encrees a
        // l'etat coche par definition, et elles fausseraient la mesure.
        const QRect zone = button->geometry();
        const QRect corps(zone.x(), zone.y(), zone.width(), zone.height() - 3);

        const bool etat = button->isChecked();
        button->setChecked(false);
        for (int i = 0; i < 2; ++i)
            QCoreApplication::processEvents();
        const QPair<int, int> eteinte = etendueDeLEncre(bar->grab().toImage(), fond, corps);

        button->setChecked(true);
        for (int i = 0; i < 2; ++i)
            QCoreApplication::processEvents();
        const QPair<int, int> allumee = etendueDeLEncre(bar->grab().toImage(), fond, corps);

        button->setChecked(etat);
        for (int i = 0; i < 2; ++i)
            QCoreApplication::processEvents();

        REQUIRE(eteinte.first >= 0);
        REQUIRE(allumee.first >= 0);
        CAPTURE(eteinte.first, eteinte.second, allumee.first, allumee.second);

        // Le texte occupe EXACTEMENT la meme place dans les deux etats : la
        // distinction se fait par le filet et par la couleur, jamais par la
        // graisse. Un font-weight pose sur l'etat coche elargirait le trace
        // sans que Qt en tienne compte dans la taille du bouton — c'est ainsi
        // que le texte se rogne, et le bouton n'en dit rien.
        // Un pixel de tolerance : l'anticrenelage decide differemment selon
        // la couleur du texte, et la bascule change de couleur d'un etat a
        // l'autre. Une graisse, elle, deplace le bord de plusieurs pixels.
        CHECK(std::abs(eteinte.first - allumee.first) <= 1);
        CHECK(std::abs(eteinte.second - allumee.second) <= 1);

        // Et le trace respire de part et d'autre : il ne touche pas les bords.
        CHECK(allumee.first - corps.left() >= 3);
        CHECK(corps.right() - allumee.second >= 3);
    }
}

TEST_CASE("Une valeur qui change ne pousse pas ses voisines", "[ui][barre-etat]")
{
    // La chasse fixe ne suffit pas : « 0,00 » et « -184,50 » n'ont pas le meme
    // nombre de caracteres, et la case pousserait ses voisines a chaque
    // mouvement de souris. Une barre d'etat qui danse sous le curseur est
    // illisible, et c'est le widget qu'on regarde le plus souvent.
    MainWindow window;
    window.resize(1560, 980);
    window.show();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();

    QStatusBar *bar = window.statusBar();
    FolioView *view = window.findChild<FolioView *>();
    REQUIRE(view);

    QHash<QWidget *, int> avant;
    for (QWidget *w : bar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (w->isVisible())
            avant.insert(w, w->x());
    }
    REQUIRE(avant.size() > 10);

    // Le curseur parcourt la feuille : la valeur passe de quatre a huit
    // caracteres, et repasse en negatif hors du cadre.
    for (const QPointF &point : { QPointF(184.5, 96.25), QPointF(-12.75, -3.5),
                                  QPointF(0.0, 0.0), QPointF(419.99, 296.5) }) {
        Q_EMIT view->cursorMoved(point, QStringLiteral("B4"));
        for (int i = 0; i < 3; ++i)
            QCoreApplication::processEvents();
        for (auto it = avant.cbegin(); it != avant.cend(); ++it) {
            CAPTURE(point.x(), point.y());
            CHECK(it.key()->x() == it.value());
        }
    }
}

TEST_CASE("La barre d'état se replie plutôt que de se faire rogner", "[ui][barre-etat]")
{
    // Qt ne dit rien quand une barre d'etat deborde : il rogne la fin, et une
    // valeur coupee en deux (« 420x29 ») ment au lieu de manquer. La barre se
    // replie donc elle-meme, dans un ordre declare — le format d'abord, il se
    // lit dans la mise en page ; le rang du folio ensuite, il est dans le
    // titre de la fenetre ; la zone en dernier, elle se deduit de la position.
    MainWindow window;
    window.resize(1560, 980);
    window.show();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();

    QStatusBar *bar = window.statusBar();
    QLabel *format = valeurDeCase(bar, QStringLiteral("FORMAT"));
    QLabel *folio = valeurDeCase(bar, QStringLiteral("FOLIO"));
    QLabel *zone = valeurDeCase(bar, QStringLiteral("ZONE"));
    QLabel *position = valeurDeCase(bar, QStringLiteral("POSITION"));
    REQUIRE(format);
    REQUIRE(folio);
    REQUIRE(zone);
    REQUIRE(position);

    CHECK(format->isVisible());
    CHECK(folio->isVisible());

    for (int largeur : { 1560, 1440, 1280, 1100, 1024, 940, 860 }) {
        window.resize(largeur, 900);
        for (int i = 0; i < 6; ++i)
            QCoreApplication::processEvents();
        CAPTURE(largeur);

        // Rien de ce qui reste visible ne deborde de la barre.
        for (QWidget *w : bar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (!w->isVisible())
                continue;
            CHECK(w->x() >= 0);
            CHECK(w->x() + w->width() <= bar->width());
        }

        // Et ce qui n'est nulle part ailleurs sous les yeux reste, toujours :
        // la position, et les six bascules.
        CHECK(position->isVisible());
        for (QToolButton *button : bar->findChildren<QToolButton *>()) {
            if (button->property("statusToggle").toBool())
                CHECK(button->isVisible());
        }

        // L'ordre du sacrifice est tenu : le format part le premier, le rang
        // du folio ensuite, la zone en dernier. Donc si le format est encore
        // la, tout ce qui part apres lui l'est aussi.
        if (format->isVisible())
            CHECK(folio->isVisible());
        if (folio->isVisible())
            CHECK(zone->isVisible());
    }

    // Et tout revient quand la place revient.
    window.resize(1560, 980);
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();
    CHECK(format->isVisible());
    CHECK(folio->isVisible());
    CHECK(zone->isVisible());
}

TEST_CASE("La case de révision est en creux, et le creux suit le thème", "[ui][barre-etat]")
{
    // C'est le seul element PEINT de la barre, donc le seul qui puisse mentir
    // sans qu'aucune valeur soit fausse. Il a menti une fois : pose par une
    // QPalette, le creux n'apparaissait pas du tout — un QWidget nu ignore un
    // fond de feuille de style tant qu'on ne lui donne pas
    // Qt::WA_StyledBackground, et la palette, elle, aurait fige la couleur du
    // theme du moment. Le test regarde le pixel.
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    REQUIRE(app);

    MainWindow window;
    window.resize(1560, 980);
    window.show();

    for (bool sombre : { true, false }) {
        Theme::apply(*app, sombre);
        for (int i = 0; i < 6; ++i)
            QCoreApplication::processEvents();
        CAPTURE(sombre);

        QStatusBar *bar = window.statusBar();
        QWidget *creux = nullptr;
        for (QWidget *w : bar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (w->property("revisionCell").toBool())
                creux = w;
        }
        REQUIRE(creux);
        REQUIRE(creux->width() > 20);

        const QImage image = bar->grab().toImage();
        // Un coin de la case, loin du texte : c'est le fond qu'on mesure.
        const QPoint coin(creux->x() + 2, creux->y() + creux->height() / 2);
        const QColor peint = image.pixelColor(coin);
        CHECK(peint == Theme::colors().canvas);
        // Et le creux est bien un creux : plus profond que la bande.
        CHECK(peint != Theme::colors().window);
    }
    Theme::apply(*app, true);
}

TEST_CASE("Les cases du cartouche suivent le document", "[ui][barre-etat]")
{
    // Invariant 15 : ce que le rapport imprime, le dessin le montre. La barre
    // d'etat lit FORMAT, FOLIO et RÉV par TitleBlock::values(), c'est-a-dire
    // par le meme chemin que le cartouche imprime — un champ pose sur la
    // planche l'emporte sur le reglage du dossier, et la barre ne peut pas
    // dire autre chose que le papier.
    MainWindow window;
    window.resize(1560, 980);
    window.show();
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();

    Document *document = window.findChild<Document *>();
    REQUIRE(document);
    QStatusBar *bar = window.statusBar();
    QLabel *format = valeurDeCase(bar, QStringLiteral("FORMAT"));
    QLabel *folio = valeurDeCase(bar, QStringLiteral("FOLIO"));
    REQUIRE(format);
    REQUIRE(folio);

    const Folio *courant = document->currentFolio();
    REQUIRE(courant);
    CHECK(format->text().startsWith(courant->sheet.id));
    CHECK(format->text().contains(QString::number(int(std::lround(courant->sheet.width)))));
    CHECK(folio->text() == QStringLiteral("1/1"));

    // Une planche de plus, et le rang suit.
    auto seconde = std::make_unique<Folio>();
    seconde->number = QStringLiteral("2");
    seconde->title = QStringLiteral("Commande");
    document->push(std::make_unique<AddFolioCommand>(document->project(), std::move(seconde)));
    for (int i = 0; i < 3; ++i)
        QCoreApplication::processEvents();
    CHECK(folio->text().endsWith(QStringLiteral("/2")));

    // L'indice du dossier arrive dans la case RÉV, par le meme chemin que le
    // cartouche : c'est ProjectInfo::revision que TitleBlock::values publie.
    ProjectInfo info = document->project().info;
    info.revision = QStringLiteral("C");
    document->push(std::make_unique<ChangeProjectInfoCommand>(document->project(), info));
    for (int i = 0; i < 3; ++i)
        QCoreApplication::processEvents();

    QLabel *revision = nullptr;
    for (QWidget *cell : bar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (qobject_cast<QLabel *>(cell) || cell->property("cellRule").toBool())
            continue;
        for (QLabel *inner : cell->findChildren<QLabel *>()) {
            if (inner->property("cellValue").toBool())
                revision = inner;
        }
    }
    REQUIRE(revision);
    CHECK(revision->text() == QStringLiteral("C"));

    // Et le champ pose sur LA PLANCHE l'emporte sur le reglage du dossier :
    // c'est toute la raison de passer par TitleBlock::values() plutot que de
    // lire ProjectInfo::revision. Une planche revisee seule porte son propre
    // indice au cartouche ; la barre d'etat doit dire la meme chose, sinon
    // elle contredit le papier que le cableur a en main.
    Folio *planche = document->currentFolio();
    REQUIRE(planche);
    planche->titleBlock.insert(QStringLiteral("revision"), QStringLiteral("D"));
    document->push(std::make_unique<ChangeProjectInfoCommand>(document->project(),
                                                              document->project().info));
    for (int i = 0; i < 3; ++i)
        QCoreApplication::processEvents();
    CHECK(revision->text() == QStringLiteral("D"));
}
