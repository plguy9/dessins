#include "numbering.h"

#include "core/entities.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace dsn {

namespace {

// Ordre de lecture d'un schema : de haut en bas, puis de gauche a droite.
// C'est l'ordre dans lequel un electricien parcourt un folio, donc l'ordre
// dans lequel il s'attend a voir les reperes progresser.
bool readingOrder(const QPointF &a, const QPointF &b)
{
    if (!fuzzyEqual(a.y(), b.y(), 0.5))
        return a.y() < b.y();
    return a.x() < b.x();
}

QString disambiguate(const QString &base, const QSet<QString> &taken)
{
    if (!taken.contains(base))
        return base;
    // Suffixe alphabetique : 305, 305A, 305B... plutot qu'un increment
    // numerique qui empieterait sur la colonne suivante.
    for (char suffix = 'A'; suffix <= 'Z'; ++suffix) {
        const QString candidate = base + QChar::fromLatin1(suffix);
        if (!taken.contains(candidate))
            return candidate;
    }
    int n = 2;
    while (taken.contains(base + QLatin1Char('.') + QString::number(n)))
        ++n;
    return base + QLatin1Char('.') + QString::number(n);
}

struct NetPlacement {
    const Folio *folio = nullptr;
    int folioIndex = -1;
    QPointF anchor;
};

// Point representatif d'un potentiel : le premier de ses fils dans l'ordre de
// lecture, sur le premier folio ou il apparait.
NetPlacement placementOf(const Project &project, const Netlist::Net &net)
{
    NetPlacement best;
    for (const Netlist::WireRef &ref : net.wires) {
        const int index = project.indexOf(ref.folioId);
        if (index < 0)
            continue;
        const Folio *folio = project.folioAt(index);
        const auto *wire = dynamic_cast<const Wire *>(folio->entity(ref.wireId));
        if (!wire || wire->points.isEmpty())
            continue;

        QPointF anchor = wire->points.first();
        for (const QPointF &p : wire->points) {
            if (readingOrder(p, anchor))
                anchor = p;
        }
        if (best.folioIndex < 0 || index < best.folioIndex
            || (index == best.folioIndex && readingOrder(anchor, best.anchor))) {
            best.folio = folio;
            best.folioIndex = index;
            best.anchor = anchor;
        }
    }
    return best;
}

} // namespace

QSet<QString> Numbering::usedWireNumbers(const Project &project)
{
    QSet<QString> used;
    for (const Folio *folio : project.folios()) {
        for (const Wire *wire : folio->entitiesOfType<Wire>()) {
            if (wire->numberLocked && !wire->number.isEmpty())
                used.insert(wire->number);
        }
    }
    return used;
}

QSet<QString> Numbering::usedDesignations(const Project &project)
{
    QSet<QString> used;
    for (const Folio *folio : project.folios()) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            if (symbol->designationLocked && !symbol->designation().isEmpty())
                used.insert(symbol->designation());
        }
    }
    return used;
}

NumberingResult Numbering::numberWires(Project &project, const Netlist &netlist,
                                       const Profile &profile)
{
    NumberingResult result;
    const WireNumberingRule &rule = profile.wireNumbering;

    QSet<QString> taken = usedWireNumbers(project);
    QHash<QString, int> counterByFolio; // compteur sequentiel par folio
    int globalCounter = rule.start;

    // Ordre stable des potentiels : sans cela, deux regenerations successives
    // produisent des reperes differents sur un dessin inchange.
    QVector<const Netlist::Net *> ordered;
    ordered.reserve(netlist.netCount());
    for (const Netlist::Net &net : netlist.nets()) {
        if (!net.wires.isEmpty())
            ordered.append(&net);
    }

    QHash<const Netlist::Net *, NetPlacement> placements;
    for (const Netlist::Net *net : ordered)
        placements.insert(net, placementOf(project, *net));

    std::sort(ordered.begin(), ordered.end(),
              [&](const Netlist::Net *a, const Netlist::Net *b) {
                  const NetPlacement &pa = placements.value(a);
                  const NetPlacement &pb = placements.value(b);
                  if (pa.folioIndex != pb.folioIndex)
                      return pa.folioIndex < pb.folioIndex;
                  return readingOrder(pa.anchor, pb.anchor);
              });

    for (const Netlist::Net *net : ordered) {
        // Un repere verrouille quelque part sur le potentiel gouverne tout le
        // potentiel : le meme fil electrique ne peut pas porter deux reperes.
        QString manual;
        for (const Netlist::WireRef &ref : net->wires) {
            const Folio *folio = project.folio(ref.folioId);
            if (!folio)
                continue;
            const auto *wire = dynamic_cast<const Wire *>(folio->entity(ref.wireId));
            if (wire && wire->numberLocked && !wire->number.isEmpty()) {
                manual = wire->number;
                break;
            }
        }

        QString number = manual;
        if (number.isEmpty()) {
            const NetPlacement &placement = placements.value(net);
            switch (rule.strategy) {
            case WireNumberingRule::Strategy::PotentialName:
                if (!net->name.isEmpty())
                    number = net->name;
                break;
            case WireNumberingRule::Strategy::FolioColumn:
                if (placement.folio) {
                    const int column = placement.folio->columnAt(placement.anchor);
                    if (column > 0) {
                        const QString folioTag = placement.folio->number.isEmpty()
                                ? QString::number(placement.folioIndex + 1)
                                : placement.folio->number;
                        number = rule.prefix + folioTag
                                + QString::number(column).rightJustified(2, QLatin1Char('0'));
                    }
                }
                break;
            case WireNumberingRule::Strategy::Sequential:
                break;
            }

            if (number.isEmpty()) {
                // Repli sequentiel : c'est aussi le mode nominal de la
                // strategie Sequential.
                if (rule.perFolio) {
                    const QString key = net->folioIds.isEmpty() ? QString() : net->folioIds.first();
                    int &counter = counterByFolio[key];
                    if (counter == 0)
                        counter = rule.start;
                    number = rule.format(counter);
                    counter += rule.step;
                } else {
                    number = rule.format(globalCounter);
                    globalCounter += rule.step;
                }
            }
            number = disambiguate(number, taken);
        } else {
            ++result.keptManual;
        }

        taken.insert(number);
        ++result.netsNumbered;

        for (const Netlist::WireRef &ref : net->wires) {
            Folio *folio = project.folio(ref.folioId);
            if (!folio)
                continue;
            auto *wire = dynamic_cast<Wire *>(folio->entity(ref.wireId));
            if (!wire || wire->numberLocked)
                continue;
            if (wire->number != number) {
                wire->number = number;
                ++result.wiresNumbered;
            }
        }
    }

    return result;
}

