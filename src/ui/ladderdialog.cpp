#include "ladderdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dsn {

LadderDialog::LadderDialog(const Folio &folio, QWidget *parent)
    : QDialog(parent), m_folio(folio)
{
    setWindowTitle(tr("Insérer une échelle de commande"));

    auto *layout = new QVBoxLayout(this);

    auto *placement = new QGroupBox(tr("Position et dimensions"), this);
    auto *placementForm = new QFormLayout(placement);

    const QRectF frame = folio.frameRect();

    auto lengthBox = [&](QWidget *parent, double value, double minimum, double maximum) {
        auto *box = new QDoubleSpinBox(parent);
        box->setRange(minimum, maximum);
        box->setDecimals(1);
        box->setSingleStep(2.5);
        box->setSuffix(QStringLiteral(" mm"));
        box->setValue(value);
        return box;
    };

    // L'echelle se pose par defaut en haut a gauche du cadre, avec assez de
    // marge pour les numeros de ligne qui debordent a gauche du rail.
    m_x = lengthBox(placement, frame.left() + 22.0, 0.0, 2000.0);
    m_y = lengthBox(placement, frame.top() + 24.0, 0.0, 2000.0);
    m_width = lengthBox(placement, 150.0, 20.0, 1500.0);
    placementForm->addRow(tr("X du rail gauche"), m_x);
    placementForm->addRow(tr("Y du haut"), m_y);
    placementForm->addRow(tr("Écart entre rails"), m_width);
    layout->addWidget(placement);

    auto *rungBox = new QGroupBox(tr("Lignes"), this);
    auto *rungForm = new QFormLayout(rungBox);

    m_rungs = new QSpinBox(rungBox);
    m_rungs->setRange(1, 200);
    m_rungs->setValue(12);
    rungForm->addRow(tr("Nombre de lignes"), m_rungs);

    m_spacing = lengthBox(rungBox, 18.0, 2.0, 200.0);
    rungForm->addRow(tr("Pas entre lignes"), m_spacing);

    m_firstNumber = new QSpinBox(rungBox);
    m_firstNumber->setRange(0, 99999);
    m_firstNumber->setValue(1);
    rungForm->addRow(tr("Première ligne"), m_firstNumber);

    m_numberStep = new QSpinBox(rungBox);
    m_numberStep->setRange(1, 100);
    m_numberStep->setValue(1);
    m_numberStep->setToolTip(tr("Un pas de 10 donne 10, 20, 30… : cela laisse de la place "
                                "pour intercaler des lignes plus tard"));
    rungForm->addRow(tr("Incrément"), m_numberStep);

    m_numberRungs = new QCheckBox(tr("Numéroter les lignes"), rungBox);
    m_numberRungs->setChecked(true);
    rungForm->addRow(m_numberRungs);

    m_drawRungs = new QCheckBox(tr("Tracer les barreaux"), rungBox);
    m_drawRungs->setToolTip(tr("Sinon, seuls les rails sont posés et chaque ligne se trace "
                               "au fur et à mesure"));
    rungForm->addRow(m_drawRungs);
    layout->addWidget(rungBox);

    auto *railBox = new QGroupBox(tr("Rails d'alimentation"), this);
    auto *railForm = new QFormLayout(railBox);
    m_leftRail = new QLineEdit(QStringLiteral("L1"), railBox);
    m_rightRail = new QLineEdit(QStringLiteral("N"), railBox);
    railForm->addRow(tr("Rail gauche"), m_leftRail);
    railForm->addRow(tr("Rail droit"), m_rightRail);
    layout->addWidget(railBox);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Insérer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    for (QDoubleSpinBox *box : { m_x, m_y, m_width, m_spacing })
        connect(box, &QDoubleSpinBox::valueChanged, this, &LadderDialog::refresh);
    for (QSpinBox *box : { m_rungs, m_firstNumber, m_numberStep })
        connect(box, &QSpinBox::valueChanged, this, &LadderDialog::refresh);

    refresh();
}

LadderSpec LadderDialog::spec() const
{
    LadderSpec spec;
    spec.origin = QPointF(m_x->value(), m_y->value());
    spec.width = m_width->value();
    spec.rungs = m_rungs->value();
    spec.rungSpacing = m_spacing->value();
    spec.firstRungNumber = m_firstNumber->value();
    spec.rungNumberStep = m_numberStep->value();
    spec.leftRailName = m_leftRail->text().trimmed();
    spec.rightRailName = m_rightRail->text().trimmed();
    spec.drawRungs = m_drawRungs->isChecked();
    spec.numberRungs = m_numberRungs->isChecked();
    return spec;
}

void LadderDialog::refresh()
{
    const LadderSpec current = spec();
    const QString warning = LadderBuilder::fitWarning(current, m_folio.frameRect());

    const QString size = tr("Hauteur totale : %1 mm — lignes %2 à %3.")
                                 .arg(current.height(), 0, 'f', 1)
                                 .arg(current.rungNumber(0))
                                 .arg(current.rungNumber(current.rungs - 1));

    // L'avertissement de debordement se voit avant l'insertion : poser
    // cinquante entites hors cadre et le decouvrir a l'impression serait
    // une mauvaise surprise evitable.
    m_summary->setText(warning.isEmpty()
                               ? size
                               : QStringLiteral("%1<br><b style='color:#c0392b'>%2</b>")
                                         .arg(size, warning));
}

} // namespace dsn
