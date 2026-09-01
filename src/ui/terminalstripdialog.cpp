#include "terminalstripdialog.h"

#include "core/documentcommands.h"
#include "core/entities.h"
#include "rules/crossref.h"

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

enum Column { ColNumber = 0, ColFolio, ColZone, ColWire, ColTarget, ColPin, ColCount };

bool isTerminal(const Project &project, const SymbolInstance &symbol)
{
    const SymbolDefinition *definition = project.library.definition(symbol.definitionId);
    return definition && definition->deviceKind == QLatin1String("terminal");
}

// Ordre de lecture du dossier : folio, puis haut vers bas, puis gauche a
// droite. C'est l'ordre dans lequel un cableur parcourt le schema, donc
// l'ordre dans lequel il s'attend a trouver ses bornes.
bool readingOrder(const Project &project, const TerminalStripDialog::Terminal &a,
                  const TerminalStripDialog::Terminal &b, const QPointF &pa, const QPointF &pb)
{
    const int fa = project.indexOf(a.folioId);
    const int fb = project.indexOf(b.folioId);
    if (fa != fb)
        return fa < fb;
    if (std::abs(pa.y() - pb.y()) > 0.5)
        return pa.y() < pb.y();
    return pa.x() < pb.x();
}

} // namespace

QStringList TerminalStripDialog::blocksOf(const Project &project)
{
    QStringList blocks;
    for (const Folio *folio : project.folios()) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            if (!isTerminal(project, *symbol))
                continue;
            const QString block = symbol->designation();
            if (!block.isEmpty() && !blocks.contains(block))
                blocks.append(block);
        }
    }
    blocks.sort();
    return blocks;
}

QVector<TerminalStripDialog::Terminal> TerminalStripDialog::terminalsOf(const Project &project,
                                                                        const Netlist &netlist,
                                                                        const QString &block)
{
    struct Row {
        Terminal terminal;
        QPointF position;
    };
    QVector<Row> rows;

    for (const Folio *folio : project.folios()) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            if (!isTerminal(project, *symbol))
                continue;
            if (!block.isEmpty() && symbol->designation() != block)
                continue;

            Terminal terminal;
            terminal.entityId = symbol->id();
            terminal.folioId = folio->id();
            terminal.block = symbol->designation();
            terminal.number = symbol->fields.value(QStringLiteral("terminal"));
            terminal.folio = folio->number.isEmpty() ? folio->title : folio->number;
            terminal.zone = folio->zoneAt(symbol->placement.position);

            // Ce qui est raccorde a la borne : le fil, et l'appareil de
            // l'autre cote. C'est cela qu'un cableur cherche.
            const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
            if (definition) {
                for (const Pin &pin : definition->pins) {
                    const Netlist::Net *net = netlist.netOfPin(symbol->id(), pin.number);
                    if (!net)
                        continue;
                    // Le repere vient des fils du potentiel, pas du potentiel
                    // lui-meme : celui-ci ne le porte qu'apres un reperage
                    // automatique, alors que le fil l'a des sa saisie.
                    if (terminal.wireNumber.isEmpty()) {
                        for (const Netlist::WireRef &ref : net->wires) {
                            const Folio *wireFolio = project.folio(ref.folioId);
                            const auto *wire = wireFolio
                                    ? dynamic_cast<const Wire *>(wireFolio->entity(ref.wireId))
                                    : nullptr;
                            if (wire && !wire->number.isEmpty()) {
                                terminal.wireNumber = wire->number;
                                break;
                            }
                        }
                        if (terminal.wireNumber.isEmpty())
                            terminal.wireNumber = net->name;
                    }
                    for (const Netlist::PinRef &other : net->pins) {
                        if (other.symbolId == symbol->id() || other.designation.isEmpty())
                            continue;
                        if (terminal.target.isEmpty()) {
                            terminal.target = other.designation;
                            terminal.targetPin = other.pinNumber;
                        }
                    }
                }
            }
            rows.append({ terminal, symbol->placement.position });
        }
    }

    std::sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
        return readingOrder(project, a.terminal, b.terminal, a.position, b.position);
    });

    QVector<Terminal> out;
    out.reserve(rows.size());
    for (const Row &row : rows)
        out.append(row.terminal);
    return out;
}

