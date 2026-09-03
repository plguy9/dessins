#include "componenttools.h"

#include "entities.h"
#include "wiretools.h"

#include <algorithm>
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

namespace {

// Retire les sommets qui n'inflechissent rien : apres une recouture, les deux
// points de broche se retrouvent souvent alignes avec leurs voisins, et un
// sommet inutile se voit des qu'on redeplace le fil.
void dropCollinear(QVector<QPointF> &points)
{
    for (int i = int(points.size()) - 2; i >= 1; --i) {
        const QPointF a = points.at(i - 1);
        const QPointF b = points.at(i);
        const QPointF d = points.at(i + 1);
        const double cross = (b.x() - a.x()) * (d.y() - a.y()) - (b.y() - a.y()) * (d.x() - a.x());
        // Le produit vectoriel est une aire : le seuil doit suivre la longueur,
        // sinon un long segment presque droit passerait pour un coude.
        const double echelle = std::max(1.0, std::hypot(d.x() - a.x(), d.y() - a.y()));
        if (std::abs(cross) / echelle < kEpsilon)
            points.remove(i);
    }
}

} // namespace

std::optional<ComponentTools::WireHeal> ComponentTools::healOnRemoval(
        const Folio &folio, const SymbolLibrary &library, const SymbolInstance &symbol,
        const QSet<QString> &alsoRemoved)
{
    QVector<WireEnd> ends;
    for (const WireEnd &end : attachedWireEnds(folio, library, symbol)) {
        if (!alsoRemoved.contains(end.wireId))
            ends.append(end);
    }
    // Deux extremites, et deux fils distincts. Un appareil a une seule
    // extremite raccordee n'ouvre rien en partant ; a trois, il y a un noeud,
    // et recoudre reviendrait a choisir a la place du dessinateur.
    if (ends.size() != 2 || ends.at(0).wireId == ends.at(1).wireId)
        return std::nullopt;

    const auto *first = dynamic_cast<const Wire *>(folio.entity(ends.at(0).wireId));
    const auto *second = dynamic_cast<const Wire *>(folio.entity(ends.at(1).wireId));
    if (!first || !second || first->points.size() < 2 || second->points.size() < 2)
        return std::nullopt;
    // Meme regle que JOINDRE : souder deux types de fils ferait disparaitre
    // une couleur — donc une section, donc une information de cablage — sans
    // que rien ne le dise.
    if (first->wireType != second->wireType || first->conductors != second->conductors)
        return std::nullopt;

    // Le survivant est celui qui porte le plus d'identite : un repere
    // verrouille d'abord, puis un repere tout court. Le fil recousu est le
    // meme conducteur qu'avant la pose de l'appareil ; il doit en garder le
    // nom, et c'est le nom saisi a la main qui compte le plus.
    const bool keepSecond = (second->numberLocked && !first->numberLocked)
            || (second->numberLocked == first->numberLocked && first->number.isEmpty()
                && !second->number.isEmpty());
    const Wire *keep = keepSecond ? second : first;
    const Wire *drop = keepSecond ? first : second;
    const WireEnd keepEnd = keepSecond ? ends.at(1) : ends.at(0);
    const WireEnd dropEnd = keepSecond ? ends.at(0) : ends.at(1);

    // Le survivant est oriente pour finir sur sa broche, l'absorbe pour en
    // repartir : la couture se fait alors bout a bout.
    QVector<QPointF> points = keep->points;
    if (keepEnd.vertex == 0)
        std::reverse(points.begin(), points.end());
    QVector<QPointF> suite = drop->points;
    if (dropEnd.vertex != 0)
        std::reverse(suite.begin(), suite.end());
    points += suite;
    dropCollinear(points);

    WireHeal heal;
    heal.keepWireId = keep->id();
    heal.removeWireId = drop->id();
    heal.points = points;
    return heal;
}

ComponentTools::SwapPlan ComponentTools::planSwap(const Folio &folio,
                                                  const SymbolLibrary &library,
                                                  const SymbolInstance &symbol,
                                                  const QString &newDefinitionId)
{
    SwapPlan plan;
    const SymbolDefinition *ancien = library.definition(symbol.definitionId);
    const SymbolDefinition *neuf = library.definition(newDefinitionId);
    if (!ancien || !neuf)
        return plan;
    plan.valid = true;

    // Les broches sont comparees dans le repere LOCAL du symbole : le
    // placement est le meme avant et apres, donc l'appariement ne doit pas
    // dependre de l'orientation.
    QVector<const Pin *> reprises;
    for (const Pin &pin : neuf->pins) {
        if (pin.type != PinType::NotConnected)
            reprises.append(&pin);
    }

    QSet<const Pin *> deja;
    for (const Pin &ancienne : ancien->pins) {
        if (ancienne.type == PinType::NotConnected)
            continue;
        const QPointF depart = symbol.placement.map(ancienne.position);

        // Quelles extremites de fil tiennent a cette broche ?
        QVector<WireEnd> tenues;
        for (const Wire *wire : folio.entitiesOfType<Wire>()) {
            if (wire->points.size() < 2)
                continue;
            const int ends[2] = { 0, int(wire->points.size()) - 1 };
            for (int index : ends) {
                if (samePoint(wire->points.at(index), depart))
                    tenues.append({ wire->id(), index });
            }
        }
        if (tenues.isEmpty())
            continue;

        // Le numero d'abord : un 13/14 reste un 13/14, quel que soit le
        // dessin du symbole. C'est le seul appariement qui a un sens
        // electrique ; la distance n'est qu'un secours.
        const Pin *cible = nullptr;
        for (const Pin *candidat : reprises) {
            if (!candidat->number.isEmpty() && candidat->number == ancienne.number
                && !deja.contains(candidat)) {
                cible = candidat;
                break;
            }
        }
        if (!cible) {
            double meilleure = 0.0;
            for (const Pin *candidat : reprises) {
                if (deja.contains(candidat))
                    continue;
                const double d = std::hypot(candidat->position.x() - ancienne.position.x(),
                                            candidat->position.y() - ancienne.position.y());
                if (!cible || d < meilleure) {
                    cible = candidat;
                    meilleure = d;
                }
            }
        }
        if (!cible) {
            plan.orphaned += int(tenues.size());
            continue;
        }
        deja.insert(cible);

        const QPointF arrivee = symbol.placement.map(cible->position);
        if (samePoint(arrivee, depart))
            continue; // la broche ne bouge pas : rien a suivre
        for (const WireEnd &end : tenues)
            plan.moves.append({ end.wireId, end.vertex, arrivee });
    }
    return plan;
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
