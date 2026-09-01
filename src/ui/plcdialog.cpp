#include "plcdialog.h"

#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace dsn {

namespace {

// Le libelle d'un type d'entree-sortie. La base stocke un identifiant stable,
// pas une phrase : le fichier ne doit pas changer quand le libelle change.
QString ioTypeLabel(const QString &tag)
{
    if (tag == QLatin1String("entree-tor"))
        return PlcDialog::tr("Entrées TOR");
    if (tag == QLatin1String("sortie-tor"))
        return PlcDialog::tr("Sorties TOR");
    if (tag == QLatin1String("entree-analogique"))
        return PlcDialog::tr("Entrées analogiques");
    if (tag == QLatin1String("sortie-analogique"))
        return PlcDialog::tr("Sorties analogiques");
    if (tag == QLatin1String("mixte-tor"))
        return PlcDialog::tr("Entrées/sorties TOR");
    return tag;
}

} // namespace

PlcDialog::PlcDialog(const PlcDatabase &database, const SymbolInstance *existing, QWidget *parent)
    : QDialog(parent), m_database(database)
{
    setWindowTitle(existing ? tr("Module d'automate") : tr("Insérer un automate"));
    resize(760, 620);

    auto *layout = new QVBoxLayout(this);

    // ---- le choix de la carte -------------------------------------------
    auto *pick = new QFormLayout;
    pick->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_manufacturer = new QComboBox(this);
    m_manufacturer->addItem(tr("Tous les constructeurs"), QString());
    for (const QString &name : database.manufacturers())
        m_manufacturer->addItem(name, name);
    pick->addRow(tr("Constructeur"), m_manufacturer);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("référence, série, type d'entrée…"));
    m_search->setClearButtonEnabled(true);
    pick->addRow(tr("Chercher"), m_search);

    m_moduleBox = new QComboBox(this);
    pick->addRow(tr("Module"), m_moduleBox);

    m_details = new QLabel(this);
    m_details->setWordWrap(true);
    m_details->setProperty("hint", true);
    pick->addRow(QString(), m_details);
    layout->addLayout(pick);

    // ---- l'adresse de depart ---------------------------------------------
    auto *addressRow = new QHBoxLayout;
    addressRow->setSpacing(Theme::space(4));

    auto addSpin = [&](const QString &label, QSpinBox **target, int maximum,
                       const QString &hint) {
        auto *box = new QVBoxLayout;
        auto *caption = new QLabel(label, this);
        Theme::engrave(caption);
        *target = new QSpinBox(this);
        (*target)->setRange(0, maximum);
        (*target)->setToolTip(hint);
        box->addWidget(caption);
        box->addWidget(*target);
        addressRow->addLayout(box);
    };
    addSpin(tr("Rack"), &m_rack, 63, tr("Numéro de châssis, quand le constructeur l'adresse."));
    addSpin(tr("Emplacement"), &m_slot, 63, tr("Position de la carte dans le rack."));
    addSpin(tr("Premier point"), &m_firstPoint, 4095,
            tr("Rang du premier point dans l'espace d'adressage. Les suivants "
               "s'enchaînent — c'est ce qui évite de retaper seize adresses."));

    auto *tagBox = new QVBoxLayout;
    auto *tagCaption = new QLabel(tr("Repère"), this);
    Theme::engrave(tagCaption);
    m_designation = new QLineEdit(this);
    m_designation->setPlaceholderText(tr("-A1"));
    tagBox->addWidget(tagCaption);
    tagBox->addWidget(m_designation);
    addressRow->addLayout(tagBox, 1);
    layout->addLayout(addressRow);

    // ---- l'apercu des points ---------------------------------------------
    auto *previewCaption = new QLabel(tr("Points du module"), this);
    Theme::engrave(previewCaption);
    layout->addWidget(previewCaption);

    m_preview = new QTableWidget(this);
    m_preview->setColumnCount(3);
    m_preview->setHorizontalHeaderLabels(
            { tr("Borne"), tr("Adresse"), tr("Description du point") });
    m_preview->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_preview->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_preview->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_preview->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_preview->verticalHeader()->setVisible(false);
    m_preview->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_preview, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)
            ->setText(existing ? tr("Appliquer") : tr("Insérer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        captureDescriptions();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // ---- reprise d'un module deja pose ------------------------------------
    QString wanted;
    if (existing) {
        wanted = PlcModule::moduleId(*existing);
        m_rack->setValue(PlcModule::rack(*existing));
        m_slot->setValue(PlcModule::slot(*existing));
        m_firstPoint->setValue(PlcModule::firstPoint(*existing));
        m_designation->setText(existing->designation());
        if (const PlcModuleDef *def = database.find(wanted)) {
            for (int i = 0; i < def->points; ++i) {
                const QString text = PlcModule::description(*existing, i);
                if (!text.isEmpty())
                    m_descriptions.insert(i, text);
            }
            const int index = m_manufacturer->findData(def->manufacturer);
            if (index >= 0)
                m_manufacturer->setCurrentIndex(index);
        }
    }

    reloadModules();
    if (!wanted.isEmpty()) {
        const int index = m_moduleBox->findData(wanted);
        if (index >= 0)
            m_moduleBox->setCurrentIndex(index);
    }
    refreshPreview();

    // Le changement de module conserve les descriptions deja saisies : on
    // hesite souvent entre deux cartes apres avoir ecrit la liste des entrees,
    // et les reperdre a chaque essai decourage de comparer.
    connect(m_manufacturer, &QComboBox::currentIndexChanged, this, [this] {
        captureDescriptions();
        reloadModules();
        refreshPreview();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        captureDescriptions();
        reloadModules();
        refreshPreview();
    });
    connect(m_moduleBox, &QComboBox::currentIndexChanged, this, [this] {
        captureDescriptions();
        refreshPreview();
    });
    for (QSpinBox *spin : { m_rack, m_slot, m_firstPoint }) {
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            captureDescriptions();
            refreshPreview();
        });
    }
}

