#include "reports.h"

#include "core/entities.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace dsn {

namespace {

QString folioTag(const Project &project, const QString &folioId)
{
    const Folio *folio = project.folio(folioId);
    if (!folio)
        return QString();
    return folio->number.isEmpty() ? folio->title : folio->number;
}

QStringList folioTags(const Project &project, const QStringList &folioIds)
{
    QStringList tags;
    for (const QString &id : folioIds) {
        const QString tag = folioTag(project, id);
        if (!tag.isEmpty() && !tags.contains(tag))
            tags.append(tag);
    }
    tags.sort();
    return tags;
}

// Tri naturel des designations : -K2 avant -K10, ce qu'un tri alphabetique
// ne donne pas et qu'un lecteur de nomenclature remarque immediatement.
bool naturalLess(const QString &a, const QString &b)
{
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            int startI = i, startJ = j;
            while (i < a.size() && a.at(i).isDigit()) ++i;
            while (j < b.size() && b.at(j).isDigit()) ++j;
            const long long na = a.mid(startI, i - startI).toLongLong();
            const long long nb = b.mid(startJ, j - startJ).toLongLong();
            if (na != nb)
                return na < nb;
        } else {
            if (a.at(i) != b.at(j))
                return a.at(i) < b.at(j);
            ++i;
            ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

// Folios retenus par le perimetre. Un seul point de filtrage : tous les
// rapports le traversent, aucun ne peut l'oublier.
std::vector<const Folio *> foliosInScope(const Project &project, const ReportScope &scope)
{
    std::vector<const Folio *> kept;
    for (const Folio *folio : project.folios()) {
        if (scope.includes(folio->id()))
            kept.push_back(folio);
    }
    return kept;
}

} // namespace

QVector<BomLine> Reports::billOfMaterials(const Project &project, const ReportScope &scope)
{
    // Un appareil multi-blocs — un contacteur, sa bobine, ses contacts
    // auxiliaires — est un seul article a commander. Le regroupement se fait
    // donc par appareil, pas par symbole pose : sans cela un contacteur
    // apparait trois fois dans la nomenclature.
    struct Device {
        const SymbolInstance *representative = nullptr;
        int representativeBlock = 0;
        int representativeFolio = 0;
        QString designation;
        QString value;
        QString manufacturer;
        QString partNumber;
        QStringList folios;
    };

    QHash<QString, Device> devices;
    QStringList order;

    const auto folios = foliosInScope(project, scope);
    for (int folioIndex = 0; folioIndex < int(folios.size()); ++folioIndex) {
        const Folio *folio = folios[std::size_t(folioIndex)];
        const QString tag = folio->number.isEmpty() ? folio->title : folio->number;

        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            const QString key = symbol->deviceGroup.isEmpty()
                    ? QStringLiteral("#") + symbol->id()
                    : QStringLiteral("g:") + symbol->deviceGroup;

            auto it = devices.find(key);
            if (it == devices.end()) {
                Device device;
                device.representative = symbol;
                device.representativeBlock = symbol->blockIndex;
                device.representativeFolio = folioIndex;
                it = devices.insert(key, device);
                order.append(key);
            } else if (symbol->blockIndex < it->representativeBlock
                       || (symbol->blockIndex == it->representativeBlock
                           && folioIndex < it->representativeFolio)) {
                // Le bloc principal represente l'appareil : a defaut d'indice
                // explicite, c'est le premier pose qui fait foi.
                it->representative = symbol;
                it->representativeBlock = symbol->blockIndex;
                it->representativeFolio = folioIndex;
            }

            // Les caracteristiques peuvent etre renseignees sur n'importe quel
            // bloc de l'appareil.
            if (it->designation.isEmpty())
                it->designation = symbol->designation();
            if (it->partNumber.isEmpty())
                it->partNumber = symbol->fields.value(QStringLiteral("partNumber"));
            if (it->manufacturer.isEmpty())
                it->manufacturer = symbol->fields.value(QStringLiteral("manufacturer"));
            if (it->value.isEmpty())
                it->value = symbol->fields.value(QStringLiteral("value"));
            if (!tag.isEmpty() && !it->folios.contains(tag))
                it->folios.append(tag);
        }
    }

