// Editeur de borniers — l'equivalent de l'AETSE d'AutoCAD Electrical.
//
// Un bornier ne se lit pas sur le schema : ses bornes sont dispersees sur
// plusieurs folios, et c'est leur ordre dans l'armoire qui compte. Cet
// editeur les rassemble par bornier, montre ce qui est raccorde a chacune, et
// permet de les renumeroter d'un coup.
//
// Il ne dessine rien : il modifie les reperes de borne des symboles poses,
// par commandes annulables comme tout le reste.
#pragma once

#include "document.h"
#include "rules/reports.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QTableWidget;

namespace dsn {

class TerminalStripDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TerminalStripDialog(Document *document, QWidget *parent = nullptr);

    // Bornes d'un bornier, dans l'ordre de lecture du dossier. Calcule sans
    // interface : c'est ce qui garantit que la table dit vrai.
    struct Terminal {
        QString entityId;
        QString folioId;
        QString block;      // repere du bornier, ex. -X1
        QString number;     // repere de la borne
        QString folio;
        QString zone;
        QString wireNumber;
        QString target;     // appareil raccorde
        QString targetPin;
    };
    static QStringList blocksOf(const Project &project);
    static QVector<Terminal> terminalsOf(const Project &project, const Netlist &netlist,
                                         const QString &block);

Q_SIGNALS:
    void locateRequested(const QString &folioId, const QString &entityId);

private:
    void reloadBlocks();
    void reload();
    void renumber();
    void applyEdits();

    Document *m_document = nullptr;
    QVector<Terminal> m_terminals;
    bool m_updating = false;

    QComboBox *m_block = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
};

} // namespace dsn