TerminalStripDialog::TerminalStripDialog(Document *document, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    setWindowTitle(tr("Éditeur de borniers"));
    resize(820, 480);

    auto *layout = new QVBoxLayout(this);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Bornier"), this));
    m_block = new QComboBox(this);
    m_block->setMinimumWidth(160);
    row->addWidget(m_block);
    row->addStretch(1);
    auto *renumber = new QPushButton(tr("Renuméroter 1, 2, 3…"), this);
    renumber->setToolTip(tr("Numérote les bornes dans l'ordre de lecture du dossier : "
                            "folio, puis de haut en bas."));
    row->addWidget(renumber);
    layout->addLayout(row);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({ tr("Borne"), tr("Folio"), tr("Zone"), tr("Fil"),
                                         tr("Appareil"), tr("Broche") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    // C'est l'appareil raccorde qui merite la place : les autres colonnes ont
    // une largeur naturelle, et un numero de broche etale sur la moitie de la
    // fenetre ne dit rien de plus.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(ColTarget, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Appliquer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Fermer"));
    layout->addWidget(buttons);

    connect(m_block, &QComboBox::currentIndexChanged, this, [this] { reload(); });
    connect(renumber, &QPushButton::clicked, this, &TerminalStripDialog::renumber);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        applyEdits();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Double-clic sur une ligne : aller voir la borne sur le folio. Un bornier
    // se corrige souvent en regardant le schema.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == ColNumber || row < 0 || row >= m_terminals.size())
            return;
        Q_EMIT locateRequested(m_terminals.at(row).folioId, m_terminals.at(row).entityId);
        accept();
    });

    reloadBlocks();
    reload();
}

void TerminalStripDialog::reloadBlocks()
{
    QSignalBlocker blocker(m_block);
    m_block->clear();
    const QStringList blocks = blocksOf(m_document->project());
    for (const QString &block : blocks)
        m_block->addItem(block, block);
    if (blocks.isEmpty())
        m_block->addItem(tr("aucun bornier"), QString());
}

void TerminalStripDialog::reload()
{
    m_updating = true;
    m_terminals = terminalsOf(m_document->project(), m_document->netlist(),
                              m_block->currentData().toString());
    m_table->setRowCount(int(m_terminals.size()));
    for (int row = 0; row < m_terminals.size(); ++row) {
        const Terminal &terminal = m_terminals.at(row);
        m_table->setItem(row, ColNumber, new QTableWidgetItem(terminal.number));
        auto readOnly = [&](int column, const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_table->setItem(row, column, item);
        };
        readOnly(ColFolio, terminal.folio);
        readOnly(ColZone, terminal.zone);
        readOnly(ColWire, terminal.wireNumber);
        readOnly(ColTarget, terminal.target);
        readOnly(ColPin, terminal.targetPin);
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(ColTarget, QHeaderView::Stretch);
    m_updating = false;

    // Les doublons se voient tout de suite : deux bornes du meme numero sur un
    // bornier, c'est une erreur de cablage qui coute cher a l'atelier.
    QStringList seen;
    QStringList duplicates;
    for (const Terminal &terminal : std::as_const(m_terminals)) {
        if (terminal.number.isEmpty())
            continue;
        if (seen.contains(terminal.number) && !duplicates.contains(terminal.number))
            duplicates.append(terminal.number);
        seen.append(terminal.number);
    }
    if (duplicates.isEmpty()) {
        m_summary->setText(tr("%n borne(s). Double-cliquez une ligne pour aller la voir.", "",
                              int(m_terminals.size())));
    } else {
        m_summary->setText(tr("%n borne(s) — repères en double : %1.", "",
                              int(m_terminals.size()))
                                   .arg(duplicates.join(QStringLiteral(", "))));
    }
}

void TerminalStripDialog::renumber()
{
    m_updating = true;
    for (int row = 0; row < m_terminals.size(); ++row) {
        if (auto *item = m_table->item(row, ColNumber))
            item->setText(QString::number(row + 1));
    }
    m_updating = false;
    m_summary->setText(tr("Numérotation proposée. « Appliquer » l'inscrit sur le schéma."));
}

void TerminalStripDialog::applyEdits()
{
    Project &project = m_document->project();
    QVector<QPair<QString, QString>> changes; // entite -> nouveau repere
    for (int row = 0; row < m_terminals.size(); ++row) {
        const QTableWidgetItem *item = m_table->item(row, ColNumber);
        if (!item)
            continue;
        const QString wanted = item->text().trimmed();
        if (wanted == m_terminals.at(row).number)
            continue;
        changes.append({ m_terminals.at(row).entityId, wanted });
    }
    if (changes.isEmpty())
        return;

    // Renumeroter un bornier touche des dizaines de bornes : une seule
    // annulation, sinon revenir en arriere devient impraticable.
    m_document->pushMacro(tr("Renuméroter le bornier"), [&] {
        for (const auto &change : changes) {
            Folio *folio = nullptr;
            Entity *entity = project.findEntity(change.first, &folio);
            auto *symbol = dynamic_cast<SymbolInstance *>(entity);
            if (!symbol || !folio)
                continue;
            auto before = symbol->clone();
            auto after = std::make_unique<SymbolInstance>(*symbol);
            if (change.second.isEmpty())
                after->fields.remove(QStringLiteral("terminal"));
            else
                after->fields.insert(QStringLiteral("terminal"), change.second);
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    project, folio->id(), std::move(before), std::move(after),
                    tr("Renuméroter une borne")));
        }
    });
}

} // namespace dsn
