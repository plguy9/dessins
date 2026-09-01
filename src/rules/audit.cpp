#include "audit.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace dsn {

namespace {

const QString kSymbols = QStringLiteral("Symboles");
const QString kWires = QStringLiteral("Fils");
const QString kTags = QStringLiteral("Repères");
const QString kSignals = QStringLiteral("Signaux");
const QString kTerminals = QStringLiteral("Bornes");
const QString kPlc = QStringLiteral("Automates");
const QString kCatalog = QStringLiteral("Catalogue");

// La categorie d'un constat venu de la netlist se lit dans son code. Sans
// cette table, tout ce qui n'est pas un signal finirait sous « Fils » — et un
// symbole introuvable apparaitrait dans la mauvaise famille.
QString categoryOfCode(const QString &code)
{
    if (code.startsWith(QLatin1String("symbol.")))
        return kSymbols;
    if (code.startsWith(QLatin1String("signal.")))
        return kSignals;
    if (code.startsWith(QLatin1String("terminal.")))
        return kTerminals;
    if (code.startsWith(QLatin1String("plc.")))
        return kPlc;
    return kWires;
}

// Le coeur ecrit ses messages sans accents — c'est sa convention, et elle est
// bonne pour du code qui ne depend pas de Qt::Gui. Mais ces messages
// finissent sous les yeux de quelqu'un : `rules/` les reecrit en francais
// correct pour les codes qu'il connait, et laisse passer les autres.
QString humanize(const QString &code, const QString &fallback)
{
    if (code == QLatin1String("symbol.missingDefinition"))
        return QStringLiteral("Définition de symbole introuvable : %1")
                .arg(fallback.section(QLatin1Char(':'), 1).trimmed());
    if (code == QLatin1String("net.dangling"))
        return QStringLiteral("Potentiel sans point de connexion utile");
    return fallback;
}

QString folioTagOf(const Folio *folio)
{
    if (!folio)
        return QString();
    return folio->number.isEmpty() ? folio->title : folio->number;
}

// Les folios retenus par le perimetre. Meme regle que les rapports : un seul
// point de filtrage, pour qu'aucun controle ne puisse l'oublier.
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

QString AuditFinding::severityLabel() const
{
    switch (severity) {
    case Severity::Info: return QStringLiteral("Information");
    case Severity::Warning: return QStringLiteral("Avertissement");
    case Severity::Error: return QStringLiteral("Erreur");
    }
    return QString();
}

QStringList Audit::categories()
{
    return { kSymbols, kWires, kTags, kSignals, kTerminals, kPlc, kCatalog };
}

QVector<AuditFinding> Audit::run(const Project &project, const Netlist &netlist,
                                 const PlcDatabase &plc, const ReportScope &scope)
{
    QVector<AuditFinding> findings;

    auto add = [&](const QString &category, const QString &code, const QString &message,
                   AuditFinding::Severity severity, const Folio *folio,
                   const QString &entityId, const QPointF &at) {
        AuditFinding f;
        f.category = category;
        f.code = code;
        f.message = message;
        f.severity = severity;
        f.folioId = folio ? folio->id() : QString();
        f.entityId = entityId;
        f.folioTag = folioTagOf(folio);
        f.zone = folio ? folio->zoneAt(at) : QString();
        findings.append(f);
    };

    const auto folios = foliosInScope(project, scope);

    // ---- ce que la netlist a deja constate --------------------------------
    //
    // Repris tels quels plutot que recalcules : deux implementations de la
    // meme regle finiraient par se contredire, et c'est une divergence qu'on
    // ne remarque qu'apres avoir livre le dossier.
    for (const Netlist::Diagnostic &d : netlist.diagnostics()) {
        if (!scope.includes(d.folioId))
            continue;
        const Folio *folio = project.folio(d.folioId);
        add(categoryOfCode(d.code), d.code, humanize(d.code, d.message),
            static_cast<AuditFinding::Severity>(d.severity), folio, d.entityId, d.position);
    }

    // Le symbole introuvable n'est pas recontrole ici : la netlist le
    // constate deja en la construisant, et c'est justement le genre de regle
    // qu'on ne veut pas voir implementee deux fois.

    // ---- reperes ----------------------------------------------------------
    //
    // Deux appareils distincts qui portent le meme repere rendent la
    // nomenclature et le cablage ambigus : le cableur ne sait plus lequel des
    // deux -K1 est celui du folio 3.
    QHash<QString, QString> deviceOfTag;   // repere -> appareil (groupe ou id)
    for (const Folio *folio : folios) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
            const QString tag = symbol->designation();
            if (tag.isEmpty()) {
                add(kTags, QStringLiteral("tag.missing"),
                    QStringLiteral("Appareil sans repère : %1")
                            .arg(definition ? definition->name : symbol->definitionId),
                    AuditFinding::Severity::Warning, folio, symbol->id(),
                    symbol->placement.position);
                continue;
            }
            const QString device = symbol->deviceGroup.isEmpty() ? symbol->id()
                                                                 : symbol->deviceGroup;
            const auto known = deviceOfTag.constFind(tag);
            if (known == deviceOfTag.constEnd())
                deviceOfTag.insert(tag, device);
            else if (known.value() != device) {
                add(kTags, QStringLiteral("tag.duplicate"),
                    QStringLiteral("Repère %1 porté par deux appareils différents").arg(tag),
                    AuditFinding::Severity::Error, folio, symbol->id(),
                    symbol->placement.position);
            }
        }
    }

    // Un potentiel dont les fils ne portent pas tous le meme repere : c'est
    // le meme conducteur, il ne peut pas avoir deux numeros.
    for (const Netlist::Net &net : netlist.nets()) {
        if (!scope.touches(net.folioIds))
            continue;
        QStringList numbers;
        const Folio *first = nullptr;
        QString firstWire;
        QPointF at;
        for (const Netlist::WireRef &ref : net.wires) {
            const Folio *folio = project.folio(ref.folioId);
            if (!folio)
                continue;
            const auto *wire = dynamic_cast<const Wire *>(folio->entity(ref.wireId));
            if (!wire || wire->number.isEmpty())
                continue;
            if (!numbers.contains(wire->number))
                numbers.append(wire->number);
            if (!first) {
                first = folio;
                firstWire = ref.wireId;
                at = wire->points.isEmpty() ? QPointF() : wire->points.first();
            }
        }
        if (numbers.size() > 1) {
            add(kTags, QStringLiteral("wire.conflictingNumbers"),
                QStringLiteral("Un même potentiel porte plusieurs repères : %1")
                        .arg(numbers.join(QStringLiteral(", "))),
                AuditFinding::Severity::Error, first, firstWire, at);
        }
    }

    // ---- fils -------------------------------------------------------------
    for (const Folio *folio : folios) {
        for (const Wire *wire : folio->entitiesOfType<Wire>()) {
            if (wire->points.size() < 2) {
                add(kWires, QStringLiteral("wire.degenerate"),
                    QStringLiteral("Fil sans tracé"), AuditFinding::Severity::Error, folio,
                    wire->id(), wire->points.isEmpty() ? QPointF() : wire->points.first());
                continue;
            }
            // Un fil de longueur nulle est invisible a l'ecran mais bien
            // present dans la netlist : c'est exactement le genre d'objet qui
            // fait douter d'un dossier sans qu'on trouve pourquoi.
            double length = 0.0;
            for (int i = 1; i < wire->points.size(); ++i) {
                const QPointF d = wire->points.at(i) - wire->points.at(i - 1);
                length += std::hypot(d.x(), d.y());
            }
            if (length < 0.01) {
                add(kWires, QStringLiteral("wire.zeroLength"),
                    QStringLiteral("Fil de longueur nulle"), AuditFinding::Severity::Warning,
                    folio, wire->id(), wire->points.first());
            }
            if (!project.wireTypes.contains(wire->wireType) && !wire->wireType.isEmpty()) {
                add(kWires, QStringLiteral("wire.unknownType"),
                    QStringLiteral("Type de fil inconnu : %1 — le fil retombe sur le type "
                                   "par défaut")
                            .arg(wire->wireType),
                    AuditFinding::Severity::Warning, folio, wire->id(), wire->points.first());
            }
        }
    }

    // ---- bornes -----------------------------------------------------------
    //
    // Une borne sans bornier ne se retrouve pas a l'armoire, et deux bornes
    // de meme numero dans un bornier ne se cablent pas.
    QSet<QString> seenTerminals;
    for (const Folio *folio : folios) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
            if (!definition || definition->deviceKind != QLatin1String("terminal"))
                continue;
            const QString block = symbol->designation();
            const QString number = symbol->fields.value(QStringLiteral("terminal"));
            if (block.isEmpty()) {
                add(kTerminals, QStringLiteral("terminal.noBlock"),
                    QStringLiteral("Borne sans repère de bornier"),
                    AuditFinding::Severity::Warning, folio, symbol->id(),
                    symbol->placement.position);
                continue;
            }
            if (number.isEmpty()) {
                add(kTerminals, QStringLiteral("terminal.noNumber"),
                    QStringLiteral("Borne sans numéro dans le bornier %1").arg(block),
                    AuditFinding::Severity::Warning, folio, symbol->id(),
                    symbol->placement.position);
                continue;
            }
            const QString key = block + QLatin1Char('/') + number;
            if (seenTerminals.contains(key)) {
                add(kTerminals, QStringLiteral("terminal.duplicate"),
                    QStringLiteral("Deux bornes %1 dans le bornier %2").arg(number, block),
                    AuditFinding::Severity::Error, folio, symbol->id(),
                    symbol->placement.position);
            }
            seenTerminals.insert(key);
        }
    }

    // ---- automates --------------------------------------------------------
    //
    // Deux points a la meme adresse est l'erreur d'automate par excellence :
    // elle ne se voit pas sur le folio, elle se voit a la mise en service.
    QHash<QString, QString> ownerOfAddress;   // adresse -> repere du module
    for (const Folio *folio : folios) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            if (!PlcModule::isModule(*symbol))
                continue;
            if (!plc.find(PlcModule::moduleId(*symbol))) {
                add(kPlc, QStringLiteral("plc.unknownModule"),
                    QStringLiteral("Module d'automate inconnu de la base : %1")
                            .arg(PlcModule::moduleId(*symbol)),
                    AuditFinding::Severity::Warning, folio, symbol->id(),
                    symbol->placement.position);
                continue;
            }
            const QString owner = symbol->designation().isEmpty()
                    ? PlcModule::moduleId(*symbol)
                    : symbol->designation();
            bool reported = false;
            for (const PlcPoint &point : PlcModule::points(*symbol, plc)) {
                const auto known = ownerOfAddress.constFind(point.address);
                if (known != ownerOfAddress.constEnd() && known.value() != owner && !reported) {
                    add(kPlc, QStringLiteral("plc.addressOverlap"),
                        QStringLiteral("L'adresse %1 est déjà occupée par %2")
                                .arg(point.address, known.value()),
                        AuditFinding::Severity::Error, folio, symbol->id(),
                        symbol->placement.position);
                    // Un seul constat par carte : seize lignes identiques
                    // pour un meme chevauchement noieraient le rapport.
                    reported = true;
                }
                ownerOfAddress.insert(point.address, owner);
            }
        }
    }

    // ---- catalogue --------------------------------------------------------
    //
    // Information et non avertissement : un schema se dessine avant de se
    // chiffrer, et rappeler a chaque enregistrement ce qui reste a
    // referencer ferait ignorer tout le reste de l'audit.
    QSet<QString> devicesWithoutPart;
    for (const Folio *folio : folios) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            if (!symbol->fields.value(QStringLiteral("partNumber")).isEmpty())
                continue;
            const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
            // Les jonctions, textes et symboles sans famille ne se commandent
            // pas : les signaler serait du bruit pur.
            if (!definition || definition->deviceKind.isEmpty())
                continue;
            const QString device = symbol->deviceGroup.isEmpty() ? symbol->id()
                                                                 : symbol->deviceGroup;
            if (devicesWithoutPart.contains(device))
                continue;
            devicesWithoutPart.insert(device);
            add(kCatalog, QStringLiteral("catalog.noPartNumber"),
                QStringLiteral("Sans référence fabricant : %1")
                        .arg(symbol->designation().isEmpty() ? definition->name
                                                             : symbol->designation()),
                AuditFinding::Severity::Info, folio, symbol->id(), symbol->placement.position);
        }
    }

    // Erreurs d'abord, puis par categorie : on corrige ce qui bloque avant ce
    // qui gene.
    const QStringList order = categories();
    std::stable_sort(findings.begin(), findings.end(),
                     [&order](const AuditFinding &a, const AuditFinding &b) {
                         if (a.severity != b.severity)
                             return a.severity > b.severity;
                         return order.indexOf(a.category) < order.indexOf(b.category);
                     });
    return findings;
}

QMap<QString, int> Audit::countByCategory(const QVector<AuditFinding> &findings)
{
    QMap<QString, int> counts;
    for (const QString &category : categories())
        counts.insert(category, 0);
    for (const AuditFinding &finding : findings)
        counts[finding.category] += 1;
    return counts;
}

ReportTable Audit::toTable(const QVector<AuditFinding> &findings)
{
    ReportTable table;
    table.title = QStringLiteral("Audit électrique");
    table.headers = { QStringLiteral("Gravité"), QStringLiteral("Catégorie"),
                      QStringLiteral("Constat"), QStringLiteral("Folio"),
                      QStringLiteral("Zone"), QStringLiteral("Code") };
    for (const AuditFinding &finding : findings) {
        table.rows.append({ finding.severityLabel(), finding.category, finding.message,
                            finding.folioTag, finding.zone, finding.code });
    }
    return table;
}

} // namespace dsn
