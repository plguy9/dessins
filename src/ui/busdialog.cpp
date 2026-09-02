#include "busdialog.h"

#include "core/wiretype.h"
#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dsn {

namespace {

QStringList splitNames(const QString &text)
{
    QStringList names;
    for (const QString &part : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        names << part.trimmed();
    return names;
}

} // namespace

BusDialog::BusDialog(const WireTypeSet &types, const QString &currentTypeId, double gridStep,
                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Fil multiple"));
    resize(460, 320);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_count = new QSpinBox(this);
    m_count->setRange(2, 12);
    m_count->setValue(3);

    m_spacing = new QDoubleSpinBox(this);
    m_spacing->setRange(0.5, 100.0);
    m_spacing->setDecimals(2);
    m_spacing->setSuffix(tr(" mm"));
    // Le pas propose est un pas de grille : un conducteur qui tombe entre
    // deux points de grille ne peut plus recevoir d'appareil accroche.
    m_spacing->setValue(gridStep > 0.0 ? gridStep : 5.0);
    m_spacing->setSingleStep(gridStep > 0.0 ? gridStep : 1.0);

    m_names = new QLineEdit(QStringLiteral("L1, L2, L3"), this);
    m_names->setPlaceholderText(tr("L1, L2, L3 — séparés par des virgules"));

    m_type = new QComboBox(this);
    for (const WireType &type : types.all())
        m_type->addItem(type.name.isEmpty() ? type.id : type.name, type.id);
    const int current = m_type->findData(currentTypeId);
    if (current >= 0)
        m_type->setCurrentIndex(current);

    form->addRow(tr("Conducteurs"), m_count);
    form->addRow(tr("Pas"), m_spacing);
    form->addRow(tr("Noms"), m_names);
    form->addRow(tr("Type de fil"), m_type);
    layout->addLayout(form);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setProperty("hint", true);
    layout->addWidget(m_summary);
    layout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Tracer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_count, &QSpinBox::valueChanged, this, [this] { refreshSummary(); });
    connect(m_spacing, &QDoubleSpinBox::valueChanged, this, [this] { refreshSummary(); });
    connect(m_names, &QLineEdit::textChanged, this, [this] { refreshSummary(); });
    refreshSummary();
}

BusSpec BusDialog::spec() const
{
    BusSpec spec;
    spec.count = m_count->value();
    spec.spacing = m_spacing->value();
    spec.conductors = splitNames(m_names->text());
    return spec;
}

QString BusDialog::wireTypeId() const { return m_type->currentData().toString(); }

void BusDialog::refreshSummary()
{
    const BusSpec current = spec();
    const int named = int(current.conductors.size());
    const double width = current.spacing * (current.count - 1);

    QString text = tr("%n conducteur(s) sur %1 mm de large, du côté bas d'une horizontale "
                      "et du côté droit d'une verticale.", "", current.count)
                           .arg(width, 0, 'f', 1);
    if (named < current.count) {
        // Un conducteur anonyme n'est pas une erreur — la netlist le raccorde
        // alors par rang — mais il faut le dire : c'est ce qui distingue un
        // bus qui se raccorde par nom d'un bus qui se raccorde par position.
        text += QLatin1Char(' ')
                + tr("%n conducteur(s) sans nom : ceux-là se raccorderont par position.", "",
                     current.count - named);
    } else if (named > current.count) {
        text += QLatin1Char(' ')
                + tr("%n nom(s) en trop, ignoré(s).", "", named - current.count);
    }
    m_summary->setText(text);
}

} // namespace dsn