    QHash<QString, BomLine> byArticle;
    for (const QString &key : std::as_const(order)) {
        const Device &device = devices.value(key);
        if (!device.representative)
            continue;
        const SymbolDefinition *definition =
                project.library.definition(device.representative->definitionId);

        // L'article est la reference fabricant quand elle existe : c'est elle
        // qu'on commande. A defaut, le couple symbole principal + valeur.
        const QString article = !device.partNumber.isEmpty()
                ? device.partNumber
                : device.representative->definitionId + QLatin1Char('|') + device.value;

        BomLine &line = byArticle[article];
        if (line.quantity == 0) {
            line.article = device.partNumber.isEmpty()
                    ? device.representative->definitionId
                    : device.partNumber;
            line.name = definition ? definition->name : device.representative->definitionId;
            line.value = device.value;
            line.manufacturer = device.manufacturer;
            line.partNumber = device.partNumber;
        }
        if (!device.designation.isEmpty() && !line.designations.contains(device.designation))
            line.designations.append(device.designation);
        for (const QString &tag : device.folios) {
            if (!line.folios.contains(tag))
                line.folios.append(tag);
        }
        ++line.quantity;
    }

    QVector<BomLine> lines;
    lines.reserve(byArticle.size());
    for (BomLine line : byArticle) {
        std::sort(line.designations.begin(), line.designations.end(), naturalLess);
        line.folios.sort();
        lines.append(line);
    }
    std::sort(lines.begin(), lines.end(), [](const BomLine &a, const BomLine &b) {
        if (a.designations.isEmpty() != b.designations.isEmpty())
            return !a.designations.isEmpty();
        if (!a.designations.isEmpty() && !b.designations.isEmpty()
            && a.designations.first() != b.designations.first())
            return naturalLess(a.designations.first(), b.designations.first());
        return a.name < b.name;
    });
    return lines;
}

QVector<TerminalLine> Reports::terminalList(const Project &project, const Netlist &netlist,
                                            const ReportScope &scope)
{
    QVector<TerminalLine> lines;

    for (const Folio *folio : foliosInScope(project, scope)) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
            if (!definition || definition->deviceKind != QLatin1String("terminal"))
                continue;

            TerminalLine line;
            line.block = symbol->designation();
            line.terminal = symbol->fields.value(QStringLiteral("terminal"));
            line.folio = folio->number.isEmpty() ? folio->title : folio->number;

            for (const Pin &pin : definition->pins) {
                const Netlist::Net *net = netlist.netOfPin(symbol->id(), pin.number);
                if (!net)
                    continue;
                if (line.netName.isEmpty())
                    line.netName = net->name;

                // Le repere du fil raccorde a cette borne.
                for (const Netlist::WireRef &ref : net->wires) {
                    const Folio *wireFolio = project.folio(ref.folioId);
                    if (!wireFolio)
                        continue;
                    const auto *wire = dynamic_cast<const Wire *>(wireFolio->entity(ref.wireId));
                    if (wire && !wire->number.isEmpty()) {
                        line.wireNumber = wire->number;
                        break;
                    }
                }

                // L'appareil de l'autre cote de la borne.
                for (const Netlist::PinRef &other : net->pins) {
                    if (other.symbolId == symbol->id())
                        continue;
                    line.target = other.designation;
                    line.targetPin = other.pinNumber;
                    break;
                }
            }
            lines.append(line);
        }
    }

    std::sort(lines.begin(), lines.end(), [](const TerminalLine &a, const TerminalLine &b) {
        if (a.block != b.block)
            return naturalLess(a.block, b.block);
        return naturalLess(a.terminal, b.terminal);
    });
    return lines;
}

