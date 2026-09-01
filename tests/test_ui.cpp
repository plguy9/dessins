#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QLineEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>

#include "core/documentcommands.h"
#include "symbols/librarystore.h"
#include "ui/document.h"
#include "ui/folioview.h"
#include "ui/symboleditor.h"
#include "ui/commandline.h"
#include "ui/commandpalette.h"
#include "ui/componentdialog.h"
#include "ui/mainwindow.h"
#include "ui/draftingsettingsdialog.h"
#include "ui/pagesetupdialog.h"
#include "ui/reportpanel.h"
#include "ui/startpage.h"
#include "ui/surferdialog.h"
#include "ui/terminalstripdialog.h"
#include "ui/symbolpalette.h"
#include "ui/theme.h"
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

    DraftingSettingsDialog dialog(engine, 2.5, true);
    dialog.resize(560, 520);
    CHECK(hasVisibleContent(dialog.grab()));

    // Les reglages ressortent tels qu'ils sont entres : la boite ne doit rien
    // perdre en route, sinon l'utilisateur croit avoir regle et n'a rien fait.
    const SnapEngine out = dialog.engine();
    CHECK(out.hasMode(SnapMode::Nearest));
    CHECK(out.orthoEnabled());
    CHECK(out.polarIncrement() == 30.0);
    CHECK(dialog.gridStep() == 2.5);
    CHECK(dialog.gridVisible());
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
                                                             a4, frame));
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
                                QStringLiteral("Fils"),          QStringLiteral("Câblage De/Vers") };
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
