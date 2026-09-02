// RESEAU (ARRAY) — la matrice de copies d'AutoCAD.
//
// C'est la commande qui fait gagner le plus de temps sur un schema : huit
// departs moteur identiques se posent en un geste au lieu de huit
// copier-coller alignes a la main, et surtout ils sont alignes exactement.
//
// Rectangulaire seulement. Le reseau polaire d'AutoCAD — repartir des copies
// en cercle — a ete retire : il n'a aucun usage sur un schema electrique, et
// une commande qu'on garde « au cas ou » encombre le ruban pour rien
// (decision utilisateur, 2026-09-02).
//
// La boite montre le nombre de copies avant de les poser. Sans ce chiffre, on
// tape 20 colonnes au lieu de 2 et on decouvre le resultat par l'annulation.
#pragma once

#include "core/edittools.h"

#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace dsn {

class ArrayDialog : public QDialog
{
    Q_OBJECT

public:
    // `bounds` est l'enveloppe de la selection : elle sert a proposer un pas
    // qui ne fait pas se chevaucher les copies.
    explicit ArrayDialog(const QRectF &bounds, QWidget *parent = nullptr);

    ArraySpec spec() const;

private:
    void refreshSummary();

    QSpinBox *m_columns = nullptr;
    QSpinBox *m_rows = nullptr;
    QDoubleSpinBox *m_columnSpacing = nullptr;
    QDoubleSpinBox *m_rowSpacing = nullptr;
    QLabel *m_summary = nullptr;
};

} // namespace dsn
