#include "mainwindow.h"

#include "folionavigator.h"
#include "io/csvexport.h"
#include "io/dsnfile.h"
#include "io/dxfexport.h"
#include "commandline.h"
#include "ladderdialog.h"
#include "rules/ladder.h"
#include "draftingsettingsdialog.h"
#include "pagesetupdialog.h"
#include "propertiespanel.h"
#include "render/foliopainter.h"
#include "render/pdfexport.h"
#include "reportpanel.h"
#include "rules/numbering.h"
#include "rules/reportplacer.h"
#include "symboleditor.h"
#include "symbolpalette.h"
#include "componentdialog.h"
#include "wiretypedialog.h"
#include "theme.h"
#include "symbols/librarystore.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace dsn {

namespace {

// Boite de dialogue des informations du projet. Ce sont les champs qui
// remplissent le cartouche de chaque folio.
class ProjectInfoDialog : public QDialog
{
public:
    ProjectInfoDialog(ProjectInfo info, QWidget *parent) : QDialog(parent), m_info(std::move(info))
    {
        setWindowTitle(tr("Informations du projet"));
        auto *form = new QFormLayout(this);

        m_title = new QLineEdit(m_info.title, this);
        m_reference = new QLineEdit(m_info.reference, this);
        m_client = new QLineEdit(m_info.client, this);
        m_author = new QLineEdit(m_info.author, this);
        m_revision = new QLineEdit(m_info.revision, this);
        m_date = new QDateEdit(m_info.date, this);
        m_date->setCalendarPopup(true);
        m_date->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
        m_notes = new QPlainTextEdit(m_info.notes, this);
        m_notes->setMaximumHeight(90);

        form->addRow(tr("Titre"), m_title);
        form->addRow(tr("Référence"), m_reference);
        form->addRow(tr("Client"), m_client);
        form->addRow(tr("Auteur"), m_author);
        form->addRow(tr("Indice"), m_revision);
        form->addRow(tr("Date"), m_date);
        form->addRow(tr("Notes"), m_notes);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Appliquer"));
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    ProjectInfo info() const
    {
        ProjectInfo out = m_info;
        out.title = m_title->text();
        out.reference = m_reference->text();
        out.client = m_client->text();
        out.author = m_author->text();
        out.revision = m_revision->text();
        out.date = m_date->date();
        out.notes = m_notes->toPlainText();
        return out;
    }

private:
    ProjectInfo m_info;
    QLineEdit *m_title;
    QLineEdit *m_reference;
    QLineEdit *m_client;
    QLineEdit *m_author;
    QLineEdit *m_revision;
    QDateEdit *m_date;
    QPlainTextEdit *m_notes;
};

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_document = new Document(this);

    // Catalogue fabricant : celui livre avec le logiciel, complete par les
    // fichiers du poste. Charge une fois pour toute la session.
    m_catalog = Catalog::loadAll();

    // La bibliotheque integree est chargee une fois pour toutes : le logiciel
    // doit etre utilisable des le premier lancement, sans installation.
    SymbolLibrary library;
    const LibraryLoadReport report = LibraryStore::loadAll(library);
    m_document->project().library = library;
    m_document->newProject(library);

    m_view = new FolioView(m_document, this);
    setCentralWidget(m_view);

    createDocks();
    createActions();
    createStatusBar();

    m_palette->setLibrary(&m_document->project().library);
    m_palette->setNorm(m_document->profile().norm);
    m_view->setGridStep(m_document->profile().gridStep);

    connect(m_view, &FolioView::selectionChanged, this, [this] {
        m_properties->setSelection(m_view->selection());
        updateActions();
    });
    connect(m_view, &FolioView::cursorMoved, this, [this](const QPointF &mm, const QString &zone) {
        m_cursorLabel->setText(QStringLiteral("X %1   Y %2 mm")
                                       .arg(mm.x(), 0, 'f', 1)
                                       .arg(mm.y(), 0, 'f', 1));
        m_zoneLabel->setText(zone.isEmpty() ? tr("hors cadre") : tr("zone %1").arg(zone));
    });
    connect(m_view, &FolioView::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 6000); });
    connect(m_view, &FolioView::zoomChanged, this, [this] { updateActions(); });
    connect(m_document, &Document::modifiedChanged, this, &MainWindow::updateTitle);
    connect(m_view, &FolioView::contextMenuRequested, this, &MainWindow::showCanvasContextMenu);
    connect(m_view, &FolioView::componentPlaced, this, [this](const QString &id) {
        if (m_editOnInsertAction && m_editOnInsertAction->isChecked())
            editComponent(id, true);
    });
    // Double-clic sur un appareil : la meme boite, comme chez AutoCAD.
    connect(m_view, &FolioView::entityActivated, this, [this](const QString &id) {
        if (dynamic_cast<const SymbolInstance *>(m_document->project().findEntity(id)))
            editComponent(id, false);
    });
    connect(m_document, &Document::undoStateChanged, this, &MainWindow::updateActions);
    connect(m_document, &Document::undoStateChanged, this, &MainWindow::rebuildWireTypeSelector);
    connect(m_document, &Document::currentFolioChanged, this, &MainWindow::updateTitle);

    // Le theme est applique en dernier : il redessine les icones et
    // reharmonise le fond du canevas avec le chrome de la fenetre.
    QSettings settings;
    // AutoCAD ouvre la boite a chaque insertion : on fait de meme par defaut,
    // et on retient le choix de celui qui prefere poser en serie.
    m_editOnInsertAction->setChecked(
            settings.value(QStringLiteral("ui/editComponentOnInsert"), true).toBool());
    connect(m_editOnInsertAction, &QAction::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("ui/editComponentOnInsert"), on);
    });
    const bool dark = settings.value(QStringLiteral("ui/darkTheme"), true).toBool();
    m_darkAction->blockSignals(true);
    m_darkAction->setChecked(dark);
    m_darkAction->blockSignals(false);
    applyTheme(dark);

    registerCommands();
    resizeDocks({ m_commandDock }, { 108 }, Qt::Vertical);
    syncDraftingToggles();
    resize(1560, 980);
    updateTitle();
    updateActions();

    statusBar()->showMessage(
            tr("%1 symboles chargés — commencez par poser un symbole depuis la palette.")
                    .arg(report.symbolsLoaded),
            8000);
}

MainWindow::~MainWindow() = default;

// --------------------------------------------------------------------------

void MainWindow::createDocks()
{
    auto *paletteDock = new QDockWidget(tr("Symboles"), this);
    paletteDock->setObjectName(QStringLiteral("dock.symbols"));
    m_palette = new SymbolPalette(paletteDock);
    paletteDock->setWidget(m_palette);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock);

    auto *navigatorDock = new QDockWidget(tr("Folios"), this);
    navigatorDock->setObjectName(QStringLiteral("dock.folios"));
    m_navigator = new FolioNavigator(m_document, navigatorDock);
    navigatorDock->setWidget(m_navigator);
    addDockWidget(Qt::LeftDockWidgetArea, navigatorDock);

    auto *propertiesDock = new QDockWidget(tr("Propriétés"), this);
    propertiesDock->setObjectName(QStringLiteral("dock.properties"));
    m_properties = new PropertiesPanel(m_document, propertiesDock);
    propertiesDock->setWidget(m_properties);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    auto *reportDock = new QDockWidget(tr("Rapports"), this);
    reportDock->setObjectName(QStringLiteral("dock.reports"));
    m_reports = new ReportPanel(m_document, reportDock);
    reportDock->setWidget(m_reports);
    addDockWidget(Qt::BottomDockWidgetArea, reportDock);
    reportDock->hide(); // ouvert a la demande : il n'encombre pas le dessin

    // Largeurs de depart. Les noms de symboles et les titres de folios sont
    // longs : un panneau trop etroit les tronque des le premier affichage.
    paletteDock->setMinimumWidth(260);
    navigatorDock->setMinimumWidth(260);
    propertiesDock->setMinimumWidth(280);
    resizeDocks({ paletteDock, propertiesDock }, { 320, 340 }, Qt::Horizontal);
    resizeDocks({ paletteDock, navigatorDock }, { 3, 2 }, Qt::Vertical);

    m_commandDock = new QDockWidget(tr("Ligne de commande"), this);
    m_commandDock->setObjectName(QStringLiteral("dock.command"));
    m_command = new CommandLine(m_commandDock);
    m_commandDock->setWidget(m_command);
    addDockWidget(Qt::BottomDockWidgetArea, m_commandDock);
    // Trois lignes d'historique, comme la ligne de commande d'AutoCAD :
    // elle informe sans manger la place du dessin. Elle reste
    // redimensionnable pour qui veut relire une longue liste.
    m_commandDock->setMinimumHeight(84);
    m_commandDock->setMaximumHeight(320);

    // Echap dans la ligne de commande rend la main au dessin : on ne reste
    // jamais coince au clavier alors qu'on voulait tracer.
    connect(m_command, &CommandLine::escapePressed, this, [this] { m_view->setFocus(); });

    connect(m_palette, &SymbolPalette::symbolChosen, this, [this](const QString &id) {
        m_view->setPendingSymbol(id);
        statusBar()->showMessage(
                tr("Cliquez pour poser le symbole. R fait pivoter, M retourne, Échap annule."),
                6000);
    });
    connect(m_navigator, &FolioNavigator::pageSetupRequested, this, &MainWindow::editPageSetup);
    connect(m_navigator, &FolioNavigator::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 4000); });
    connect(m_properties, &PropertiesPanel::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 4000); });
}

