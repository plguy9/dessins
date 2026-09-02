#include "wiretools.h"

#include "entities.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dsn {

namespace {

struct Segment {
    QPointF a;
    QPointF b;
    QString id;
};

// Tous les segments du folio susceptibles de couper un fil : les autres fils,
// et les traits de broche. Une broche est un obstacle legitime — c'est meme
// la cible la plus frequente d'un prolongement.
QVector<Segment> obstacles(const Folio &folio, const SymbolLibrary &library,
                           const QString &excludeId)
{
    QVector<Segment> out;
    for (const EntityPtr &entity : folio.entities()) {
        if (entity->id() == excludeId)
            continue;
        if (const auto *wire = dynamic_cast<const Wire *>(entity.get())) {
            for (int i = 1; i < wire->points.size(); ++i)
                out.append({ wire->points.at(i - 1), wire->points.at(i), wire->id() });
            continue;
        }
        if (const auto *symbol = dynamic_cast<const SymbolInstance *>(entity.get())) {
            const SymbolDefinition *definition = library.definition(symbol->definitionId);
            if (!definition)
                continue;
            for (const Pin &pin : definition->pins) {
                if (pin.type == PinType::NotConnected)
                    continue;
                out.append({ symbol->placement.map(pin.root()),
                             symbol->placement.map(pin.position), symbol->id() });
            }
        }
    }
    return out;
}

} // namespace

double WireTools::polylineLength(const QVector<QPointF> &points)
{
    double total = 0.0;
    for (int i = 1; i < points.size(); ++i)
        total += std::hypot(points.at(i).x() - points.at(i - 1).x(),
                            points.at(i).y() - points.at(i - 1).y());
    return total;
}

QPointF WireTools::pointAtLength(const QVector<QPointF> &points, double distance)
{
    if (points.isEmpty())
        return QPointF();
    if (distance <= 0.0)
        return points.first();

    double walked = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        const QPointF a = points.at(i - 1);
        const QPointF b = points.at(i);
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (walked + length >= distance) {
            const double t = length <= kEpsilon ? 0.0 : (distance - walked) / length;
            return a + t * (b - a);
        }
        walked += length;
    }
    return points.last();
}

double WireTools::lengthAtPoint(const QVector<QPointF> &points, const QPointF &point)
{
    double walked = 0.0;
    double best = 0.0;
    double bestDistance = std::numeric_limits<double>::max();

    for (int i = 1; i < points.size(); ++i) {
        const QPointF a = points.at(i - 1);
        const QPointF b = points.at(i);
        const QPointF ab = b - a;
        const double length = std::hypot(ab.x(), ab.y());
        if (length > kEpsilon) {
            double t = ((point.x() - a.x()) * ab.x() + (point.y() - a.y()) * ab.y())
                    / (length * length);
            t = std::clamp(t, 0.0, 1.0);
            const QPointF projection = a + t * ab;
            const double distance = std::hypot(point.x() - projection.x(),
                                               point.y() - projection.y());
            if (distance < bestDistance) {
                bestDistance = distance;
                best = walked + t * length;
            }
        }
        walked += length;
    }
    return best;
}

QVector<QPointF> WireTools::subPolyline(const QVector<QPointF> &points, double from, double to)
{
    QVector<QPointF> out;
    if (points.size() < 2 || to - from <= kConnectTolerance)
        return out;

    out.append(pointAtLength(points, from));

    // Les sommets compris dans l'intervalle sont conserves : un fil coude
    // ajuste doit garder ses coudes, sinon la decoupe le redresse.
    double walked = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        const QPointF a = points.at(i - 1);
        const QPointF b = points.at(i);
        walked += std::hypot(b.x() - a.x(), b.y() - a.y());
        if (walked > from + kConnectTolerance && walked < to - kConnectTolerance)
            out.append(b);
    }

    out.append(pointAtLength(points, to));
    return out;
}

