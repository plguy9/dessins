#include "arraydialog.h"

#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace dsn {

ArrayDialog::ArrayDialog(const QRectF &bounds, QWidget *parent)
    : QDialog(parent), m_bounds(bounds)
{
    setWindowTitle(tr("Réseau"));
    resize(480, 420);

    auto *layout = new QVBoxLayout(this);

    m_kind = new QComboBox(this);
    m_kind->addItem(tr("Rectangulaire — en lignes et colonnes"),
                    int(ArraySpec::Kind::Rectangular));
    m_kind->addItem(tr("Polaire — autour d'un centre"), int(ArraySpec::Kind::Polar));
    auto *kindRow = new QFormLayout;
    kindRow->addRow(tr("Type"), m_kind);
    layout->addLayout(kindRow);

    auto *pages = new QStackedWidget(this);
    layout->addWidget(pages);

    // ---- rectangulaire ----------------------------------------------------
    m_rectangularPage = new QWidget(this);
    auto *rect = new QFormLayout(m_rectangularPage);
    m_columns = new QSpinBox(m_rectangularPage);
    m_columns->setRange(1, 500);
    m_columns->setValue(3);
    m_rows = new QSpinBox(m_rectangularPage);
    m_rows->setRange(1, 500);
    m_rows->setValue(1);

    // Le pas propose est la taille de la selection plus une marge : les
    // copies se suivent sans se chevaucher, ce qui est presque toujours ce
    // qu'on veut et evite un premier essai pour rien.
    m_columnSpacing = new QDoubleSpinBox(m_rectangularPage);
    m_columnSpacing->setRange(-10000.0, 10000.0);
    m_columnSpacing->setDecimals(2);
    m_columnSpacing->setSuffix(tr(" mm"));
    m_columnSpacing->setValue(bounds.isValid() ? bounds.width() + 10.0 : 30.0);
    m_rowSpacing = new QDoubleSpinBox(m_rectangularPage);
    m_rowSpacing->setRange(-10000.0, 10000.0);
    m_rowSpacing->setDecimals(2);
    m_rowSpacing->setSuffix(tr(" mm"));
    m_rowSpacing->setValue(bounds.isValid() ? bounds.height() + 10.0 : 30.0);

    rect->addRow(tr("Colonnes"), m_columns);
    rect->addRow(tr("Pas entre colonnes"), m_columnSpacing);
    rect->addRow(tr("Lignes"), m_rows);
    rect->addRow(tr("Pas entre lignes"), m_rowSpacing);
    pages->addWidget(m_rectangularPage);

    // ---- polaire ----------------------------------------------------------
    m_polarPage = new QWidget(this);
    auto *polar = new QFormLayout(m_polarPage);
    m_count = new QSpinBox(m_polarPage);
    m_count->setRange(2, 500);
    m_count->setValue(6);
    m_totalAngle = new QDoubleSpinBox(m_polarPage);
    m_totalAngle->setRange(-360.0, 360.0);
    m_totalAngle->setDecimals(1);
    m_totalAngle->setSuffix(tr(" °"));
    m_totalAngle->setValue(360.0);

    m_centerX = new QDoubleSpinBox(m_polarPage);
    m_centerX->setRange(-10000.0, 10000.0);
    m_centerX->setDecimals(2);
    m_centerX->setSuffix(tr(" mm"));
    m_centerY = new QDoubleSpinBox(m_polarPage);
    m_centerY->setRange(-10000.0, 10000.0);
    m_centerY->setDecimals(2);
    m_centerY->setSuffix(tr(" mm"));
    // Le centre propose tombe a cote de la selection, pas dedans : un reseau
    // polaire centre sur lui-meme empile ses copies au meme endroit.
    const QPointF suggested = bounds.isValid()
            ? QPointF(bounds.center().x(), bounds.center().y() + bounds.height() + 30.0)
            : QPointF(150.0, 150.0);
    m_centerX->setValue(suggested.x());
    m_centerY->setValue(suggested.y());

    m_rotateItems = new QComboBox(m_polarPage);
    m_rotateItems->addItem(tr("Oui — les copies suivent le rayon"), true);
    m_rotateItems->addItem(tr("Non — les copies restent droites"), false);

    polar->addRow(tr("Nombre"), m_count);
    polar->addRow(tr("Angle balayé"), m_totalAngle);
    polar->addRow(tr("Centre X"), m_centerX);
    polar->addRow(tr("Centre Y"), m_centerY);
    polar->addRow(tr("Faire pivoter"), m_rotateItems);
    pages->addWidget(m_polarPage);

    // ---- le compte --------------------------------------------------------
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setProperty("hint", true);
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Créer le réseau"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this, pages](int index) {
        pages->setCurrentIndex(index);
        refreshSummary();
    });
    for (QSpinBox *box : { m_columns, m_rows, m_count })
        connect(box, &QSpinBox::valueChanged, this, [this] { refreshSummary(); });
    for (QDoubleSpinBox *box : { m_columnSpacing, m_rowSpacing, m_totalAngle })
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { refreshSummary(); });

    refreshSummary();
}

ArraySpec ArrayDialog::spec() const
{
    ArraySpec s;
    s.kind = ArraySpec::Kind(m_kind->currentData().toInt());
    s.columns = m_columns->value();
    s.rows = m_rows->value();
    s.columnSpacing = m_columnSpacing->value();
    s.rowSpacing = m_rowSpacing->value();
    s.count = m_count->value();
    s.totalAngle = m_totalAngle->value();
    s.center = QPointF(m_centerX->value(), m_centerY->value());
    s.rotateItems = m_rotateItems->currentData().toBool();
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
    if (s.kind == ArraySpec::Kind::Rectangular) {
        m_summary->setText(tr("%n copie(s) posée(s), l'original resté en place — "
                              "%1 × %2 sur %3 × %4 mm.", "", copies)
                                   .arg(s.columns)
                                   .arg(s.rows)
                                   .arg(std::abs(s.columnSpacing) * (s.columns - 1), 0, 'f', 1)
                                   .arg(std::abs(s.rowSpacing) * (s.rows - 1), 0, 'f', 1));
        return;
    }
    m_summary->setText(tr("%n copie(s) posée(s), l'original resté en place — un élément "
                          "tous les %1 °.", "", copies)
                               .arg(s.totalAngle / (std::abs(std::abs(s.totalAngle) - 360.0) < 0.001
                                                            ? s.count
                                                            : std::max(1, s.count - 1)),
                                    0, 'f', 1));
}

} // namespace dsn