void MainWindow::createActions()
{
    using G = Icons::Glyph;

    // Une seule barre d'outils, en icones, groupee par separateurs. Trois
    // barres empilees mangent une bande de fenetre que le dessin utilise mieux.
    m_toolBar = addToolBar(tr("Barre d'outils"));
    m_toolBar->setObjectName(QStringLiteral("toolbar.main"));
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);

    // L'action porte son glyphe : au changement de theme, on redessine les
    // icones sans reconstruire les menus.
    auto make = [&](QMenu *menu, bool onToolBar, G glyph, const QString &text,
                    const QKeySequence &shortcut, const QString &tip, auto slot) {
        auto *action = new QAction(Icons::icon(glyph), text, this);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        if (!tip.isEmpty()) {
            action->setToolTip(shortcut.isEmpty()
                                       ? tip
                                       : QStringLiteral("%1  ·  %2").arg(tip,
                                                 shortcut.toString(QKeySequence::NativeText)));
            action->setStatusTip(tip);
        }
        connect(action, &QAction::triggered, this, slot);
        if (menu)
            menu->addAction(action);
        if (onToolBar)
            m_toolBar->addAction(action);
        m_actionGlyphs.insert(action, int(glyph));
        return action;
    };

    // ---- Fichier -------------------------------------------------------
    QMenu *fileMenu = menuBar()->addMenu(tr("&Fichier"));
    make(fileMenu, true, G::New, tr("&Nouveau projet"), QKeySequence::New,
         tr("Créer un projet vide"), &MainWindow::newProject);
    make(fileMenu, true, G::Open, tr("&Ouvrir…"), QKeySequence::Open,
         tr("Ouvrir un projet .dsn"), &MainWindow::openProject);
    m_recentMenu = fileMenu->addMenu(tr("Fichiers &récents"));
    rebuildRecentMenu();
    make(fileMenu, true, G::Save, tr("&Enregistrer"), QKeySequence::Save,
         tr("Enregistrer le projet"), [this] { saveProject(); });
    make(fileMenu, false, G::Save, tr("Enregistrer &sous…"), QKeySequence::SaveAs,
         tr("Enregistrer sous un autre nom"), [this] { saveProjectAs(); });
    fileMenu->addSeparator();
    make(fileMenu, false, G::ExportPdf, tr("Exporter en &PDF…"), QKeySequence(),
         tr("Le dossier complet, un folio par page"), &MainWindow::exportPdf);
    make(fileMenu, false, G::ExportDxf, tr("Exporter en &DXF…"), QKeySequence(),
         tr("Un fichier DXF par folio, pour AutoCAD"), &MainWindow::exportDxf);
    make(fileMenu, false, G::ExportCsv, tr("Exporter le rapport en &CSV…"), QKeySequence(),
         tr("Le rapport affiché, pour un tableur"), &MainWindow::exportCurrentReport);
    make(fileMenu, true, G::Print, tr("Im&primer…"), QKeySequence::Print,
         tr("Imprimer le dossier"), &MainWindow::printProject);
    fileMenu->addSeparator();
    make(fileMenu, false, G::Delete, tr("&Quitter"), QKeySequence::Quit, QString(),
         [this] { close(); });

    m_toolBar->addSeparator();

    // ---- Edition -------------------------------------------------------
    QMenu *editMenu = menuBar()->addMenu(tr("&Édition"));
    m_undoAction = make(editMenu, true, G::Undo, tr("&Annuler"), QKeySequence::Undo,
                        tr("Annuler la dernière action"), [this] { m_document->undo(); });
    m_redoAction = make(editMenu, true, G::Redo, tr("&Rétablir"), QKeySequence::Redo,
                        tr("Rétablir l'action annulée"), [this] { m_document->redo(); });
    editMenu->addSeparator();
    m_copyAction = make(editMenu, false, G::Copy, tr("&Copier"), QKeySequence::Copy,
                        tr("Copier la sélection"), [this] { m_view->copySelection(); });
    m_pasteAction = make(editMenu, false, G::Paste, tr("C&oller"), QKeySequence::Paste,
                         tr("Coller sous le curseur"), [this] { m_view->pasteClipboard(); });
    m_deleteAction = make(editMenu, false, G::Delete, tr("&Supprimer"), QKeySequence::Delete,
                          tr("Supprimer la sélection"), [this] { m_view->deleteSelection(); });
    m_selectAllAction = make(editMenu, false, G::Select, tr("&Tout sélectionner"),
                             QKeySequence::SelectAll, QString(), [this] { m_view->selectAll(); });
    editMenu->addSeparator();
    m_rotateAction = make(editMenu, true, G::Rotate, tr("&Pivoter"), QKeySequence(Qt::Key_R),
                          tr("Pivoter d'un quart de tour"),
                          [this] { m_view->rotateSelection(true); });
    m_mirrorAction = make(editMenu, true, G::Mirror, tr("Re&tourner"), QKeySequence(Qt::Key_M),
                          tr("Retourner selon l'axe vertical"),
                          [this] { m_view->mirrorSelection(); });
    m_highlightAction = make(editMenu, true, G::Highlight, tr("Mettre le potentiel en évidence"),
                             QKeySequence(Qt::CTRL | Qt::Key_H),
                             tr("Suivre un potentiel à travers le folio"),
                             [this] { m_view->highlightNetOfSelection(); });
    m_moveAction = make(editMenu, false, G::Select, tr("&Déplacer"), QKeySequence(Qt::Key_D),
                        tr("Déplacer la sélection d'un point de base à un autre"),
                        [this] { m_view->beginMoveSelection(); });
    m_offsetAction = make(editMenu, false, G::Duplicate, tr("Déca&ler…"), QKeySequence(Qt::Key_O),
                          tr("Copier un fil parallèlement, à une distance donnée"),
                          [this] { offsetSelection(); });
    m_stretchAction = make(editMenu, false, G::Highlight, tr("Éti&rer"), QKeySequence(Qt::Key_E),
                           tr("Encadrer des sommets par capture, puis les déplacer"),
                           [this] { m_view->beginStretch(); });

    m_toolBar->addSeparator();

    // ---- Outils --------------------------------------------------------
    QMenu *toolMenu = menuBar()->addMenu(tr("&Outils"));
    auto *toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    struct ToolSpec {
        FolioView::Tool tool;
        G glyph;
        const char *label;
        Qt::Key key;
        const char *hint;
    };
    const ToolSpec tools[] = {
        { FolioView::Tool::Select, G::Select, QT_TR_NOOP("&Sélection"), Qt::Key_S,
          QT_TR_NOOP("Sélectionner, déplacer, encadrer") },
        { FolioView::Tool::Wire, G::Wire, QT_TR_NOOP("&Fil"), Qt::Key_W,
          QT_TR_NOOP("Tracer un fil orthogonal") },
        { FolioView::Tool::Junction, G::Junction, QT_TR_NOOP("&Jonction"), Qt::Key_J,
          QT_TR_NOOP("Poser un point de connexion") },
        { FolioView::Tool::Label, G::LabelTag, QT_TR_NOOP("&Étiquette"), Qt::Key_L,
          QT_TR_NOOP("Nommer un potentiel") },
        { FolioView::Tool::Text, G::Text, QT_TR_NOOP("&Texte"), Qt::Key_T,
          QT_TR_NOOP("Annoter le folio") },
        { FolioView::Tool::Trim, G::Delete, QT_TR_NOOP("&Ajuster"), Qt::Key_A,
          QT_TR_NOOP("Couper un fil entre deux croisements") },
        { FolioView::Tool::Extend, G::Highlight, QT_TR_NOOP("Pro&longer"), Qt::Key_P,
          QT_TR_NOOP("Allonger un fil jusqu'au premier obstacle") },
    };
    for (const ToolSpec &spec : tools) {
        auto *action = new QAction(Icons::icon(spec.glyph), tr(spec.label), this);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(spec.key));
        action->setToolTip(QStringLiteral("%1  ·  %2")
                                   .arg(tr(spec.hint), QKeySequence(spec.key)
                                                               .toString(QKeySequence::NativeText)));
        action->setStatusTip(tr(spec.hint));
        action->setData(int(spec.tool));
        toolGroup->addAction(action);
        toolMenu->addAction(action);
        m_toolBar->addAction(action);
        m_toolActions.append(action);
        m_actionGlyphs.insert(action, int(spec.glyph));
        connect(action, &QAction::triggered, this, [this, spec] { m_view->setTool(spec.tool); });
    }
    m_toolActions.first()->setChecked(true);

    toolMenu->addSeparator();

    // Nature de l'etiquette a poser. Les fleches de signal d'AutoCAD
    // Electrical sont des renvois orientes : la source dit ou part le signal,
    // la destination d'ou il vient, et les deux portent le meme nom de code.
    QMenu *labelMenu = toolMenu->addMenu(tr("&Nature de l'étiquette"));
    auto *labelGroup = new QActionGroup(this);
    struct LabelKind {
        const char *label;
        const char *hint;
        Label::Scope scope;
        Label::Role role;
    };
    const LabelKind labelKinds[] = {
        { QT_TR_NOOP("Étiquette de &potentiel"),
          QT_TR_NOOP("Nomme un potentiel dans son folio seulement"), Label::Scope::Folio,
          Label::Role::Plain },
        { QT_TR_NOOP("&Renvoi de folio"),
          QT_TR_NOOP("Relie le même potentiel à travers tout le dossier"), Label::Scope::Project,
          Label::Role::Plain },
        { QT_TR_NOOP("Flèche de signal — &source"),
          QT_TR_NOOP("Origine d'un signal qui se poursuit sur une autre page"),
          Label::Scope::Project, Label::Role::Source },
        { QT_TR_NOOP("Flèche de signal — &destination"),
          QT_TR_NOOP("Reprise d'un signal venu d'une autre page"), Label::Scope::Project,
          Label::Role::Destination },
    };
    for (const LabelKind &kind : labelKinds) {
        auto *action = new QAction(tr(kind.label), this);
        action->setCheckable(true);
        action->setStatusTip(tr(kind.hint));
        labelGroup->addAction(action);
        labelMenu->addAction(action);
        connect(action, &QAction::triggered, this, [this, kind] {
            m_view->setLabelScope(kind.scope);
            m_view->setLabelRole(kind.role);
            m_view->setTool(FolioView::Tool::Label);
        });
    }
    labelGroup->actions().first()->setChecked(true);

    connect(m_view, &FolioView::toolChanged, this, [this](FolioView::Tool tool) {
        for (QAction *action : std::as_const(m_toolActions)) {
            if (action->data().toInt() == int(tool))
                action->setChecked(true);
        }
    });

    // Type de fil courant, dans la barre d'outils. AutoCAD Electrical arme de
    // meme le type avant de tracer : c'est le reglage qu'on change le plus
    // souvent d'un schema a l'autre, il ne peut pas etre enfoui dans un menu.
    m_wireTypeSelector = new QComboBox(m_toolBar);
    m_wireTypeSelector->setToolTip(tr("Type des fils à venir"));
    m_wireTypeSelector->setMinimumWidth(170);
    m_wireTypeSelector->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    m_toolBar->addWidget(m_wireTypeSelector);
    connect(m_wireTypeSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
            m_view->setCurrentWireType(m_wireTypeSelector->itemData(index).toString());
    });
    rebuildWireTypeSelector();

    m_toolBar->addSeparator();

    // ---- Affichage -----------------------------------------------------
    QMenu *viewMenu = menuBar()->addMenu(tr("&Affichage"));
    make(viewMenu, true, G::ZoomIn, tr("Zoom &avant"), QKeySequence::ZoomIn, tr("Zoom avant"),
         [this] { m_view->zoomIn(); });
    make(viewMenu, true, G::ZoomOut, tr("Zoom a&rrière"), QKeySequence::ZoomOut,
         tr("Zoom arrière"), [this] { m_view->zoomOut(); });
    m_zoomFitAction = make(viewMenu, true, G::ZoomFit, tr("&Ajuster au folio"),
                           QKeySequence(Qt::CTRL | Qt::Key_0), tr("Voir le folio entier"),
                           [this] { m_view->zoomToFit(); });
    make(viewMenu, false, G::ZoomIn, tr("Zoom &fenêtre"), QKeySequence(),
         tr("Encadrer la zone à agrandir"), [this] { m_view->beginZoomWindow(); });
    m_zoomPreviousAction = make(viewMenu, false, G::Undo, tr("Vue &précédente"), QKeySequence(),
                                tr("Revenir à la vue précédente"),
                                [this] { m_view->zoomPrevious(); });
    make(viewMenu, false, G::Text, tr("Ligne de &commande"),
         QKeySequence(Qt::CTRL | Qt::Key_9), tr("Placer le curseur dans la ligne de commande"),
         [this] {
             m_commandDock->show();
             m_commandDock->raise();
             m_command->focusInput();
         });
    make(viewMenu, false, G::ZoomFit, tr("&Taille réelle"),
         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_0),
         tr("Un millimètre du dessin pour un millimètre à l'écran"),
         [this] { m_view->zoomActual(); });
    viewMenu->addSeparator();

    // Raccourcis de palettes d'AutoCAD : Ctrl+1 les proprietes, Ctrl+3 les
    // palettes d'outils, Ctrl+4 le gestionnaire de jeu de feuilles. Ce sont
    // les trois que tout dessinateur a dans les doigts.
    struct PaletteShortcut {
        const char *label;
        const char *dock;
        Qt::Key key;
        const char *hint;
    };
    const PaletteShortcut palettes[] = {
        { QT_TR_NOOP("&Propriétés"), "dock.properties", Qt::Key_1,
          QT_TR_NOOP("Ouvrir la palette des propriétés de la sélection") },
        { QT_TR_NOOP("Palette de &symboles"), "dock.symbols", Qt::Key_3,
          QT_TR_NOOP("Ouvrir la bibliothèque de symboles") },
        { QT_TR_NOOP("Navigateur de &folios"), "dock.folios", Qt::Key_4,
          QT_TR_NOOP("Ouvrir la liste des pages du dossier") },
    };
    for (const PaletteShortcut &palette : palettes) {
        const QString name = QString::fromLatin1(palette.dock);
        make(viewMenu, false, G::Palette, tr(palette.label),
             QKeySequence(Qt::CTRL | palette.key), tr(palette.hint), [this, name] {
                 if (auto *dock = findChild<QDockWidget *>(name)) {
                     dock->show();
                     dock->raise();
                     dock->setFocus();
                 }
             });
    }
    viewMenu->addSeparator();

    auto addToggle = [&](QMenu *menu, bool onToolBar, G glyph, const QString &text, bool checked,
                         const QString &tip, auto slot) {
        auto *action = new QAction(Icons::icon(glyph), text, this);
        action->setCheckable(true);
        action->setChecked(checked);
        if (!tip.isEmpty()) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        }
        connect(action, &QAction::toggled, this, slot);
        menu->addAction(action);
        if (onToolBar)
            m_toolBar->addAction(action);
        m_actionGlyphs.insert(action, int(glyph));
        return action;
    };

    createDraftingToggles(viewMenu);
    addToggle(viewMenu, false, G::SymbolPlace, tr("&Numéros de broches"), false, QString(),
              [this](bool on) {
                  RenderStyle style = m_view->style();
                  style.showPinNumbers = on;
                  m_view->setStyle(style);
              });
    addToggle(viewMenu, false, G::Check, tr("&Broches non raccordées"), true,
              tr("Marquer d'une croix les broches en l'air"), [this](bool on) {
                  RenderStyle style = m_view->style();
                  style.showUnconnectedPins = on;
                  m_view->setStyle(style);
              });
    viewMenu->addSeparator();
    m_darkAction = addToggle(viewMenu, false, G::Theme, tr("Thème &sombre"), true, QString(),
                             [this](bool on) { applyTheme(on); });
    viewMenu->addSeparator();
    for (QDockWidget *dock : findChildren<QDockWidget *>())
        viewMenu->addAction(dock->toggleViewAction());

    m_toolBar->addSeparator();

    // ---- Projet --------------------------------------------------------
    QMenu *projectMenu = menuBar()->addMenu(tr("&Projet"));
    m_pageSetupAction = make(projectMenu, true, G::Folios, tr("&Mise en page…"),
                             QKeySequence(Qt::CTRL | Qt::Key_P),
                             tr("Format de feuille, cadre, zones de repérage, cartouche"),
                             &MainWindow::editPageSetup);
    make(projectMenu, false, G::Info, tr("&Informations du projet…"), QKeySequence(),
         tr("Titre, client, référence — ce que porte le cartouche"),
         &MainWindow::editProjectInfo);
    make(projectMenu, false, G::Grid, tr("Insérer une &échelle de commande…"), QKeySequence(),
         tr("Deux rails d'alimentation et des lignes numérotées"),
         &MainWindow::insertLadder);
    make(projectMenu, false, G::Wire, tr("&Types de fils…"), QKeySequence(),
         tr("Couleur, section, calque et style de chaque type de fil"),
         &MainWindow::editWireTypes);
    make(projectMenu, false, G::Reports, tr("&Poser le rapport dans le dessin…"), QKeySequence(),
         tr("Insère le rapport affiché sous forme de table sur le folio actif"),
         &MainWindow::placeCurrentReport);
    make(projectMenu, false, G::Renumber, tr("&Format des repères…"), QKeySequence(),
         tr("Séquentiel ou par référence de ligne, avec ses paramètres remplaçables"),
         &MainWindow::editTagFormat);
    make(projectMenu, true, G::Renumber, tr("&Repérage automatique"),
         QKeySequence(Qt::CTRL | Qt::Key_R),
         tr("Désigner les appareils et repérer les fils"), &MainWindow::renumberAll);
    make(projectMenu, true, G::Check, tr("&Contrôler le schéma"), QKeySequence(Qt::Key_F8),
         tr("Chercher les fils en l'air et les symboles manquants"),
         &MainWindow::checkSchematic);
    projectMenu->addSeparator();

    QMenu *profileMenu = projectMenu->addMenu(tr("&Profil métier"));
    auto *profileGroup = new QActionGroup(this);
    const QList<Profile> profiles = Profile::all();
    for (const Profile &profile : profiles) {
        auto *action = new QAction(profile.name, this);
        action->setCheckable(true);
        action->setChecked(profile.id == m_document->project().profileId);
        profileGroup->addAction(action);
        profileMenu->addAction(action);
        const QString id = profile.id;
        connect(action, &QAction::triggered, this, [this, id] { setProfile(id); });
    }

    // ---- Symboles ------------------------------------------------------
    QMenu *symbolMenu = menuBar()->addMenu(tr("&Symboles"));
    m_editComponentAction = make(symbolMenu, true, G::Properties, tr("&Éditer le composant…"),
                                 QKeySequence(Qt::Key_F2),
                                 tr("Repère, description, catalogue et rattachement de "
                                    "l'appareil sélectionné"),
                                 &MainWindow::editSelectedComponent);
    m_editOnInsertAction = new QAction(tr("Éditer le composant à l'&insertion"), this);
    m_editOnInsertAction->setCheckable(true);
    m_editOnInsertAction->setStatusTip(
            tr("Ouvrir la boîte du composant juste après avoir posé un symbole"));
    symbolMenu->addAction(m_editOnInsertAction);
    symbolMenu->addSeparator();
    make(symbolMenu, false, G::Plus, tr("&Nouveau symbole…"), QKeySequence(),
         tr("Dessiner un symbole et l'ajouter à la bibliothèque"), &MainWindow::newSymbol);
    make(symbolMenu, true, G::Edit, tr("&Modifier le symbole de la palette…"), QKeySequence(),
         tr("Ouvrir l'éditeur sur le symbole sélectionné dans la palette"),
         [this] { editCurrentSymbol(false); });
    make(symbolMenu, false, G::Duplicate, tr("&Dupliquer puis modifier…"), QKeySequence(),
         tr("Partir d'un symbole existant sans toucher à l'original"),
         [this] { editCurrentSymbol(true); });

    // ---- Aide ----------------------------------------------------------
    QMenu *helpMenu = menuBar()->addMenu(tr("&Aide"));
    make(helpMenu, false, G::Info, tr("À &propos"), QKeySequence(), QString(), [this] {
        QMessageBox::about(
                this, tr("À propos de Dessins"),
                tr("<h3>Dessins %1</h3>"
                   "<p>Logiciel de dessin électrique — schémas de commande et de puissance, "
                   "unifilaires de distribution, circuits électroniques.</p>"
                   "<p>Symboles CEI 60617 et ANSI, repérage automatique des fils et des "
                   "appareils, nomenclature et bornier déduits du schéma, export PDF et DXF.</p>"
                   "<p>Qt %2</p>")
                        .arg(QApplication::applicationVersion(), QT_VERSION_STR));
    });
}

