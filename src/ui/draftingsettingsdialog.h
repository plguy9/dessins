// Reglages de dessin — l'equivalent de la commande DSETTINGS d'AutoCAD.
//
// Trois onglets, comme la-bas : la grille et sa resolution, les modes
// d'accrochage aux objets, le suivi polaire. Chaque mode d'accrochage montre
// son propre marqueur en vignette : c'est ainsi qu'on apprend a reconnaitre
// un triangle « milieu » d'un carre « extremite » sans lire la documentation.
#pragma once

#include "core/snapengine.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QDoubleSpinBox;

namespace dsn {

class DraftingSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    DraftingSettingsDialog(const SnapEngine &engine, double gridStep, bool gridVisible,
                           QWidget *parent = nullptr);

    SnapEngine engine() const;
    double gridStep() const;
    bool gridVisible() const;

private:
    QWidget *buildGridTab();
    QWidget *buildObjectSnapTab();
    QWidget *buildPolarTab();

    SnapEngine m_engine;
    QHash<int, QCheckBox *> m_modeBoxes; // SnapMode -> case a cocher
    QCheckBox *m_gridVisible = nullptr;
    QCheckBox *m_gridSnap = nullptr;
    QCheckBox *m_objectSnap = nullptr;
    QCheckBox *m_ortho = nullptr;
    QCheckBox *m_polar = nullptr;
    QDoubleSpinBox *m_gridStep = nullptr;
    QDoubleSpinBox *m_polarIncrement = nullptr;
};

} // namespace dsn
