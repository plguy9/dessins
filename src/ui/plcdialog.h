// Insertion et edition d'un module d'automate — l'equivalent de l'insertion
// parametrique d'AutoCAD Electrical.
//
// Le geste est celui du logiciel d'origine : on choisit une carte dans la
// base des constructeurs, on donne son adresse de depart (rack, emplacement,
// premier point), et on voit immediatement les adresses que les points vont
// porter. La table d'apercu n'est pas decorative : c'est elle qui evite de
// poser une carte sur un espace d'adressage deja occupe, et c'est aussi la
// ou se saisissent les descriptions des points — le seul texte du module qui
// ne se deduise de rien.
//
// La meme boite sert a l'insertion et a la reprise d'un module deja pose :
// changer l'emplacement d'une carte doit se faire au meme endroit qu'on l'a
// choisi, sinon on la supprime et on la repose.
#pragma once

#include "core/entities.h"
#include "rules/plc.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace dsn {

class PlcDialog : public QDialog
{
    Q_OBJECT

public:
    // `existing` vide : insertion. Sinon la boite s'ouvre sur ce module et le
    // bouton de validation le met a jour.
    PlcDialog(const PlcDatabase &database, const SymbolInstance *existing = nullptr,
              QWidget *parent = nullptr);

    // Le module choisi, ou nullptr si la base est vide.
    const PlcModuleDef *module() const;
    int node() const;
    int rack() const;
    int slot() const;
    int firstPoint() const;
    QString designation() const;

    // Applique le choix a une instance : identite, adresse de depart et
    // descriptions de points. C'est le seul endroit qui ecrit ces champs
    // depuis l'interface.
    void applyTo(SymbolInstance &symbol) const;

private:
    void reloadModules();
    void refreshPreview();
    void captureDescriptions();

    const PlcDatabase &m_database;
    QList<PlcModuleDef> m_visible;
    // Descriptions saisies, par rang de point. Elles survivent au changement
    // de module : on hesite souvent entre deux cartes apres avoir ecrit la
    // liste des entrees.
    QHash<int, QString> m_descriptions;

    QComboBox *m_manufacturer = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_moduleBox = nullptr;
    QLabel *m_details = nullptr;
    QSpinBox *m_node = nullptr;
    QSpinBox *m_rack = nullptr;
    QSpinBox *m_slot = nullptr;
    QSpinBox *m_firstPoint = nullptr;
    QLineEdit *m_designation = nullptr;
    QTableWidget *m_preview = nullptr;
};

} // namespace dsn
