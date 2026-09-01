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

} // namespace

QVector<BomLine> Reports::billOfMaterials(const Project &project)
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

    const auto folios = project.folios();
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

QVector<TerminalLine> Reports::terminalList(const Project &project, const Netlist &netlist)
{
    QVector<TerminalLine> lines;

    for (const Folio *folio : project.folios()) {
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

QVector<WireLine> Reports::wireList(const Project &project, const Netlist &netlist)
{
    QVector<WireLine> lines;

    for (const Netlist::Net &net : netlist.nets()) {
        if (net.wires.isEmpty())
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
                      QStringLiteral("Broche"),  QStringLiteral("Conducteurs"),
                      QStringLiteral("Longueur (mm)") };
    for (const WireLine &line : lines) {
        table.rows.append({ line.number, line.netName, line.folios.join(QStringLiteral(", ")),
                            line.from, line.fromPin, line.to, line.toPin,
                            QString::number(line.conductorCount),
                            QString::number(line.length, 'f', 1) });
    }
    return table;
}

ReportTable Reports::projectSummary(const Project &project, const Netlist &netlist)
{
    int symbols = 0;
    int wires = 0;
    double wireLength = 0.0;
    for (const Folio *folio : project.folios()) {
        symbols += int(folio->entitiesOfType<SymbolInstance>().size());
        for (const Wire *wire : folio->entitiesOfType<Wire>()) {
            ++wires;
            wireLength += wire->length();
        }
    }

    ReportTable table;
    table.title = QStringLiteral("Récapitulatif du projet");
    table.headers = { QStringLiteral("Indicateur"), QStringLiteral("Valeur") };
    table.rows = {
        { QStringLiteral("Folios"), QString::number(project.folioCount()) },
        { QStringLiteral("Appareils"), QString::number(symbols) },
        { QStringLiteral("Fils"), QString::number(wires) },
        { QStringLiteral("Longueur de fil"),
          QString::number(wireLength / 1000.0, 'f', 2) + QStringLiteral(" m") },
        { QStringLiteral("Potentiels"), QString::number(netlist.netCount()) },
        { QStringLiteral("Potentiels inter-folios"),
          QString::number(std::count_if(netlist.nets().cbegin(), netlist.nets().cend(),
                                        [](const Netlist::Net &n) { return n.crossesFolios(); })) },
        { QStringLiteral("Anomalies"), QString::number(netlist.diagnostics().size()) },
        { QStringLiteral("Symboles manquants"),
          QString::number(project.missingDefinitions().size()) },
    };
    return table;
}

ReportTable Reports::diagnostics(const Netlist &netlist)
{
    ReportTable table;
    table.title = QStringLiteral("Contrôles");
    table.headers = { QStringLiteral("Gravité"), QStringLiteral("Code"),
                      QStringLiteral("Message") };
    for (const Netlist::Diagnostic &d : netlist.diagnostics()) {
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
