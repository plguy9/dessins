#include "edittools.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dsn {

namespace {

double toRadians(double degrees) { return degrees * std::numbers::pi / 180.0; }

QPointF rotatedAbout(const QPointF &p, const QPointF &center, double degrees)
{
    const double a = toRadians(degrees);
    const double c = std::cos(a);
    const double s = std::sin(a);
    const QPointF d = p - center;
    return center + QPointF(d.x() * c - d.y() * s, d.x() * s + d.y() * c);
}

// Le modele n'accepte que les quarts de tour pour un symbole : un angle libre
// compliquerait l'accrochage aux broches sans benefice metier. Un reseau
// polaire a 60 degres pivote donc ses symboles au quart le plus proche, et
// laisse le reste — fils, textes, graphiques — tourner librement.
Orientation nearestQuarter(double degrees)
{
    double normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    const int quarter = int(std::lround(normalized / 90.0)) % 4;
    return orientationFromDegrees(quarter * 90);
}

Orientation addedTo(Orientation base, Orientation extra)
{
    return orientationFromDegrees((toDegrees(base) + toDegrees(extra)) % 360);
}

bool collinear(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const QPointF u = b - a;
    const QPointF v = c - b;
    // Produit vectoriel rapporte aux longueurs : un ecart d'un centieme de
    // millimetre sur un segment d'un metre ne doit pas empecher la soudure.
    const double cross = u.x() * v.y() - u.y() * v.x();
    const double lengths = std::hypot(u.x(), u.y()) * std::hypot(v.x(), v.y());
    if (lengths <= kEpsilon)
        return true;
    return std::abs(cross) / lengths <= 1e-4;
}

} // namespace

// --------------------------------------------------------------------------
// RESEAU

int ArraySpec::itemCount() const
{
    if (kind == Kind::Polar)
        return std::max(1, count);
    return std::max(1, columns) * std::max(1, rows);
}

bool ArraySpec::isValid() const
{
    if (kind == Kind::Polar)
        return count >= 2;
    if (columns < 1 || rows < 1 || columns * rows < 2)
        return false;
    // Un reseau rectangulaire a pas nul empilerait toutes les copies au meme
    // endroit : elles seraient invisibles et impossibles a rattraper.
    if (columns > 1 && std::abs(columnSpacing) <= kConnectTolerance)
        return false;
    if (rows > 1 && std::abs(rowSpacing) <= kConnectTolerance)
        return false;
    return true;
}

QVector<ArrayPlacement> ArrayTools::placements(const ArraySpec &spec, const QPointF &anchor)
{
    QVector<ArrayPlacement> out;
    if (!spec.isValid())
        return out;

    if (spec.kind == ArraySpec::Kind::Rectangular) {
        out.reserve(spec.columns * spec.rows);
        int index = 0;
        for (int row = 0; row < spec.rows; ++row) {
            for (int column = 0; column < spec.columns; ++column) {
                ArrayPlacement p;
                p.offset = QPointF(column * spec.columnSpacing, row * spec.rowSpacing);
                p.column = column;
                p.row = row;
                p.index = index++;
                out.append(p);
            }
        }
        return out;
    }

    // Polaire. Un tour complet ne repete pas la position de depart : le pas
    // est l'angle total divise par le nombre d'elements. Sur un secteur, il
    // est divise par les intervalles, pour que le dernier element tombe
    // exactement sur la borne demandee.
    const bool fullTurn = std::abs(std::abs(spec.totalAngle) - 360.0) <= kEpsilon;
    const int divisor = fullTurn ? spec.count : std::max(1, spec.count - 1);
    const double step = spec.totalAngle / divisor;

    out.reserve(spec.count);
    for (int i = 0; i < spec.count; ++i) {
        ArrayPlacement p;
        p.angle = step * i;
        p.offset = rotatedAbout(anchor, spec.center, p.angle) - anchor;
        p.index = i;
        out.append(p);
    }
    return out;
}

