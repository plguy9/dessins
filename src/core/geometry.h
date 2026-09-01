// Geometrie de base. Toutes les longueurs internes du logiciel sont exprimees
// en millimetres, en double precision : les formats de feuille et l'impression
// sont metriques, et les unites imperiales ne sont qu'une conversion de
// presentation (voir Units).
#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>

#include <cmath>
#include <optional>

namespace dsn {

using Millimetre = double;

inline constexpr double kEpsilon = 1e-6;

// Tolerance de coincidence electrique. Deux points distants de moins d'un
// centieme de millimetre designent le meme noeud du schema.
inline constexpr double kConnectTolerance = 0.01;

inline constexpr double kMmPerInch = 25.4;

inline bool fuzzyZero(double v, double eps = kEpsilon) { return std::abs(v) <= eps; }
inline bool fuzzyEqual(double a, double b, double eps = kEpsilon) { return std::abs(a - b) <= eps; }

inline bool samePoint(const QPointF &a, const QPointF &b, double tol = kConnectTolerance)
{
    return std::abs(a.x() - b.x()) <= tol && std::abs(a.y() - b.y()) <= tol;
}

inline double mmFromInch(double in) { return in * kMmPerInch; }
inline double inchFromMm(double mm) { return mm / kMmPerInch; }

// --------------------------------------------------------------------------
// Orientation

// Rotation d'un symbole. Les schemas electriques n'utilisent que les quarts de
// tour : autoriser un angle libre compliquerait l'accrochage aux broches sans
// benefice metier.
enum class Orientation { R0 = 0, R90 = 90, R180 = 180, R270 = 270 };

int toDegrees(Orientation o);
Orientation orientationFromDegrees(int degrees);
Orientation rotateCw(Orientation o);
Orientation rotateCcw(Orientation o);

// Direction d'une broche ou d'un segment, exprimee en local puis transformee
// avec le symbole.
enum class Direction { Right = 0, Down = 90, Left = 180, Up = 270 };

int toDegrees(Direction d);
Direction directionFromDegrees(int degrees);
Direction rotatedBy(Direction d, Orientation o, bool mirrored);
QPointF unitVector(Direction d);

// --------------------------------------------------------------------------
// Transformation affine 2D.
//
// QTransform vit dans QtGui, et le coeur ne doit dependre que de QtCore : une
// affine 2x3 maison coute trente lignes et preserve cette frontiere. La
// convention d'angle est celle de l'ecran (y vers le bas), donc +90 degres
// envoie (1,0) sur (0,1), comme QTransform.
struct Transform2D {
    double m11 = 1.0, m12 = 0.0; // premiere colonne
    double m21 = 0.0, m22 = 1.0; // deuxieme colonne
    double dx = 0.0, dy = 0.0;

    QPointF map(const QPointF &p) const;
    QRectF mapRect(const QRectF &r) const;
    double determinant() const { return m11 * m22 - m21 * m12; }
    Transform2D inverted(bool *ok = nullptr) const;

    // Composition : a.then(b).map(p) == b.map(a.map(p)).
    Transform2D then(const Transform2D &next) const;

    static Transform2D translation(double dx, double dy);
    static Transform2D rotation(double degrees);
    static Transform2D scaling(double sx, double sy);
};

// --------------------------------------------------------------------------
// Placement : position + rotation + miroir d'une entite dans le folio.

struct Placement {
    QPointF position;
    Orientation orientation = Orientation::R0;
    bool mirrored = false; // miroir selon l'axe vertical local, applique avant la rotation

    Transform2D transform() const;
    QPointF map(const QPointF &local) const;
    QRectF mapRect(const QRectF &local) const;

    bool operator==(const Placement &o) const
    {
        return samePoint(position, o.position, kEpsilon) && orientation == o.orientation
               && mirrored == o.mirrored;
    }
};

// --------------------------------------------------------------------------
// Utilitaires de segments, utilises par la detection de connexions.

double distancePointToSegment(const QPointF &p, const QPointF &a, const QPointF &b);
bool pointOnSegment(const QPointF &p, const QPointF &a, const QPointF &b,
                    double tol = kConnectTolerance);
std::optional<QPointF> segmentIntersection(const QPointF &a1, const QPointF &a2,
                                           const QPointF &b1, const QPointF &b2);

QPointF snapToGrid(const QPointF &p, double step);
double snapToGrid(double v, double step);

// Contraint b sur l'horizontale ou la verticale passant par a, en gardant le
// plus grand deplacement : c'est le comportement attendu du trace de fil.
QPointF orthogonalize(const QPointF &a, const QPointF &b);

QRectF normalized(const QPointF &a, const QPointF &b);

// --------------------------------------------------------------------------
// Formats de feuille

struct SheetFormat {
    QString id;
    QString label;
    Millimetre width = 420.0;
    Millimetre height = 297.0;

    QSizeF size() const { return QSizeF(width, height); }
    SheetFormat portrait() const;
    SheetFormat landscape() const;
};

// Formats ISO (A4..A0) et ANSI (A..E). Renvoie un format A3 paysage si l'id
// est inconnu, plutot que d'echouer au chargement d'un fichier.
SheetFormat sheetFormatById(const QString &id);
QList<SheetFormat> allSheetFormats();

} // namespace dsn
