// Fenetre principale : menus, barres d'outils, panneaux et actions.
#pragma once

#include "document.h"
#include "folioview.h"
#include "core/edittools.h"
#include "rules/catalog.h"
#include "rules/plc.h"

#include <QDockWidget>
#include <QHash>
#include <QMainWindow>
#include <QSet>

class QMenuBar;

class QComboBox;
class QLabel;
class QToolButton;

namespace dsn {

class DockTitle;
class DockRail;

// Ce qui rend une commande IMPOSSIBLE — jamais « ce que l'utilisateur a oublie
// de faire ». La nuance est tout le bloc A : une commande qui a besoin d'objets
// les demande (voir FolioView::requestSelection), donc l'absence de selection
// ne la grise pas. Ce qui la grise, c'est qu'il n'y ait rien dans le folio sur
// quoi elle puisse porter — couper un fil dans un dossier sans un seul fil.
enum class Need {
    Always,     // rien a exiger
    Undo,
    Redo,
    Clipboard,
    AnyEntity,  // le folio porte au moins une entite
    TwoEntities,// au moins deux : aligner, repartir
    AnyWire,
    TwoWires,   // joindre
    AnySymbol,
};

class FolioNavigator;
class ReportPanel;
class SymbolPalette;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString &path);
    // Appele par le demarrage : l'ecran d'accueil est public parce que c'est
    // l'application qui decide de le montrer, pas la fenetre.
    void showStartPage();

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    // Invocable : le test de la palette l'ouvre pour verifier qu'elle expose
    // bien tout ce que les menus offrent.
    void openCommandPalette();

