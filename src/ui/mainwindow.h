// Fenetre principale : menus, barres d'outils, panneaux et actions.
#pragma once

#include "document.h"
#include "folioview.h"

#include <QMainWindow>

class QLabel;

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

    // Affichage
    void applyTheme(bool dark);

    bool maybeSave();
    QString suggestedFileName(const QString &extension) const;

    Document *m_document = nullptr;
    FolioView *m_view = nullptr;
    SymbolPalette *m_palette = nullptr;
    PropertiesPanel *m_properties = nullptr;
    FolioNavigator *m_navigator = nullptr;
    ReportPanel *m_reports = nullptr;

    QLabel *m_cursorLabel = nullptr;
    QLabel *m_zoneLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;

    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_darkAction = nullptr;
    QList<QAction *> m_toolActions;
    bool m_dark = false;
};

} // namespace dsn
