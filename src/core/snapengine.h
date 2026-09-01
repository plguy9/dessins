// Accrochage aux objets, a la maniere d'AutoCAD.
//
// C'est ce qui separe un logiciel de dessin d'un outil de CAO : sans lui, on
// vise a la souris et le schema est faux d'un dixieme de millimetre partout.
// Le moteur vit dans le coeur plutot que dans la vue, pour la meme raison que
// tout le reste du coeur : il se teste sans ecran, et une regle d'accrochage
// fausse est le genre de defaut qu'on ne voit pas a l'oeil.
//
// Les modes reprennent la nomenclature AutoCAD (OSNAP) et leurs marqueurs
// visuels : c'est ce qui rend l'outil immediatement familier a qui vient de
// la, et les formes sont aussi normatives que les symboles eux-memes.
#pragma once

#include "folio.h"
#include "symbollibrary.h"

#include <QFlags>
#include <QList>
#include <QVector>

#include <optional>

namespace dsn {

enum class SnapMode : quint32 {
    Grid = 0x0001,          // grille — pas de marqueur
    Endpoint = 0x0002,      // extremite — carre
    Midpoint = 0x0004,      // milieu — triangle
    Center = 0x0008,        // centre — cercle
    Node = 0x0010,          // nodal : broche, jonction — cercle barre
    Quadrant = 0x0020,      // quadrant — losange
    Intersection = 0x0040,  // intersection — croix en X
    Perpendicular = 0x0080, // perpendiculaire — equerre
    Nearest = 0x0100,       // proche — sablier
    Insertion = 0x0200,     // point d'insertion — deux carres
    Extension = 0x0400,     // prolongement — croix en plus
};
Q_DECLARE_FLAGS(SnapModes, SnapMode)
Q_DECLARE_OPERATORS_FOR_FLAGS(SnapModes)

QString snapModeName(SnapMode mode);  // libelle affiche : « Milieu »
QString snapModeTag(SnapMode mode);   // cle de reglage : « midpoint »
SnapMode snapModeFromTag(const QString &tag);

struct SnapHit {
    QPointF point;
    SnapMode mode = SnapMode::Grid;
    QString entityId;
    double distance = 0.0;

    // Pour le prolongement : l'extremite d'ou part la ligne d'aide, afin que
    // la vue puisse tracer le trait pointille qui explique l'accrochage.
    QPointF origin;
    bool hasOrigin = false;

    QString label() const { return snapModeName(mode); }
};

class SnapEngine
{
public:
    SnapEngine();

    // ---- modes d'accrochage aux objets ---------------------------------
    SnapModes modes() const noexcept { return m_modes; }
    void setModes(SnapModes modes) { m_modes = modes; }
    void setMode(SnapMode mode, bool on);
    bool hasMode(SnapMode mode) const { return m_modes.testFlag(mode); }

    // ---- bascules, avec les raccourcis d'AutoCAD -----------------------
    bool objectSnapEnabled() const noexcept { return m_objectSnap; }   // F3
    void setObjectSnapEnabled(bool on) { m_objectSnap = on; }
    bool gridSnapEnabled() const noexcept { return m_gridSnap; }       // F9
    void setGridSnapEnabled(bool on) { m_gridSnap = on; }
    bool orthoEnabled() const noexcept { return m_ortho; }             // F8
    void setOrthoEnabled(bool on) { m_ortho = on; }
    bool polarEnabled() const noexcept { return m_polar; }             // F10
    void setPolarEnabled(bool on) { m_polar = on; }

    double gridStep() const noexcept { return m_gridStep; }
    void setGridStep(double step);
    double polarIncrement() const noexcept { return m_polarIncrement; }
    void setPolarIncrement(double degrees);

    // ---- accrochage ----------------------------------------------------
    // Renvoie le meilleur point d'accrochage sous le curseur, ou rien.
    // `from` sert aux modes qui ont besoin d'une origine (perpendiculaire) ;
    // `exclude` ecarte l'entite en cours de trace, qui s'accrocherait a
    // elle-meme.
    std::optional<SnapHit> snap(const Folio &folio, const SymbolLibrary &library,
                                const QPointF &cursor, double apertureMm,
                                const QPointF *from = nullptr,
                                const QString &exclude = QString()) const;

    // Tous les candidats tries, pour les tests et le cycle a la touche Tab.
    QVector<SnapHit> candidates(const Folio &folio, const SymbolLibrary &library,
                                const QPointF &cursor, double apertureMm,
                                const QPointF *from = nullptr,
                                const QString &exclude = QString()) const;

    // Contraint une direction depuis une origine : ortho au quart de tour,
    // suivi polaire au pas choisi, sinon libre.
    QPointF constrain(const QPointF &from, const QPointF &to) const;

    // Angle retenu par la contrainte, en degres ecran, ou rien si libre.
    std::optional<double> constrainedAngle(const QPointF &from, const QPointF &to) const;

    QPointF snapToGridPoint(const QPointF &p) const;

    static SnapModes defaultModes();
    static QList<SnapMode> allModes();
    // Priorite d'un mode quand plusieurs candidats se disputent le curseur.
    static int priority(SnapMode mode);

private:
    void collect(QVector<SnapHit> &out, const Folio &folio, const SymbolLibrary &library,
                 const QPointF &cursor, double aperture, const QPointF *from,
                 const QString &exclude) const;

    SnapModes m_modes;
    bool m_objectSnap = true;
    bool m_gridSnap = true;
    // Ortho allume par defaut : un schema electrique se trace en traits
    // horizontaux et verticaux, et AutoCAD Electrical impose lui aussi
    // l'orthogonalite a ses fils. Qui veut une diagonale coupe F8.
    bool m_ortho = true;
    bool m_polar = true;
    double m_gridStep = 2.5;
    double m_polarIncrement = 45.0;
};

} // namespace dsn
