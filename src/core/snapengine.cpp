#include "snapengine.h"

#include "entities.h"

#include <QHash>

#include <numbers>

#include <algorithm>
#include <cmath>

namespace dsn {

namespace {

// Projection d'un point sur un segment, bornee aux extremites.
QPointF projectOnSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= kEpsilon * kEpsilon)
        return a;
    double t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
    t = std::clamp(t, 0.0, 1.0);
    return a + t * ab;
}

// Pied de la perpendiculaire, NON borne : AutoCAD accroche a la
// perpendiculaire meme quand elle tombe sur le prolongement du segment.
QPointF perpendicularFoot(const QPointF &from, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= kEpsilon * kEpsilon)
        return a;
    const double t = ((from.x() - a.x()) * ab.x() + (from.y() - a.y()) * ab.y()) / len2;
    return a + t * ab;
}

double distanceBetween(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

} // namespace

// --------------------------------------------------------------------------

QString snapModeName(SnapMode mode)
{
    switch (mode) {
    case SnapMode::Grid: return QStringLiteral("Grille");
    case SnapMode::Endpoint: return QStringLiteral("Extrémité");
    case SnapMode::Midpoint: return QStringLiteral("Milieu");
    case SnapMode::Center: return QStringLiteral("Centre");
    case SnapMode::Node: return QStringLiteral("Nodal");
    case SnapMode::Quadrant: return QStringLiteral("Quadrant");
    case SnapMode::Intersection: return QStringLiteral("Intersection");
    case SnapMode::Perpendicular: return QStringLiteral("Perpendiculaire");
    case SnapMode::Nearest: return QStringLiteral("Proche");
    case SnapMode::Insertion: return QStringLiteral("Insertion");
    case SnapMode::Extension: return QStringLiteral("Prolongement");
    }
    return QString();
}

QString snapModeTag(SnapMode mode)
{
    switch (mode) {
    case SnapMode::Grid: return QStringLiteral("grid");
    case SnapMode::Endpoint: return QStringLiteral("endpoint");
    case SnapMode::Midpoint: return QStringLiteral("midpoint");
    case SnapMode::Center: return QStringLiteral("center");
    case SnapMode::Node: return QStringLiteral("node");
    case SnapMode::Quadrant: return QStringLiteral("quadrant");
    case SnapMode::Intersection: return QStringLiteral("intersection");
    case SnapMode::Perpendicular: return QStringLiteral("perpendicular");
    case SnapMode::Nearest: return QStringLiteral("nearest");
    case SnapMode::Insertion: return QStringLiteral("insertion");
    case SnapMode::Extension: return QStringLiteral("extension");
    }
    return QString();
}

SnapMode snapModeFromTag(const QString &tag)
{
    const QList<SnapMode> all = SnapEngine::allModes();
    for (SnapMode mode : all) {
        if (snapModeTag(mode) == tag)
            return mode;
    }
    return SnapMode::Grid;
}

QList<SnapMode> SnapEngine::allModes()
{
    return { SnapMode::Endpoint,      SnapMode::Midpoint,  SnapMode::Center,
             SnapMode::Node,          SnapMode::Quadrant,  SnapMode::Intersection,
             SnapMode::Perpendicular, SnapMode::Insertion, SnapMode::Extension,
             SnapMode::Nearest,       SnapMode::Grid };
}

SnapModes SnapEngine::defaultModes()
{
    // « Proche » et « Prolongement » restent eteints par defaut, comme dans
    // AutoCAD : ils accrochent partout et masqueraient les modes precis.
    return SnapMode::Endpoint | SnapMode::Midpoint | SnapMode::Center | SnapMode::Node
            | SnapMode::Quadrant | SnapMode::Intersection | SnapMode::Perpendicular
            | SnapMode::Insertion | SnapMode::Grid;
}

int SnapEngine::priority(SnapMode mode)
{
    // Quand plusieurs candidats se disputent le curseur, le plus significatif
    // gagne a distance comparable. Une extremite ou une broche prime sur un
    // milieu, qui prime sur un point quelconque du trait.
    switch (mode) {
    case SnapMode::Node: return 0;
    case SnapMode::Endpoint: return 0;
    case SnapMode::Intersection: return 1;
    case SnapMode::Midpoint: return 2;
    case SnapMode::Center: return 3;
    case SnapMode::Quadrant: return 3;
    case SnapMode::Insertion: return 4;
    case SnapMode::Perpendicular: return 5;
    case SnapMode::Extension: return 6;
    case SnapMode::Nearest: return 7;
    case SnapMode::Grid: return 8;
    }
    return 9;
}

