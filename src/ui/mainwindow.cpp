#include "mainwindow.h"

#include "folionavigator.h"
#include "io/csvexport.h"
#include "io/dsnfile.h"
#include "io/dxfexport.h"
#include "propertiespanel.h"
#include "render/foliopainter.h"
#include "render/pdfexport.h"
#include "reportpanel.h"
#include "rules/numbering.h"
#include "symbolpalette.h"
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
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>

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

    resize(1500, 950);
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
    // ---- Fichier -------------------------------------------------------
    QMenu *fileMenu = menuBar()->addMenu(tr("&Fichier"));
    auto *fileBar = addToolBar(tr("Fichier"));
    fileBar->setObjectName(QStringLiteral("toolbar.file"));

    auto addAction = [&](QMenu *menu, QToolBar *bar, const QString &text,
                         const QKeySequence &shortcut, auto slot) {
        auto *action = new QAction(text, this);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, slot);
        if (menu)
            menu->addAction(action);
        if (bar)
            bar->addAction(action);
        return action;
    };

    addAction(fileMenu, fileBar, tr("&Nouveau"), QKeySequence::New, &MainWindow::newProject);
    addAction(fileMenu, fileBar, tr("&Ouvrir…"), QKeySequence::Open, &MainWindow::openProject);
    addAction(fileMenu, fileBar, tr("&Enregistrer"), QKeySequence::Save,
              [this] { saveProject(); });
    addAction(fileMenu, nullptr, tr("Enregistrer &sous…"), QKeySequence::SaveAs,
              [this] { saveProjectAs(); });
    fileMenu->addSeparator();
    addAction(fileMenu, nullptr, tr("Exporter en &PDF…"), QKeySequence(),
              &MainWindow::exportPdf);
    addAction(fileMenu, nullptr, tr("Exporter en &DXF…"), QKeySequence(),
              &MainWindow::exportDxf);
    addAction(fileMenu, nullptr, tr("Exporter le rapport en &CSV…"), QKeySequence(),
              &MainWindow::exportCurrentReport);
    addAction(fileMenu, nullptr, tr("Im&primer…"), QKeySequence::Print,
              &MainWindow::printProject);
    fileMenu->addSeparator();
    addAction(fileMenu, nullptr, tr("&Quitter"), QKeySequence::Quit, [this] { close(); });

    // ---- Edition -------------------------------------------------------
    QMenu *editMenu = menuBar()->addMenu(tr("&Édition"));
    m_undoAction = addAction(editMenu, nullptr, tr("&Annuler"), QKeySequence::Undo,
                             [this] { m_document->undo(); });
    m_redoAction = addAction(editMenu, nullptr, tr("&Rétablir"), QKeySequence::Redo,
                             [this] { m_document->redo(); });
    editMenu->addSeparator();
    addAction(editMenu, nullptr, tr("&Copier"), QKeySequence::Copy,
              [this] { m_view->copySelection(); });
    addAction(editMenu, nullptr, tr("C&oller"), QKeySequence::Paste,
              [this] { m_view->pasteClipboard(); });
    addAction(editMenu, nullptr, tr("&Supprimer"), QKeySequence::Delete,
              [this] { m_view->deleteSelection(); });
    addAction(editMenu, nullptr, tr("&Tout sélectionner"), QKeySequence::SelectAll,
              [this] { m_view->selectAll(); });
    editMenu->addSeparator();
    addAction(editMenu, nullptr, tr("&Pivoter"), QKeySequence(Qt::Key_R),
              [this] { m_view->rotateSelection(true); });
    addAction(editMenu, nullptr, tr("Re&tourner"), QKeySequence(Qt::Key_M),
              [this] { m_view->mirrorSelection(); });
    addAction(editMenu, nullptr, tr("Mettre le potentiel en évidence"),
              QKeySequence(Qt::CTRL | Qt::Key_H), [this] { m_view->highlightNetOfSelection(); });

    // ---- Insertion : les outils ----------------------------------------
    QMenu *toolMenu = menuBar()->addMenu(tr("&Outils"));
    auto *toolBar = addToolBar(tr("Outils"));
    toolBar->setObjectName(QStringLiteral("toolbar.tools"));
    auto *toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    struct ToolSpec {
        FolioView::Tool tool;
        const char *label;
        Qt::Key key;
        const char *hint;
    };
    const ToolSpec tools[] = {
        { FolioView::Tool::Select, QT_TR_NOOP("&Sélection"), Qt::Key_S,
          QT_TR_NOOP("Sélectionner, déplacer, encadrer") },
        { FolioView::Tool::Wire, QT_TR_NOOP("&Fil"), Qt::Key_W,
          QT_TR_NOOP("Tracer un fil orthogonal") },
        { FolioView::Tool::Junction, QT_TR_NOOP("&Jonction"), Qt::Key_J,
          QT_TR_NOOP("Poser un point de connexion") },
        { FolioView::Tool::Label, QT_TR_NOOP("&Étiquette"), Qt::Key_L,
          QT_TR_NOOP("Nommer un potentiel") },
        { FolioView::Tool::Text, QT_TR_NOOP("&Texte"), Qt::Key_T, QT_TR_NOOP("Annoter le folio") },
    };
    for (const ToolSpec &spec : tools) {
        auto *action = new QAction(tr(spec.label), this);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(spec.key));
        action->setStatusTip(tr(spec.hint));
        action->setData(int(spec.tool));
        toolGroup->addAction(action);
        toolMenu->addAction(action);
        toolBar->addAction(action);
        m_toolActions.append(action);
        connect(action, &QAction::triggered, this,
                [this, spec] { m_view->setTool(spec.tool); });
    }
    m_toolActions.first()->setChecked(true);

    toolMenu->addSeparator();
    auto *crossReference = new QAction(tr("Étiquette = renvoi de folio"), this);
    crossReference->setCheckable(true);
    crossReference->setStatusTip(tr("Un renvoi relie le même potentiel à travers tout le dossier"));
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

    // ---- Affichage -----------------------------------------------------
    QMenu *viewMenu = menuBar()->addMenu(tr("&Affichage"));
    auto *viewBar = addToolBar(tr("Affichage"));
    viewBar->setObjectName(QStringLiteral("toolbar.view"));

    addAction(viewMenu, viewBar, tr("Zoom &avant"), QKeySequence::ZoomIn,
              [this] { m_view->zoomIn(); });
    addAction(viewMenu, viewBar, tr("Zoom a&rrière"), QKeySequence::ZoomOut,
              [this] { m_view->zoomOut(); });
    addAction(viewMenu, viewBar, tr("&Ajuster au folio"), QKeySequence(Qt::CTRL | Qt::Key_0),
              [this] { m_view->zoomToFit(); });
    addAction(viewMenu, nullptr, tr("&Taille réelle"), QKeySequence(Qt::CTRL | Qt::Key_1),
              [this] { m_view->zoomActual(); });
    viewMenu->addSeparator();

    auto addToggle = [&](QMenu *menu, const QString &text, bool checked, auto slot) {
        auto *action = new QAction(text, this);
        action->setCheckable(true);
        action->setChecked(checked);
        connect(action, &QAction::toggled, this, slot);
        menu->addAction(action);
        return action;
    };

    addToggle(viewMenu, tr("&Grille"), true, [this](bool on) { m_view->setGridVisible(on); });
    addToggle(viewMenu, tr("&Magnétisme"), true, [this](bool on) { m_view->setSnapEnabled(on); });
    addToggle(viewMenu, tr("&Numéros de broches"), false, [this](bool on) {
        RenderStyle style = m_view->style();
        style.showPinNumbers = on;
        m_view->setStyle(style);
    });
    addToggle(viewMenu, tr("&Broches non raccordées"), true, [this](bool on) {
        RenderStyle style = m_view->style();
        style.showUnconnectedPins = on;
        m_view->setStyle(style);
    });
    viewMenu->addSeparator();
    m_darkAction = addToggle(viewMenu, tr("Thème &sombre"), false,
                             [this](bool on) { applyTheme(on); });
    viewMenu->addSeparator();
    for (QDockWidget *dock : findChildren<QDockWidget *>())
        viewMenu->addAction(dock->toggleViewAction());

    // ---- Projet --------------------------------------------------------
    QMenu *projectMenu = menuBar()->addMenu(tr("&Projet"));
    addAction(projectMenu, nullptr, tr("&Informations du projet…"), QKeySequence(),
              &MainWindow::editProjectInfo);
    addAction(projectMenu, nullptr, tr("&Repérage automatique"),
              QKeySequence(Qt::CTRL | Qt::Key_R), &MainWindow::renumberAll);
    addAction(projectMenu, nullptr, tr("&Contrôler le schéma"), QKeySequence(Qt::Key_F8),
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

    // ---- Aide ----------------------------------------------------------
    QMenu *helpMenu = menuBar()->addMenu(tr("&Aide"));
    addAction(helpMenu, nullptr, tr("À &propos"), QKeySequence(), [this] {
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

void MainWindow::createStatusBar()
{
    m_cursorLabel = new QLabel(this);
    m_zoneLabel = new QLabel(this);
    m_zoomLabel = new QLabel(this);
    m_selectionLabel = new QLabel(this);
    for (QLabel *label : { m_cursorLabel, m_zoneLabel, m_zoomLabel, m_selectionLabel }) {
        label->setMinimumWidth(120);
        statusBar()->addPermanentWidget(label);
    }
    m_cursorLabel->setText(QStringLiteral("X 0,0   Y 0,0 mm"));
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
    m_view->setStyle(dark ? RenderStyle::screenDark() : RenderStyle::screen());
    m_view->setGridStep(m_document->profile().gridStep);
    m_palette->setLibrary(&m_document->project().library);
}

// --------------------------------------------------------------------------
// Fichier

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