void MainWindow::refreshIcons()
{
    for (auto it = m_actionGlyphs.constBegin(); it != m_actionGlyphs.constEnd(); ++it)
        it.key()->setIcon(Icons::icon(Icons::Glyph(it.value())));
}

void MainWindow::editComponent(const QString &entityId, bool insertion)
{
    Folio *folio = nullptr;
    Entity *entity = m_document->project().findEntity(entityId, &folio);
    auto *symbol = dynamic_cast<SymbolInstance *>(entity);
    if (!symbol || !folio)
        return;

    ComponentDialog dialog(m_document->project(), *symbol, m_catalog, insertion, this);
    if (dialog.exec() != QDialog::Accepted) {
        // Annuler a l'insertion annule vraiment la pose, comme chez AutoCAD.
        // La pose vient d'etre empilee : la defaire est sans effet de bord.
        if (insertion && m_document->commands().canUndo())
            m_document->undo();
        return;
    }

    auto after = std::make_unique<SymbolInstance>(dialog.result());
    m_document->push(std::make_unique<ModifyEntityCommand>(
            m_document->project(), folio->id(), symbol->clone(), std::move(after),
            tr("Éditer le composant")));
    m_view->update();
}

void MainWindow::editSelectedComponent()
{
    for (const QString &id : m_view->selection()) {
        if (dynamic_cast<const SymbolInstance *>(m_document->project().findEntity(id))) {
            editComponent(id, false);
            return;
        }
    }
    statusBar()->showMessage(tr("Sélectionner un appareil pour l'éditer"), 4000);
}