QVector<WireLine> Reports::wireList(const Project &project, const Netlist &netlist,
                                    const ReportScope &scope)
{
    QVector<WireLine> lines;

    for (const Netlist::Net &net : netlist.nets()) {
        if (net.wires.isEmpty() || !scope.touches(net.folioIds))
            continue;

        WireLine line;
        line.netName = net.name;
        line.folios = folioTags(project, net.folioIds);
        line.connectionCount = net.pinCount();

        int conductors = 1;
        for (const Netlist::WireRef &ref : net.wires) {
            const Folio *folio = project.folio(ref.folioId);
            if (!folio)
                continue;
            const auto *wire = dynamic_cast<const Wire *>(folio->entity(ref.wireId));
            if (!wire)
                continue;
            if (line.number.isEmpty())
                line.number = wire->number;
            if (line.crossSection.isEmpty() && !wire->wireType.isEmpty()) {
                const WireType &type = project.wireTypes.resolve(wire->wireType);
                line.wireTypeName = type.name;
                line.crossSection = type.crossSection;
            }
            conductors = std::max(conductors, wire->conductorCount());
            // Un fil multi-conducteurs apparait une fois par conducteur dans la
            // netlist : sa longueur ne doit etre comptee qu'une fois.
            if (ref.conductor == 0)
                line.length += wire->length();
        }
        line.conductorCount = conductors;

        if (!net.pins.isEmpty()) {
            line.from = net.pins.first().designation;
            line.fromPin = net.pins.first().pinNumber;
        }
        if (net.pins.size() > 1) {
            line.to = net.pins.at(1).designation;
            line.toPin = net.pins.at(1).pinNumber;
        }
        lines.append(line);
    }

    std::sort(lines.begin(), lines.end(),
              [](const WireLine &a, const WireLine &b) { return naturalLess(a.number, b.number); });
    return lines;
}

QVector<WireRunLine> Reports::wireFromTo(const Project &project, const Netlist &netlist,
                                         const ReportScope &scope)
{
    QVector<WireRunLine> lines;

    for (const Netlist::Net &net : netlist.nets()) {
        // Un potentiel a une seule broche n'est pas une liaison a cabler.
        if (net.pins.size() < 2 || !scope.touches(net.folioIds))
            continue;

        // Le type de fil du potentiel : il est porte par ses fils, pas par
        // ses broches. On prend le premier fil type rencontre — un potentiel
        // melangeant deux types est deja une anomalie de saisie.
        QString wireTypeId;
        QString number = net.number;
        for (const Netlist::WireRef &ref : net.wires) {
            const Folio *folio = project.folio(ref.folioId);
            if (!folio)
                continue;
            const auto *wire = dynamic_cast<const Wire *>(folio->entity(ref.wireId));
            if (!wire)
                continue;
            if (number.isEmpty())
                number = wire->number;
            if (wireTypeId.isEmpty() && !wire->wireType.isEmpty())
                wireTypeId = wire->wireType;
        }
        const WireType &type = project.wireTypes.resolve(wireTypeId);

        // Chainage des broches. Toutes les broches d'un potentiel sont
        // electriquement equivalentes : n'importe quel chainage est un cablage
        // valide. On chaine de proche en proche, sans quitter un folio tant
        // qu'il reste des broches a y relier — c'est le trajet que suit
        // reellement un cableur, et le plus court en longueur de fil.
        QVector<Netlist::PinRef> remaining = net.pins;
        std::sort(remaining.begin(), remaining.end(),
                  [&](const Netlist::PinRef &a, const Netlist::PinRef &b) {
                      const int fa = project.indexOf(a.folioId);
                      const int fb = project.indexOf(b.folioId);
                      if (fa != fb)
                          return fa < fb;
                      if (a.position.y() != b.position.y())
                          return a.position.y() < b.position.y();
                      return a.position.x() < b.position.x();
                  });

        QVector<Netlist::PinRef> chain;
        chain.append(remaining.takeFirst());
        while (!remaining.isEmpty()) {
            const Netlist::PinRef &last = chain.last();
            int best = 0;
            double bestScore = -1.0;
            for (int i = 0; i < remaining.size(); ++i) {
                const Netlist::PinRef &candidate = remaining.at(i);
                const QPointF d = candidate.position - last.position;
                double score = std::hypot(d.x(), d.y());
                // Changer de folio coute cher : la liaison passe alors par un
                // renvoi et un bornier, ce qui n'est jamais le trajet direct.
                if (candidate.folioId != last.folioId)
                    score += 100000.0;
                if (bestScore < 0.0 || score < bestScore) {
                    bestScore = score;
                    best = i;
                }
            }
            chain.append(remaining.takeAt(best));
        }

        auto describe = [&](const Netlist::PinRef &pin, QString &folioTagOut, QString &zoneOut) {
            folioTagOut = folioTag(project, pin.folioId);
            const Folio *folio = project.folio(pin.folioId);
            zoneOut = folio ? folio->zoneAt(pin.position) : QString();
        };

        for (int i = 1; i < chain.size(); ++i) {
            const Netlist::PinRef &from = chain.at(i - 1);
            const Netlist::PinRef &to = chain.at(i);

            WireRunLine line;
            line.wireNumber = number;
            line.netName = net.name;
            line.fromDesignation = from.designation;
            line.fromPin = from.pinNumber;
            describe(from, line.fromFolio, line.fromZone);
            line.toDesignation = to.designation;
            line.toPin = to.pinNumber;
            describe(to, line.toFolio, line.toZone);
            line.wireTypeName = type.name;
            line.colorName = type.colorName();
            line.crossSection = type.crossSection;
            line.crossesFolios = from.folioId != to.folioId;
            lines.append(line);
        }
    }

    std::sort(lines.begin(), lines.end(), [](const WireRunLine &a, const WireRunLine &b) {
        if (a.wireNumber != b.wireNumber)
            return naturalLess(a.wireNumber, b.wireNumber);
        return naturalLess(a.fromDesignation, b.fromDesignation);
    });
    return lines;
}

