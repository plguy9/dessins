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

// Aspect de la grille. AutoCAD sait la tracer en points ou en carreaux, et le
// choix n'est pas cosmetique : les points laissent le dessin seul a l'oeil,
// les carreaux donnent un reperage de distance immediat. On ajoute la croix,
// qui marque le point sans le lien continu — c'est le meilleur compromis quand
// on trace beaucoup de traits horizontaux.
enum class GridStyle {
    Dots,    // points, le defaut
    Lines,   // carreaux
    Crosses  // petites croix
};

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
    QColor dimension{ 0x5A, 0x62, 0x60 }; // cotations : gris, en retrait du circuit
    QColor selection{ 0xE8, 0x8B, 0x0B };
    QColor highlight{ 0xE8, 0x40, 0x40 }; // potentiel mis en evidence
    QColor pinMarker{ 0xC0, 0x50, 0x20 };
    // Retour d'accrochage. Le jaune-vert est la couleur d'AutoSnap
    // d'AutoCAD : elle ne ressemble a aucune couleur de conducteur, donc un
    // marqueur ne peut jamais etre pris pour un element du dessin.
    QColor snapMarker{ 0xC8, 0xD8, 0x1E };
    QColor snapGuide{ 0x9A, 0xA8, 0x22 };
    double snapMarkerSize = 3.2;   // cote du marqueur, en millimetres
    // Reticule : les deux traits qui traversent la vue au curseur. C'est la
    // signature visuelle d'AutoCAD, et son utilite est reelle — il aligne
    // l'oeil sur ce qui est deja pose ailleurs sur la feuille.
    QColor crosshair{ 0x7A, 0x88, 0x86 };
    bool showCrosshair = true;
    double crosshairPercent = 100.0;  // 100 % = pleine vue, comme CURSORSIZE
    double pickBoxPixels = 9.0;       // carre de selection au centre du reticule
    bool showDynamicInput = true;

    // Epaisseurs, en millimetres
    double wireWidth = 0.35;
    double symbolWidth = 0.25;
    double frameWidth = 0.5;
    double gridDotWidth = 0.12;
    // Cotation : trait fin, comme sur une planche. Une cote ne doit jamais
    // peser autant qu'un conducteur — elle accompagne le dessin, elle n'en
    // fait pas partie.
    double dimensionWidth = 0.18;
    double dimensionArrow = 2.5;  // longueur de la fleche, un module

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
    // Couleur portee par le type de fil. Eteindre revient au trait unique de
    // la couleur `wire` — ce que veut une sortie monochrome.
    bool useWireTypeColors = true;
    // Sur fond sombre, une couleur de type presque noire — le L2 de la CEI —
    // devient invisible. AutoCAD retourne le noir sur fond noir ; on eclaircit
    // de meme les teintes trop proches du fond, sans toucher aux autres.
    bool lightenDarkWires = false;

    double gridStep = 2.5;

    // Echelle d'affichage, en pixels par millimetre. Zero = inconnue (export
    // PDF, vignette) : la grille garde alors son pas nominal.
    //
    // Elle sert a une seule chose, mais elle compte : ADAPTER LE PAS TRACE AU
    // ZOOM. Une grille dont les marques tombent a trois pixels l'une de
    // l'autre n'est plus une grille, c'est un voile gris ; et le garde-fou de
    // densite l'abandonnait carrement — d'ou le « il manque des carreaux »
    // signale a l'usage. On l'espace plutot que de la perdre.
    double pixelsPerMm = 0.0;
    int gridMajorEvery = 4;
    GridStyle gridStyle = GridStyle::Dots;
    double gridCrossSize = 0.8; // demi-branche de la croix, en millimetres
    double designationHeight = 2.5;
    double valueHeight = 2.0;
    double wireNumberHeight = 2.0;
    QString fontFamily = QStringLiteral("Noto Sans");

    // APPLIQUE LES JETONS DU THEME AU DESSIN.
    //
    // C'est la couche `ui` qui appelle : `render/` ne connait pas l'en-tete
    // de theme de `ui/` et ne doit pas le connaitre — chaque couche ne connait
    // que celles situees sous elle (BRIEF.md §3), et `render/` est SOUS `ui/`.
    // L'injection garde la regle de dependance intacte tout en supprimant la
    // divergence des deux palettes.
    //
    // Ne touche QUE ce qui depend du theme : la feuille, le vide autour, et
    // l'encre du trace. Le fil, le repere, l'etiquette, la cote, la selection
    // et le marqueur d'accrochage n'en dependent pas — le papier etant blanc
    // dans les deux themes, ils sont valables dans les deux.
    void applyTheme(const QColor &paper, const QColor &ink, const QColor &voidColor);

    static RenderStyle screen();
    static RenderStyle print();
    // Fond de dessin sombre. N'est PLUS declenche par le theme d'interface :
    // c'est une preference explicite, pour qui vient d'AutoCAD et a le noir
    // dans les yeux depuis vingt ans (reglage « display/darkSheet »).
    static RenderStyle screenDark();
};

} // namespace dsn