SnapEngine::SnapEngine() : m_modes(defaultModes()) {}

void SnapEngine::setMode(SnapMode mode, bool on)
{
    m_modes.setFlag(mode, on);
}

void SnapEngine::setGridStep(double step)
{
    if (step > 0.0)
        m_gridStep = step;
}

void SnapEngine::setPolarIncrement(double degrees)
{
    if (degrees > 0.0 && degrees <= 180.0)
        m_polarIncrement = degrees;
}

QPointF SnapEngine::snapToGridPoint(const QPointF &p) const
{
    return snapToGrid(p, m_gridStep);
}

// --------------------------------------------------------------------------
// Contrainte de direction

std::optional<double> SnapEngine::constrainedAngle(const QPointF &from, const QPointF &to) const
{
    const QPointF delta = to - from;
    if (std::hypot(delta.x(), delta.y()) < kEpsilon)
        return std::nullopt;

    if (m_ortho) {
        // Ortho gagne toujours sur le suivi polaire : c'est la regle d'AutoCAD,
        // et c'est aussi ce qu'on attend d'un mode qu'on a active expres.
        return std::abs(delta.x()) >= std::abs(delta.y()) ? (delta.x() >= 0 ? 0.0 : 180.0)
                                                          : (delta.y() >= 0 ? 90.0 : 270.0);
    }
    if (!m_polar)
        return std::nullopt;

    const double angle = std::atan2(delta.y(), delta.x()) * 180.0 / std::numbers::pi;
    const double snapped = std::round(angle / m_polarIncrement) * m_polarIncrement;
    const double deviation = std::abs(angle - snapped);
    // Au-dela de deux degres d'ecart, l'utilisateur vise clairement autre
    // chose : on le laisse tracer librement plutot que de tordre son geste.
    if (deviation > 2.0)
        return std::nullopt;
    return snapped;
}

QPointF SnapEngine::constrain(const QPointF &from, const QPointF &to) const
{
    const auto angle = constrainedAngle(from, to);
    if (!angle)
        return to;

    const QPointF delta = to - from;
    const double radians = *angle * std::numbers::pi / 180.0;
    const QPointF direction(std::cos(radians), std::sin(radians));
    // Projection sur la direction retenue : le curseur garde la main sur la
    // longueur, la contrainte ne decide que de l'orientation.
    const double length = delta.x() * direction.x() + delta.y() * direction.y();
    return from + direction * std::max(0.0, length);
}

// --------------------------------------------------------------------------
// Recolte des candidats