QVector<ComponentLine> Reports::componentList(const Project &project, const Netlist &netlist,
                                              const ReportScope &scope)
{
    // Un appareil multi-blocs ne fait qu'une ligne, comme dans la
    // nomenclature — mais ici l'unite est l'appareil pose, pas l'article
    // commande : deux contacteurs de meme reference font deux lignes.
    struct Device {
        const SymbolInstance *representative = nullptr;
        int representativeBlock = 0;
        const Folio *representativeFolio = nullptr;
        QString value;
        QString manufacturer;
        QString partNumber;
        QString description;
        QStringList folios;
        int blockCount = 0;
        int pinCount = 0;
    };

    QHash<QString, Device> devices;
    QStringList order;
    QHash<QString, QString> deviceOfSymbol; // identifiant de symbole -> appareil

    for (const Folio *folio : foliosInScope(project, scope)) {
        const QString tag = folio->number.isEmpty() ? folio->title : folio->number;

        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            const QString key = symbol->deviceGroup.isEmpty()
                    ? QStringLiteral("#") + symbol->id()
                    : QStringLiteral("g:") + symbol->deviceGroup;
            deviceOfSymbol.insert(symbol->id(), key);

            auto it = devices.find(key);
            if (it == devices.end()) {
                Device device;
                device.representative = symbol;
                device.representativeBlock = symbol->blockIndex;
                device.representativeFolio = folio;
                it = devices.insert(key, device);
                order.append(key);
            } else if (symbol->blockIndex < it->representativeBlock) {
                it->representative = symbol;
                it->representativeBlock = symbol->blockIndex;
                it->representativeFolio = folio;
            }

            ++it->blockCount;
            if (it->value.isEmpty())
                it->value = symbol->fields.value(QStringLiteral("value"));
            if (it->manufacturer.isEmpty())
                it->manufacturer = symbol->fields.value(QStringLiteral("manufacturer"));
            if (it->partNumber.isEmpty())
                it->partNumber = symbol->fields.value(QStringLiteral("partNumber"));
            if (it->description.isEmpty())
                it->description = symbol->fields.value(QStringLiteral("description"));
            if (!tag.isEmpty() && !it->folios.contains(tag))
                it->folios.append(tag);
        }
    }

    // Broches raccordees, comptees depuis la netlist : c'est ce qui distingue
    // un appareil cable d'un appareil pose et oublie. Le rattachement se fait
    // par identifiant de symbole, jamais par designation — deux appareils
    // peuvent porter le meme repere tant que le reperage n'a pas tourne.
    for (const Netlist::Net &net : netlist.nets()) {
        for (const Netlist::PinRef &pin : net.pins) {
            const auto key = deviceOfSymbol.constFind(pin.symbolId);
            if (key == deviceOfSymbol.constEnd())
                continue;
            auto device = devices.find(key.value());
            if (device != devices.end())
                ++device->pinCount;
        }
    }

    QVector<ComponentLine> lines;
    for (const QString &key : std::as_const(order)) {
        const Device &device = devices.value(key);
        if (!device.representative)
            continue;
        const SymbolDefinition *definition =
                project.library.definition(device.representative->definitionId);

        ComponentLine line;
        line.designation = device.representative->designation();
        line.family = definition ? definition->category : QString();
        line.description = !device.description.isEmpty()
                ? device.description
                : (definition ? definition->name : device.representative->definitionId);
        line.value = device.value;
        line.manufacturer = device.manufacturer;
        line.partNumber = device.partNumber;
        line.folios = device.folios;
        line.blockCount = device.blockCount;
        line.pinCount = device.pinCount;
        if (device.representativeFolio) {
            line.folio = device.representativeFolio->number.isEmpty()
                    ? device.representativeFolio->title
                    : device.representativeFolio->number;
            line.zone = device.representativeFolio->zoneAt(
                    device.representative->placement.position);
        }
        lines.append(line);
    }

    std::sort(lines.begin(), lines.end(), [](const ComponentLine &a, const ComponentLine &b) {
        return naturalLess(a.designation, b.designation);
    });
    return lines;
}

