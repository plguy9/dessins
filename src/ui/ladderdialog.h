// Insertion d'une echelle de commande, avec apercu.
#pragma once

#include "core/folio.h"
#include "core/wiretype.h"
#include "rules/ladder.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace dsn {

class LadderDialog : public QDialog
{
    Q_OBJECT

public:
    // Les types de fils du projet sont necessaires pour habiller les rails :
    // une echelle se lit d'abord a la couleur de sa phase et de son neutre.
    LadderDialog(const Folio &folio, const WireTypeSet &wireTypes, QWidget *parent = nullptr);

    LadderSpec spec() const;

private:
    void refresh();

    const Folio &m_folio;
    QDoubleSpinBox *m_x = nullptr;
    QDoubleSpinBox *m_y = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QSpinBox *m_rungs = nullptr;
    QDoubleSpinBox *m_spacing = nullptr;
    QSpinBox *m_firstNumber = nullptr;
    QSpinBox *m_numberStep = nullptr;
    QLineEdit *m_leftRail = nullptr;
    QLineEdit *m_rightRail = nullptr;
    QComboBox *m_leftRailType = nullptr;
    QComboBox *m_rightRailType = nullptr;
    QCheckBox *m_drawRungs = nullptr;
    QCheckBox *m_numberRungs = nullptr;
    QLabel *m_summary = nullptr;
};

} // namespace dsn
