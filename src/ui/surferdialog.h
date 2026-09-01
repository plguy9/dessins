// Surfer — navigation vers les references croisees, comme AESURF.
//
// Un appareil pose sur trois folios, un signal qui repart deux pages plus
// loin : sans outil, les retrouver demande de feuilleter le dossier. Le
// Surfer liste tout ce qui est lie a ce qu'on designe, et y saute.
//
// Il travaille a l'echelle du projet, pas du folio : c'est tout son interet.
#pragma once

#include "core/project.h"
#include "document.h"

#include <QDialog>

class QListWidget;

namespace dsn {

class SurferDialog : public QDialog
{
    Q_OBJECT

public:
    // Une destination : le folio, l'entite a designer, et de quoi la
    // reconnaitre dans la liste.
    struct Site {
        QString folioId;
        QString entityId;
        QString title;
        QString detail;
    };

    SurferDialog(Document *document, const QString &entityId, QWidget *parent = nullptr);

    // Liens d'une entite, calcules sans interface : les tests s'en servent, et
    // c'est ce qui garantit que la liste dit vrai.
    static QVector<Site> sitesFor(const Project &project, const Netlist &netlist,
                                  const QString &entityId);

    bool hasSites() const { return !m_sites.isEmpty(); }

Q_SIGNALS:
    // Aller voir : le folio a ouvrir et l'entite a designer.
    void locateRequested(const QString &folioId, const QString &entityId);

private:
    void jumpTo(int row);

    Document *m_document = nullptr;
    QVector<Site> m_sites;
    QListWidget *m_list = nullptr;
};

} // namespace dsn