void MainWindow::newSymbol()
{
    SymbolEditor editor(&m_document->project().library, this);
    editor.newDefinition();
    if (editor.exec() == QDialog::Accepted) {
        m_palette->setLibrary(&m_document->project().library);
        statusBar()->showMessage(tr("Symbole enregistré dans votre bibliothèque : %1")
                                         .arg(editor.savedDefinitionId()),
                                 6000);
    }
}

void MainWindow::editCurrentSymbol(bool asCopy)
{
    const QString id = m_palette->currentDefinitionId();
    if (id.isEmpty()) {
        statusBar()->showMessage(tr("Choisissez d'abord un symbole dans la palette"), 4000);
        return;
    }
    SymbolEditor editor(&m_document->project().library, this);
    editor.editDefinition(id, asCopy);
    if (editor.exec() == QDialog::Accepted) {
        m_palette->setLibrary(&m_document->project().library);
        m_document->invalidateNetlist();
        m_document->project().resolveSymbolBounds();
        m_view->update();
        statusBar()->showMessage(tr("Symbole enregistré : %1").arg(editor.savedDefinitionId()),
                                 6000);
    }
}

void MainWindow::createDraftingToggles(QMenu *menu)
{
    using G = Icons::Glyph;
    SnapEngine &engine = m_view->snapEngine();

    // Une action, deux presentations : le menu Affichage et la barre d'etat.
    // Les touches de fonction reprennent celles d'AutoCAD, que tout
    // dessinateur venant de la connait par coeur.
    auto makeToggle = [&](G glyph, const QString &text, const QString &shortText,
                          const QKeySequence &key, bool checked, const QString &tip, auto slot) {
        auto *action = new QAction(Icons::icon(glyph), text, this);
        action->setCheckable(true);
        action->setChecked(checked);
        action->setShortcut(key);
        // Le raccourci doit agir meme quand le focus est dans un panneau :
        // on lache une touche de fonction sans regarder ou est le curseur.
        action->setShortcutContext(Qt::ApplicationShortcut);
        action->setToolTip(QStringLiteral("%1  ·  %2").arg(tip, key.toString()));
        action->setStatusTip(tip);
        action->setData(shortText);
        connect(action, &QAction::toggled, this, [this, slot](bool on) {
            if (m_syncingToggles)
                return;
            slot(on);
            m_view->snapSettingsTouched();
            statusBar()->showMessage(QString(), 0);
        });
        menu->addAction(action);
        m_actionGlyphs.insert(action, int(glyph));
        return action;
    };

    m_gridSnapAction = makeToggle(G::Snap, tr("Résolution — accrochage à la grille"),
                                  tr("RESOL"), QKeySequence(Qt::Key_F9),
                                  engine.gridSnapEnabled(),
                                  tr("Accrocher le curseur aux points de la grille"),
                                  [this](bool on) {
                                      m_view->snapEngine().setGridSnapEnabled(on);
                                      m_view->snapEngine().setMode(SnapMode::Grid, on);
                                  });
    m_gridAction = makeToggle(G::Grid, tr("Afficher la &grille"), tr("GRILLE"),
                              QKeySequence(Qt::Key_F7), true, tr("Afficher la grille"),
                              [this](bool on) { m_view->setGridVisible(on); });
    m_orthoAction = makeToggle(G::Wire, tr("Mode &ortho"), tr("ORTHO"),
                               QKeySequence(Qt::Key_F8), engine.orthoEnabled(),
                               tr("Contraindre le tracé à l'horizontale et à la verticale"),
                               [this](bool on) { m_view->snapEngine().setOrthoEnabled(on); });
    m_polarAction = makeToggle(G::Rotate, tr("Repérage &polaire"), tr("POLAIRE"),
                               QKeySequence(Qt::Key_F10), engine.polarEnabled(),
                               tr("Contraindre le tracé aux angles multiples de l'incrément"),
                               [this](bool on) { m_view->snapEngine().setPolarEnabled(on); });
    m_osnapAction = makeToggle(G::Junction, tr("Accrochage aux o&bjets"), tr("ACCROBJ"),
                               QKeySequence(Qt::Key_F3), engine.objectSnapEnabled(),
                               tr("Accrocher aux extrémités, milieux, centres, broches…"),
                               [this](bool on) { m_view->snapEngine().setObjectSnapEnabled(on); });
    m_trackingAction = makeToggle(G::Snap, tr("&Repérage d'accrochage"), tr("REPÉRAGE"),
                                  QKeySequence(Qt::Key_F11), engine.trackingEnabled(),
                                  tr("Retenir un point survolé et suivre son alignement"),
                                  [this](bool on) {
                                      m_view->snapEngine().setTrackingEnabled(on);
                                      m_view->update();
                                  });

    for (QAction *action : { m_gridSnapAction, m_gridAction, m_orthoAction, m_polarAction,
                             m_osnapAction, m_trackingAction })
        m_toolBar->addAction(action);

    auto *settings = new QAction(Icons::icon(G::Properties), tr("&Paramètres de dessin…"), this);
    settings->setShortcut(QKeySequence(Qt::Key_F12));
    settings->setStatusTip(tr("Modes d'accrochage, grille, repérage polaire"));
    connect(settings, &QAction::triggered, this, &MainWindow::editDraftingSettings);
    menu->addAction(settings);
    m_actionGlyphs.insert(settings, int(G::Properties));
}

void MainWindow::syncDraftingToggles()
{
    const SnapEngine &engine = m_view->snapEngine();
    m_syncingToggles = true;
    m_gridSnapAction->setChecked(engine.gridSnapEnabled());
    m_orthoAction->setChecked(engine.orthoEnabled());
    m_polarAction->setChecked(engine.polarEnabled());
    m_osnapAction->setChecked(engine.objectSnapEnabled());
    m_trackingAction->setChecked(engine.trackingEnabled());
    m_gridAction->setChecked(m_view->style().showGrid);
    m_syncingToggles = false;

    for (auto it = m_statusToggles.cbegin(); it != m_statusToggles.cend(); ++it)
        it.key()->setChecked(it.value()->isChecked());
}

