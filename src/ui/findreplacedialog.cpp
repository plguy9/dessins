#include "findreplacedialog.h"

#include "core/documentcommands.h"
#include "core/entities.h"
#include "document.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <map>

namespace dsn {

FindReplaceDialog::FindReplaceDialog(Document *document, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    setWindowTitle(tr("Rechercher et remplacer"));
    resize(880, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(Theme::space(3));

    auto *form = new QFormLayout;
    m_needle = new QLineEdit(this);
    m_needle->setPlaceholderText(tr("Texte à chercher"));
    m_replacement = new QLineEdit(this);
    m_replacement->setPlaceholderText(tr("Laisser vide pour seulement chercher"));
    form->addRow(tr("Rechercher"), m_needle);
    form->addRow(tr("Remplacer par"), m_replacement);
    layout->addLayout(form);

    auto *options = new QHBoxLayout;
    options->setSpacing(Theme::space(4));
    m_case = new QCheckBox(tr("Respecter la casse"), this);
    m_word = new QCheckBox(tr("Mot entier"), this);
    options->addWidget(m_case);
    options->addWidget(m_word);
    options->addStretch(1);

    // Comme tous les rapports, la recherche commence par la question
    // d'AutoCAD : tout le dossier, ou le folio actif.
    auto *scopeLabel = new QLabel(tr("Portée"), this);
    Theme::engrave(scopeLabel);
    m_scope = new QComboBox(this);
    m_scope->addItem(tr("Tout le dossier"), QString());
    for (const Folio *folio : m_document->project().folios())
        m_scope->addItem(tr("Folio %1").arg(folio->number), folio->id());
    options->addWidget(scopeLabel);
    options->addWidget(m_scope);
    layout->addLayout(options);

    // Ou chercher. Renommer tous les « M1 » d'un dossier ne doit pas toucher
    // la phrase « alimentation M1 » d'un cartouche si on ne l'a pas demande :
    // c'est pour cela que les gisements sont separes.
    auto *whereRow = new QHBoxLayout;
    whereRow->setSpacing(Theme::space(4));
    auto addWhere = [&](const QString &caption, QCheckBox **target) {
        *target = new QCheckBox(caption, this);
        (*target)->setChecked(true);
        whereRow->addWidget(*target);
    };
    addWhere(tr("Repères"), &m_designations);
    addWhere(tr("Champs"), &m_fields);
    addWhere(tr("Textes"), &m_texts);
    addWhere(tr("Étiquettes"), &m_labels);
    addWhere(tr("Repères de fil"), &m_wireNumbers);
    whereRow->addStretch(1);
    layout->addLayout(whereRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
            { tr("Folio"), tr("Zone"), tr("Où"), tr("Actuel"), tr("Deviendrait") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table, 1);

    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("color:%1").arg(Theme::colors().textMuted.name()));
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(this);
    auto *search = buttons->addButton(tr("Rechercher"), QDialogButtonBox::ActionRole);
    m_replaceButton = buttons->addButton(tr("Remplacer tout"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(search, &QPushButton::clicked, this, [this] { runSearch(); });
    connect(m_replaceButton, &QPushButton::clicked, this, [this] { runReplaceAll(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_needle, &QLineEdit::returnPressed, this, [this] { runSearch(); });

    // Un double-clic saute sur place : la boite est aussi une recherche.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= m_hits.size())
            return;
        Q_EMIT locateRequested(m_hits.at(row).folioId, m_hits.at(row).entityId);
    });
}

void FindReplaceDialog::setNeedle(const QString &needle)
{
    m_needle->setText(needle);
    if (!needle.isEmpty())
        runSearch();
}

FindQuery FindReplaceDialog::buildQuery() const
{
    FindQuery query;
    query.needle = m_needle->text();
    query.replacement = m_replacement->text();
    query.caseSensitive = m_case->isChecked();
    query.wholeWord = m_word->isChecked();
    query.scope.folioId = m_scope->currentData().toString();
    query.inTexts = m_texts->isChecked();
    query.inLabels = m_labels->isChecked();
    query.inDesignations = m_designations->isChecked();
    query.inFields = m_fields->isChecked();
    query.inWireNumbers = m_wireNumbers->isChecked();
    return query;
}

void FindReplaceDialog::refreshTable()
{
    m_table->setRowCount(int(m_hits.size()));
    for (int row = 0; row < m_hits.size(); ++row) {
        const FindHit &hit = m_hits.at(row);
        const QString cells[5] = { hit.folioLabel, hit.zone, hit.where, hit.before, hit.after };
        for (int column = 0; column < 5; ++column)
            m_table->setItem(row, column, new QTableWidgetItem(cells[column]));
    }
    m_table->resizeColumnsToContents();
}

int FindReplaceDialog::runSearch()
{
    m_hits = FindReplace::find(m_document->project(), buildQuery());
    refreshTable();
    if (m_needle->text().isEmpty())
        m_summary->setText(tr("Entrez un texte à chercher."));
    else
        m_summary->setText(tr("%n occurrence(s).", "", int(m_hits.size())));
    m_replaceButton->setEnabled(!m_hits.isEmpty() && !m_replacement->text().isEmpty());
    return int(m_hits.size());
}

int FindReplaceDialog::runReplaceAll()
{
    const FindQuery query = buildQuery();
    m_hits = FindReplace::find(m_document->project(), query);
    if (m_hits.isEmpty() || query.replacement.isEmpty()) {
        refreshTable();
        return 0;
    }

    // Les occurrences sont regroupees par entite : une meme entite peut en
    // porter plusieurs (le repere et la valeur), et il faut alors une seule
    // commande sur elle — deux commandes successives sur la meme entite se
    // marcheraient dessus a l'annulation.
    std::map<QString, QVector<const FindHit *>> parEntite;
    QStringList ordre;
    for (const FindHit &hit : m_hits) {
        const QString clef = hit.folioId + QLatin1Char('/') + hit.entityId;
        if (!parEntite.count(clef))
            ordre.append(clef);
        parEntite[clef].append(&hit);
    }

    int remplacees = 0;
    m_document->pushMacro(tr("Remplacer « %1 » par « %2 »").arg(query.needle, query.replacement),
                          [&] {
        for (const QString &clef : ordre) {
            const auto &groupe = parEntite[clef];
            const QString folioId = groupe.first()->folioId;
            Folio *folio = m_document->project().folio(folioId);
            if (!folio)
                continue;
            const Entity *avant = folio->entity(groupe.first()->entityId);
            if (!avant)
                continue;
            EntityPtr apres = avant->clone();

            for (const FindHit *hit : groupe) {
                if (auto *text = dynamic_cast<TextItem *>(apres.get())) {
                    text->text = hit->after;
                } else if (auto *label = dynamic_cast<Label *>(apres.get())) {
                    label->name = hit->after;
                } else if (auto *wire = dynamic_cast<Wire *>(apres.get())) {
                    wire->number = hit->after;
                    // Le repere remplace est VERROUILLE. Sans cela, la
                    // prochaine regeneration des reperes le recalcule et le
                    // remplacement disparait sans un mot — le pire des
                    // resultats, parce qu'on l'a vu marcher.
                    wire->numberLocked = true;
                } else if (auto *symbol = dynamic_cast<SymbolInstance *>(apres.get())) {
                    symbol->fields.insert(hit->field, hit->after);
                    if (hit->field == QStringLiteral("designation"))
                        symbol->designationLocked = true;
                }
                ++remplacees;
            }

            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folioId, avant->clone(), std::move(apres),
                    tr("Remplacer le texte")));
        }
    });

    m_document->invalidateNetlist();
    m_hits = FindReplace::find(m_document->project(), query);
    refreshTable();
    m_summary->setText(tr("%n remplacement(s) effectué(s) — Ctrl+Z les défait tous.", "",
                          remplacees));
    m_replaceButton->setEnabled(!m_hits.isEmpty());
    return remplacees;
}

} // namespace dsn
