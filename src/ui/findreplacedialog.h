// « Rechercher / remplacer » dans tout le dossier.
//
// La premiere commande qu'un dessinateur venu d'AutoCAD cherche et ne
// trouvait pas. Le cas courant : l'affaire change de numero, un moteur change
// de repere d'atelier — et cela se lit dans quarante endroits repartis sur
// douze folios. A la main on en oublie toujours un, et c'est celui-la qui
// part a l'atelier.
//
// Trois decisions dans cette boite :
//
// 1. **On voit avant de remplacer.** La liste montre le lieu (folio, zone),
//    ce qui est ecrit et ce que cela deviendrait. Remplacer a l'aveugle dans
//    un dossier entier est un geste qu'on n'ose pas faire — donc qu'on ne
//    fait pas, et la commande ne sert a rien.
// 2. **Un double-clic saute sur place.** La boite est aussi une recherche :
//    trouver ou est ecrit « KM3 » vaut le remplacement.
// 3. **Tout le remplacement tient dans UNE annulation.** C'est ce qui rend le
//    geste sans risque : on essaie, on regarde, Ctrl+Z.
#pragma once

#include "rules/findreplace.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace dsn {

class Document;

class FindReplaceDialog : public QDialog
{
    Q_OBJECT

public:
    FindReplaceDialog(Document *document, QWidget *parent = nullptr);

    // Pre-remplit le champ de recherche : le clic droit sur un repere ouvre
    // la boite avec ce repere dedans, comme partout ailleurs.
    void setNeedle(const QString &needle);

    // Pour les tests : la recherche et le remplacement sans passer par les
    // boutons — ce sont eux qui portent le comportement, pas les widgets.
    int runSearch();
    int runReplaceAll();

Q_SIGNALS:
    void locateRequested(const QString &folioId, const QString &entityId);

private:
    FindQuery buildQuery() const;
    void refreshTable();

    Document *m_document = nullptr;
    QLineEdit *m_needle = nullptr;
    QLineEdit *m_replacement = nullptr;
    QCheckBox *m_case = nullptr;
    QCheckBox *m_word = nullptr;
    QComboBox *m_scope = nullptr;
    QCheckBox *m_texts = nullptr;
    QCheckBox *m_labels = nullptr;
    QCheckBox *m_designations = nullptr;
    QCheckBox *m_fields = nullptr;
    QCheckBox *m_wireNumbers = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_replaceButton = nullptr;
    QVector<FindHit> m_hits;
};

} // namespace dsn
