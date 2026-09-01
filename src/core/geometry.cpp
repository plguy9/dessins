#include "geometry.h"

#include <numbers>

#include <QList>

namespace dsn {

int toDegrees(Orientation o) { return static_cast<int>(o); }

Orientation orientationFromDegrees(int degrees)
{
    int d = ((degrees % 360) + 360) % 360;
    switch ((d + 45) / 90 % 4) {
    case 1: return Orientation::R90;
    case 2: return Orientation::R180;
    case 3: return Orientation::R270;
    default: return Orientation::R0;
    }
}

Orientation rotateCw(Orientation o) { return orientationFromDegrees(toDegrees(o) + 90); }
Orientation rotateCcw(Orientation o) { return orientationFromDegrees(toDegrees(o) - 90); }

int toDegrees(Direction d) { return static_cast<int>(d); }

Direction directionFromDegrees(int degrees)
{
    int d = ((degrees % 360) + 360) % 360;
    switch ((d + 45) / 90 % 4) {
    case 1: return Direction::Down;
    case 2: return Direction::Left;
    case 3: return Direction::Up;
    default: return Direction::Right;
    }
}

Direction rotatedBy(Direction d, Orientation o, bool mirrored)
{
    int deg = toDegrees(d);
    if (mirrored)
        deg = 180 - deg; // reflexion sur l'axe vertical
    return directionFromDegrees(deg + toDegrees(o));
}

QPointF unitVector(Direction d)
{
    switch (d) {
    case Direction::Right: return QPointF(1, 0);
    case Direction::Down: return QPointF(0, 1);
    case Direction::Left: return QPointF(-1, 0);
    case Direction::Up: return QPointF(0, -1);
    }
    return QPointF(1, 0);
}

QPointF Transform2D::map(const QPointF &p) const
{
    return QPointF(m11 * p.x() + m21 * p.y() + dx, m12 * p.x() + m22 * p.y() + dy);
}

QRectF Transform2D::mapRect(const QRectF &r) const
{
    const QPointF corners[4] = { map(r.topLeft()), map(r.topRight()), map(r.bottomRight()),
                                 map(r.bottomLeft()) };
    double minX = corners[0].x(), maxX = corners[0].x();
    double minY = corners[0].y(), maxY = corners[0].y();
    for (const QPointF &p : corners) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

Transform2D Transform2D::inverted(bool *ok) const
{
    const double det = determinant();
    if (fuzzyZero(det)) {
        if (ok)
            *ok = false;
        return Transform2D();
    }
    if (ok)
        *ok = true;
    Transform2D r;
    r.m11 = m22 / det;
    r.m12 = -m12 / det;
    r.m21 = -m21 / det;
    r.m22 = m11 / det;
    r.dx = -(r.m11 * dx + r.m21 * dy);
    r.dy = -(r.m12 * dx + r.m22 * dy);
    return r;
}

Transform2D Transform2D::then(const Transform2D &next) const
{
    Transform2D r;
    r.m11 = next.m11 * m11 + next.m21 * m12;
    r.m12 = next.m12 * m11 + next.m22 * m12;
    r.m21 = next.m11 * m21 + next.m21 * m22;
    r.m22 = next.m12 * m21 + next.m22 * m22;
    r.dx = next.m11 * dx + next.m21 * dy + next.dx;
    r.dy = next.m12 * dx + next.m22 * dy + next.dy;
    return r;
}

Transform2D Transform2D::translation(double dx, double dy)
{
    Transform2D t;
    t.dx = dx;
    t.dy = dy;
    return t;
}

Transform2D Transform2D::rotation(double degrees)
{
    // std::numbers plutot que M_PI : ce dernier n'existe pas sous MSVC
    // sans macro prealable, et l'empaquetage Windows compile avec MSVC.
    const double rad = degrees * std::numbers::pi / 180.0;
    // Les quarts de tour doivent tomber juste : std::cos(pi/2) ne vaut pas
    // exactement zero, et une derive de 1e-17 finit par decaler un accrochage.
    double c = std::cos(rad);
    double s = std::sin(rad);
    if (std::abs(c) < 1e-12) c = 0.0;
    if (std::abs(s) < 1e-12) s = 0.0;
    if (std::abs(std::abs(c) - 1.0) < 1e-12) c = c > 0 ? 1.0 : -1.0;
    if (std::abs(std::abs(s) - 1.0) < 1e-12) s = s > 0 ? 1.0 : -1.0;

    Transform2D t;
    t.m11 = c;
    t.m12 = s;
    t.m21 = -s;
    t.m22 = c;
    return t;
}

Transform2D Transform2D::scaling(double sx, double sy)
{
    Transform2D t;
    t.m11 = sx;
    t.m22 = sy;
    return t;
}

Transform2D Placement::transform() const
{
    // Miroir, puis rotation, puis translation.
    Transform2D t;
    if (mirrored)
        t = Transform2D::scaling(-1.0, 1.0);
    return t.then(Transform2D::rotation(toDegrees(orientation)))
            .then(Transform2D::translation(position.x(), position.y()));
}

QPointF Placement::map(const QPointF &local) const { return transform().map(local); }

QRectF Placement::mapRect(const QRectF &local) const { return transform().mapRect(local); }

double distancePointToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= kEpsilon * kEpsilon)
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    double t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF proj = a + t * ab;
    return std::hypot(p.x() - proj.x(), p.y() - proj.y());
}

