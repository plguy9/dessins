// FIL MULTIPLE (BUS) — la boite de reglage du trace triphase.
//
// Elle ne trace rien : elle arme le canevas, qui trace ensuite comme pour un
// fil ordinaire. C'est ce qui fait que le bus herite d'ortho, des accrochages
// et de la cote tapee sans une ligne de code en plus.
//
// La boite montre les noms des conducteurs dans un champ unique, separes par
// des virgules : « L1, L2, L3 ». Trois champs separes obligeraient a les
// redessiner quand on passe de trois a cinq conducteurs, et un tableau serait
// disproportionne pour trois mots.
#pragma once

#include "core/wiretools.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace dsn {

class WireTypeSet;

class BusDialog : public QDialog
{
    Q_OBJECT

public:
    // `gridStep` sert de pas propose : un bus dont les conducteurs ne tombent
    // pas sur la grille rend impossible d'y raccorder un appareil accroche.
    BusDialog(const WireTypeSet &types, const QString &currentTypeId, double gridStep,
              QWidget *parent = nullptr);

    BusSpec spec() const;
    QString wireTypeId() const;

private:
    void refreshSummary();

    QSpinBox *m_count = nullptr;
    QDoubleSpinBox *m_spacing = nullptr;
    QLineEdit *m_names = nullptr;
    QComboBox *m_type = nullptr;
    QLabel *m_summary = nullptr;
};

} // namespace dsn
