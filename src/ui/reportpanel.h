// Panneau des rapports : nomenclature, bornier, liste des fils, controles.
//
// Les tableaux sont recalcules depuis le document, jamais stockes. Un rapport
// qui diverge du schema est pire que pas de rapport du tout.
#pragma once

#include "document.h"
#include "rules/reports.h"

#include <QWidget>

class QTabWidget;
class QTableWidget;

namespace dsn {

class ReportPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ReportPanel(Document *document, QWidget *parent = nullptr);

    void refresh();
    ReportTable currentTable() const;
    QString currentTitle() const;

Q_SIGNALS:
    void statusMessage(const QString &message);
    void locateRequested(const QString &folioId, const QString &entityId);

private:
    QTableWidget *addTab(const QString &title);
    void fill(QTableWidget *table, const ReportTable &data);

    Document *m_document = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_summary = nullptr;
    QTableWidget *m_bom = nullptr;
    QTableWidget *m_terminals = nullptr;
    QTableWidget *m_wires = nullptr;
    QTableWidget *m_checks = nullptr;
    QVector<ReportTable> m_tables;
    bool m_dirty = true;
};

} // namespace dsn
