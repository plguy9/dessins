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

// Repere acquis pour le reperage d'accrochage. C'est un point d'accrochage
// qu'on a survole et que le moteur retient : des traits d'alignement en
// partent, et on peut viser dessus meme loin de la geometrie.
struct TrackedPoint {
    QPointF point;
    SnapMode mode = SnapMode::Endpoint;
};

// Point retenu sur un chemin d'alignement.
struct TrackHit {
    QPointF point;
    double distance = 0.0;

    // Le ou les reperes d'ou partent les traits. Le second n'est renseigne
    // que pour un croisement de deux chemins.
    QPointF origin;
    SnapMode originMode = SnapMode::Endpoint;
    bool hasSecond = false;
    QPointF secondOrigin;
    SnapMode secondMode = SnapMode::Endpoint;

    // Croisement d'un chemin d'alignement avec la direction contrainte du
    // trace en cours. C'est le cas le plus utile : « a l'aplomb du milieu de
    // ce fil, sur ma ligne horizontale ».
    bool crossesConstraint = false;
    QPointF constraintOrigin;

    // Un croisement designe un point unique ; une simple projection ne
    // designe qu'une direction. Le premier doit gagner.
    bool isCrossing() const { return hasSecond || crossesConstraint; }
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
    bool trackingEnabled() const noexcept { return m_tracking; }       // F11
    void setTrackingEnabled(bool on);

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

    // ---- reperage d'accrochage aux objets (OTRACK, F11) ----------------
    // On survole un point d'accrochage, il est acquis ; des traits
    // d'alignement en partent alors, et le curseur s'y pose. C'est ce qui
    // permet de placer un fil a l'aplomb du milieu d'un autre sans rien
    // dessiner de provisoire.
    void acquire(const QPointF &point, SnapMode mode);
    void release(const QPointF &point);
    void clearTracked();
    // Acquiert ou relache selon que le point l'est deja : survoler deux fois
    // le meme point l'oublie, comme chez AutoCAD.
    void toggleTracked(const QPointF &point, SnapMode mode);
    bool isTracked(const QPointF &point) const;
    const QVector<TrackedPoint> &trackedPoints() const noexcept { return m_tracked; }

    // Nombre maximum de reperes retenus. Au-dela, le plus ancien sort : une
    // vue couverte de traits d'alignement n'aide plus personne.
    static constexpr int kMaxTrackedPoints = 7;

    // Meilleur point d'alignement sous le curseur, ou rien. `from` est
    // l'origine du trace en cours : les croisements avec sa direction
    // contrainte sont les points les plus utiles du dispositif.
    std::optional<TrackHit> track(const QPointF &cursor, double apertureMm,
                                  const QPointF *from = nullptr) const;

    // Angles des chemins d'alignement, en degres ecran. Orthogonaux seuls,
    // ou tous les angles polaires quand le suivi polaire est actif.
    QVector<double> trackingAngles() const;

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
    bool m_tracking = true;
    double m_gridStep = 2.5;
    double m_polarIncrement = 45.0;
    QVector<TrackedPoint> m_tracked;
};

} // namespace dsn
