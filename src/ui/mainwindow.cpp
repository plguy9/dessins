#include "mainwindow.h"

#include "folionavigator.h"
#include "io/csvexport.h"
#include "io/dsnfile.h"
#include "io/dxfexport.h"
#include "commandline.h"
#include "draftingsettingsdialog.h"
#include "pagesetupdialog.h"
#include "propertiespanel.h"
#include "render/foliopainter.h"
#include "render/pdfexport.h"
#include "reportpanel.h"
#include "rules/numbering.h"
#include "symboleditor.h"
#include "symbolpalette.h"
#include "theme.h"
#include "symbols/librarystore.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSettings>
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
    connect(m_document, &Document::undoStateChanged, this, &MainWindow::updateActions);
    connect(m_document, &Document::currentFolioChanged, this, &MainWindow::updateTitle);

    // Le theme est applique en dernier : il redessine les icones et
    // reharmonise le fond du canevas avec le chrome de la fenetre.
    QSettings settings;
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
    make(editMenu, false, G::Copy, tr("&Copier"), QKeySequence::Copy, tr("Copier la sélection"),
         [this] { m_view->copySelection(); });
    make(editMenu, false, G::Paste, tr("C&oller"), QKeySequence::Paste,
         tr("Coller sous le curseur"), [this] { m_view->pasteClipboard(); });
    make(editMenu, false, G::Delete, tr("&Supprimer"), QKeySequence::Delete,
         tr("Supprimer la sélection"), [this] { m_view->deleteSelection(); });
    make(editMenu, false, G::Select, tr("&Tout sélectionner"), QKeySequence::SelectAll,
         QString(), [this] { m_view->selectAll(); });
    editMenu->addSeparator();
    make(editMenu, true, G::Rotate, tr("&Pivoter"), QKeySequence(Qt::Key_R),
         tr("Pivoter d'un quart de tour"), [this] { m_view->rotateSelection(true); });
    make(editMenu, true, G::Mirror, tr("Re&tourner"), QKeySequence(Qt::Key_M),
         tr("Retourner selon l'axe vertical"), [this] { m_view->mirrorSelection(); });
    make(editMenu, true, G::Highlight, tr("Mettre le potentiel en évidence"),
         QKeySequence(Qt::CTRL | Qt::Key_H),
         tr("Suivre un potentiel à travers le folio"),
         [this] { m_view->highlightNetOfSelection(); });

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
    auto *crossReference = new QAction(tr("Étiquette = renvoi de folio"), this);
    crossReference->setCheckable(true);
    crossReference->setStatusTip(
            tr("Un renvoi relie le même potentiel à travers tout le dossier"));
    toolMenu->addAction(crossReference);
    connect(crossReference, &QAction::toggled, this, [this](bool on) {
        m_view->setLabelScope(on ? Label::Scope::Project : Label::Scope::Folio);
    });

    connect(m_view, &FolioView::toolChanged, this, [this](FolioView::Tool tool) {
        for (QAction *action : std::as_const(m_toolActions)) {
            if (action->data().toInt() == int(tool))
                action->setChecked(true);
        }
    });

    m_toolBar->addSeparator();

    // ---- Affichage -----------------------------------------------------
    QMenu *viewMenu = menuBar()->addMenu(tr("&Affichage"));
    make(viewMenu, true, G::ZoomIn, tr("Zoom &avant"), QKeySequence::ZoomIn, tr("Zoom avant"),
         [this] { m_view->zoomIn(); });
    make(viewMenu, true, G::ZoomOut, tr("Zoom a&rrière"), QKeySequence::ZoomOut,
         tr("Zoom arrière"), [this] { m_view->zoomOut(); });
    make(viewMenu, true, G::ZoomFit, tr("&Ajuster au folio"), QKeySequence(Qt::CTRL | Qt::Key_0),
         tr("Voir le folio entier"), [this] { m_view->zoomToFit(); });
    make(viewMenu, false, G::Text, tr("Ligne de &commande"),
         QKeySequence(Qt::CTRL | Qt::Key_9), tr("Placer le curseur dans la ligne de commande"),
         [this] {
             m_commandDock->show();
             m_commandDock->raise();
             m_command->focusInput();
         });
    make(viewMenu, false, G::ZoomFit, tr("&Taille réelle"), QKeySequence(Qt::CTRL | Qt::Key_1),
         tr("Un millimètre du dessin pour un millimètre à l'écran"),
         [this] { m_view->zoomActual(); });
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
    make(projectMenu, true, G::Folios, tr("&Mise en page…"), QKeySequence(Qt::CTRL | Qt::Key_P),
         tr("Format de feuille, cadre, zones de repérage, cartouche"),
         &MainWindow::editPageSetup);
    make(projectMenu, false, G::Info, tr("&Informations du projet…"), QKeySequence(),
         tr("Titre, client, référence — ce que porte le cartouche"),
         &MainWindow::editProjectInfo);
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

    for (QAction *action : { m_gridSnapAction, m_gridAction, m_orthoAction, m_polarAction,
                             m_osnapAction })
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
    bool numeric = false;
    const double percent = option.toDouble(&numeric);
    if (numeric && percent > 0.0) {
        const double pixelsPerMm = std::max(1.0, double(logicalDpiX())) / kMmPerInch;
        m_view->setZoom(pixelsPerMm * percent / 100.0);
        m_command->write(tr("   Zoom %1 %.").arg(percent, 0, 'f', 0));
        return;
    }
    m_command->writeError(tr("   Option de zoom inconnue. Attendu : E (étendu), "
                             "R (réel) ou un pourcentage."));
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
           [this] { m_view->setTool(FolioView::Tool::Label); });
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