ReportTable Reports::toTable(const QVector<BomLine> &lines)
{
    ReportTable table;
    table.title = QStringLiteral("Nomenclature");
    table.headers = { QStringLiteral("Qté"),        QStringLiteral("Désignations"),
                      QStringLiteral("Désignation article"), QStringLiteral("Valeur"),
                      QStringLiteral("Fabricant"),  QStringLiteral("Référence"),
                      QStringLiteral("Folios") };
    for (const BomLine &line : lines) {
        table.rows.append({ QString::number(line.quantity), line.designations.join(QStringLiteral(", ")),
                            line.name, line.value, line.manufacturer, line.partNumber,
                            line.folios.join(QStringLiteral(", ")) });
    }
    return table;
}

ReportTable Reports::toTable(const QVector<TerminalLine> &lines)
{
    ReportTable table;
    table.title = QStringLiteral("Bornier");
    table.headers = { QStringLiteral("Bornier"), QStringLiteral("Borne"),
                      QStringLiteral("Folio"),   QStringLiteral("Fil"),
                      QStringLiteral("Potentiel"), QStringLiteral("Raccordé à"),
                      QStringLiteral("Broche") };
    for (const TerminalLine &line : lines) {
        table.rows.append({ line.block, line.terminal, line.folio, line.wireNumber, line.netName,
                            line.target, line.targetPin });
    }
    return table;
}

ReportTable Reports::toTable(const QVector<WireLine> &lines)
{
    ReportTable table;
    table.title = QStringLiteral("Liste des fils");
    table.headers = { QStringLiteral("Repère"),  QStringLiteral("Potentiel"),
                      QStringLiteral("Folios"),  QStringLiteral("De"),
                      QStringLiteral("Broche"),  QStringLiteral("Vers"),
                      QStringLiteral("Broche"),  QStringLiteral("Type"),
                      QStringLiteral("Section"),  QStringLiteral("Conducteurs"),
                      QStringLiteral("Longueur (mm)") };
    for (const WireLine &line : lines) {
        table.rows.append({ line.number, line.netName, line.folios.join(QStringLiteral(", ")),
                            line.from, line.fromPin, line.to, line.toPin,
                            line.wireTypeName, line.crossSection,
                            QString::number(line.conductorCount),
                            QString::number(line.length, 'f', 1) });
    }
    return table;
}

ReportTable Reports::toTable(const QVector<WireRunLine> &lines)
{
    ReportTable table;
    table.title = QStringLiteral("Câblage De / Vers");
    table.headers = { QStringLiteral("Repère fil"), QStringLiteral("Potentiel"),
                      QStringLiteral("De"),         QStringLiteral("Broche"),
                      QStringLiteral("Folio"),      QStringLiteral("Zone"),
                      QStringLiteral("Vers"),       QStringLiteral("Broche"),
                      QStringLiteral("Folio"),      QStringLiteral("Zone"),
                      QStringLiteral("Type"),       QStringLiteral("Couleur"),
                      QStringLiteral("Section") };
    for (const WireRunLine &line : lines) {
        table.rows.append({ line.wireNumber, line.netName, line.fromDesignation, line.fromPin,
                            line.fromFolio, line.fromZone, line.toDesignation, line.toPin,
                            line.toFolio, line.toZone, line.wireTypeName, line.colorName,
                            line.crossSection });
    }
    return table;
}