private:
    void createDocks();
    void createActions();
    // Le ruban se monte apres les menus : il presente leurs QAction, il n'en
    // cree aucune. Les menus restent la source de verite — la palette de
    // commandes les parcourt.
    void createRibbon();
    // Toutes les actions des menus, indexees par leur libelle nettoye. C'est
    // le meme parcours que celui de la palette de commandes : une action
    // absente des menus est absente des deux.
    QHash<QString, QAction *> indexMenuActions() const;
    void createStatusBar();
    void updateTitle();
    void updateActions();

    // Fichier
    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    void exportPdf();
    void exportDxf();
    void exportCurrentReport();
    void placeCurrentReport();
    void printProject();

    // Projet
    void editProjectInfo();
    void renumberAll();
    void setProfile(const QString &profileId);
    void checkSchematic();

    // Composants
    void editComponent(const QString &entityId, bool insertion);
    void editSelectedComponent();
    void surfSelection();
    void editTerminalStrips();
    // Modification : le groupe « Modifier » du ruban d'AutoCAD.
    void arraySelection();
    // Demande le nombre de cotes du polygone. Faux si l'utilisateur renonce.
    void measureDistance();
    void alignSelection(AlignMode mode);
    void matchProperties();
    // Automates : la meme boite pose une carte et reprend une carte posee.
    void insertPlcModule();
    void editPlcModule(const QString &entityId);
    // Ouvre le folio voulu et designe l'entite : c'est ce que fait « y aller ».
    void locate(const QString &folioId, const QString &entityId);

    // Symboles
    void newSymbol();
    void editCurrentSymbol(bool asCopy);

    // Affichage
    void applyTheme(bool dark);
    // Grise ce qui ne peut rien faire. Appelee par updateActions.
    void applyNeeds();

    // ---- TASSER ET RAMENER UN PANNEAU ----------------------------------
    //
    // Un panneau tasse revenait « visible » mais large de zero pixel :
    // Qt le restitue avec la largeur qu'il avait au moment ou on l'a cache,
    // et le canevas avait pris toute la place entre-temps. Du point de vue du
    // dessinateur, le panneau ne revenait jamais — signale sur Windows, et
    // introuvable en rendu hors ecran, ou Qt se rattrape tout seul.
    //
    // On retient donc la largeur AVANT de cacher, et on la repose au retour.
    // Les deux chemins — le chevron de la barre de titre et la commande
    // d'affichage — passent par ici : sinon l'un des deux oublierait.
    void setDockVisible(QDockWidget *dock, bool visible);
    QHash<QDockWidget *, int> m_dockWidths;
    void refreshIcons();

    // Ligne de commande
    //
    // TOUT ce que le logiciel a a dire passe par la : le compte rendu d'une
    // commande, l'erreur, l'invite. La barre d'etat ne garde que les etats
    // permanents — coordonnees, zone, zoom, selection, bascules — parce qu'un
    // message qui s'efface au bout de six secondes dans un coin de la fenetre
    // n'est pas lu. C'est la premiere lecon de la ligne de commande d'AutoCAD :
    // on y regarde parce qu'il s'y passe toujours quelque chose.
    void report(const QString &message);
    void echoMenuCommands();
    void reportError(const QString &message);
    void registerCommands();
    void zoomCommand(const QStringList &arguments);

    // Aides au dessin
    void createDraftingToggles(QMenu *menu);
    void syncDraftingToggles();
    void editDraftingSettings();
    void editPageSetup();
    void insertLadder();
    void editWireTypes();
    // La fiche de proprietes, ouverte au double-clic ou par Ctrl+1.
    void showProperties(const QSet<QString> &selection);
    void applyWireTypeToSelection();
    void insertBus();
    void editTagFormat();
    void rebuildWireTypeSelector();

    // Edition avancee
    void offsetSelection();
    void showCanvasContextMenu(const QPoint &globalPos);

    bool maybeSave();
    QString suggestedFileName(const QString &extension) const;

    // Chemin du projet d'exemple livre a cote du binaire, s'il existe.
    QString examplePath() const;

    // Fichiers recents
    void addRecentFile(const QString &path);
    void rebuildRecentMenu();

    // Catalogue fabricant, charge une fois : le logiciel doit proposer des
    // references des le premier lancement, sans installation.
    Catalog m_catalog;
    PlcDatabase m_plc;

    Document *m_document = nullptr;
    FolioView *m_view = nullptr;
    SymbolPalette *m_palette = nullptr;
    FolioNavigator *m_navigator = nullptr;
    ReportPanel *m_reports = nullptr;
    class CommandLine *m_command = nullptr;
    class CommandPalette *m_commandPalette = nullptr;
    QDockWidget *m_commandDock = nullptr;
    QList<DockTitle *> m_dockTitles;
    // Le rail des panneaux tasses, colle au bord gauche du canevas : c'est la
    // que la fleche reste pour ramener un panneau ferme.
    DockRail *m_rail = nullptr;

    QLabel *m_cursorLabel = nullptr;
    QLabel *m_zoneLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;

    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_darkAction = nullptr;
    QAction *m_gridAction = nullptr;
    QAction *m_gridSnapAction = nullptr;
    QAction *m_osnapAction = nullptr;
    QAction *m_orthoAction = nullptr;
    QAction *m_polarAction = nullptr;
    QAction *m_trackingAction = nullptr;
    QComboBox *m_wireTypeSelector = nullptr;
    // Actions reprises par le menu contextuel du canevas : elles y gardent
    // leur icone et leur raccourci, au lieu d'etre redecrites.
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_pasteKeepAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_selectAllAction = nullptr;
    QAction *m_rotateAction = nullptr;
    QAction *m_mirrorAction = nullptr;
    QAction *m_highlightAction = nullptr;
    QAction *m_moveAction = nullptr;
    QAction *m_offsetAction = nullptr;
    QAction *m_stretchAction = nullptr;
    QAction *m_editComponentAction = nullptr;
    QAction *m_scaleAction = nullptr;
    QAction *m_arrayAction = nullptr;
    QAction *m_joinAction = nullptr;
    QAction *m_busAction = nullptr;
    QAction *m_applyWireTypeAction = nullptr;
    QAction *m_lockTagsAction = nullptr;
    QAction *m_unlockTagsAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_matchAction = nullptr;
    QList<QAction *> m_alignActions;
    QAction *m_scootAction = nullptr;
    QAction *m_moveComponentAction = nullptr;
    QAction *m_surferAction = nullptr;
    QAction *m_editOnInsertAction = nullptr;
    QAction *m_zoomFitAction = nullptr;
    QAction *m_zoomPreviousAction = nullptr;
    QAction *m_pageSetupAction = nullptr;
    QString m_wireTypeSignature;   // evite de reconstruire le combo a chaque commande
    bool m_syncingToggles = false;
    QHash<QToolButton *, QAction *> m_statusToggles;
    QList<QAction *> m_toolActions;
    QHash<QAction *, int> m_actionGlyphs; // action -> Icons::Glyph, pour le rehabillage
    // Ce qui rend chaque commande impossible. Toute action passee par make() y
    // figure : c'est ce qui permet a updateActions de n'en oublier aucune, et
    // a un test de le verifier.
    QHash<QAction *, Need> m_actionNeeds;
    class QToolBar *m_toolBar = nullptr;
    class Ribbon *m_ribbon = nullptr;
    // La barre de menus, retenue explicitement. setMenuWidget() detache celle
    // de QMainWindow : menuBar() en fabriquerait ensuite une neuve et vide, et
    // la palette de commandes — qui se remplit en parcourant les menus — se
    // retrouverait sans une seule commande.
    QMenuBar *m_menuBar = nullptr;
    class QMenu *m_recentMenu = nullptr;
    bool m_dark = true;
};

} // namespace dsn