void MainWindow::editDraftingSettings()
{
    DraftingSettingsDialog dialog(m_view->snapEngine(), m_view->gridStep(),
                                  m_view->style().showGrid, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_view->snapEngine() = dialog.engine();
    m_view->setGridStep(dialog.gridStep());
    m_view->setGridVisible(dialog.gridVisible());
    m_view->snapSettingsTouched();
    syncDraftingToggles();
    statusBar()->showMessage(tr("Paramètres de dessin appliqués"), 4000);
}

void MainWindow::editPageSetup()
{
    const Folio *current = m_document->currentFolio();
    if (!current)
        return;

    PageSetupDialog dialog(m_document->project(), *current, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const Folio configured = dialog.result();
    const bool all = dialog.applyToAllFolios();

    // La mise en page passe par une commande : c'est une modification du
    // document comme une autre, et elle doit s'annuler.
    m_document->pushMacro(all ? tr("Mise en page du projet") : tr("Mise en page du folio"), [&] {
        const auto folios = m_document->project().folios();
        for (Folio *folio : folios) {
            if (!all && folio->id() != configured.id())
                continue;
            m_document->push(std::make_unique<ChangeFolioLayoutCommand>(
                    m_document->project(), folio->id(), configured.sheet, configured.frame));
        }
    });

    m_view->zoomToFit();
    m_navigator->refresh();
    statusBar()->showMessage(all ? tr("Mise en page appliquée à %n folio(s)", "",
                                      m_document->folioCount())
                                 : tr("Mise en page du folio appliquée"),
                             5000);
}

void MainWindow::zoomCommand(const QStringList &arguments)
{
    const QString option = arguments.isEmpty() ? QStringLiteral("E") : arguments.first().toUpper();
    if (option.startsWith(QLatin1Char('E')) || option.startsWith(QLatin1Char('A'))) {
        m_view->zoomToFit();
        m_command->write(tr("   Zoom étendu."));
        return;
    }
    if (option.startsWith(QLatin1Char('R'))) {
        m_view->zoomActual();
        m_command->write(tr("   Zoom à l'échelle réelle."));
        return;
    }
    if (option.startsWith(QLatin1Char('F')) || option.startsWith(QLatin1Char('W'))) {
        m_view->beginZoomWindow();
        return;
    }
    if (option.startsWith(QLatin1Char('P'))) {
        m_view->zoomPrevious();
        m_command->write(tr("   Vue précédente."));
        return;
    }
    bool numeric = false;
    const double percent = option.toDouble(&numeric);
    if (numeric && percent > 0.0) {
        const double pixelsPerMm = std::max(1.0, double(logicalDpiX())) / kMmPerInch;
        m_view->setZoom(pixelsPerMm * percent / 100.0);
        m_command->write(tr("   Zoom %1 %.").arg(percent, 0, 'f', 0));
        return;
    }
    m_command->writeError(tr("   Option de zoom inconnue. Attendu : E (étendu), F (fenêtre), "
                             "P (précédent), R (réel) ou un pourcentage."));
}

void MainWindow::registerCommands()
{
    // Les alias reprennent ceux d'AutoCAD, en francais et en anglais : un
    // dessinateur les a dans les doigts, pas dans la tete.
    auto add = [this](const QString &name, const QStringList &aliases,
                      const QString &description, auto handler) {
        m_command->registerCommand({ name, aliases, description,
                                     [handler](const QStringList &args) { handler(args); } });
    };
    auto simple = [&](const QString &name, const QStringList &aliases,
                      const QString &description, auto action) {
        add(name, aliases, description, [action](const QStringList &) { action(); });
    };

    // ---- outils de trace ------------------------------------------------
    simple(QStringLiteral("LIGNE"), { QStringLiteral("L"), QStringLiteral("FIL") },
           tr("Tracer un fil"), [this] { m_view->setTool(FolioView::Tool::Wire); });
    simple(QStringLiteral("SELECTION"), { QStringLiteral("S") }, tr("Outil de sélection"),
           [this] { m_view->setTool(FolioView::Tool::Select); });
    simple(QStringLiteral("JONCTION"), { QStringLiteral("J") }, tr("Poser une jonction"),
           [this] { m_view->setTool(FolioView::Tool::Junction); });
    simple(QStringLiteral("ETIQUETTE"), { QStringLiteral("ET") }, tr("Nommer un potentiel"),
           [this] {
               m_view->setLabelScope(Label::Scope::Folio);
               m_view->setLabelRole(Label::Role::Plain);
               m_view->setTool(FolioView::Tool::Label);
           });
    simple(QStringLiteral("RENVOI"), { QStringLiteral("RV") },
           tr("Poser un renvoi de folio"), [this] {
               m_view->setLabelScope(Label::Scope::Project);
               m_view->setLabelRole(Label::Role::Plain);
               m_view->setTool(FolioView::Tool::Label);
           });
    simple(QStringLiteral("SOURCE"), { QStringLiteral("SO") },
           tr("Poser une flèche de signal source"), [this] {
               m_view->setLabelRole(Label::Role::Source);
               m_view->setTool(FolioView::Tool::Label);
           });
    simple(QStringLiteral("DESTINATION"), { QStringLiteral("DE") },
           tr("Poser une flèche de signal destination"), [this] {
               m_view->setLabelRole(Label::Role::Destination);
               m_view->setTool(FolioView::Tool::Label);
           });
    simple(QStringLiteral("TEXTE"), { QStringLiteral("T"), QStringLiteral("DT") },
           tr("Annoter le folio"), [this] { m_view->setTool(FolioView::Tool::Text); });
    simple(QStringLiteral("INSERER"), { QStringLiteral("I") },
           tr("Chercher un symbole dans la palette"), [this] {
               if (auto *dock = findChild<QDockWidget *>(QStringLiteral("dock.symbols")))
                   dock->show();
               m_palette->setFocus();
           });

    // ---- edition ---------------------------------------------------------
    simple(QStringLiteral("EFFACER"), { QStringLiteral("E"), QStringLiteral("SU") },
           tr("Supprimer la sélection"), [this] { m_view->deleteSelection(); });
    simple(QStringLiteral("COPIER"), { QStringLiteral("CP"), QStringLiteral("CO") },
           tr("Copier la sélection"), [this] { m_view->copySelection(); });
    simple(QStringLiteral("COLLER"), { QStringLiteral("CC") }, tr("Coller"),
           [this] { m_view->pasteClipboard(); });
    simple(QStringLiteral("PIVOTER"), { QStringLiteral("RO"), QStringLiteral("RT") },
           tr("Pivoter d'un quart de tour"), [this] { m_view->rotateSelection(true); });
    simple(QStringLiteral("MIROIR"), { QStringLiteral("MI") }, tr("Retourner la sélection"),
           [this] { m_view->mirrorSelection(); });
    simple(QStringLiteral("ANNULER"), { QStringLiteral("U"), QStringLiteral("AN") },
           tr("Annuler la dernière action"), [this] { m_document->undo(); });
    simple(QStringLiteral("RETABLIR"), { QStringLiteral("RT2") }, tr("Rétablir"),
           [this] { m_document->redo(); });
    simple(QStringLiteral("TOUTSELECT"), { QStringLiteral("SELTOUT") }, tr("Tout sélectionner"),
           [this] { m_view->selectAll(); });
    simple(QStringLiteral("AJUSTER"), { QStringLiteral("AJ"), QStringLiteral("TR") },
           tr("Couper un fil entre deux croisements"),
           [this] { m_view->setTool(FolioView::Tool::Trim); });
    simple(QStringLiteral("PROLONGER"), { QStringLiteral("PR"), QStringLiteral("ED") },
           tr("Allonger un fil jusqu'au premier obstacle"),
           [this] { m_view->setTool(FolioView::Tool::Extend); });
    simple(QStringLiteral("ZOOMFENETRE"), { QStringLiteral("ZF") }, tr("Zoom fenêtre"),
           [this] { m_view->beginZoomWindow(); });
    simple(QStringLiteral("ZOOMPRECEDENT"), { QStringLiteral("ZP") }, tr("Vue précédente"),
           [this] { m_view->zoomPrevious(); });
    simple(QStringLiteral("DEPLACER"), { QStringLiteral("DP"), QStringLiteral("M") },
           tr("Déplacer d'un point de base à un point d'arrivée"),
           [this] { m_view->beginMoveSelection(); });
    // DECALER accepte sa distance en argument, comme OFFSET : « DC 5 » ne
    // demande plus rien et attend seulement le cote.
    add(QStringLiteral("DECALER"), { QStringLiteral("DC"), QStringLiteral("O") },
        tr("Copier un fil parallèlement, à une distance donnée"),
        [this](const QStringList &args) {
            bool ok = false;
            const double distance = args.isEmpty()
                    ? 0.0
                    : args.first().toDouble(&ok);
            if (ok && distance > 0.0)
                m_view->beginOffset(distance);
            else
                offsetSelection();
        });
    simple(QStringLiteral("ETIRER"), { QStringLiteral("ETI") },
           tr("Étirer les sommets pris dans une fenêtre de capture"),
           [this] { m_view->beginStretch(); });
    simple(QStringLiteral("COMPOSANT"), { QStringLiteral("CO2"), QStringLiteral("EDC") },
           tr("Éditer l'appareil sélectionné"), [this] { editSelectedComponent(); });
    simple(QStringLiteral("POSERRAPPORT"), { QStringLiteral("PRA") },
           tr("Poser le rapport affiché dans le dessin"),
           [this] { placeCurrentReport(); });
    simple(QStringLiteral("TYPEFIL"), { QStringLiteral("TF") },
           tr("Gérer les types de fils"), [this] { editWireTypes(); });
    simple(QStringLiteral("POTENTIEL"), { QStringLiteral("PT") },
           tr("Mettre en évidence le potentiel de la sélection"),
           [this] { m_view->highlightNetOfSelection(); });

    // ---- vue et aides ----------------------------------------------------
    add(QStringLiteral("ZOOM"), { QStringLiteral("Z") },
        tr("Zoom : E étendu, R réel, ou un pourcentage"),
        [this](const QStringList &args) { zoomCommand(args); });
    simple(QStringLiteral("REGEN"), { QStringLiteral("RG") }, tr("Redessiner la vue"),
           [this] { m_view->update(); });
    simple(QStringLiteral("ORTHO"), { QStringLiteral("OR") }, tr("Basculer le mode ortho"),
           [this] { m_orthoAction->toggle(); });
    simple(QStringLiteral("POLAIRE"), { QStringLiteral("PO") }, tr("Basculer le repérage polaire"),
           [this] { m_polarAction->toggle(); });
    simple(QStringLiteral("ACCROBJ"), { QStringLiteral("OS") },
           tr("Basculer l'accrochage aux objets"), [this] { m_osnapAction->toggle(); });
    simple(QStringLiteral("GRILLE"), { QStringLiteral("GR") }, tr("Afficher ou masquer la grille"),
           [this] { m_gridAction->toggle(); });
    simple(QStringLiteral("RESOL"), {}, tr("Basculer l'accrochage à la grille"),
           [this] { m_gridSnapAction->toggle(); });
    simple(QStringLiteral("PARAMDESSIN"), { QStringLiteral("PD"), QStringLiteral("DSETTINGS") },
           tr("Paramètres de dessin"), [this] { editDraftingSettings(); });

    // ---- fichier et projet ----------------------------------------------
    simple(QStringLiteral("NOUVEAU"), { QStringLiteral("NEW") }, tr("Nouveau projet"),
           [this] { newProject(); });
    simple(QStringLiteral("OUVRIR"), { QStringLiteral("OPEN") }, tr("Ouvrir un projet"),
           [this] { openProject(); });
    simple(QStringLiteral("ENREGISTRER"), { QStringLiteral("SAVE"), QStringLiteral("QSAVE") },
           tr("Enregistrer le projet"), [this] { saveProject(); });
    simple(QStringLiteral("ENREGISTRERSOUS"), { QStringLiteral("SAVEAS") },
           tr("Enregistrer sous un autre nom"), [this] { saveProjectAs(); });
    simple(QStringLiteral("IMPRIMER"), { QStringLiteral("PLOT"), QStringLiteral("IMP") },
           tr("Imprimer le dossier"), [this] { printProject(); });
    simple(QStringLiteral("EXPORTPDF"), { QStringLiteral("PDF") }, tr("Exporter en PDF"),
           [this] { exportPdf(); });
    simple(QStringLiteral("EXPORTDXF"), { QStringLiteral("DXF") }, tr("Exporter en DXF"),
           [this] { exportDxf(); });
    simple(QStringLiteral("MISENPAGE"), { QStringLiteral("MP"), QStringLiteral("PAGESETUP") },
           tr("Format de feuille et cadre"), [this] { editPageSetup(); });
    simple(QStringLiteral("INFOPROJET"), { QStringLiteral("IP") },
           tr("Informations du projet"), [this] { editProjectInfo(); });

    // ---- metier -----------------------------------------------------------
    simple(QStringLiteral("REPERAGE"), { QStringLiteral("RN"), QStringLiteral("RENUM") },
           tr("Repérage automatique des fils et des appareils"), [this] { renumberAll(); });
    simple(QStringLiteral("CONTROLE"), { QStringLiteral("VERIF"), QStringLiteral("AUDIT") },
           tr("Contrôler le schéma"), [this] { checkSchematic(); });
    simple(QStringLiteral("RAPPORTS"), { QStringLiteral("NOMENCLATURE"), QStringLiteral("BOM") },
           tr("Afficher les rapports"), [this] {
               if (auto *dock = findChild<QDockWidget *>(QStringLiteral("dock.reports"))) {
                   dock->show();
                   dock->raise();
               }
               m_reports->refresh();
           });
    simple(QStringLiteral("ECHELLE"), { QStringLiteral("EC"), QStringLiteral("LADDER") },
           tr("Insérer une échelle de commande"), [this] { insertLadder(); });
    simple(QStringLiteral("NOUVSYMBOLE"), { QStringLiteral("NS") },
           tr("Créer un symbole"), [this] { newSymbol(); });

    // ---- folios -----------------------------------------------------------
    simple(QStringLiteral("NOUVFOLIO"), { QStringLiteral("NF") }, tr("Ajouter un folio"),
           [this] { m_navigator->addFolioFromCommand(); });
    simple(QStringLiteral("FOLIOSUIVANT"), { QStringLiteral("FS") }, tr("Folio suivant"),
           [this] {
               m_document->setCurrentFolioIndex(m_document->currentFolioIndex() + 1);
           });
    simple(QStringLiteral("FOLIOPRECEDENT"), { QStringLiteral("FP") }, tr("Folio précédent"),
           [this] {
               m_document->setCurrentFolioIndex(m_document->currentFolioIndex() - 1);
           });

    connect(m_command, &CommandLine::commandExecuted, this, [this](const QString &name) {
        statusBar()->showMessage(name, 2500);
    });
}

void MainWindow::insertLadder()
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;

    LadderDialog dialog(*folio, m_document->project().wireTypes, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto entities = LadderBuilder::build(dialog.spec());
    if (entities.empty())
        return;

    const int count = int(entities.size());
    // Une echelle pose des dizaines d'entites : elle doit se defaire d'une
    // seule annulation, sinon la corriger devient un supplice.
    m_document->pushMacro(tr("Insérer une échelle"), [&] {
        for (auto &entity : entities) {
            m_document->push(std::make_unique<AddEntityCommand>(m_document->project(),
                                                                folio->id(), std::move(entity),
                                                                tr("Insérer une échelle")));
        }
    });

    m_view->update();
    statusBar()->showMessage(tr("Échelle insérée : %n élément(s).", "", count), 5000);
}

void MainWindow::editWireTypes()
{
    WireTypeDialog dialog(m_document->project().wireTypes, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_document->push(std::make_unique<ChangeWireTypesCommand>(m_document->project(),
                                                              dialog.result()));
    rebuildWireTypeSelector();
    m_view->update();
    statusBar()->showMessage(tr("Types de fils mis à jour."), 5000);
}

void MainWindow::rebuildWireTypeSelector()
{
    if (!m_wireTypeSelector)
        return;

    // Le combo se reconstruit a chaque annulation ; la signature evite de le
    // refaire quand rien n'a bouge, ce qui ferait clignoter la barre d'outils.
    QString signature;
    for (const WireType &type : m_document->project().wireTypes.all())
        signature += type.id + QLatin1Char('\x1f') + type.name + type.colorName();
    if (signature == m_wireTypeSignature && m_wireTypeSelector->count() > 0)
        return;
    m_wireTypeSignature = signature;

    const QString wanted = m_view->currentWireType();
    QSignalBlocker blocker(m_wireTypeSelector);
    m_wireTypeSelector->clear();
    for (const WireType &type : m_document->project().wireTypes.all()) {
        QPixmap swatch(14, 14);
        swatch.fill(FolioPainter::wireTypeColor(type));
        m_wireTypeSelector->addItem(QIcon(swatch), type.name.isEmpty() ? type.id : type.name,
                                    type.id);
    }
    const int index = m_wireTypeSelector->findData(wanted);
    m_wireTypeSelector->setCurrentIndex(index >= 0 ? index : 0);
    // Le type arme a pu disparaitre du jeu : la vue doit suivre le combo,
    // sinon les fils suivants pointeraient vers un type qui n'existe plus.
    m_view->setCurrentWireType(m_wireTypeSelector->currentData().toString());
}

void MainWindow::offsetSelection()
{
    // La distance par defaut est le pas de la grille : c'est l'ecart auquel
    // on double un fil neuf fois sur dix.
    bool ok = false;
    const double distance = QInputDialog::getDouble(
            this, tr("Décaler"), tr("Distance de décalage (mm) :"),
            m_view->gridStep(), 0.01, 1000.0, 2, &ok);
    if (!ok)
        return;
    m_view->beginOffset(distance);
}

void MainWindow::showCanvasContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    const bool hasSelection = m_view->hasSelection();

    // Le menu contextuel d'AutoCAD commence par « Répéter » : la derniere
    // commande est ce qu'on veut relancer le plus souvent.
    if (m_document->commands().canUndo())
        menu.addAction(m_undoAction);
    if (m_document->commands().canRedo())
        menu.addAction(m_redoAction);
    menu.addSeparator();

    if (hasSelection) {
        menu.addAction(m_copyAction);
        menu.addAction(m_deleteAction);
        menu.addSeparator();
        menu.addAction(m_editComponentAction);
        menu.addAction(m_moveAction);
        menu.addAction(m_rotateAction);
        menu.addAction(m_mirrorAction);
        menu.addAction(m_offsetAction);
        menu.addAction(m_stretchAction);
        menu.addSeparator();
        menu.addAction(m_highlightAction);
    } else {
        menu.addAction(m_pasteAction);
        menu.addAction(m_selectAllAction);
    }

    menu.addSeparator();
    menu.addAction(m_zoomFitAction);
    m_zoomPreviousAction->setEnabled(m_view->canZoomPrevious());
    menu.addAction(m_zoomPreviousAction);
    menu.addSeparator();
    menu.addAction(m_pageSetupAction);

    menu.exec(globalPos);
}

void MainWindow::createStatusBar()
{
    m_cursorLabel = new QLabel(this);
    m_zoneLabel = new QLabel(this);
    m_zoomLabel = new QLabel(this);
    m_selectionLabel = new QLabel(this);
    for (QLabel *label : { m_cursorLabel, m_zoneLabel, m_zoomLabel, m_selectionLabel }) {
        label->setMinimumWidth(110);
        statusBar()->addPermanentWidget(label);
    }
    m_cursorLabel->setText(QStringLiteral("X 0,0   Y 0,0 mm"));

    // Les bascules d'aide au dessin, sous forme d'etiquettes courtes comme
    // dans la barre d'etat d'AutoCAD : elles s'allument, se cliquent, et
    // rappellent leur touche de fonction en info-bulle.
    for (QAction *action : { m_gridSnapAction, m_gridAction, m_orthoAction, m_polarAction,
                             m_osnapAction }) {
        if (!action)
            continue;
        auto *button = new QToolButton(this);
        button->setText(action->data().toString());
        button->setToolTip(action->toolTip());
        button->setCheckable(true);
        button->setChecked(action->isChecked());
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setProperty("statusToggle", true);
        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::toggled, button, &QToolButton::setChecked);
        statusBar()->addPermanentWidget(button);
        m_statusToggles.insert(button, action);
    }
}

// --------------------------------------------------------------------------

void MainWindow::updateTitle()
{
    const QString name = m_document->displayName();
    const QString folio = m_document->currentFolio()
            ? tr(" — folio %1").arg(m_document->currentFolio()->number)
            : QString();
    setWindowTitle(QStringLiteral("%1%2%3 — Dessins")
                           .arg(name, folio,
                                m_document->isModified() ? QStringLiteral(" *") : QString()));
}

void MainWindow::updateActions()
{
    m_undoAction->setEnabled(m_document->commands().canUndo());
    m_redoAction->setEnabled(m_document->commands().canRedo());
    m_undoAction->setText(m_document->commands().canUndo()
                                  ? tr("&Annuler : %1").arg(m_document->commands().undoText())
                                  : tr("&Annuler"));
    m_redoAction->setText(m_document->commands().canRedo()
                                  ? tr("&Rétablir : %1").arg(m_document->commands().redoText())
                                  : tr("&Rétablir"));

    const int count = m_view->selection().size();
    m_selectionLabel->setText(count == 0 ? tr("aucune sélection")
                                         : tr("%n élément(s)", "", count));
    // Cent pour cent, c'est un millimetre du dessin pour un millimetre a
    // l'ecran : le pourcentage se calcule donc contre la resolution reelle.
    const double pixelsPerMm = std::max(1.0, double(logicalDpiX())) / kMmPerInch;
    m_zoomLabel->setText(tr("zoom %1 %").arg(int(std::lround(m_view->zoom() / pixelsPerMm * 100))));
    updateTitle();
}

void MainWindow::applyTheme(bool dark)
{
    m_dark = dark;
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
        Theme::apply(*app, dark);
    refreshIcons();

    // La feuille reste blanche en theme clair et gris tres sombre en theme
    // sombre : le dessin doit rester lisible, mais un rectangle blanc eclatant
    // au milieu d'une interface sombre fatigue en fin de journee.
    RenderStyle style = dark ? RenderStyle::screenDark() : RenderStyle::screen();
    style.gridStep = m_view->style().gridStep;
    style.showGrid = m_view->style().showGrid;
    style.showPinNumbers = m_view->style().showPinNumbers;
    style.showUnconnectedPins = m_view->style().showUnconnectedPins;
    // Le pourtour du canevas prend la couleur du chrome : la feuille flotte
    // alors dans la fenetre au lieu d'etre posee sur un gris etranger.
    style.pageBackground = Theme::colors().window;
    m_view->setStyle(style);

    // Les apercus de la palette sont redessines dans les couleurs du theme.
    m_palette->setLibrary(&m_document->project().library);

    QSettings settings;
    settings.setValue(QStringLiteral("ui/darkTheme"), dark);
}

// --------------------------------------------------------------------------
// Fichier

namespace {
constexpr auto kRecentKey = "files/recent";
constexpr int kRecentMax = 10;
}

void MainWindow::addRecentFile(const QString &path)
{
    if (path.isEmpty())
        return;
    QSettings settings;
    QStringList recent = settings.value(QLatin1String(kRecentKey)).toStringList();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    recent.removeAll(absolute);
    recent.prepend(absolute);
    while (recent.size() > kRecentMax)
        recent.removeLast();
    settings.setValue(QLatin1String(kRecentKey), recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();

    const QStringList recent = QSettings().value(QLatin1String(kRecentKey)).toStringList();
    int shown = 0;
    for (const QString &path : recent) {
        // Un fichier disparu n'encombre pas le menu, mais il n'est pas retire
        // de la liste : une cle USB debranchee peut revenir.
        if (!QFileInfo::exists(path))
            continue;
        ++shown;
        auto *action = m_recentMenu->addAction(
                QStringLiteral("&%1  %2").arg(shown).arg(QFileInfo(path).completeBaseName()));
        action->setStatusTip(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] {
            if (maybeSave())
                openFile(path);
        });
    }

    m_recentMenu->setEnabled(shown > 0);
    if (shown > 0) {
        m_recentMenu->addSeparator();
        connect(m_recentMenu->addAction(tr("&Vider la liste")), &QAction::triggered, this,
                [this] {
                    QSettings().remove(QLatin1String(kRecentKey));
                    rebuildRecentMenu();
                });
    }
}

bool MainWindow::maybeSave()
{
    if (!m_document->isModified())
        return true;
    const auto answer = QMessageBox::warning(
            this, tr("Dessins"),
            tr("Le projet « %1 » comporte des modifications non enregistrées.")
                    .arg(m_document->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveProject();
    return answer == QMessageBox::Discard;
}

QString MainWindow::suggestedFileName(const QString &extension) const
{
    QString base = m_document->filePath();
    if (base.isEmpty()) {
        base = m_document->project().info.title;
        if (base.isEmpty())
            base = tr("projet");
        base.replace(QLatin1Char('/'), QLatin1Char('-'));
        return base + QLatin1Char('.') + extension;
    }
    const QFileInfo info(base);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QLatin1Char('.')
            + extension;
}

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    m_document->newProject(m_document->project().library);
    m_navigator->refresh();
    rebuildWireTypeSelector();
    m_view->zoomToFit();
    updateTitle();
}

void MainWindow::openProject()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"), QString(),
                                                      DsnFile::fileFilter());
    if (path.isEmpty())
        return;
    openFile(path);
}

