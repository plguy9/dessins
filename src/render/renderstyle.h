// Style de rendu : couleurs, epaisseurs, ce qu'on affiche.
//
// Un seul jeu de reglages sert l'ecran et le papier. Le mode impression n'est
// pas un autre moteur, seulement d'autres valeurs : sans quoi ce qu'on voit et
// ce qu'on imprime finissent par diverger.
#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace dsn {

struct RenderStyle {
    // Couleurs
    QColor pageBackground{ 0x33, 0x36, 0x35 }; // autour de la feuille, a l'ecran
    QColor sheet{ 0xFF, 0xFF, 0xFF };
    QColor sheetShadow{ 0, 0, 0, 60 };
    QColor grid{ 0xDD, 0xE1, 0xDD };
    QColor gridMajor{ 0xC3, 0xC9, 0xC3 };
    QColor frame{ 0x22, 0x26, 0x24 };
    QColor wire{ 0x0A, 0x5C, 0x9E };
    QColor symbol{ 0x15, 0x1A, 0x18 };
    QColor text{ 0x15, 0x1A, 0x18 };
    QColor tag{ 0x1F, 0x6B, 0x2E };      // reperes de fil et designations
    QColor label{ 0x7A, 0x4A, 0x2B };    // etiquettes de potentiel et renvois
    QColor selection{ 0xE8, 0x8B, 0x0B };
    QColor highlight{ 0xE8, 0x40, 0x40 }; // potentiel mis en evidence
    QColor pinMarker{ 0xC0, 0x50, 0x20 };

    // Epaisseurs, en millimetres
    double wireWidth = 0.35;
    double symbolWidth = 0.25;
    double frameWidth = 0.5;
    double gridDotWidth = 0.12;

    // Contenu affiche
    bool showGrid = true;
    bool showFrame = true;
    bool showZoneLabels = true;
    bool showTitleBlock = true;
    bool showWireNumbers = true;
    bool showDesignations = true;
    bool showValues = true;
    bool showPinNumbers = false;
    bool showUnconnectedPins = true; // marque les broches libres, aide au cablage
    bool showSheetShadow = true;

    double gridStep = 2.5;
    int gridMajorEvery = 4;
    double designationHeight = 2.5;
    double valueHeight = 2.0;
    double wireNumberHeight = 2.0;
    QString fontFamily = QStringLiteral("Noto Sans");

    static RenderStyle screen();
    static RenderStyle print();
    static RenderStyle screenDark();
};

} // namespace dsn
