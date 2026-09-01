#include "reportpanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace dsn {

ReportPanel::ReportPanel(Document *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Perimetre du rapport, comme la question posee par chaque rapport
    // d'AutoCAD Electrical : tout le projet, ou le folio actif.
    auto *scopeRow = new QHBoxLayout;
    scopeRow->setContentsMargins(6, 4, 6, 0);
    scopeRow->addWidget(new QLabel(tr("Portée"), this));
    m_scope = new QComboBox(this);
    scopeRow->addWidget(m_scope, 1);
    layout->addLayout(scopeRow);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    layout->addWidget(m_tabs);

    m_summary = addTab(tr("Récapitulatif"));
    m_bom = addTab(tr("Nomenclature"));
    m_components = addTab(tr("Composants"));
    m_terminals = addTab(tr("Bornier"));
    m_wires = addTab(tr("Fils"));
    m_runs = addTab(tr("Câblage De/Vers"));
    m_checks = addTab(tr("Contrôles"));

    rebuildScopeChoices();
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_document, &Document::folioListChanged, this, [this] {
        rebuildScopeChoices();
        refresh();
    });
    // « Folio actif » suit la page ouverte : changer de folio doit changer le
    // rapport, sinon la portee ment.
    connect(m_document, &Document::currentFolioChanged, this, [this] {
        if (m_scope->currentIndex() == 1)
            refresh();
    });

    // Les rapports sont recalcules en differe et seulement quand le panneau
    // est visible : reconstruire quatre tableaux a chaque coup de souris
    // rendrait le dessin poussif pour rien.
    connect(m_document, &Document::changed, this, [this] {
        m_dirty = true;
        if (isVisible())
            QTimer::singleShot(250, this, &ReportPanel::refresh);
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        if (m_dirty)
            refresh();
    });

    refresh();
}

QTableWidget *ReportPanel::addTab(const QString &title)
{
    auto *table = new QTableWidget(this);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    m_tabs->addTab(table, title);
    return table;
}

void ReportPanel::fill(QTableWidget *table, const ReportTable &data)
{
    table->clear();
    table->setColumnCount(int(data.headers.size()));
    table->setHorizontalHeaderLabels(data.headers);
    table->setRowCount(int(data.rows.size()));
    for (int row = 0; row < int(data.rows.size()); ++row) {
        const QStringList &cells = data.rows.at(row);
        for (int column = 0; column < cells.size() && column < table->columnCount(); ++column)
            table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
    table->resizeColumnsToContents();
}

void ReportPanel::rebuildScopeChoices()
{
    if (!m_scope)
        return;
    QSignalBlocker blocker(m_scope);
    const int previous = m_scope->currentIndex();
    m_scope->clear();
    m_scope->addItem(tr("Tout le projet"));
    m_scope->addItem(tr("Folio actif"));
    m_scope->setCurrentIndex(previous >= 0 ? previous : 0);
}

ReportScope ReportPanel::scope() const
{
    ReportScope s;
    if (m_scope && m_scope->currentIndex() == 1) {
        if (const Folio *folio = m_document->currentFolio())
            s.folioId = folio->id();
    }
    return s;
}

void ReportPanel::refresh()
{
    const Netlist &netlist = m_document->netlist();
    const Project &project = m_document->project();
    const ReportScope current = scope();

    m_tables.clear();
    m_tables.append(Reports::projectSummary(project, netlist, current));
    m_tables.append(Reports::toTable(Reports::billOfMaterials(project, current)));
    m_tables.append(Reports::toTable(Reports::componentList(project, netlist, current)));
    m_tables.append(Reports::toTable(Reports::terminalList(project, netlist, current)));
    m_tables.append(Reports::toTable(Reports::wireList(project, netlist, current)));
    m_tables.append(Reports::toTable(Reports::wireFromTo(project, netlist, current)));
    m_tables.append(Reports::diagnostics(netlist, current));

    fill(m_summary, m_tables.at(0));
    fill(m_bom, m_tables.at(1));
    fill(m_components, m_tables.at(2));
    fill(m_terminals, m_tables.at(3));
    fill(m_wires, m_tables.at(4));
    fill(m_runs, m_tables.at(5));
    fill(m_checks, m_tables.at(6));

    // Le nombre d'anomalies reste visible sans ouvrir l'onglet : c'est
    // l'information qu'on veut voir sans la chercher.
    const int problems = m_tables.at(6).rowCount();
    m_tabs->setTabText(6, problems == 0 ? tr("Contrôles") : tr("Contrôles (%1)").arg(problems));
    m_dirty = false;
}

ReportTable ReportPanel::currentTable() const
{
    const int index = m_tabs->currentIndex();
    if (index < 0 || index >= m_tables.size())
        return {};
    return m_tables.at(index);
}

QString ReportPanel::currentTitle() const { return m_tabs->tabText(m_tabs->currentIndex()); }

} // namespace dsn
