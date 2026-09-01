#include "auditdialog.h"

#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace dsn {

namespace {

QColor severityColor(AuditFinding::Severity severity)
{
    const ThemeColors &c = Theme::colors();
    switch (severity) {
    case AuditFinding::Severity::Error: return c.danger;
    case AuditFinding::Severity::Warning: return c.warning;
    case AuditFinding::Severity::Info: return c.textMuted;
    }
    return c.text;
}

} // namespace

AuditDialog::AuditDialog(Document *document, const PlcDatabase &plc, QWidget *parent)
    : QDialog(parent), m_document(document), m_plc(plc)
{
    setWindowTitle(tr("Audit électrique"));
    resize(900, 620);

    auto *layout = new QVBoxLayout(this);

    // ---- les filtres ------------------------------------------------------
    auto *filters = new QHBoxLayout;
    filters->setSpacing(Theme::space(4));

    auto addChoice = [&](const QString &caption, QComboBox **target) {
        auto *box = new QVBoxLayout;
        auto *label = new QLabel(caption, this);
        Theme::engrave(label);
        *target = new QComboBox(this);
        box->addWidget(label);
        box->addWidget(*target);
        filters->addLayout(box, 1);
    };

    // Comme tous les rapports, l'audit commence par la question d'AutoCAD :
    // tout le projet, ou le folio actif.
    addChoice(tr("Portée"), &m_scope);
    m_scope->addItem(tr("Tout le projet"), QString());
    for (const Folio *folio : m_document->project().folios()) {
        m_scope->addItem(tr("Folio %1 — %2").arg(folio->number, folio->title), folio->id());
    }

    addChoice(tr("Catégorie"), &m_category);
    m_category->addItem(tr("Toutes"), QString());
    for (const QString &category : Audit::categories())
        m_category->addItem(category, category);

    addChoice(tr("Gravité"), &m_severity);
    m_severity->addItem(tr("Tout afficher"), -1);
    m_severity->addItem(tr("Erreurs seulement"), int(AuditFinding::Severity::Error));
    m_severity->addItem(tr("Erreurs et avertissements"),
                        int(AuditFinding::Severity::Warning));
    layout->addLayout(filters);

    m_summary = new QLabel(this);
    m_summary->setProperty("hint", true);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    // ---- les constats -----------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({ tr("Gravité"), tr("Catégorie"), tr("Constat"),
                                         tr("Folio"), tr("Zone") });
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    // ---- les commandes ----------------------------------------------------
    auto *buttons = new QDialogButtonBox(this);
    m_goto = buttons->addButton(tr("Y aller"), QDialogButtonBox::ActionRole);
    m_goto->setDefault(true);
    auto *recheck = buttons->addButton(tr("Recontrôler"), QDialogButtonBox::ActionRole);
    buttons->addButton(tr("Fermer"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_goto, &QPushButton::clicked, this, &AuditDialog::gotoSelected);
    connect(recheck, &QPushButton::clicked, this, [this] {
        m_document->invalidateNetlist();
        reload();
    });
    layout->addWidget(buttons);

    for (QComboBox *box : { m_scope, m_category, m_severity })
        connect(box, &QComboBox::currentIndexChanged, this, [this] { refreshTable(); });
    // Double-clic sur un constat : on y va. C'est le geste attendu d'une
    // liste de choses a corriger.
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { gotoSelected(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this] { m_goto->setEnabled(selectedFinding() != nullptr); });

    reload();
}

void AuditDialog::reload()
{
    m_findings = Audit::run(m_document->project(), m_document->netlist(), m_plc);
    refreshTable();
}

void AuditDialog::refreshTable()
{
    const QString folioId = m_scope->currentData().toString();
    const QString category = m_category->currentData().toString();
    const int minimum = m_severity->currentData().toInt();

    QVector<AuditFinding> shown;
    for (const AuditFinding &finding : m_findings) {
        if (!folioId.isEmpty() && finding.folioId != folioId)
            continue;
        if (!category.isEmpty() && finding.category != category)
            continue;
        if (minimum >= 0 && int(finding.severity) < minimum)
            continue;
        shown.append(finding);
    }

    m_table->setRowCount(shown.size());
    for (int row = 0; row < shown.size(); ++row) {
        const AuditFinding &finding = shown.at(row);
        const QStringList cells{ finding.severityLabel(), finding.category, finding.message,
                                 finding.folioTag, finding.zone };
        for (int column = 0; column < cells.size(); ++column) {
            auto *item = new QTableWidgetItem(cells.at(column));
            if (column == 0)
                item->setForeground(severityColor(finding.severity));
            if (column == 4)
                item->setFont(Theme::monoFont());
            // Le lieu voyage avec la ligne : « Y aller » n'a alors rien a
            // rechercher, et une ligne filtree ne peut pas designer une autre.
            item->setData(Qt::UserRole, finding.folioId);
            item->setData(Qt::UserRole + 1, finding.entityId);
            m_table->setItem(row, column, item);
        }
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_goto->setEnabled(false);

    int errors = 0;
    int warnings = 0;
    for (const AuditFinding &finding : m_findings) {
        if (finding.severity == AuditFinding::Severity::Error)
            ++errors;
        else if (finding.severity == AuditFinding::Severity::Warning)
            ++warnings;
    }
    if (m_findings.isEmpty()) {
        m_summary->setText(tr("Aucune anomalie sur %n potentiel(s). Le dossier est cohérent.",
                              "", m_document->netlist().netCount()));
    } else {
        m_summary->setText(tr("%1 constat(s) : %2 erreur(s), %3 avertissement(s), "
                              "%4 information(s). Double-cliquez pour aller voir.")
                                   .arg(m_findings.size())
                                   .arg(errors)
                                   .arg(warnings)
                                   .arg(int(m_findings.size()) - errors - warnings));
    }
}

const AuditFinding *AuditDialog::selectedFinding() const
{
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    if (selected.isEmpty())
        return nullptr;
    const QString folioId = selected.first()->data(Qt::UserRole).toString();
    const QString entityId = selected.first()->data(Qt::UserRole + 1).toString();
    if (folioId.isEmpty())
        return nullptr;
    for (const AuditFinding &finding : m_findings) {
        if (finding.folioId == folioId && finding.entityId == entityId)
            return &finding;
    }
    return nullptr;
}

void AuditDialog::gotoSelected()
{
    const AuditFinding *finding = selectedFinding();
    if (!finding)
        return;
    Q_EMIT locateRequested(finding->folioId, finding->entityId);
}

} // namespace dsn
