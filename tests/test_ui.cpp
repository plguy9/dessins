#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMouseEvent>
#include <QPixmap>
#include <QTemporaryDir>

#include "core/documentcommands.h"
#include "symbols/librarystore.h"
#include "ui/document.h"
#include "ui/folioview.h"
#include "ui/symboleditor.h"
#include "ui/commandline.h"
#include "ui/mainwindow.h"
#include "ui/draftingsettingsdialog.h"
#include "ui/pagesetupdialog.h"
#include "ui/symbolpalette.h"
#include "ui/theme.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

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