std::optional<TrimResult> WireTools::trim(const Folio &folio, const SymbolLibrary &library,
                                          const QString &wireId, const QPointF &at)
{
    const auto *wire = dynamic_cast<const Wire *>(folio.entity(wireId));
    if (!wire || wire->points.size() < 2)
        return std::nullopt;

    const double total = polylineLength(wire->points);
    if (total <= kConnectTolerance)
        return std::nullopt;

    // Abscisses curvilignes de tous les croisements avec le reste du folio.
    QVector<double> cuts;
    const QVector<Segment> others = obstacles(folio, library, wireId);
    for (int i = 1; i < wire->points.size(); ++i) {
        for (const Segment &segment : others) {
            const auto crossing = segmentIntersection(wire->points.at(i - 1), wire->points.at(i),
                                                      segment.a, segment.b);
            if (crossing)
                cuts.append(lengthAtPoint(wire->points, *crossing));
        }
    }
    std::sort(cuts.begin(), cuts.end());

    const double target = lengthAtPoint(wire->points, at);

    // Les bornes qui encadrent le point vise.
    double lower = 0.0;
    double upper = total;
    bool hasLower = false;
    bool hasUpper = false;
    for (double cut : std::as_const(cuts)) {
        if (cut < target - kConnectTolerance) {
            lower = cut;
            hasLower = true;
        } else if (cut > target + kConnectTolerance && !hasUpper) {
            upper = cut;
            hasUpper = true;
        }
    }

    TrimResult result;
    result.cutFrom = pointAtLength(wire->points, hasLower ? lower : 0.0);
    result.cutTo = pointAtLength(wire->points, hasUpper ? upper : total);

    // Sans croisement d'aucun cote, il n'y a rien a garder : le fil entier
    // s'en va. C'est le comportement d'AutoCAD, et il reste previsible.
    if (hasLower) {
        const QVector<QPointF> head = subPolyline(wire->points, 0.0, lower);
        if (head.size() >= 2)
            result.pieces.append(head);
    }
    if (hasUpper) {
        const QVector<QPointF> tail = subPolyline(wire->points, upper, total);
        if (tail.size() >= 2)
            result.pieces.append(tail);
    }
    return result;
}

std::optional<QPointF> WireTools::extend(const Folio &folio, const SymbolLibrary &library,
                                         const QString &wireId, bool lastEnd)
{
    const auto *wire = dynamic_cast<const Wire *>(folio.entity(wireId));
    if (!wire || wire->points.size() < 2)
        return std::nullopt;

    const QPointF tip = lastEnd ? wire->points.last() : wire->points.first();
    const QPointF inner = lastEnd ? wire->points.at(wire->points.size() - 2) : wire->points.at(1);
    QPointF direction = tip - inner;
    const double length = std::hypot(direction.x(), direction.y());
    if (length <= kEpsilon)
        return std::nullopt;
    direction /= length;

    // Un rayon assez long pour traverser n'importe quel format de feuille.
    const QPointF far = tip + direction * 5000.0;

    std::optional<QPointF> best;
    double bestDistance = std::numeric_limits<double>::max();

    // Le point de depart lui-meme n'est pas une cible : sans ce garde-fou, un
    // fil deja raccorde ne se prolongerait jamais.
    const auto consider = [&](const QPointF &candidate) {
        const double distance = std::hypot(candidate.x() - tip.x(), candidate.y() - tip.y());
        if (distance <= kConnectTolerance || distance >= bestDistance)
            return;
        bestDistance = distance;
        best = candidate;
    };

    // Distance signee le long du rayon : negative derriere l'extremite.
    const auto along = [&](const QPointF &p) {
        const QPointF relative = p - tip;
        return relative.x() * direction.x() + relative.y() * direction.y();
    };
    // Ecart lateral au rayon : sert a reconnaitre un obstacle aligne.
    const auto lateral = [&](const QPointF &p) {
        const QPointF relative = p - tip;
        return std::abs(relative.x() * direction.y() - relative.y() * direction.x());
    };

    const QVector<Segment> others = obstacles(folio, library, wireId);
    for (const Segment &segment : others) {
        if (const auto crossing = segmentIntersection(tip, far, segment.a, segment.b)) {
            consider(*crossing);
            continue;
        }

        // Un obstacle colineaire ne « croise » rien au sens geometrique, mais
        // c'est le cas le plus frequent sur un schema : prolonger un fil
        // jusqu'a une borne alignee. On vise alors son extremite la plus
        // proche, en avant du rayon.
        if (lateral(segment.a) > kConnectTolerance || lateral(segment.b) > kConnectTolerance)
            continue;
        for (const QPointF &end : { segment.a, segment.b }) {
            if (along(end) > kConnectTolerance)
                consider(end);
        }
    }
    return best;
}


