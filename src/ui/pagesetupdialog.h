// Mise en page — l'equivalent du Page Setup Manager d'AutoCAD.
//
// L'apercu montre le folio REEL sous les nouveaux reglages, pas une feuille
// vide : changer de format sans voir si le dessin tient encore, c'est
// decouvrir le debordement a l'impression.
#pragma once

#include "core/folio.h"
#include "core/project.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QRadioButton;
class QSpinBox;

namespace dsn {

// Vignette du folio sous les reglages en cours d'edition.
class PagePreview : public QWidget
{
    Q_OBJECT

public:
    PagePreview(const Project &project, QWidget *parent = nullptr);
    void setFolio(const Folio &folio);
    QSize sizeHint() const override { return QSize(300, 300); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const Project &m_project;
    Folio m_folio;
};

class PageSetupDialog : public QDialog
{
    Q_OBJECT

public:
    PageSetupDialog(const Project &project, const Folio &folio, QWidget *parent = nullptr);

    // Le folio tel que regle. Seuls le format et le cadre sont modifies :
    // le contenu et l'identite du folio sont recopies tels quels.
    Folio result() const;
    bool applyToAllFolios() const;

private:
    void refresh();
    SheetFormat currentFormat() const;

    const Project &m_project;
    Folio m_folio;
    bool m_updating = false;

    QComboBox *m_format = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QRadioButton *m_landscape = nullptr;
    QRadioButton *m_portrait = nullptr;
    QDoubleSpinBox *m_margin = nullptr;
    QDoubleSpinBox *m_bindingMargin = nullptr;
    QSpinBox *m_columns = nullptr;
    QSpinBox *m_rows = nullptr;
    QCheckBox *m_zoneLabels = nullptr;
    QDoubleSpinBox *m_blockWidth = nullptr;
    QDoubleSpinBox *m_blockHeight = nullptr;
    QCheckBox *m_applyAll = nullptr;
    PagePreview *m_preview = nullptr;
};

} // namespace dsn
