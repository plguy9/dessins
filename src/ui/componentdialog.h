// Boite « Inserer / Editer composant » — l'equivalent d'AEEDITCOMPONENT.
//
// C'est la boite la plus utilisee d'AutoCAD Electrical : elle s'ouvre juste
// apres avoir pose un symbole, et a chaque fois qu'on revient sur un
// appareil. Tout ce qui identifie l'appareil y tient en un ecran — repere,
// description, codes d'installation et d'emplacement, references catalogue,
// rattachement a un appareil parent — au lieu d'etre reparti entre un
// panneau lateral et trois menus.
#pragma once

#include "core/entities.h"
#include "core/project.h"
#include "rules/catalog.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace dsn {

class ComponentDialog : public QDialog
{
    Q_OBJECT

public:
    // `insertion` change seulement le titre et le bouton : la boite est la
    // meme a la pose et a l'edition, exactement comme chez AutoCAD.
    ComponentDialog(const Project &project, const SymbolInstance &symbol,
                    const Catalog &catalog, bool insertion, QWidget *parent = nullptr);

    // L'appareil tel que regle. Seuls les champs de la boite sont modifies :
    // la position et la definition sont recopiees telles quelles.
    SymbolInstance result() const;

Q_SIGNALS:
    // Demande d'aller voir un autre bloc du meme appareil.
    void locateRequested(const QString &folioId, const QString &entityId);

private:
    void lookupCatalog();
    void refreshCrossReferences();
    QString familyOf() const;

    const Project &m_project;
    SymbolInstance m_symbol;
    Catalog m_catalog;

    QLineEdit *m_designation = nullptr;
    QCheckBox *m_locked = nullptr;
    QLineEdit *m_description = nullptr;
    QLineEdit *m_value = nullptr;
    QLineEdit *m_installation = nullptr;
    QLineEdit *m_location = nullptr;
    QLineEdit *m_family = nullptr;
    QLineEdit *m_sector = nullptr;
    QLineEdit *m_loop = nullptr;
    QLineEdit *m_manufacturer = nullptr;
    QLineEdit *m_partNumber = nullptr;
    QComboBox *m_parent = nullptr;
    QLabel *m_crossReferences = nullptr;
    QLabel *m_pins = nullptr;
};

// Recherche dans le catalogue fabricant, ouverte depuis la boite du
// composant. Elle part de la famille du symbole, mais laisse en sortir : un
// catalogue reel ne colle jamais parfaitement a nos familles.
class CatalogDialog : public QDialog
{
    Q_OBJECT

public:
    CatalogDialog(const Catalog &catalog, const QString &deviceKind, QWidget *parent = nullptr);

    CatalogItem selected() const { return m_selected; }

private:
    void refresh();

    Catalog m_catalog;
    QString m_deviceKind;
    CatalogItem m_selected;

    QLineEdit *m_search = nullptr;
    QComboBox *m_family = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace dsn