void SnapEngine::collect(QVector<SnapHit> &out, const Folio &folio, const SymbolLibrary &library,
                         const QPointF &cursor, double aperture, const QPointF *from,
                         const QString &exclude) const
{
    auto add = [&](SnapMode mode, const QPointF &point, const QString &id,
                   const QPointF *origin = nullptr) {
        if (!m_modes.testFlag(mode))
            return;
        const double distance = distanceBetween(point, cursor);
        if (distance > aperture)
            return;
        SnapHit hit;
        hit.point = point;
        hit.mode = mode;
        hit.entityId = id;
        hit.distance = distance;
        if (origin) {
            hit.origin = *origin;
            hit.hasOrigin = true;
        }
        out.append(hit);
    };

    // Les segments retenus servent ensuite au calcul des intersections : on
    // les collecte une fois plutot que de reparcourir le folio.
    struct Segment {
        QPointF a;
        QPointF b;
        QString id;
    };
    QVector<Segment> segments;

    auto addPrimitiveSnaps = [&](const Primitive &primitive, const Placement *placement,
                                 const QString &id) {
        auto map = [&](const QPointF &p) { return placement ? placement->map(p) : p; };

        switch (primitive.kind) {
        case Primitive::Kind::Line:
        case Primitive::Kind::Polyline: {
            for (int i = 0; i < primitive.points.size(); ++i) {
                const QPointF p = map(primitive.points.at(i));
                if (i == 0 || i == primitive.points.size() - 1)
                    add(SnapMode::Endpoint, p, id);
                if (i > 0) {
                    const QPointF previous = map(primitive.points.at(i - 1));
                    add(SnapMode::Midpoint, (previous + p) / 2.0, id);
                    segments.append({ previous, p, id });
                }
            }
            break;
        }
        case Primitive::Kind::Rect: {
            if (primitive.points.size() < 2)
                break;
            const QRectF r = normalized(primitive.points.at(0), primitive.points.at(1));
            const QPointF corners[4] = { map(r.topLeft()), map(r.topRight()),
                                         map(r.bottomRight()), map(r.bottomLeft()) };
            for (int i = 0; i < 4; ++i) {
                add(SnapMode::Endpoint, corners[i], id);
                const QPointF &next = corners[(i + 1) % 4];
                add(SnapMode::Midpoint, (corners[i] + next) / 2.0, id);
                segments.append({ corners[i], next, id });
            }
            add(SnapMode::Center, map(r.center()), id);
            break;
        }
        case Primitive::Kind::Circle:
        case Primitive::Kind::Arc: {
            if (primitive.points.isEmpty())
                break;
            const QPointF centre = map(primitive.points.first());
            add(SnapMode::Center, centre, id);
            // Les quadrants sont pris dans le repere du folio : ce sont les
            // points a trois, six, neuf et midi tels qu'on les voit, ce qui
            // est le sens du mode dans AutoCAD.
            const double r = primitive.radius;
            add(SnapMode::Quadrant, centre + QPointF(r, 0), id);
            add(SnapMode::Quadrant, centre + QPointF(-r, 0), id);
            add(SnapMode::Quadrant, centre + QPointF(0, r), id);
            add(SnapMode::Quadrant, centre + QPointF(0, -r), id);
            break;
        }
        case Primitive::Kind::Text:
            if (!primitive.points.isEmpty())
                add(SnapMode::Insertion, map(primitive.points.first()), id);
            break;
        }
    };

    for (const EntityPtr &entity : folio.entities()) {
        if (!exclude.isEmpty() && entity->id() == exclude)
            continue;

        if (const auto *wire = dynamic_cast<const Wire *>(entity.get())) {
            const QString id = wire->id();
            for (int i = 0; i < wire->points.size(); ++i) {
                const QPointF p = wire->points.at(i);
                if (i == 0 || i == wire->points.size() - 1)
                    add(SnapMode::Endpoint, p, id);
                else
                    add(SnapMode::Node, p, id); // un coude est un point remarquable
                if (i > 0) {
                    const QPointF previous = wire->points.at(i - 1);
                    // Le milieu de chaque segment, pas seulement du fil entier :
                    // c'est le comportement d'AutoCAD sur une polyligne, et
                    // c'est ce qu'on veut pour piquer au milieu d'une liaison.
                    add(SnapMode::Midpoint, (previous + p) / 2.0, id);
                    add(SnapMode::Nearest, projectOnSegment(cursor, previous, p), id);
                    segments.append({ previous, p, id });

                    if (from && m_modes.testFlag(SnapMode::Perpendicular))
                        add(SnapMode::Perpendicular, perpendicularFoot(*from, previous, p), id);
                }
            }
            // Prolongement : au-dela de chaque extremite, dans l'axe du fil.
            if (m_modes.testFlag(SnapMode::Extension) && wire->points.size() >= 2) {
                const auto extend = [&](const QPointF &tip, const QPointF &inner) {
                    QPointF direction = tip - inner;
                    const double length = std::hypot(direction.x(), direction.y());
                    if (length < kEpsilon)
                        return;
                    direction /= length;
                    const QPointF relative = cursor - tip;
                    const double along = relative.x() * direction.x() + relative.y() * direction.y();
                    if (along <= 0.0)
                        return; // le curseur est en deca : pas un prolongement
                    add(SnapMode::Extension, tip + direction * along, id, &tip);
                };
                extend(wire->points.first(), wire->points.at(1));
                extend(wire->points.last(), wire->points.at(wire->points.size() - 2));
            }
            continue;
        }

        if (const auto *symbol = dynamic_cast<const SymbolInstance *>(entity.get())) {
            const QString id = symbol->id();
            add(SnapMode::Insertion, symbol->placement.position, id);
            const SymbolDefinition *definition = library.definition(symbol->definitionId);
            if (!definition)
                continue;
            for (const Pin &pin : definition->pins) {
                if (pin.type == PinType::NotConnected)
                    continue;
                // Une broche est le point nodal par excellence : c'est la que
                // le fil doit atterrir pour etre electriquement raccorde.
                const QPointF tip = symbol->placement.map(pin.position);
                add(SnapMode::Node, tip, id);
                const QPointF root = symbol->placement.map(pin.root());
                add(SnapMode::Midpoint, (tip + root) / 2.0, id);
                segments.append({ root, tip, id });
                if (from && m_modes.testFlag(SnapMode::Perpendicular))
                    add(SnapMode::Perpendicular, perpendicularFoot(*from, root, tip), id);
            }
            for (const Primitive &primitive : definition->graphics)
                addPrimitiveSnaps(primitive, &symbol->placement, id);
            continue;
        }

        if (const auto *junction = dynamic_cast<const Junction *>(entity.get())) {
            add(SnapMode::Node, junction->point, junction->id());
            continue;
        }
        if (const auto *label = dynamic_cast<const Label *>(entity.get())) {
            add(SnapMode::Node, label->point, label->id());
            continue;
        }
        if (const auto *text = dynamic_cast<const TextItem *>(entity.get())) {
            add(SnapMode::Insertion, text->placement.position, text->id());
            continue;
        }
        if (const auto *graphic = dynamic_cast<const GraphicItem *>(entity.get())) {
            addPrimitiveSnaps(graphic->shape, nullptr, graphic->id());
            continue;
        }
        if (const auto *dim = dynamic_cast<const DimensionItem *>(entity.get())) {
            // SEULS LES POINTS D'ATTACHE, jamais la ligne de cote.
            //
            // Une cote se pose souvent a la suite d'une autre — trois entraxes
            // alignes le long d'un rail — et repartir exactement du point ou
            // la precedente s'arretait est le geste courant. Mais s'accrocher
            // a la LIGNE de cote enchainerait les cotes les unes sur les
            // autres au lieu de les rattacher au dessin : on coterait la
            // cotation.
            add(SnapMode::Endpoint, dim->first, dim->id());
            add(SnapMode::Endpoint, dim->second, dim->id());
            continue;
        }
    }

    // Intersections. Le cout est quadratique sur les segments, mais on ne
    // garde que ceux qui passent pres du curseur : sur un folio dense cela
    // ramene la comparaison a une poignee de paires.
    if (m_modes.testFlag(SnapMode::Intersection)) {
        QVector<const Segment *> near;
        for (const Segment &segment : segments) {
            if (distancePointToSegment(cursor, segment.a, segment.b) <= aperture * 2.0)
                near.append(&segment);
        }
        for (int i = 0; i < near.size(); ++i) {
            for (int j = i + 1; j < near.size(); ++j) {
                if (near.at(i)->id == near.at(j)->id)
                    continue; // une entite ne se croise pas elle-meme
                const auto crossing = segmentIntersection(near.at(i)->a, near.at(i)->b,
                                                          near.at(j)->a, near.at(j)->b);
                if (crossing)
                    add(SnapMode::Intersection, *crossing, near.at(i)->id);
            }
        }
    }

    // La grille ferme la marche : elle accroche toujours, donc elle ne doit
    // jamais primer sur un point du dessin.
    if (m_gridSnap && m_modes.testFlag(SnapMode::Grid))
        add(SnapMode::Grid, snapToGridPoint(cursor), QString());
}