QVector<QPointF> WireTools::offsetPolyline(const QVector<QPointF> &points, double distance)
{
    if (points.size() < 2 || std::abs(distance) < 1e-9)
        return points;

    struct Segment {
        QPointF a;
        QPointF b;
    };
    QVector<Segment> shifted;
    shifted.reserve(points.size() - 1);
    for (int i = 0; i + 1 < points.size(); ++i) {
        QPointF direction = points.at(i + 1) - points.at(i);
        const double length = std::hypot(direction.x(), direction.y());
        // Un sommet double ne porte aucune direction : il ne definit pas de
        // droite a decaler, et l'ignorer vaut mieux que decaler au hasard.
        if (length < 1e-9)
            continue;
        direction /= length;
        const QPointF normal(-direction.y() * distance, direction.x() * distance);
        shifted.append({ points.at(i) + normal, points.at(i + 1) + normal });
    }
    if (shifted.isEmpty())
        return points;

    QVector<QPointF> result;
    result.reserve(shifted.size() + 1);
    result.append(shifted.first().a);
    for (int i = 1; i < shifted.size(); ++i) {
        // Intersection des DROITES, pas des segments : sur un coude rentrant
        // le point cherche tombe hors des deux segments decales, et exiger
        // qu'il soit dedans rendrait le sommet introuvable.
        const QPointF p = shifted.at(i - 1).a;
        const QPointF r = shifted.at(i - 1).b - p;
        const QPointF q = shifted.at(i).a;
        const QPointF s = shifted.at(i).b - q;
        const double denominator = r.x() * s.y() - r.y() * s.x();
        if (std::abs(denominator) < 1e-12) {
            // Segments colineaires : il n'y a pas d'angle a rattraper, le
            // sommet decale convient tel quel.
            result.append(shifted.at(i).a);
            continue;
        }
        const QPointF delta = q - p;
        const double t = (delta.x() * s.y() - delta.y() * s.x()) / denominator;
        result.append(p + r * t);
    }
    result.append(shifted.last().b);
    return result;
}

QVector<QVector<QPointF>> WireTools::busPaths(const QVector<QPointF> &path,
                                             const BusSpec &spec)
{
    QVector<QVector<QPointF>> paths;
    if (path.size() < 2 || !spec.isValid())
        return paths;

    // Normale du premier segment, ramenee vers le bas — ou vers la droite si
    // le segment est vertical. Le signe se decide une fois pour tout le bus :
    // le decider segment par segment ferait se croiser les conducteurs au
    // premier coude.
    QPointF direction = path.at(1) - path.at(0);
    for (int i = 1; i < path.size() && std::hypot(direction.x(), direction.y()) < 1e-9; ++i)
        direction = path.at(i) - path.at(0);
    const QPointF normal(-direction.y(), direction.x());
    double sign = 1.0;
    if (normal.y() < -1e-9 || (std::abs(normal.y()) <= 1e-9 && normal.x() < 0.0))
        sign = -1.0;

    paths.append(path);
    for (int k = 1; k < spec.count; ++k)
        paths.append(offsetPolyline(path, sign * spec.spacing * k));
    return paths;
}

} // namespace dsn
