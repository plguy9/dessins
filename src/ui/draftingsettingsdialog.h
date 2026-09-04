// Reglages de dessin — l'equivalent de DSETTINGS et des options d'affichage
// d'AutoCAD, reunis.
//
// Quatre onglets : la grille et sa resolution, les modes d'accrochage aux
// objets, le suivi polaire, et l'affichage. Chaque mode d'accrochage montre
// son propre marqueur en vignette : c'est ainsi qu'on apprend a reconnaitre
// un triangle « milieu » d'un carre « extremite » sans lire la documentation.
//
// L'onglet Affichage porte ce que chacun regle a son confort : la taille et la
// couleur du reticule, l'aspect de la grille — points, carreaux ou croix — le
// fond de la feuille et son pourtour. Ces reglages sont retenus d'une session
// a l'autre (voir ui/appearance.*), les couleurs par theme.
#pragma once

#include "core/snapengine.h"
#include "render/renderstyle.h"

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QToolButton>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace dsn {

// Un bouton qui montre la couleur qu'il porte et ouvre le selecteur au clic.
// Une pastille vaut mieux qu'un nom de couleur : c'est la couleur qu'on
// choisit, pas son code.
class ColorButton : public QToolButton
{
    Q_OBJECT

public:
    explicit ColorButton(const QColor &color, QWidget *parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor &color);

private:
    QColor m_color;
};

class DraftingSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // `style` porte deja le pas de grille, son affichage et tout l'aspect :
    // le passer entier evite d'enumerer les reglages dans la signature, et
    // surtout d'en oublier un a chaque ajout.
    DraftingSettingsDialog(const SnapEngine &engine, const RenderStyle &style,
                           QWidget *parent = nullptr);

    SnapEngine engine() const;
    RenderStyle style() const;
    double gridStep() const;
    bool gridVisible() const;

    // Vrai si l'utilisateur a demandé les valeurs d'origine : la fenetre
    // principale doit alors oublier ce qui etait retenu, pas seulement
    // appliquer le style rendu.
    bool resetRequested() const { return m_reset; }

private:
    QWidget *buildGridTab();
    QWidget *buildObjectSnapTab();
    QWidget *buildPolarTab();
    QWidget *buildDisplayTab();

    SnapEngine m_engine;
    RenderStyle m_style;
    bool m_reset = false;
    QHash<int, QCheckBox *> m_modeBoxes; // SnapMode -> case a cocher
    QCheckBox *m_gridVisible = nullptr;
    QCheckBox *m_gridSnap = nullptr;
    QCheckBox *m_objectSnap = nullptr;
    QCheckBox *m_ortho = nullptr;
    QCheckBox *m_polar = nullptr;
    QCheckBox *m_tracking = nullptr;
    QDoubleSpinBox *m_gridStep = nullptr;
    QDoubleSpinBox *m_polarIncrement = nullptr;

    // ---- affichage ----------------------------------------------------
    QCheckBox *m_showCrosshair = nullptr;
    QSpinBox *m_crosshairPercent = nullptr;
    QSpinBox *m_pickBox = nullptr;
    QCheckBox *m_dynamicInput = nullptr;
    QComboBox *m_gridStyle = nullptr;
    QSpinBox *m_gridMajorEvery = nullptr;
    QCheckBox *m_sheetShadow = nullptr;
    QCheckBox *m_darkSheet = nullptr;
    ColorButton *m_crosshairColor = nullptr;
    ColorButton *m_gridColor = nullptr;
    ColorButton *m_gridMajorColor = nullptr;
    ColorButton *m_sheetColor = nullptr;
    ColorButton *m_backdropColor = nullptr;
};

} // namespace dsn