void ArrayTools::apply(Entity &entity, const ArrayPlacement &placement, const QPointF &center)
{
    if (fuzzyZero(placement.angle)) {
        entity.translate(placement.offset);
        return;
    }

    // Rotation autour du centre du reseau, puis reprise de l'orientation pour
    // les entites qui en portent une.
    if (auto *symbol = dynamic_cast<SymbolInstance *>(&entity)) {
        symbol->placement.position = rotatedAbout(symbol->placement.position, center,
                                                  placement.angle);
        symbol->placement.orientation =
                addedTo(symbol->placement.orientation, nearestQuarter(placement.angle));
        return;
    }
    if (auto *text = dynamic_cast<TextItem *>(&entity)) {
        text->placement.position = rotatedAbout(text->placement.position, center,
                                                placement.angle);
        text->placement.orientation =
                addedTo(text->placement.orientation, nearestQuarter(placement.angle));
        return;
    }
    if (auto *wire = dynamic_cast<Wire *>(&entity)) {
        for (QPointF &p : wire->points)
            p = rotatedAbout(p, center, placement.angle);
        return;
    }
    if (auto *junction = dynamic_cast<Junction *>(&entity)) {
        junction->point = rotatedAbout(junction->point, center, placement.angle);
        return;
    }
    if (auto *label = dynamic_cast<Label *>(&entity)) {
        label->point = rotatedAbout(label->point, center, placement.angle);
        label->direction = directionFromDegrees(
                (toDegrees(label->direction) + toDegrees(nearestQuarter(placement.angle))) % 360);
        return;
    }
    if (auto *graphic = dynamic_cast<GraphicItem *>(&entity)) {
        for (QPointF &p : graphic->shape.points)
            p = rotatedAbout(p, center, placement.angle);
        graphic->shape.startAngle -= placement.angle; // convention Qt : sens trigonometrique
        return;
    }
    entity.translate(placement.offset);
}

// --------------------------------------------------------------------------
// ALIGNER

QString alignModeLabel(AlignMode mode)
{
    switch (mode) {
    case AlignMode::Left: return QStringLiteral("Aligner a gauche");
    case AlignMode::HorizontalCenter: return QStringLiteral("Centrer horizontalement");
    case AlignMode::Right: return QStringLiteral("Aligner a droite");
    case AlignMode::Top: return QStringLiteral("Aligner en haut");
    case AlignMode::VerticalCenter: return QStringLiteral("Centrer verticalement");
    case AlignMode::Bottom: return QStringLiteral("Aligner en bas");
    case AlignMode::DistributeHorizontally: return QStringLiteral("Repartir horizontalement");
    case AlignMode::DistributeVertically: return QStringLiteral("Repartir verticalement");
    }
    return QString();
}

int AlignTools::minimumCount(AlignMode mode)
{
    switch (mode) {
    case AlignMode::DistributeHorizontally:
    case AlignMode::DistributeVertically:
        // Repartir deux elements ne fait rien : les extremes ne bougent pas,
        // et il n'y a personne entre eux.
        return 3;
    default:
        return 2;
    }
}

QVector<QPointF> AlignTools::offsets(const QVector<QRectF> &boxes, AlignMode mode)
{
    QVector<QPointF> out;
    if (boxes.size() < minimumCount(mode))
        return out;

    out.resize(boxes.size());

    QRectF hull = boxes.first();
    for (const QRectF &box : boxes)
        hull = hull.united(box);

    switch (mode) {
    case AlignMode::Left:
        for (int i = 0; i < boxes.size(); ++i)
            out[i] = QPointF(hull.left() - boxes.at(i).left(), 0.0);
        return out;
    case AlignMode::Right:
        for (int i = 0; i < boxes.size(); ++i)
            out[i] = QPointF(hull.right() - boxes.at(i).right(), 0.0);
        return out;
    case AlignMode::HorizontalCenter:
        for (int i = 0; i < boxes.size(); ++i)
            out[i] = QPointF(hull.center().x() - boxes.at(i).center().x(), 0.0);
        return out;
    case AlignMode::Top:
        for (int i = 0; i < boxes.size(); ++i)
            out[i] = QPointF(0.0, hull.top() - boxes.at(i).top());
        return out;
    case AlignMode::Bottom:
        for (int i = 0; i < boxes.size(); ++i)
            out[i] = QPointF(0.0, hull.bottom() - boxes.at(i).bottom());
        return out;
    case AlignMode::VerticalCenter:
        for (int i = 0; i < boxes.size(); ++i)
            out[i] = QPointF(0.0, hull.center().y() - boxes.at(i).center().y());
        return out;
    case AlignMode::DistributeHorizontally:
    case AlignMode::DistributeVertically:
        break;
    }

    // Repartition : les deux extremes ne bougent pas, les autres se placent a
    // pas egal entre eux. On repartit les centres et non les bords, sinon des
    // elements de tailles differentes paraissent mal espaces.
    const bool horizontal = mode == AlignMode::DistributeHorizontally;
    QVector<int> order(boxes.size());
    for (int i = 0; i < boxes.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return horizontal ? boxes.at(a).center().x() < boxes.at(b).center().x()
                          : boxes.at(a).center().y() < boxes.at(b).center().y();
    });

    const QRectF &first = boxes.at(order.first());
    const QRectF &last = boxes.at(order.last());
    const double from = horizontal ? first.center().x() : first.center().y();
    const double to = horizontal ? last.center().x() : last.center().y();
    const double step = (to - from) / (boxes.size() - 1);

    for (int rank = 0; rank < order.size(); ++rank) {
        const int index = order.at(rank);
        const QRectF &box = boxes.at(index);
        const double target = from + step * rank;
        const double current = horizontal ? box.center().x() : box.center().y();
        out[index] = horizontal ? QPointF(target - current, 0.0)
                                : QPointF(0.0, target - current);
    }
    return out;
}