bool MainWindow::openFile(const QString &path)
{
    QString error;
    QStringList warnings;
    if (!m_document->load(path, &error, &warnings)) {
        QMessageBox::critical(this, tr("Ouverture impossible"),
                              tr("%1\n\n%2").arg(QFileInfo(path).fileName(), error));
        return false;
    }
    m_navigator->refresh();
    m_palette->setNorm(m_document->profile().norm);
    m_view->setGridStep(m_document->profile().gridStep);
    rebuildWireTypeSelector();
    m_view->zoomToFit();
    updateTitle();

    if (!warnings.isEmpty()) {
        // Un avertissement de chargement doit se voir : un symbole manquant
        // change le dessin sans rien casser d'apparent.
        QMessageBox::warning(this, tr("Projet ouvert avec des réserves"),
                             warnings.join(QStringLiteral("\n")));
    }
    addRecentFile(path);
    statusBar()->showMessage(tr("Projet ouvert : %1").arg(QFileInfo(path).fileName()), 5000);
    return true;
}

bool MainWindow::saveProject()
{
    if (m_document->filePath().isEmpty())
        return saveProjectAs();
    QString error;
    if (!m_document->save(m_document->filePath(), &error)) {
        QMessageBox::critical(this, tr("Enregistrement impossible"), error);
        return false;
    }
    addRecentFile(m_document->filePath());
    statusBar()->showMessage(tr("Projet enregistré"), 4000);
    return true;
}