void PlcDialog::reloadModules()
{
    const QString kept = m_moduleBox->currentData().toString();
    m_visible = m_database.search(m_search->text(), m_manufacturer->currentData().toString());

    m_moduleBox->blockSignals(true);
    m_moduleBox->clear();
    for (const PlcModuleDef &module : m_visible) {
        m_moduleBox->addItem(QStringLiteral("%1 — %2 pts — %3")
                                     .arg(module.partNumber)
                                     .arg(module.points)
                                     .arg(ioTypeLabel(module.ioType)),
                             module.id);
    }
    const int index = m_moduleBox->findData(kept);
    if (index >= 0)
        m_moduleBox->setCurrentIndex(index);
    m_moduleBox->blockSignals(false);
}

const PlcModuleDef *PlcDialog::module() const
{
    const QString id = m_moduleBox->currentData().toString();
    return id.isEmpty() ? nullptr : m_database.find(id);
}

int PlcDialog::rack() const { return m_rack->value(); }
int PlcDialog::slot() const { return m_slot->value(); }
int PlcDialog::firstPoint() const { return m_firstPoint->value(); }
QString PlcDialog::designation() const { return m_designation->text().trimmed(); }

void PlcDialog::captureDescriptions()
{
    if (!m_preview)
        return;
    for (int row = 0; row < m_preview->rowCount(); ++row) {
        const QTableWidgetItem *item = m_preview->item(row, 2);
        if (!item)
            continue;
        const QString text = item->text().trimmed();
        if (text.isEmpty())
            m_descriptions.remove(row);
        else
            m_descriptions.insert(row, text);
    }
}

void PlcDialog::refreshPreview()
{
    const PlcModuleDef *def = module();
    if (!def) {
        m_details->setText(tr("Aucun module ne correspond à cette recherche."));
        m_preview->setRowCount(0);
        return;
    }

    // La tension n'est ajoutee que si la description ne la porte pas deja :
    // les fiches constructeur la repetent souvent, et « 120 V c.a. — 120 V
    // c.a. » donne l'impression que le logiciel ne se relit pas.
    const bool repeats = !def->voltage.isEmpty() && def->description.contains(def->voltage);
    m_details->setText(tr("%1 — %2. %3%4")
                               .arg(def->manufacturer, def->series, def->description,
                                    (def->voltage.isEmpty() || repeats)
                                            ? QString()
                                            : tr(" — %1").arg(def->voltage)));

    // Les adresses de l'apercu sont calculees exactement comme celles du
    // module pose : c'est la meme fonction, donc ce qu'on lit ici est ce
    // qu'on aura sur le folio.
    m_preview->setRowCount(def->points);
    const QFont mono = Theme::monoFont();
    for (int i = 0; i < def->points; ++i) {
        const QString terminal = QStringLiteral("%1").arg(i, 2, 10, QLatin1Char('0'));
        const QString address = PlcAddress::format(def->addressFormat, m_rack->value(),
                                                   m_slot->value(),
                                                   m_firstPoint->value() + i, def->bitsPerByte);

        auto *terminalItem = new QTableWidgetItem(terminal);
        terminalItem->setFlags(terminalItem->flags() & ~Qt::ItemIsEditable);
        terminalItem->setFont(mono);
        m_preview->setItem(i, 0, terminalItem);

        auto *addressItem = new QTableWidgetItem(address);
        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
        addressItem->setFont(mono);
        m_preview->setItem(i, 1, addressItem);

        m_preview->setItem(i, 2, new QTableWidgetItem(m_descriptions.value(i)));
    }
}

void PlcDialog::applyTo(SymbolInstance &symbol) const
{
    const PlcModuleDef *def = module();
    if (!def)
        return;
    PlcModule::configure(symbol, *def, rack(), slot(), firstPoint());
    if (!designation().isEmpty()) {
        symbol.setDesignation(designation());
        // Un repere saisi a la main n'est jamais repris par le reperage
        // automatique : c'est l'invariant du logiciel, il vaut ici aussi.
        symbol.designationLocked = true;
    }
    for (int i = 0; i < def->points; ++i)
        PlcModule::setDescription(symbol, i, m_descriptions.value(i));
}

} // namespace dsn
