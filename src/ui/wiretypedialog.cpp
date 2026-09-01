#include "wiretypedialog.h"

#include "render/foliopainter.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace dsn {

namespace {

// Colonnes du tableau. Les nommer evite les indices magiques disperses.
enum Column { ColName = 0, ColColor, ColSection, ColLayer, ColStyle, ColWidth, ColCount };

QString styleLabel(const QString &style)
{
    if (style == WireStyle::dashed())
        return QObject::tr("Tirets");
    if (style == WireStyle::dotted())
        return QObject::tr("Pointillés");
    if (style == WireStyle::dashDot())
        return QObject::tr("Mixte");
    return QObject::tr("Continu");
}

} // namespace

WireTypeDialog::WireTypeDialog(const WireTypeSet &types, QWidget *parent)
    : QDialog(parent), m_types(types)
{
    setWindowTitle(tr("Types de fils"));
    resize(720, 420);

    auto *layout = new QVBoxLayout(this);

    auto *hint = new QLabel(tr("Chaque fil du schéma porte un type. Le type gouverne la "
                               "couleur à l'écran et à l'impression, la section reportée "
                               "dans la nomenclature et le calque de l'export DXF."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({ tr("Nom"), tr("Couleur"), tr("Section"), tr("Calque"),
                                         tr("Style"), tr("Épaisseur (mm)") });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table, 1);

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(tr("Ajouter"), this);
    m_remove = new QPushButton(tr("Supprimer"), this);
    auto *norms = new QPushButton(tr("Charger un jeu…"), this);
    auto *menu = new QMenu(norms);
    menu->addAction(tr("Couleurs CEI"), this, [this] { loadNorm(QStringLiteral("iec")); });
    menu->addAction(tr("Usage nord-américain (ANSI)"), this,
                    [this] { loadNorm(QStringLiteral("ansi")); });
    norms->setMenu(menu);
    buttons->addWidget(add);
    buttons->addWidget(m_remove);
    buttons->addWidget(norms);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(box);

    connect(add, &QPushButton::clicked, this, &WireTypeDialog::addType);
    connect(m_remove, &QPushButton::clicked, this, &WireTypeDialog::removeSelected);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!m_updating && item)
            commitRow(item->row());
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == ColColor)
            pickColor(row);
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &WireTypeDialog::updateButtons);

    reload();
}

void WireTypeDialog::reload()
{
    m_updating = true;
    const QList<WireType> all = m_types.all();
    m_table->setRowCount(int(all.size()));
    for (int row = 0; row < all.size(); ++row) {
        const WireType &t = all.at(row);

        auto *name = new QTableWidgetItem(t.name.isEmpty() ? t.id : t.name);
        name->setData(Qt::UserRole, t.id);
        m_table->setItem(row, ColName, name);

        auto *color = new QTableWidgetItem(t.colorName());
        QPixmap swatch(16, 16);
        swatch.fill(FolioPainter::wireTypeColor(t));
        color->setIcon(QIcon(swatch));
        // La couleur se choisit au double-clic, pas en tapant un code hexa.
        color->setFlags(color->flags() & ~Qt::ItemIsEditable);
        color->setToolTip(tr("Double-cliquer pour choisir la couleur"));
        m_table->setItem(row, ColColor, color);

        m_table->setItem(row, ColSection, new QTableWidgetItem(t.crossSection));
        m_table->setItem(row, ColLayer, new QTableWidgetItem(t.layer));

        auto *style = new QTableWidgetItem(styleLabel(t.style));
        style->setFlags(style->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, ColStyle, style);
        auto *combo = new QComboBox(m_table);
        combo->addItem(tr("Continu"), WireStyle::solid());
        combo->addItem(tr("Tirets"), WireStyle::dashed());
        combo->addItem(tr("Pointillés"), WireStyle::dotted());
        combo->addItem(tr("Mixte"), WireStyle::dashDot());
        const int index = combo->findData(t.style.isEmpty() ? WireStyle::solid() : t.style);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this, row] { commitRow(row); });
        m_table->setCellWidget(row, ColStyle, combo);

        m_table->setItem(row, ColWidth, new QTableWidgetItem(QString::number(t.width, 'f', 2)));

        // Le type par defaut est le repli de tous les fils : son identifiant
        // ne se renomme pas, mais son apparence reste modifiable.
        if (t.id == WireTypeSet::defaultId())
            name->setToolTip(tr("Type de repli : il ne peut pas être supprimé."));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_updating = false;
    updateButtons();
}

void WireTypeDialog::commitRow(int row)
{
    QTableWidgetItem *name = m_table->item(row, ColName);
    if (!name)
        return;
    const QString id = name->data(Qt::UserRole).toString();
    const WireType *existing = m_types.type(id);
    if (!existing)
        return;

    WireType t = *existing;
    t.name = name->text();
    if (QTableWidgetItem *item = m_table->item(row, ColSection))
        t.crossSection = item->text().trimmed();
    if (QTableWidgetItem *item = m_table->item(row, ColLayer))
        t.layer = item->text().trimmed();
    if (auto *combo = qobject_cast<QComboBox *>(m_table->cellWidget(row, ColStyle)))
        t.style = combo->currentData().toString();
    if (QTableWidgetItem *item = m_table->item(row, ColWidth)) {
        bool ok = false;
        const double width = item->text().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        // Une epaisseur nulle rendrait le fil invisible : on garde l'ancienne.
        if (ok && width > 0.0)
            t.width = width;
    }
    m_types.insert(t);
}

void WireTypeDialog::pickColor(int row)
{
    QTableWidgetItem *name = m_table->item(row, ColName);
    if (!name)
        return;
    const QString id = name->data(Qt::UserRole).toString();
    const WireType *existing = m_types.type(id);
    if (!existing)
        return;

    const QColor chosen = QColorDialog::getColor(FolioPainter::wireTypeColor(*existing), this,
                                                 tr("Couleur du type « %1 »").arg(existing->name));
    if (!chosen.isValid())
        return;
    WireType t = *existing;
    t.rgb = quint32(chosen.rgb() & 0x00FFFFFFu);
    m_types.insert(t);
    reload();
}

void WireTypeDialog::addType()
{
    // Identifiant fabrique et unique : l'utilisateur nomme le type, il n'a
    // pas a inventer une cle stable.
    int suffix = m_types.count();
    QString id;
    do {
        id = QStringLiteral("type%1").arg(++suffix);
    } while (m_types.contains(id));

    WireType t;
    t.id = id;
    t.name = tr("Nouveau type");
    t.layer = QStringLiteral("FILS_%1").arg(suffix);
    m_types.insert(t);
    reload();
    m_table->selectRow(m_table->rowCount() - 1);
    m_table->editItem(m_table->item(m_table->rowCount() - 1, ColName));
}

void WireTypeDialog::removeSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    QTableWidgetItem *name = m_table->item(row, ColName);
    if (!name)
        return;
    m_types.remove(name->data(Qt::UserRole).toString());
    reload();
}

void WireTypeDialog::loadNorm(const QString &norm)
{
    m_types = WireTypeSet::forNorm(norm);
    reload();
}

void WireTypeDialog::updateButtons()
{
    const int row = m_table->currentRow();
    bool removable = false;
    if (row >= 0) {
        if (QTableWidgetItem *name = m_table->item(row, ColName))
            removable = name->data(Qt::UserRole).toString() != WireTypeSet::defaultId();
    }
    m_remove->setEnabled(removable);
}

} // namespace dsn