QVector<SnapHit> SnapEngine::candidates(const Folio &folio, const SymbolLibrary &library,
                                        const QPointF &cursor, double apertureMm,
                                        const QPointF *from, const QString &exclude) const
{
    QVector<SnapHit> hits;
    if (!m_objectSnap) {
        if (m_gridSnap) {
            SnapHit grid;
            grid.mode = SnapMode::Grid;
            grid.point = snapToGridPoint(cursor);
            grid.distance = distanceBetween(grid.point, cursor);
            hits.append(grid);
        }
        return hits;
    }

    collect(hits, folio, library, cursor, apertureMm, from, exclude);

    // Deux candidats au meme endroit et du meme mode font double emploi :
    // un coin partage par deux traits ne doit pas compter deux fois.
    QVector<SnapHit> unique;
    unique.reserve(hits.size());
    for (const SnapHit &hit : hits) {
        const bool duplicate = std::any_of(unique.cbegin(), unique.cend(), [&](const SnapHit &kept) {
            return kept.mode == hit.mode && samePoint(kept.point, hit.point, 1e-4);
        });
        if (!duplicate)
            unique.append(hit);
    }

    // Le classement mele distance et priorite : un milieu nettement plus
    // proche l'emporte sur une extremite lointaine, mais a distance egale
    // c'est l'extremite qui gagne.
    const double bias = apertureMm * 0.12;
    std::sort(unique.begin(), unique.end(), [&](const SnapHit &a, const SnapHit &b) {
        const double sa = a.distance + priority(a.mode) * bias;
        const double sb = b.distance + priority(b.mode) * bias;
        if (std::abs(sa - sb) > 1e-9)
            return sa < sb;
        return priority(a.mode) < priority(b.mode);
    });
    return unique;
}