NumberingResult Numbering::designateDevices(Project &project, const Profile &profile)
{
    NumberingResult result;
    const DesignationRule &rule = profile.designation;

    // Un appareil multi-blocs (bobine + contacts auxiliaires) partage une seule
    // designation : on raisonne par groupe, pas par instance.
    struct Group {
        QString key;
        QVector<SymbolInstance *> members;
        int folioIndex = 0;
        QPointF anchor;
        QString prefix;
        bool locked = false;
        QString lockedDesignation;
    };

    QHash<QString, Group> groups;
    QVector<QString> order;

    const auto folios = project.folios();
    for (int folioIndex = 0; folioIndex < int(folios.size()); ++folioIndex) {
        Folio *folio = folios[std::size_t(folioIndex)];
        for (SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
            if (!definition)
                continue;

            QString prefix = rule.prefixByDeviceKind.value(definition->deviceKind);
            if (prefix.isEmpty())
                prefix = definition->designationPrefix;
            if (prefix.isEmpty())
                prefix = QStringLiteral("A");

            const QString key = symbol->deviceGroup.isEmpty()
                    ? QStringLiteral("#") + symbol->id()
                    : QStringLiteral("g:") + symbol->deviceGroup;

            auto it = groups.find(key);
            if (it == groups.end()) {
                Group group;
                group.key = key;
                group.folioIndex = folioIndex;
                group.anchor = symbol->placement.position;
                group.prefix = prefix;
                it = groups.insert(key, group);
                order.append(key);
            } else if (folioIndex < it->folioIndex
                       || (folioIndex == it->folioIndex
                           && readingOrder(symbol->placement.position, it->anchor))) {
                it->folioIndex = folioIndex;
                it->anchor = symbol->placement.position;
            }

            it->members.append(symbol);
            if (symbol->designationLocked && !symbol->designation().isEmpty()) {
                it->locked = true;
                it->lockedDesignation = symbol->designation();
            }
        }
    }

    std::sort(order.begin(), order.end(), [&](const QString &a, const QString &b) {
        const Group &ga = groups[a];
        const Group &gb = groups[b];
        if (ga.folioIndex != gb.folioIndex)
            return ga.folioIndex < gb.folioIndex;
        return readingOrder(ga.anchor, gb.anchor);
    });

    QSet<QString> taken = usedDesignations(project);
    QHash<QString, int> counters; // cle : prefixe, ou folio + prefixe

    for (const QString &key : std::as_const(order)) {
        Group &group = groups[key];

        if (group.locked) {
            // Le groupe adopte la designation saisie a la main, y compris ses
            // blocs qui ne l'avaient pas encore.
            for (SymbolInstance *symbol : group.members) {
                if (symbol->designation() != group.lockedDesignation)
                    symbol->setDesignation(group.lockedDesignation);
            }
            ++result.keptManual;
            continue;
        }

        const QString counterKey = rule.perFolio
                ? QString::number(group.folioIndex) + QLatin1Char('/') + group.prefix
                : group.prefix;
        int &counter = counters[counterKey];

        QString designation;
        do {
            ++counter;
            designation = rule.format(group.prefix, counter);
        } while (taken.contains(designation));

        taken.insert(designation);
        for (SymbolInstance *symbol : group.members)
            symbol->setDesignation(designation);
        ++result.devicesDesignated;
    }

    return result;
}

NumberingResult Numbering::renumberAll(Project &project, const Profile &profile)
{
    NumberingResult result = designateDevices(project, profile);
    // La netlist est reconstruite apres la designation : les references de
    // broches qu'elle porte doivent citer les designations definitives.
    const Netlist netlist = Netlist::build(project);
    const NumberingResult wires = numberWires(project, netlist, profile);
    result.wiresNumbered = wires.wiresNumbered;
    result.netsNumbered = wires.netsNumbered;
    result.keptManual += wires.keptManual;
    result.notes += wires.notes;
    return result;
}

} // namespace dsn
