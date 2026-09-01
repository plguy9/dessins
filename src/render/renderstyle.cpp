#include "renderstyle.h"

namespace dsn {

RenderStyle RenderStyle::screen() { return RenderStyle(); }

RenderStyle RenderStyle::print()
{
    RenderStyle s;
    // Sur papier, ni grille ni ombre ni fond : seulement le dessin.
    s.showGrid = false;
    s.showSheetShadow = false;
    s.showUnconnectedPins = false;
    s.showCrosshair = false;   // le reticule est une aide a l'ecran, pas du dessin
    s.showDynamicInput = false;
    s.pageBackground = QColor(255, 255, 255);
    // Le noir pur passe mieux a l'impression que les couleurs d'ecran, mais on
    // garde le bleu des fils : c'est une convention de lecture, pas un artifice.
    s.symbol = QColor(0, 0, 0);
    s.text = QColor(0, 0, 0);
    s.frame = QColor(0, 0, 0);
    s.tag = QColor(0x14, 0x4D, 0x20);
    return s;
}

RenderStyle RenderStyle::screenDark()
{
    RenderStyle s;
    // Les neutres du dessin sont ceux du theme : legerement bleutes, jamais
    // verts. La feuille est nettement plus claire que le fond — c'est ce qui
    // la fait flotter — et l'ombre est franche pour la meme raison.
    s.pageBackground = QColor(0x0A, 0x0D, 0x0F);
    s.sheet = QColor(0x1A, 0x20, 0x24);
    s.sheetShadow = QColor(0, 0, 0, 160);
    s.grid = QColor(0x26, 0x2D, 0x33);
    s.gridMajor = QColor(0x35, 0x3E, 0x45);
    s.frame = QColor(0xC5, 0xCF, 0xD5);
    s.symbol = QColor(0xE4, 0xEA, 0xEE);
    s.text = QColor(0xE4, 0xEA, 0xEE);
    s.wire = QColor(0x5F, 0xB6, 0xF0);
    s.tag = QColor(0x8F, 0xC9, 0x62);
    s.label = QColor(0xC0, 0x8B, 0x62);
    s.snapMarker = QColor(0xE2, 0xEE, 0x4A);
    s.snapGuide = QColor(0xB4, 0xC4, 0x3A);
    s.crosshair = QColor(0x69, 0x77, 0x80);
    s.lightenDarkWires = true;
    return s;
}

} // namespace dsn
