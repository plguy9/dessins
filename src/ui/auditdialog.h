// Fenetre d'audit electrique.
//
// Elle ne dessine rien et ne modifie rien : elle constate et elle emmene.
// C'est toute la difference avec la boite de message qu'elle remplace — on
// ne corrige pas un schema en lisant une liste, on le corrige en allant sur
// le folio, et « Y aller » est donc la commande principale de cette fenetre,
// pas un supplement.
#pragma once

#include "document.h"
#include "rules/audit.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace dsn {

class AuditDialog : public QDialog
{
    Q_OBJECT

public:
    AuditDialog(Document *document, const PlcDatabase &plc, QWidget *parent = nullptr);

    int findingCount() const { return int(m_findings.size()); }

Q_SIGNALS:
    // La fenetre ne sait pas ouvrir un folio : la fenetre principale, si.
    void locateRequested(const QString &folioId, const QString &entityId);

private:
    void reload();
    void refreshTable();
    void gotoSelected();
    const AuditFinding *selectedFinding() const;

    Document *m_document = nullptr;
    const PlcDatabase &m_plc;
    QVector<AuditFinding> m_findings;

    QComboBox *m_scope = nullptr;
    QComboBox *m_category = nullptr;
    QComboBox *m_severity = nullptr;
    QLabel *m_summary = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_goto = nullptr;
};

} // namespace dsn