bool MainWindow::saveProjectAs()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"),
                                                      suggestedFileName(DsnFile::fileExtension()),
                                                      DsnFile::fileFilter());
    if (path.isEmpty())
        return false;
    QString error;
    if (!m_document->save(path, &error)) {
        QMessageBox::critical(this, tr("Enregistrement impossible"), error);
        return false;
    }
    updateTitle();
    addRecentFile(path);
    statusBar()->showMessage(tr("Projet enregistré : %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

void MainWindow::exportPdf()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Exporter en PDF"),
                                                      suggestedFileName(QStringLiteral("pdf")),
                                                      PdfExport::fileFilter());
    if (path.isEmpty())
        return;

    PdfExportOptions options;
    options.title = m_document->project().info.title;
    QString error;
    if (!PdfExport::write(path, m_document->project(), options, &error)) {
        QMessageBox::critical(this, tr("Export PDF impossible"), error);
        return;
    }
    statusBar()->showMessage(tr("PDF écrit : %1 (%n folio(s))", "", m_document->folioCount())
                                     .arg(QFileInfo(path).fileName()),
                             6000);
}

void MainWindow::exportDxf()
{
    const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Dossier de destination des fichiers DXF"));
    if (directory.isEmpty())
        return;

    QString base = m_document->project().info.title;
    if (base.isEmpty())
        base = tr("projet");
    base.replace(QLatin1Char(' '), QLatin1Char('-'));

    QStringList errors;
    const int written = DxfExport::writeProject(directory, base, m_document->project(), {},
                                                &errors);
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Export DXF"), errors.join(QStringLiteral("\n")));

    // Le DXF est un echange graphique : le dire ici evite une mauvaise
    // surprise a la reouverture ailleurs.
    QMessageBox::information(
            this, tr("Export DXF terminé"),
            tr("%n fichier(s) écrit(s), un par folio.\n\n"
               "Le DXF transporte la géométrie, les calques et les textes, mais pas la "
               "connectivité : les potentiels, les broches et les repères automatiques ne "
               "survivent pas au format.",
               "", written));
}

