#include "componenttools.h"

#include "entities.h"
#include "wiretools.h"

#include <cmath>

namespace dsn {

QVector<ComponentTools::WireEnd> ComponentTools::attachedWireEnds(const Folio &folio,
                                                                  const SymbolLibrary &library,
                                                                  const SymbolInstance &symbol)
{
    QVector<WireEnd> out;
    const SymbolDefinition *definition = library.definition(symbol.definitionId);
    if (!definition)
        return out;

    QVector<QPointF> pins;
    pins.reserve(definition->pins.size());
    for (const Pin &pin : definition->pins) {
        if (pin.type == PinType::NotConnected)
            continue;
        pins.append(symbol.placement.map(pin.position));
    }
    if (pins.isEmpty())
        return out;

    for (const Wire *wire : folio.entitiesOfType<Wire>()) {
        if (wire->points.size() < 2)
            continue;
        // Seules les deux extremites : un fil qui passe au milieu d'une broche
        // sans y finir la croise, il n'y est pas raccorde.
        const int ends[2] = { 0, int(wire->points.size()) - 1 };
        for (int index : ends) {
            const QPointF &end = wire->points.at(index);
            for (const QPointF &pin : pins) {
                if (samePoint(end, pin)) {
                    out.append({ wire->id(), index });
                    break;
                }
            }
        }
    }
    return out;
}

std::optional<QPointF> ComponentTools::scootAxis(const Folio &folio,
                                                 const SymbolLibrary &library,
                                                 const SymbolInstance &symbol)
{
    const QVector<WireEnd> ends = attachedWireEnds(folio, library, symbol);
    if (ends.isEmpty())
        return std::nullopt;

    std::optional<QPointF> axis;
    for (const WireEnd &end : ends) {
        const auto *wire = dynamic_cast<const Wire *>(folio.entity(end.wireId));
        if (!wire || wire->points.size() < 2)
            continue;
        // Direction du segment qui touche la broche, ramenee a un demi-tour
        // pres : un fil qui part a gauche et un qui part a droite decrivent
        // le meme axe.
        const int other = end.vertex == 0 ? 1 : int(wire->points.size()) - 2;
        QPointF direction = wire->points.at(other) - wire->points.at(end.vertex);
        const double length = std::hypot(direction.x(), direction.y());
        if (length < kEpsilon)
            continue;
        direction /= length;
        if (direction.x() < -kEpsilon
            || (std::abs(direction.x()) <= kEpsilon && direction.y() < 0.0)) {
            direction = -direction;
        }

        if (!axis) {
            axis = direction;
            continue;
        }
        const double dot = axis->x() * direction.x() + axis->y() * direction.y();
        // Deux fils qui ne tirent pas dans le meme axe ne designent pas une
        // direction de glissement : mieux vaut ne rien contraindre.
        if (std::abs(dot) < 0.999)
            return std::nullopt;
    }
    return axis;
}

std::optional<ComponentTools::WireSplit> ComponentTools::splitForInsertion(
        const Folio &folio, const SymbolLibrary &library, const SymbolInstance &symbol)
{
    const SymbolDefinition *definition = library.definition(symbol.definitionId);
    if (!definition)
        return std::nullopt;

    QVector<QPointF> pins;
    for (const Pin &pin : definition->pins) {
        if (pin.type == PinType::NotConnected)
            continue;
        pins.append(symbol.placement.map(pin.position));
    }
    // Seul un appareil de passage se branche en coupant : une bobine a deux
    // bornes oui, un moteur a quatre non.
    if (pins.size() != 2)
        return std::nullopt;

    const QPointF insertion = symbol.placement.position;

    for (const Wire *wire : folio.entitiesOfType<Wire>()) {
        if (wire->points.size() < 2)
            continue;
        // Le fil doit passer par le point d'insertion : c'est ce qui dit que
        // l'appareil est pose dessus et non a cote.
        bool crosses = false;
        for (int i = 1; i < wire->points.size() && !crosses; ++i)
            crosses = pointOnSegment(insertion, wire->points.at(i - 1), wire->points.at(i));
        if (!crosses)
            continue;

        // Et les deux broches doivent tomber sur le trace, sinon l'appareil
        // est en travers du fil et le couper le laisserait en l'air.
        bool aligned = true;
        for (const QPointF &pin : pins) {
            bool onWire = false;
            for (int i = 1; i < wire->points.size() && !onWire; ++i)
                onWire = pointOnSegment(pin, wire->points.at(i - 1), wire->points.at(i));
            if (!onWire) {
                aligned = false;
                break;
            }
        }
        if (!aligned)
            continue;

        const double total = WireTools::polylineLength(wire->points);
        double first = WireTools::lengthAtPoint(wire->points, pins.at(0));
        double second = WireTools::lengthAtPoint(wire->points, pins.at(1));
        if (first > second)
            std::swap(first, second);
        if (first < 0.0 || second < 0.0 || second > total)
            continue;

        WireSplit split;
        split.wireId = wire->id();
        // Un morceau de longueur nulle n'est pas un fil : une borne posee a
        // l'extremite d'un fil ne doit pas en creer un degenere.
        if (first > kConnectTolerance)
            split.before = WireTools::subPolyline(wire->points, 0.0, first);
        if (total - second > kConnectTolerance)
            split.after = WireTools::subPolyline(wire->points, second, total);
        return split;
    }
    return std::nullopt;
}

QPointF ComponentTools::constrainToAxis(const QPointF &delta, const QPointF &axis)
{
    const double length = std::hypot(axis.x(), axis.y());
    if (length < kEpsilon)
        return delta;
    const QPointF unit = axis / length;
    const double along = delta.x() * unit.x() + delta.y() * unit.y();
    return unit * along;
}

} // namespace dsn
