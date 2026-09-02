#include "arraydialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace dsn {

ArrayDialog::ArrayDialog(const QRectF &bounds, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Réseau"));
    resize(460, 300);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_columns = new QSpinBox(this);
    m_columns->setRange(1, 500);
    m_columns->setValue(3);
    m_rows = new QSpinBox(this);
    m_rows->setRange(1, 500);
    m_rows->setValue(1);

    // Le pas propose est la taille de la selection plus une marge : les
    // copies se suivent sans se chevaucher, ce qui est presque toujours ce
    // qu'on veut et evite un premier essai pour rien.
    m_columnSpacing = new QDoubleSpinBox(this);
    m_columnSpacing->setRange(-10000.0, 10000.0);
    m_columnSpacing->setDecimals(2);
    m_columnSpacing->setSuffix(tr(" mm"));
    m_columnSpacing->setValue(bounds.isValid() ? bounds.width() + 10.0 : 30.0);
    m_rowSpacing = new QDoubleSpinBox(this);
    m_rowSpacing->setRange(-10000.0, 10000.0);
    m_rowSpacing->setDecimals(2);
    m_rowSpacing->setSuffix(tr(" mm"));
    m_rowSpacing->setValue(bounds.isValid() ? bounds.height() + 10.0 : 30.0);

    form->addRow(tr("Colonnes"), m_columns);
    form->addRow(tr("Pas entre colonnes"), m_columnSpacing);
    form->addRow(tr("Lignes"), m_rows);
    form->addRow(tr("Pas entre lignes"), m_rowSpacing);
    layout->addLayout(form);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setProperty("hint", true);
    layout->addWidget(m_summary);
    layout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Créer le réseau"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (QSpinBox *box : { m_columns, m_rows })
        connect(box, &QSpinBox::valueChanged, this, [this] { refreshSummary(); });
    for (QDoubleSpinBox *box : { m_columnSpacing, m_rowSpacing })
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { refreshSummary(); });

    refreshSummary();
}

ArraySpec ArrayDialog::spec() const
{
    ArraySpec s;
    s.columns = m_columns->value();
    s.rows = m_rows->value();
    s.columnSpacing = m_columnSpacing->value();
    s.rowSpacing = m_rowSpacing->value();
    return s;
}

void ArrayDialog::refreshSummary()
{
    const ArraySpec s = spec();
    if (!s.isValid()) {
        m_summary->setText(tr("Réglage sans effet : il faut au moins deux éléments, "
                              "et un pas non nul."));
        return;
    }
    const int copies = s.itemCount() - 1;
    m_summary->setText(tr("%n copie(s) posée(s), l'original resté en place — "
                          "%1 × %2 sur %3 × %4 mm.", "", copies)
                               .arg(s.columns)
                               .arg(s.rows)
                               .arg(std::abs(s.columnSpacing) * (s.columns - 1), 0, 'f', 1)
                               .arg(std::abs(s.rowSpacing) * (s.rows - 1), 0, 'f', 1));
}

} // namespace dsn
