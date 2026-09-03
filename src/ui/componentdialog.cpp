#include "componentdialog.h"

#include "rules/crossref.h"

#include <QCheckBox>
#include <QJsonDocument>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace dsn {

namespace {

QString fieldOf(const SymbolInstance &symbol, const char *key)
{
    return symbol.fields.value(QString::fromLatin1(key));
}

void setField(SymbolInstance &symbol, const char *key, const QString &value)
{
    const QString name = QString::fromLatin1(key);
    // Un champ vide est retire plutot que stocke vide : le fichier reste
    // lisible et la comparaison de deux appareils reste franche.
    if (value.trimmed().isEmpty())
        symbol.fields.remove(name);
    else
        symbol.fields.insert(name, value.trimmed());
}

} // namespace

ComponentDialog::ComponentDialog(const Project &project, const SymbolInstance &symbol,
                                 const Catalog &catalog, bool insertion, QWidget *parent)
    : QDialog(parent), m_project(project), m_symbol(symbol), m_catalog(catalog)
{
    const SymbolDefinition *definition = project.library.definition(symbol.definitionId);
    setWindowTitle(insertion ? tr("Insérer un composant") : tr("Éditer le composant"));
    resize(620, 560);

    auto *layout = new QVBoxLayout(this);

    if (definition) {
        auto *header = new QLabel(tr("<b>%1</b> — %2").arg(definition->name, definition->category),
                                  this);
        header->setWordWrap(true);
        layout->addWidget(header);
    }

    // ---- repere --------------------------------------------------------
    auto *tagBox = new QGroupBox(tr("Repère"), this);
    auto *tagForm = new QFormLayout(tagBox);
    m_designation = new QLineEdit(symbol.designation(), tagBox);
    m_designation->setPlaceholderText(tr("vide = attribué par le repérage automatique"));
    tagForm->addRow(tr("Repère"), m_designation);
    m_locked = new QCheckBox(tr("Figé — le repérage automatique ne le remplacera pas"), tagBox);
    m_locked->setChecked(symbol.designationLocked);
    tagForm->addRow(QString(), m_locked);
    // Saisir un repere a la main, c'est vouloir le garder : la case suit.
    connect(m_designation, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty())
            m_locked->setChecked(true);
    });
    layout->addWidget(tagBox);

    // ---- description et reperage d'armoire -----------------------------
    auto *descBox = new QGroupBox(tr("Description"), this);
    auto *descForm = new QFormLayout(descBox);
    m_description = new QLineEdit(fieldOf(symbol, "description"), descBox);
    m_description->setPlaceholderText(definition ? definition->name : QString());
    descForm->addRow(tr("Description"), m_description);
    m_value = new QLineEdit(fieldOf(symbol, "value"), descBox);
    m_value->setPlaceholderText(tr("calibre, puissance, valeur…"));
    descForm->addRow(tr("Valeur"), m_value);
    m_installation = new QLineEdit(fieldOf(symbol, "installation"), descBox);
    m_installation->setPlaceholderText(tr("=  code d'installation"));
    descForm->addRow(tr("Installation"), m_installation);
    m_location = new QLineEdit(fieldOf(symbol, "location"), descBox);
    m_location->setPlaceholderText(tr("+  armoire, coffret, pupitre…"));
    descForm->addRow(tr("Emplacement"), m_location);
    // Secteur et boucle : ce qui compose le repere d'un instrument
    // (« 022TT8917A »). Ils ne servent qu'aux formats qui les demandent, et
    // ne coutent rien a qui dessine un schema de commande.
    m_sector = new QLineEdit(fieldOf(symbol, "sector"), descBox);
    m_sector->setPlaceholderText(tr("%C  aire de l'usine — vide = celui de la planche"));
    descForm->addRow(tr("Secteur"), m_sector);
    m_loop = new QLineEdit(fieldOf(symbol, "loop"), descBox);
    m_loop->setPlaceholderText(tr("%B  numéro de boucle, ex. 8917"));
    descForm->addRow(tr("Boucle"), m_loop);
    layout->addWidget(descBox);

    // ---- catalogue -----------------------------------------------------
    auto *catBox = new QGroupBox(tr("Données catalogue"), this);
    auto *catForm = new QFormLayout(catBox);
    m_manufacturer = new QLineEdit(fieldOf(symbol, "manufacturer"), catBox);
    catForm->addRow(tr("Fabricant"), m_manufacturer);

    auto *partRow = new QHBoxLayout;
    m_partNumber = new QLineEdit(fieldOf(symbol, "partNumber"), catBox);
    partRow->addWidget(m_partNumber, 1);
    auto *lookup = new QPushButton(tr("Chercher…"), catBox);
    lookup->setToolTip(tr("Ouvrir le catalogue fabricant sur la famille de ce symbole"));
    partRow->addWidget(lookup);
    catForm->addRow(tr("Référence"), partRow);
    connect(lookup, &QPushButton::clicked, this, &ComponentDialog::lookupCatalog);
    layout->addWidget(catBox);

    // ---- rattachement parent / enfant ----------------------------------
    auto *linkBox = new QGroupBox(tr("Appareil"), this);
    auto *linkForm = new QFormLayout(linkBox);
    m_parent = new QComboBox(linkBox);
    m_parent->addItem(tr("Appareil autonome"), QString());
    // Les appareils deja poses, pour rattacher un contact a sa bobine. Un
    // contact d'un contacteur n'est pas un appareil : c'est un bloc de plus
    // du meme appareil, et la nomenclature ne doit le compter qu'une fois.
    QStringList seen;
    for (const Folio *folio : project.folios()) {
        for (const SymbolInstance *other : folio->entitiesOfType<SymbolInstance>()) {
            if (other->deviceGroup.isEmpty() || seen.contains(other->deviceGroup))
                continue;
            seen.append(other->deviceGroup);
            const QString label = other->designation().isEmpty()
                    ? other->deviceGroup
                    : tr("%1 — folio %2").arg(other->designation(), folio->number);
            m_parent->addItem(label, other->deviceGroup);
        }
    }
    if (!symbol.deviceGroup.isEmpty() && !seen.contains(symbol.deviceGroup))
        m_parent->addItem(symbol.deviceGroup, symbol.deviceGroup);
    m_parent->setCurrentIndex(std::max(0, m_parent->findData(symbol.deviceGroup)));
    m_parent->setToolTip(tr("Rattacher ce symbole à un appareil déjà posé : la bobine "
                            "et ses contacts ne font qu'un appareil."));
    linkForm->addRow(tr("Rattaché à"), m_parent);

    m_crossReferences = new QLabel(linkBox);
    m_crossReferences->setWordWrap(true);
    linkForm->addRow(tr("Autres blocs"), m_crossReferences);

    m_pins = new QLabel(linkBox);
    m_pins->setWordWrap(true);
    if (definition) {
        QStringList numbers;
        for (const Pin &pin : definition->pins)
            numbers.append(pin.number);
        m_pins->setText(numbers.isEmpty() ? tr("aucune") : numbers.join(QStringLiteral(", ")));
    }
    linkForm->addRow(tr("Broches"), m_pins);
    layout->addWidget(linkBox);

    connect(m_parent, &QComboBox::currentIndexChanged, this,
            &ComponentDialog::refreshCrossReferences);
    refreshCrossReferences();

    layout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(insertion ? tr("Poser") : tr("Appliquer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_designation->setFocus();
    m_designation->selectAll();
}

QString ComponentDialog::familyOf() const
{
    const SymbolDefinition *definition = m_project.library.definition(m_symbol.definitionId);
    return definition ? definition->deviceKind : QString();
}

void ComponentDialog::refreshCrossReferences()
{
    const QString group = m_parent->currentData().toString();
    if (group.isEmpty()) {
        m_crossReferences->setText(tr("aucun — cet appareil n'a qu'un bloc"));
        return;
    }

    QStringList sites;
    for (const Folio *folio : m_project.folios()) {
        for (const SymbolInstance *other : folio->entitiesOfType<SymbolInstance>()) {
            if (other->deviceGroup != group || other->id() == m_symbol.id())
                continue;
            const QString where = CrossReference::locationOf(*folio, other->placement.position);
            if (!where.isEmpty() && !sites.contains(where))
                sites.append(where);
        }
    }
    sites.sort();
    m_crossReferences->setText(sites.isEmpty() ? tr("aucun autre bloc posé pour l'instant")
                                               : sites.join(QStringLiteral(", ")));
}

void ComponentDialog::lookupCatalog()
{
    CatalogDialog dialog(m_catalog, familyOf(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const CatalogItem item = dialog.selected();
    if (!item.isValid())
        return;

    m_manufacturer->setText(item.manufacturer);
    m_partNumber->setText(item.partNumber);
    // La description du catalogue ne remplace pas celle qu'on a saisie : ce
    // qui vient de la main de l'utilisateur prime toujours.
    if (m_description->text().trimmed().isEmpty())
        m_description->setText(item.description);
    if (m_value->text().trimmed().isEmpty())
        m_value->setText(item.rating);
}

SymbolInstance ComponentDialog::result() const
{
    SymbolInstance out = m_symbol;
    const QString designation = m_designation->text().trimmed();
    if (designation.isEmpty())
        out.fields.remove(QStringLiteral("designation"));
    else
        out.setDesignation(designation);
    out.designationLocked = m_locked->isChecked() && !designation.isEmpty();

    setField(out, "description", m_description->text());
    setField(out, "value", m_value->text());
    setField(out, "installation", m_installation->text());
    setField(out, "location", m_location->text());
    setField(out, "sector", m_sector->text());
    setField(out, "loop", m_loop->text());
    setField(out, "manufacturer", m_manufacturer->text());
    setField(out, "partNumber", m_partNumber->text());
    out.deviceGroup = m_parent->currentData().toString();
    return out;
}

// --------------------------------------------------------------------------

CatalogDialog::CatalogDialog(const Catalog &catalog, const QString &deviceKind, QWidget *parent)
    : QDialog(parent), m_catalog(catalog), m_deviceKind(deviceKind)
{
    setWindowTitle(tr("Catalogue fabricant"));
    resize(760, 460);

    auto *layout = new QVBoxLayout(this);

    auto *row = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Chercher un fabricant, une référence, un calibre…"));
    m_search->setClearButtonEnabled(true);
    row->addWidget(m_search, 1);

    m_family = new QComboBox(this);
    m_family->addItem(tr("Toutes les familles"), QString());
    for (const QString &kind : catalog.deviceKinds())
        m_family->addItem(kind, kind);
    // On part de la famille du symbole, mais on peut en sortir : un catalogue
    // reel ne colle jamais parfaitement a nos familles.
    const int index = m_family->findData(deviceKind);
    m_family->setCurrentIndex(index >= 0 ? index : 0);
    row->addWidget(m_family);
    layout->addLayout(row);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({ tr("Fabricant"), tr("Référence"), tr("Description"),
                                         tr("Calibre"), tr("Tension") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Affecter"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    layout->addWidget(buttons);

    auto accept = [this] {
        const int row = m_table->currentRow();
        if (row < 0)
            return;
        m_selected = CatalogItem::fromJson(
                QJsonDocument::fromJson(m_table->item(row, 1)->data(Qt::UserRole).toByteArray())
                        .object());
        QDialog::accept();
    };
    connect(buttons, &QDialogButtonBox::accepted, this, accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [accept](int, int) { accept(); });
    connect(m_search, &QLineEdit::textChanged, this, &CatalogDialog::refresh);
    connect(m_family, &QComboBox::currentIndexChanged, this, &CatalogDialog::refresh);

    refresh();
    m_search->setFocus();
}

void CatalogDialog::refresh()
{
    const QList<CatalogItem> items =
            m_catalog.search(m_search->text(), m_family->currentData().toString());
    m_table->setRowCount(int(items.size()));
    for (int row = 0; row < items.size(); ++row) {
        const CatalogItem &item = items.at(row);
        m_table->setItem(row, 0, new QTableWidgetItem(item.manufacturer));
        auto *part = new QTableWidgetItem(item.partNumber);
        // L'article complet voyage avec sa ligne : le tri ou le filtrage ne
        // peuvent alors pas desynchroniser la selection de sa donnee.
        part->setData(Qt::UserRole, QJsonDocument(item.toJson()).toJson(QJsonDocument::Compact));
        m_table->setItem(row, 1, part);
        m_table->setItem(row, 2, new QTableWidgetItem(item.description));
        m_table->setItem(row, 3, new QTableWidgetItem(item.rating));
        m_table->setItem(row, 4, new QTableWidgetItem(item.voltage));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    if (m_table->rowCount() > 0)
        m_table->selectRow(0);
}

} // namespace dsn
