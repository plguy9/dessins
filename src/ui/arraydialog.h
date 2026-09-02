// RESEAU (ARRAY) — la matrice de copies d'AutoCAD.
//
// C'est la commande qui fait gagner le plus de temps sur un schema : huit
// departs moteur identiques se posent en un geste au lieu de huit
// copier-coller alignes a la main, et surtout ils sont alignes exactement.
//
// La boite montre le nombre de copies avant de les poser. Sans ce chiffre, on
// tape 20 colonnes au lieu de 2 et on decouvre le resultat par l'annulation.
#pragma once

#include "core/edittools.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace dsn {

class ArrayDialog : public QDialog
{
    Q_OBJECT

public:
    // `bounds` est l'enveloppe de la selection : elle sert a proposer un pas
    // qui ne fait pas se chevaucher les copies, et un centre de rotation qui
    // tombe a cote plutot que dedans.
    ArrayDialog(const QRectF &bounds, QWidget *parent = nullptr);

    ArraySpec spec() const;

private:
    void refreshSummary();

    QRectF m_bounds;

    QComboBox *m_kind = nullptr;
    QWidget *m_rectangularPage = nullptr;
    QWidget *m_polarPage = nullptr;

    QSpinBox *m_columns = nullptr;
    QSpinBox *m_rows = nullptr;
    QDoubleSpinBox *m_columnSpacing = nullptr;
    QDoubleSpinBox *m_rowSpacing = nullptr;

    QSpinBox *m_count = nullptr;
    QDoubleSpinBox *m_totalAngle = nullptr;
    QDoubleSpinBox *m_centerX = nullptr;
    QDoubleSpinBox *m_centerY = nullptr;
    QComboBox *m_rotateItems = nullptr;

    QLabel *m_summary = nullptr;
};

} // namespace dsn