std::optional<SnapHit> SnapEngine::snap(const Folio &folio, const SymbolLibrary &library,
                                        const QPointF &cursor, double apertureMm,
                                        const QPointF *from, const QString &exclude) const
{
    const QVector<SnapHit> hits = candidates(folio, library, cursor, apertureMm, from, exclude);
    if (hits.isEmpty())
        return std::nullopt;
    return hits.first();
}

// --------------------------------------------------------------------------
// Reperage d'accrochage aux objets (OTRACK)

namespace {

// Intersection de deux droites infinies, donnees par un point et un angle en
// degres ecran. Rien quand elles sont paralleles.
std::optional<QPointF> lineIntersection(const QPointF &p1, double a1,
                                        const QPointF &p2, double a2)
{
    const double r1 = a1 * std::numbers::pi / 180.0;
    const double r2 = a2 * std::numbers::pi / 180.0;
    const QPointF d1(std::cos(r1), std::sin(r1));
    const QPointF d2(std::cos(r2), std::sin(r2));
    const double denominator = d1.x() * d2.y() - d1.y() * d2.x();
    // Deux droites paralleles ne se croisent pas : elles se confondent ou
    // s'ignorent, et dans les deux cas il n'y a pas de point a designer.
    if (std::abs(denominator) < 1e-9)
        return std::nullopt;
    const QPointF delta = p2 - p1;
    const double t = (delta.x() * d2.y() - delta.y() * d2.x()) / denominator;
    return p1 + d1 * t;
}

// Distance du point a la droite passant par `origin` avec cet angle, et
// projete du point sur cette droite.
double distanceToLine(const QPointF &origin, double angleDegrees, const QPointF &point,
                      QPointF *projection)
{
    const double radians = angleDegrees * std::numbers::pi / 180.0;
    const QPointF direction(std::cos(radians), std::sin(radians));
    const QPointF delta = point - origin;
    const double along = delta.x() * direction.x() + delta.y() * direction.y();
    const QPointF foot = origin + direction * along;
    if (projection)
        *projection = foot;
    const QPointF offset = point - foot;
    return std::hypot(offset.x(), offset.y());
}

} // namespace

void SnapEngine::setTrackingEnabled(bool on)
{
    m_tracking = on;
    // Eteindre le reperage oublie les reperes : les garder ferait reapparaitre
    // des traits d'alignement surgis de nulle part au rallumage.
    if (!on)
        m_tracked.clear();
}

void SnapEngine::acquire(const QPointF &point, SnapMode mode)
{
    if (isTracked(point))
        return;
    m_tracked.append({ point, mode });
    while (m_tracked.size() > kMaxTrackedPoints)
        m_tracked.removeFirst();
}

void SnapEngine::release(const QPointF &point)
{
    m_tracked.removeIf([&](const TrackedPoint &t) { return samePoint(t.point, point); });
}

void SnapEngine::clearTracked() { m_tracked.clear(); }

void SnapEngine::toggleTracked(const QPointF &point, SnapMode mode)
{
    if (isTracked(point))
        release(point);
    else
        acquire(point, mode);
}