bool pointOnSegment(const QPointF &p, const QPointF &a, const QPointF &b, double tol)
{
    return distancePointToSegment(p, a, b) <= tol;
}

std::optional<QPointF> segmentIntersection(const QPointF &a1, const QPointF &a2,
                                           const QPointF &b1, const QPointF &b2)
{
    const QPointF r = a2 - a1;
    const QPointF s = b2 - b1;
    const double denom = r.x() * s.y() - r.y() * s.x();
    if (fuzzyZero(denom))
        return std::nullopt; // paralleles ou colineaires
    const QPointF d = b1 - a1;
    const double t = (d.x() * s.y() - d.y() * s.x()) / denom;
    const double u = (d.x() * r.y() - d.y() * r.x()) / denom;
    if (t < -kEpsilon || t > 1 + kEpsilon || u < -kEpsilon || u > 1 + kEpsilon)
        return std::nullopt;
    return a1 + t * r;
}

double snapToGrid(double v, double step)
{
    if (step <= kEpsilon)
        return v;
    return std::round(v / step) * step;
}

QPointF snapToGrid(const QPointF &p, double step)
{
    return QPointF(snapToGrid(p.x(), step), snapToGrid(p.y(), step));
}

QPointF orthogonalize(const QPointF &a, const QPointF &b)
{
    const double dx = std::abs(b.x() - a.x());
    const double dy = std::abs(b.y() - a.y());
    return dx >= dy ? QPointF(b.x(), a.y()) : QPointF(a.x(), b.y());
}

QRectF normalized(const QPointF &a, const QPointF &b)
{
    return QRectF(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                  QPointF(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

bool segmentIntersectsRect(const QPointF &a, const QPointF &b, const QRectF &rect)
{
    const QRectF box = rect.normalized();
    if (box.contains(a) || box.contains(b))
        return true;
    // Rejet rapide : deux boites qui ne se touchent pas ne peuvent pas se
    // croiser, et c'est le cas de l'immense majorite des segments d'un folio.
    if (!normalized(a, b).intersects(box))
        return false;

    const QPointF corners[4] = { box.topLeft(), box.topRight(), box.bottomRight(),
                                 box.bottomLeft() };
    for (int i = 0; i < 4; ++i) {
        if (segmentIntersection(a, b, corners[i], corners[(i + 1) % 4]))
            return true;
    }
    return false;
}

SheetFormat SheetFormat::portrait() const
{
    SheetFormat f = *this;
    if (f.width > f.height)
        std::swap(f.width, f.height);
    return f;
}

SheetFormat SheetFormat::landscape() const
{
    SheetFormat f = *this;
    if (f.height > f.width)
        std::swap(f.width, f.height);
    return f;
}

QList<SheetFormat> allSheetFormats()
{
    return {
        { QStringLiteral("A4"), QStringLiteral("A4 — 297 × 210 mm"), 297.0, 210.0 },
        { QStringLiteral("A3"), QStringLiteral("A3 — 420 × 297 mm"), 420.0, 297.0 },
        { QStringLiteral("A2"), QStringLiteral("A2 — 594 × 420 mm"), 594.0, 420.0 },
        { QStringLiteral("A1"), QStringLiteral("A1 — 841 × 594 mm"), 841.0, 594.0 },
        { QStringLiteral("A0"), QStringLiteral("A0 — 1189 × 841 mm"), 1189.0, 841.0 },
        { QStringLiteral("ANSI_A"), QStringLiteral("ANSI A — 11 × 8.5 in"), mmFromInch(11.0), mmFromInch(8.5) },
        { QStringLiteral("ANSI_B"), QStringLiteral("ANSI B — 17 × 11 in"), mmFromInch(17.0), mmFromInch(11.0) },
        { QStringLiteral("ANSI_C"), QStringLiteral("ANSI C — 22 × 17 in"), mmFromInch(22.0), mmFromInch(17.0) },
        { QStringLiteral("ANSI_D"), QStringLiteral("ANSI D — 34 × 22 in"), mmFromInch(34.0), mmFromInch(22.0) },
        { QStringLiteral("ANSI_E"), QStringLiteral("ANSI E — 44 × 34 in"), mmFromInch(44.0), mmFromInch(34.0) },
    };
}

SheetFormat sheetFormatById(const QString &id)
{
    const auto formats = allSheetFormats();
    for (const SheetFormat &f : formats) {
        if (f.id.compare(id, Qt::CaseInsensitive) == 0)
            return f;
    }
    return formats.at(1); // A3 paysage : defaut sur id inconnu plutot qu'echec de chargement
}

} // namespace dsn
