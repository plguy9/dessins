// Identite visuelle de l'application.
//
// Deux choix structurent ce fichier. D'abord une palette et une feuille de
// style ecrites a la main plutot que le theme du systeme : un logiciel de CAO
// passe sa vie a cote d'un dessin, et le contraste entre l'interface et la
// feuille doit etre maitrise, pas subi. Ensuite des icones dessinees a
// l'execution plutot que chargees depuis des fichiers : elles s'adaptent au
// theme et a la densite d'ecran, ne peuvent pas manquer a l'installation, et
// c'est la meme technique vectorielle que le reste du logiciel.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

class QApplication;
class QWidget;

namespace dsn {

// Un plan par usage, et un seul. « canvas » est le vide derriere la feuille,
// « window » le chrome, « surface » les panneaux, « elevated » ce qui flotte
// au-dessus. Le fond du dessin est plus profond que le chrome : c'est ce qui
// fait flotter la feuille au lieu de la poser sur un gris etranger.
struct ThemeColors {
    QColor canvas;
    QColor window;
    QColor surface;
    QColor elevated;
    QColor border;
    QColor borderStrong;
    QColor text;
    QColor textMuted;
    QColor textFaint;
    QColor accent;
    QColor accentHover;
    QColor accentText;
    QColor danger;
    QColor success;
    QColor warning;
    bool dark = true;
};

class Theme
{
public:
    static void apply(QApplication &app, bool dark);
    static const ThemeColors &colors();
    static bool isDark();

    // Espacements : un seul pas de quatre pixels, multiplie. Toutes les
    // marges du logiciel en sortent, ce qui donne un rythme vertical unique
    // d'une boite de dialogue a l'autre.
    static int space(int steps = 1) { return 4 * steps; }
    static int gap() { return space(2); }
    static int radius() { return 6; }

    // Fonte d'interface : la premiere disponible d'une liste de fontes
    // modernes, sinon celle du systeme. Aucune fonte n'a besoin d'etre
    // installee pour que le logiciel soit correct.
    static QFont uiFont(int pointSize = 0, int weight = 0);

    // Chasse fixe, pour tout ce qui est chiffre : coordonnees, zoom, cotes,
    // ligne de commande. Un nombre qui change ne doit pas deplacer ses
    // voisins.
    static QFont monoFont(double pointSize = 0.0);

    // Etiquette gravee : petites capitales espacees, en retrait. C'est le
    // reperage de l'interface — titres de panneaux, en-tetes, sections.
    static QFont engravedFont(double pointSize = 0.0);

    // Applique la fonte gravee et la propriete de style correspondante.
    static void engrave(QWidget *widget);
};

// Icones vectorielles. Chaque icone est un petit programme de trace : elle est
// donc nette a toute densite et suit la couleur du theme.
class Icons
{
public:
    enum class Glyph {
        New, Open, Save, Print, ExportPdf, ExportDxf, ExportCsv,
        Undo, Redo, Copy, Paste, PasteKeepTags, Delete,
        Select, Wire, WireBus, WireTypeApply, Junction, LabelTag, Text, SymbolPlace,
        LockTag, UnlockTag,
        Rotate, Mirror, Highlight,
        ZoomIn, ZoomOut, ZoomFit, Grid, Snap, Tracking, Palette2,
        Renumber, Check, Info, Palette, Folios, Properties, Reports,
        Scale, Stretch, Array, Align, Trim, Extend,
        SwapSymbol, Find,
        Line, Rectangle, Circle, Arc, Polyline,
        // Les styles de trait des formes : le contour d'une armoire se dessine
        // en pointille, et il faut pouvoir le choisir d'un coup d'oeil.
        StrokeSolid, StrokeDashed, StrokeDotted, StrokeDashDot,
        MeasureLength, Join, Break,
        Dimension, DimensionH, DimensionV,
        LabelFolio, SignalOut, SignalIn,
        WireTypes, SaveAs, TagFormat, ZoomWindow, MoveComponent,
        ViewGrid, ViewList, Collapse, Expand,
        Plus, Minus, Duplicate, Up, Down, Edit, Theme,
        // Bloc C5 : une icone par commande, partout. Ces glyphes existent
        // parce que deux commandes differentes portaient le meme dessin —
        // dans un menu comme dans un panneau, on clique alors au hasard.
        Ortho, Polar, Surfer, SelectAll, Move, Pan, Scoot,
        DraftingSettings, EditComponent, PinNumbers, Plc, UnconnectedPins, Audit,
        Quit, Ladder, ZoomPrevious, DuplicateEdit, Terminals,
        CommandLine, CommandPalette, MatchProps, StartPage, ProjectInfo,
        PageSetup, ObjectSnap, TextH1, TextH2, TextH3, TextH4,
        TitleBlock,

        // Sentinelle : elle permet de parcourir tous les glyphes sans en
        // tenir la liste a jour ailleurs. Un test s'en sert pour verifier
        // qu'aucun glyphe n'est vide et qu'aucun n'en repete un autre.
        Count
    };

    static QIcon icon(Glyph glyph, const QColor &color = QColor());
    static QIcon icon(Glyph glyph, bool dark);

    // Vide le cache : a appeler au changement de theme.
    static void invalidate();

    // Icone de l'application : la marque du logiciel, dessinee comme le reste
    // en vectoriel. Sert a la fenetre et a la generation du .ico Windows.
    static QIcon appIcon();
};

} // namespace dsn
