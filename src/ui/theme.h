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
#include <QIcon>
#include <QString>

class QApplication;

namespace dsn {

struct ThemeColors {
    QColor window;
    QColor surface;
    QColor elevated;
    QColor border;
    QColor borderStrong;
    QColor text;
    QColor textMuted;
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

    // Espacements de base, en pixels logiques.
    static int gap() { return 8; }
    static int radius() { return 6; }
};

// Icones vectorielles. Chaque icone est un petit programme de trace : elle est
// donc nette a toute densite et suit la couleur du theme.
class Icons
{
public:
    enum class Glyph {
        New, Open, Save, Print, ExportPdf, ExportDxf, ExportCsv,
        Undo, Redo, Copy, Paste, Delete,
        Select, Wire, Junction, LabelTag, Text, SymbolPlace,
        Rotate, Mirror, Highlight,
        ZoomIn, ZoomOut, ZoomFit, Grid, Snap, Tracking, Palette2,
        Renumber, Check, Info, Palette, Folios, Properties, Reports,
        Plus, Minus, Duplicate, Up, Down, Edit, Theme
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