// --------------------------------------------------------------------------
// JOINDRE et COUPER

std::optional<WireJoin> EditTools::joinable(const Folio &folio, const QString &firstId,
                                            const QString &secondId)
{
    const auto *a = dynamic_cast<const Wire *>(folio.entity(firstId));
    const auto *b = dynamic_cast<const Wire *>(folio.entity(secondId));
    if (!a || !b || a == b || a->points.size() < 2 || b->points.size() < 2)
        return std::nullopt;

    // Souder deux types differents perdrait une couleur en silence : c'est
    // exactement le genre de perte qu'on ne remarque qu'a la relecture du
    // dossier imprime.
    if (a->wireType != b->wireType || a->conductors != b->conductors)
        return std::nullopt;

    // Quatre facons de se toucher : fin-debut, fin-fin, debut-debut,
    // debut-fin. On ramene toutes a « la fin de A rejoint le debut de B ».
    QVector<QPointF> first = a->points;
    QVector<QPointF> second = b->points;

    if (samePoint(first.last(), second.first())) {
        // deja dans le bon sens
    } else if (samePoint(first.last(), second.last())) {
        std::reverse(second.begin(), second.end());
    } else if (samePoint(first.first(), second.last())) {
        std::swap(first, second);
    } else if (samePoint(first.first(), second.first())) {
        std::reverse(first.begin(), first.end());
    } else {
        return std::nullopt;
    }

    // Le sommet commun ne survit que s'il forme un vrai coude. Colineaire, il
    // disparait : deux fils bout a bout font un fil droit, pas un fil avec un
    // point de rebroussement invisible.
    WireJoin join;
    join.firstId = firstId;
    join.secondId = secondId;
    join.merged = first;
    const bool straight = collinear(first.at(first.size() - 2), first.last(), second.at(1));
    for (int i = straight ? 1 : 0; i < second.size(); ++i) {
        if (i == 0)
            continue;
        join.merged.append(second.at(i));
    }
    if (straight)
        join.merged.remove(first.size() - 1);
    return join;
}

std::optional<WireCut> EditTools::cut(const Folio &folio, const QString &wireId,
                                      const QPointF &at)
{
    const auto *wire = dynamic_cast<const Wire *>(folio.entity(wireId));
    if (!wire || wire->points.size() < 2)
        return std::nullopt;

    // Couper sur une extremite ne produirait qu'un fil vide et un fil
    // identique : on refuse plutot que de laisser croire que c'est fait.
    if (samePoint(at, wire->points.first()) || samePoint(at, wire->points.last()))
        return std::nullopt;

    for (int i = 1; i < wire->points.size(); ++i) {
        const QPointF &a = wire->points.at(i - 1);
        const QPointF &b = wire->points.at(i);
        if (!pointOnSegment(at, a, b))
            continue;

        WireCut cut;
        cut.wireId = wireId;
        for (int j = 0; j < i; ++j)
            cut.before.append(wire->points.at(j));
        cut.before.append(at);

        cut.after.append(at);
        for (int j = i; j < wire->points.size(); ++j)
            cut.after.append(wire->points.at(j));

        // Un morceau reduit a deux points confondus n'est pas un fil : cela
        // arrive quand le point de coupe tombe pile sur un sommet interieur.
        if (cut.before.size() < 2 || cut.after.size() < 2)
            return std::nullopt;
        if (samePoint(cut.before.first(), cut.before.last()) && cut.before.size() == 2)
            return std::nullopt;
        if (samePoint(cut.after.first(), cut.after.last()) && cut.after.size() == 2)
            return std::nullopt;
        return cut;
    }
    return std::nullopt;
}

} // namespace dsn
