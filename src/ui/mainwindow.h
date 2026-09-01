// Fenetre principale : menus, barres d'outils, panneaux et actions.
#pragma once

#include "document.h"
#include "folioview.h"

#include <QDockWidget>
#include <QHash>
#include <QMainWindow>

class QComboBox;
class QLabel;
class QToolButton;

namespace dsn {

class FolioNavigator;
class PropertiesPanel;
class ReportPanel;
class SymbolPalette;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createDocks();
    void createActions();
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
    void printProject();

    // Projet
    void editProjectInfo();
    void renumberAll();
    void setProfile(const QString &profileId);
    void checkSchematic();

    // Symboles
    void newSymbol();
    void editCurrentSymbol(bool asCopy);

    // Affichage
    void applyTheme(bool dark);
    void refreshIcons();

    // Ligne de commande
    void registerCommands();
    void zoomCommand(const QStringList &arguments);

    // Aides au dessin
    void createDraftingToggles(QMenu *menu);
    void syncDraftingToggles();
    void editDraftingSettings();
    void editPageSetup();
    void insertLadder();
    void editWireTypes();
    void rebuildWireTypeSelector();

    // Edition avancee
    void offsetSelection();
    void showCanvasContextMenu(const QPoint &globalPos);

    bool maybeSave();
    QString suggestedFileName(const QString &extension) const;

    // Fichiers recents
    void addRecentFile(const QString &path);
    void rebuildRecentMenu();

    Document *m_document = nullptr;
    FolioView *m_view = nullptr;
    SymbolPalette *m_palette = nullptr;
    PropertiesPanel *m_properties = nullptr;
    FolioNavigator *m_navigator = nullptr;
    ReportPanel *m_reports = nullptr;
    class CommandLine *m_command = nullptr;
    QDockWidget *m_commandDock = nullptr;

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
    QComboBox *m_wireTypeSelector = nullptr;
    // Actions reprises par le menu contextuel du canevas : elles y gardent
    // leur icone et leur raccourci, au lieu d'etre redecrites.
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_selectAllAction = nullptr;
    QAction *m_rotateAction = nullptr;
    QAction *m_mirrorAction = nullptr;
    QAction *m_highlightAction = nullptr;
    QAction *m_moveAction = nullptr;
    QAction *m_offsetAction = nullptr;
    QAction *m_stretchAction = nullptr;
    QAction *m_zoomFitAction = nullptr;
    QAction *m_zoomPreviousAction = nullptr;
    QAction *m_pageSetupAction = nullptr;
    QString m_wireTypeSignature;   // evite de reconstruire le combo a chaque commande
    bool m_syncingToggles = false;
    QHash<QToolButton *, QAction *> m_statusToggles;
    QList<QAction *> m_toolActions;
    QHash<QAction *, int> m_actionGlyphs; // action -> Icons::Glyph, pour le rehabillage
    class QToolBar *m_toolBar = nullptr;
    class QMenu *m_recentMenu = nullptr;
    bool m_dark = true;
};

} // namespace dsn