ReportTable Reports::toTable(const QVector<ComponentLine> &lines)
{
    ReportTable table;
    table.title = QStringLiteral("Composants");
    table.headers = { QStringLiteral("Repère"),   QStringLiteral("Famille"),
                      QStringLiteral("Description"), QStringLiteral("Valeur"),
                      QStringLiteral("Fabricant"), QStringLiteral("Référence"),
                      QStringLiteral("Folio"),    QStringLiteral("Zone"),
                      QStringLiteral("Blocs"),    QStringLiteral("Broches câblées"),
                      QStringLiteral("Folios") };
    for (const ComponentLine &line : lines) {
        table.rows.append({ line.designation, line.family, line.description, line.value,
                            line.manufacturer, line.partNumber, line.folio, line.zone,
                            QString::number(line.blockCount), QString::number(line.pinCount),
                            line.folios.join(QStringLiteral(", ")) });
    }
    return table;
}

ReportTable Reports::projectSummary(const Project &project, const Netlist &netlist,
                                    const ReportScope &scope)
{
    int symbols = 0;
    int wires = 0;
    double wireLength = 0.0;
    const auto folios = foliosInScope(project, scope);
    for (const Folio *folio : folios) {
        symbols += int(folio->entitiesOfType<SymbolInstance>().size());
        for (const Wire *wire : folio->entitiesOfType<Wire>()) {
            ++wires;
            wireLength += wire->length();
        }
    }

    ReportTable table;
    table.title = scope.isProject() ? QStringLiteral("Récapitulatif du projet")
                                    : QStringLiteral("Récapitulatif du folio");
    int nets = 0;
    int crossing = 0;
    int problems = 0;
    for (const Netlist::Net &net : netlist.nets()) {
        if (!scope.touches(net.folioIds))
            continue;
        ++nets;
        if (net.crossesFolios())
            ++crossing;
    }
    for (const Netlist::Diagnostic &d : netlist.diagnostics()) {
        if (scope.includes(d.folioId))
            ++problems;
    }

    table.headers = { QStringLiteral("Indicateur"), QStringLiteral("Valeur") };
    table.rows = {
        { QStringLiteral("Folios"), QString::number(folios.size()) },
        { QStringLiteral("Appareils"), QString::number(symbols) },
        { QStringLiteral("Fils"), QString::number(wires) },
        { QStringLiteral("Longueur de fil"),
          QString::number(wireLength / 1000.0, 'f', 2) + QStringLiteral(" m") },
        { QStringLiteral("Potentiels"), QString::number(nets) },
        { QStringLiteral("Potentiels inter-folios"), QString::number(crossing) },
        { QStringLiteral("Anomalies"), QString::number(problems) },
        { QStringLiteral("Symboles manquants"),
          QString::number(project.missingDefinitions().size()) },
    };
    return table;
}

ReportTable Reports::diagnostics(const Netlist &netlist, const ReportScope &scope)
{
    ReportTable table;
    table.title = QStringLiteral("Contrôles");
    table.headers = { QStringLiteral("Gravité"), QStringLiteral("Code"),
                      QStringLiteral("Message") };
    for (const Netlist::Diagnostic &d : netlist.diagnostics()) {
        if (!scope.includes(d.folioId))
            continue;
        QString severity;
        switch (d.severity) {
        case Netlist::Diagnostic::Severity::Info: severity = QStringLiteral("Information"); break;
        case Netlist::Diagnostic::Severity::Warning: severity = QStringLiteral("Avertissement"); break;
        case Netlist::Diagnostic::Severity::Error: severity = QStringLiteral("Erreur"); break;
        }
        table.rows.append({ severity, d.code, d.message });
    }
    return table;
}

} // namespace dsn