bool SnapEngine::isTracked(const QPointF &point) const
{
    return std::any_of(m_tracked.cbegin(), m_tracked.cend(),
                       [&](const TrackedPoint &t) { return samePoint(t.point, point); });
}

QVector<double> SnapEngine::trackingAngles() const
{
    // Sans suivi polaire, on ne suit que les orthogonales : c'est le reglage
    // par defaut d'AutoCAD, et sur un schema electrique c'est presque
    // toujours ce qu'on veut.
    QVector<double> angles{ 0.0, 90.0, 180.0, 270.0 };
    if (!m_polar || m_polarIncrement <= 0.0)
        return angles;

    for (double a = 0.0; a < 360.0 - 1e-9; a += m_polarIncrement) {
        const double normalized = std::fmod(a + 360.0, 360.0);
        if (!std::any_of(angles.cbegin(), angles.cend(),
                         [&](double existing) { return std::abs(existing - normalized) < 1e-6; })) {
            angles.append(normalized);
        }
    }
    return angles;
}

std::optional<TrackHit> SnapEngine::track(const QPointF &cursor, double apertureMm,
                                          const QPointF *from) const
{
    if (!m_tracking || !m_objectSnap || m_tracked.isEmpty() || apertureMm <= 0.0)
        return std::nullopt;

    const QVector<double> angles = trackingAngles();
    QVector<TrackHit> candidates;

    auto consider = [&](TrackHit hit) {
        const QPointF delta = hit.point - cursor;
        hit.distance = std::hypot(delta.x(), delta.y());
        if (hit.distance <= apertureMm)
            candidates.append(hit);
    };

    // 1. Projection sur un chemin d'alignement.
    for (const TrackedPoint &tracked : m_tracked) {
        for (double angle : angles) {
            QPointF projection;
            if (distanceToLine(tracked.point, angle, cursor, &projection) > apertureMm)
                continue;
            TrackHit hit;
            hit.point = projection;
            hit.origin = tracked.point;
            hit.originMode = tracked.mode;
            consider(hit);
        }
    }

    // 2. Croisement de deux chemins issus de reperes differents.
    for (int i = 0; i < m_tracked.size(); ++i) {
        for (int j = i + 1; j < m_tracked.size(); ++j) {
            for (double a1 : angles) {
                for (double a2 : angles) {
                    const auto crossing = lineIntersection(m_tracked.at(i).point, a1,
                                                           m_tracked.at(j).point, a2);
                    if (!crossing)
                        continue;
                    TrackHit hit;
                    hit.point = *crossing;
                    hit.origin = m_tracked.at(i).point;
                    hit.originMode = m_tracked.at(i).mode;
                    hit.hasSecond = true;
                    hit.secondOrigin = m_tracked.at(j).point;
                    hit.secondMode = m_tracked.at(j).mode;
                    consider(hit);
                }
            }
        }
    }

    // 3. Croisement avec la direction contrainte du trace en cours. C'est le
    // geste que le dispositif sert vraiment : « a l'aplomb du milieu de ce
    // fil, sur ma ligne horizontale ».
    if (from) {
        const auto constrained = constrainedAngle(*from, cursor);
        if (constrained) {
            for (const TrackedPoint &tracked : m_tracked) {
                for (double angle : angles) {
                    const auto crossing = lineIntersection(*from, *constrained,
                                                           tracked.point, angle);
                    if (!crossing)
                        continue;
                    TrackHit hit;
                    hit.point = *crossing;
                    hit.origin = tracked.point;
                    hit.originMode = tracked.mode;
                    hit.crossesConstraint = true;
                    hit.constraintOrigin = *from;
                    consider(hit);
                }
            }
        }
    }

    if (candidates.isEmpty())
        return std::nullopt;

    // Un croisement designe un point unique, une projection seulement une
    // direction : le croisement gagne meme un peu plus loin du curseur.
    std::sort(candidates.begin(), candidates.end(), [&](const TrackHit &a, const TrackHit &b) {
        const double bias = apertureMm * 0.5;
        const double sa = a.distance + (a.isCrossing() ? 0.0 : bias);
        const double sb = b.distance + (b.isCrossing() ? 0.0 : bias);
        if (std::abs(sa - sb) > 1e-9)
            return sa < sb;
        return a.distance < b.distance;
    });
    return candidates.first();
}

} // namespace dsn