void MainWindow::exportCurrentReport()
{
    const ReportTable table = m_reports->currentTable();
    if (table.headers.isEmpty()) {
        statusBar()->showMessage(tr("Aucun rapport à exporter"), 4000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
            this, tr("Exporter le rapport"),
            suggestedFileName(QStringLiteral("csv")),
            tr("Tableur (*.csv)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!CsvExport::write(path, table, {}, &error)) {
        QMessageBox::critical(this, tr("Export impossible"), error);
        return;
    }
    statusBar()->showMessage(tr("Rapport exporté : %1").arg(QFileInfo(path).fileName()), 5000);
}

void MainWindow::placeCurrentReport()
{
    const ReportTable table = m_reports->currentTable();
    Folio *folio = m_document->currentFolio();
    if (!folio || table.rows.isEmpty()) {
        statusBar()->showMessage(tr("Aucun rapport à poser"), 4000);
        return;
    }

    // Reglages de la table, comme le « Table Generation Setup » d'AutoCAD.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Poser le rapport dans le dessin"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    auto *x = new QDoubleSpinBox(&dialog);
    auto *y = new QDoubleSpinBox(&dialog);
    for (QDoubleSpinBox *box : { x, y }) {
        box->setRange(0.0, 2000.0);
        box->setDecimals(1);
        box->setSuffix(tr(" mm"));
    }
    const QRectF frame = folio->frameRect();
    x->setValue(frame.left() + 5.0);
    y->setValue(frame.top() + 5.0);
    form->addRow(tr("Coin supérieur gauche — X"), x);
    form->addRow(tr("Coin supérieur gauche — Y"), y);

    auto *height = new QDoubleSpinBox(&dialog);
    height->setRange(1.0, 10.0);
    height->setDecimals(1);
    height->setValue(2.0);
    height->setSuffix(tr(" mm"));
    form->addRow(tr("Hauteur de texte"), height);

    auto *rows = new QSpinBox(&dialog);
    rows->setRange(0, 500);
    rows->setValue(0);
    rows->setSpecialValueText(tr("tout d'un bloc"));
    rows->setToolTip(tr("Découpe le rapport en sections posées côte à côte, "
                        "comme le fait AutoCAD quand il ne tient pas en hauteur."));
    form->addRow(tr("Lignes par section"), rows);

    auto *summary = new QLabel(&dialog);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    // Largeurs mesurees avec la police reelle du rendu. Le module des
    // rapports ne peut qu'estimer la largeur d'un texte ; ici on la connait,
    // et une colonne trop etroite rendrait la table illisible une fois posee.
    QFont font;
    font.setFamily(m_view->style().fontFamily);
    auto spec = [&] {
        ReportTableSpec s;
        s.origin = QPointF(x->value(), y->value());
        s.textHeight = height->value();
        s.rowHeight = height->value() * 2.5;
        s.rowsPerSection = rows->value();

        QVector<double> widths;
        for (int c = 0; c < table.headers.size(); ++c) {
            double w = FolioPainter::textWidthMm(font, table.headers.at(c), s.textHeight);
            for (const QStringList &row : table.rows) {
                if (c < row.size())
                    w = std::max(w, FolioPainter::textWidthMm(font, row.at(c), s.textHeight));
            }
            widths.append(w + s.padding * 2.0);
        }
        s.explicitWidths = widths;
        return s;
    };
    auto refresh = [&] {
        const QRectF box = ReportPlacer::bounds(table, spec());
        const bool fits = folio->frameRect().contains(box);
        summary->setText(fits
                ? tr("%n ligne(s) — table de %1 × %2 mm.", "", table.rowCount())
                          .arg(box.width(), 0, 'f', 0)
                          .arg(box.height(), 0, 'f', 0)
                : tr("%n ligne(s) — table de %1 × %2 mm : elle déborde du cadre. "
                     "Réduire la hauteur de texte ou découper en sections.", "",
                     table.rowCount())
                          .arg(box.width(), 0, 'f', 0)
                          .arg(box.height(), 0, 'f', 0));
    };
    connect(x, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    connect(y, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    connect(height, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    connect(rows, &QSpinBox::valueChanged, &dialog, refresh);
    refresh();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Poser"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    auto entities = ReportPlacer::build(table, spec());
    if (entities.empty())
        return;
    const int count = int(entities.size());

    // Une table pose des centaines d'entites : elle doit se defaire d'une
    // seule annulation, sinon la retirer devient impraticable.
    m_document->pushMacro(tr("Poser un rapport"), [&] {
        for (auto &entity : entities) {
            m_document->push(std::make_unique<AddEntityCommand>(m_document->project(),
                                                                folio->id(), std::move(entity),
                                                                tr("Poser un rapport")));
        }
    });
    m_view->update();
    statusBar()->showMessage(tr("Rapport posé : %1 (%n élément(s)).", "", count)
                                     .arg(table.title),
                             6000);
}

void MainWindow::printProject()
{
    QPrinter printer(QPrinter::HighResolution);
    const Folio *folio = m_document->currentFolio();
    if (folio) {
        printer.setPageSize(QPageSize(QSizeF(folio->sheet.width, folio->sheet.height),
                                      QPageSize::Millimeter));
        printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    }

    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Imprimer le dossier"));
    if (dialog.exec() != QDialog::Accepted)
        return;

    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::critical(this, tr("Impression impossible"),
                              tr("L'imprimante n'a pas pu être ouverte."));
        return;
    }

    // Meme peintre que l'ecran : ce qui est imprime est ce qui a ete dessine.
    const double scale = printer.resolution() / kMmPerInch;
    FolioPainter folioPainter(m_document->project(), RenderStyle::print());
    for (int i = 0; i < m_document->folioCount(); ++i) {
        if (i > 0)
            printer.newPage();
        painter.save();
        painter.scale(scale, scale);
        folioPainter.paint(painter, *m_document->project().folioAt(i));
        painter.restore();
    }
    painter.end();
    statusBar()->showMessage(tr("Dossier envoyé à l'impression"), 5000);
}

// --------------------------------------------------------------------------
// Projet

void MainWindow::editProjectInfo()
{
    ProjectInfoDialog dialog(m_document->project().info, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_document->push(std::make_unique<ChangeProjectInfoCommand>(m_document->project(),
                                                                dialog.info()));
    updateTitle();
}

void MainWindow::editTagFormat()
{
    // Le « Component TAG Format » d'AutoCAD, range dans les proprietes du
    // dessin : le format de repere est une convention de bureau d'etudes,
    // pas une norme, et il doit se regler sans toucher au profil metier.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Format des repères d'appareil"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    const Profile profile = m_document->profile();

    auto *mode = new QComboBox(&dialog);
    mode->addItem(tr("Séquentiel — un compteur par famille (K1, K2…)"),
                  DesignationRule::modeTag(DesignationRule::Mode::Sequential));
    mode->addItem(tr("Par référence de ligne — l'endroit fait le repère (104K)"),
                  DesignationRule::modeTag(DesignationRule::Mode::LineReference));
    mode->setCurrentIndex(std::max(0, mode->findData(
            DesignationRule::modeTag(profile.designation.mode))));
    form->addRow(tr("Mode"), mode);

    auto *format = new QLineEdit(m_document->project().designationFormat, &dialog);
    format->setPlaceholderText(tr("vide = le format du mode"));
    form->addRow(tr("Format"), format);

    auto *help = new QLabel(tr("<b>%F</b> famille · <b>%N</b> numéro · <b>%S</b> folio · "
                               "<b>%X</b> référence de ligne · <b>%I</b> installation · "
                               "<b>%L</b> emplacement · <b>%%</b> un pour cent"),
                            &dialog);
    help->setWordWrap(true);
    form->addRow(QString(), help);

    auto *preview = new QLabel(&dialog);
    preview->setWordWrap(true);
    form->addRow(tr("Aperçu"), preview);

    auto refresh = [&] {
        DesignationRule rule = profile.designation;
        rule.mode = DesignationRule::modeFromTag(mode->currentData().toString());
        rule.tagFormat = format->text();
        // Un exemple concret vaut mieux qu'une explication : on montre le
        // repere que donnerait un contacteur en colonne 4 du folio 1.
        DesignationContext context;
        context.family = QStringLiteral("K");
        context.number = 2;
        context.sheet = QStringLiteral("1");
        context.lineReference = QStringLiteral("104");
        context.installation = QStringLiteral("A1");
        context.location = QStringLiteral("ARM");
        preview->setText(tr("Un contacteur en colonne 4 du folio 1 : <b>%1</b>")
                                 .arg(rule.format(context)));
    };
    connect(mode, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(format, &QLineEdit::textChanged, &dialog, refresh);
    refresh();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    m_document->project().designationMode = mode->currentData().toString();
    m_document->project().designationFormat = format->text().trimmed();
    m_document->invalidateNetlist();
    statusBar()->showMessage(tr("Format de repère modifié. Relancer le repérage "
                                "automatique pour l'appliquer."), 8000);
}

void MainWindow::renumberAll()
{
    const Profile profile = m_document->profile();
    // Le reperage modifie beaucoup d'entites d'un coup : il doit se defaire
    // d'une seule annulation.
    Project before = m_document->project();
    const NumberingResult result = Numbering::renumberAll(m_document->project(), profile);
    m_document->invalidateNetlist();

    statusBar()->showMessage(
            tr("Repérage : %1 appareils désignés, %2 potentiels, %3 fils repérés, "
               "%4 saisie(s) manuelle(s) préservée(s)")
                    .arg(result.devicesDesignated)
                    .arg(result.netsNumbered)
                    .arg(result.wiresNumbered)
                    .arg(result.keptManual),
            10000);
    m_document->commands().resetClean();
    m_reports->refresh();
    m_view->update();
    m_properties->setSelection(m_view->selection());
}

void MainWindow::setProfile(const QString &profileId)
{
    m_document->setProfileId(profileId);
    const Profile profile = m_document->profile();
    m_palette->setNorm(profile.norm);
    m_view->setGridStep(profile.gridStep);
    statusBar()->showMessage(tr("Profil métier : %1 — grille %2 mm, symboles %3")
                                     .arg(profile.name)
                                     .arg(profile.gridStep)
                                     .arg(profile.norm),
                             6000);
}

void MainWindow::checkSchematic()
{
    const Netlist &netlist = m_document->netlist();
    m_reports->refresh();

    const auto &diagnostics = netlist.diagnostics();
    if (diagnostics.isEmpty()) {
        QMessageBox::information(this, tr("Contrôle du schéma"),
                                 tr("Aucune anomalie détectée sur %n potentiel(s).", "",
                                    netlist.netCount()));
        return;
    }

    QStringList lines;
    for (const auto &d : diagnostics) {
        const Folio *folio = m_document->project().folio(d.folioId);
        lines.append(QStringLiteral("• %1%2")
                             .arg(d.message,
                                  folio ? tr(" (folio %1)").arg(folio->number) : QString()));
    }
    QMessageBox::warning(this, tr("Contrôle du schéma"),
                         tr("%n anomalie(s) :\n\n", "", int(diagnostics.size()))
                                 + lines.mid(0, 20).join(QStringLiteral("\n")));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

} // namespace dsn
