// Gestionnaire des types de fils — l'equivalent du Wire Type Manager
// d'AutoCAD Electrical.
//
// Un type porte la couleur, la section, le calque d'export et le style de
// trait. Le regler ici plutot que fil par fil est ce qui rend un changement
// de norme ou de section faisable sur un dossier deja dessine.
#pragma once

#include "core/wiretype.h"

#include <QDialog>

class QPushButton;
class QTableWidget;

namespace dsn {

class WireTypeDialog : public QDialog
{
    Q_OBJECT

public:
    WireTypeDialog(const WireTypeSet &types, QWidget *parent = nullptr);

    WireTypeSet result() const { return m_types; }

private:
    void reload();
    void commitRow(int row);
    void addType();
    void removeSelected();
    void loadNorm(const QString &norm);
    void pickColor(int row);
    void updateButtons();

    WireTypeSet m_types;
    bool m_updating = false;

    QTableWidget *m_table = nullptr;
    QPushButton *m_remove = nullptr;
};

} // namespace dsn
