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
    s.pageBackground = QColor(0x14, 0x18, 0x16);
    s.sheet = QColor(0x1D, 0x22, 0x20);
    s.sheetShadow = QColor(0, 0, 0, 120);
    s.grid = QColor(0x2A, 0x31, 0x2E);
    s.gridMajor = QColor(0x39, 0x42, 0x3E);
    s.frame = QColor(0xC8, 0xD0, 0xCB);
    s.symbol = QColor(0xE2, 0xE8, 0xE4);
    s.text = QColor(0xE2, 0xE8, 0xE4);
    s.wire = QColor(0x63, 0xB4, 0xEE);
    s.tag = QColor(0x8F, 0xC9, 0x62);
    s.label = QColor(0xC0, 0x8B, 0x62);
    s.snapMarker = QColor(0xE2, 0xEE, 0x4A);
    s.snapGuide = QColor(0xB4, 0xC4, 0x3A);
    return s;
}

} // namespace dsn
