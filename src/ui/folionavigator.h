// Navigateur de folios : la liste des pages du dossier, avec vignettes.
//
// La vignette est le vrai rendu du folio, pas une icone generique : sur un
// dossier de trente pages, c'est ce qui permet de retrouver la bonne d'un coup
// d'oeil plutot qu'en lisant trente titres.
#pragma once

#include "document.h"

#include <QWidget>

class QListWidget;
class QToolButton;

namespace dsn {

class FolioNavigator : public QWidget
{
    Q_OBJECT

public:
    explicit FolioNavigator(Document *document, QWidget *parent = nullptr);

    void refresh();
    // Expose l'ajout de folio a la ligne de commande.
    void addFolioFromCommand() { addFolio(); }

Q_SIGNALS:
    void statusMessage(const QString &message);
    // La mise en page appartient a la fenetre principale : le navigateur se
    // contente de la demander pour le folio designe.
    void pageSetupRequested();

private:
    void addFolio();
    void duplicateFolio();
    void removeFolio();
    void moveFolio(int delta);
    void renameFolio();
    void showListContextMenu(const QPoint &pos);
    void scheduleThumbnails();

    Document *m_document = nullptr;
    QListWidget *m_list = nullptr;
    QToolButton *m_remove = nullptr;
    QToolButton *m_up = nullptr;
    QToolButton *m_down = nullptr;
    bool m_updating = false;
};

} // namespace dsn
