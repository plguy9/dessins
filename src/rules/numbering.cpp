#include "numbering.h"

#include "core/entities.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace dsn {

namespace {

// Reference de ligne d'un point : folio + colonne du cadre, sur deux
// chiffres. C'est la convention « X-Zones » d'AutoCAD, et c'est deja celle
// que suivent les reperes de fil : un appareil et le fil qui le touche
// doivent citer le meme endroit.
QString lineReferenceOf(const Folio &folio, const QString &sheetTag, const QPointF &at)
{
    const int column = folio.columnAt(at);
    if (column <= 0)
        return sheetTag;
    return sheetTag + QString::number(column).rightJustified(2, QLatin1Char('0'));
}

// Lettre de departage : A, B, ... Z, puis AA. Deux appareils sur la meme
// ligne sont frequents (un contact et sa bobine cote a cote) et doivent
// rester distinguables.
QString suffixLetter(int index)
{
    QString out;
    ++index;
    while (index > 0) {
        const int rest = (index - 1) % 26;
        out.prepend(QChar(char16_t(u'A' + rest)));
        index = (index - 1) / 26;
    }
    return out;
}

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

QSet<QString> Numbering::allDesignations(const Project &project)
{
    QSet<QString> used;
    for (const Folio *folio : project.folios()) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            if (!symbol->designation().isEmpty())
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

namespace {

// Un appareil a designer, accompagne de l'endroit ou il se trouve. Le folio
// ne se deduit pas de l'appareil : la designation d'un lot colle travaille
// sur des copies qui ne sont pas encore posees dans leur folio.
struct DesignationTarget {
    SymbolInstance *symbol = nullptr;
    const Folio *folio = nullptr;
    int folioIndex = 0;
};

// Le coeur de la designation, partage par la regeneration globale et par la
// designation d'un lot. `taken` arrive rempli par l'appelant : c'est la seule
// chose qui distingue les deux usages — la regeneration globale n'evite que
// les reperes verrouilles, la designation de lot les evite tous.
NumberingResult designateTargets(const QVector<DesignationTarget> &targets,
                                 const Project &project, const DesignationRule &rule,
                                 QSet<QString> taken)
{
    NumberingResult result;

    // Un appareil multi-blocs (bobine + contacts auxiliaires) partage une seule
    // designation : on raisonne par groupe, pas par instance.
    struct Group {
        QString key;
        QVector<SymbolInstance *> members;
        int folioIndex = 0;
        const Folio *folio = nullptr;
        QPointF anchor;
        QString prefix;
        bool locked = false;
        QString lockedDesignation;
    };

    QHash<QString, Group> groups;
    QVector<QString> order;

    // Les bornes, mises de cote pendant la passe des appareils.
    struct Borne {
        SymbolInstance *symbol = nullptr;
        const Folio *folio = nullptr;
        int folioIndex = 0;
        QString prefix;
    };
    QVector<Borne> bornes;

    for (const DesignationTarget &target : targets) {
        SymbolInstance *symbol = target.symbol;
        if (!symbol)
            continue;
        const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
        if (!definition)
            continue;

        // LE PREFIXE PEUT VENIR DE L'APPAREIL LUI-MEME, et il prime sur tout
        // le reste. Sur une bulle ISA, la lettre de fonction — TT, FT, PT,
        // LT — est ce que le dessinateur ECRIT DANS le symbole : elle ne peut
        // pas venir de la definition, qui est la meme pour tous les
        // instruments au champ. Sans ce champ, un transmetteur de temperature
        // et un debitmetre porteraient la meme lettre, et le repere compose
        // (« 022TT8917 ») serait hors d'atteinte.
        QString prefix = symbol->fields.value(QStringLiteral("family")).trimmed();
        if (prefix.isEmpty())
            prefix = rule.prefixByDeviceKind.value(definition->deviceKind);
        if (prefix.isEmpty())
            prefix = definition->designationPrefix;
        if (prefix.isEmpty())
            prefix = QStringLiteral("A");

        // UNE BORNE N'EST PAS UN APPAREIL : c'est une PLACE DANS UN BORNIER.
        // Traitee comme un appareil, chaque borne recevait sa propre
        // designation — trois bornes cote a cote donnaient -X1, -X2, -X3,
        // soit trois borniers d'une borne chacun. Sur un dossier de soixante
        // bornes, cela fait soixante borniers, et l'editeur de borniers
        // devient inutilisable. Les bornes sont donc mises de cote ici et
        // reprises plus bas, ou elles recoivent un bornier PARTAGE et un
        // NUMERO dans ce bornier.
        if (definition->deviceKind == QLatin1String("terminal")) {
            bornes.append({ symbol, target.folio, target.folioIndex, prefix });
            continue;
        }

        const QString key = symbol->deviceGroup.isEmpty()
                ? QStringLiteral("#") + symbol->id()
                : QStringLiteral("g:") + symbol->deviceGroup;

        auto it = groups.find(key);
        if (it == groups.end()) {
            Group group;
            group.key = key;
            group.folioIndex = target.folioIndex;
            group.folio = target.folio;
            group.anchor = symbol->placement.position;
            group.prefix = prefix;
            it = groups.insert(key, group);
            order.append(key);
        } else if (target.folioIndex < it->folioIndex
                   || (target.folioIndex == it->folioIndex
                       && readingOrder(symbol->placement.position, it->anchor))) {
            it->folioIndex = target.folioIndex;
            it->folio = target.folio;
            it->anchor = symbol->placement.position;
        }

        it->members.append(symbol);
        if (symbol->designationLocked && !symbol->designation().isEmpty()) {
            it->locked = true;
            it->lockedDesignation = symbol->designation();
        }
    }

    std::sort(order.begin(), order.end(), [&](const QString &a, const QString &b) {
        const Group &ga = groups[a];
        const Group &gb = groups[b];
        if (ga.folioIndex != gb.folioIndex)
            return ga.folioIndex < gb.folioIndex;
        return readingOrder(ga.anchor, gb.anchor);
    });

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

        DesignationContext context;
        context.family = group.prefix;
        if (group.folio) {
            context.sheet = group.folio->number.isEmpty()
                    ? QString::number(group.folioIndex + 1)
                    : group.folio->number;
            context.lineReference = lineReferenceOf(*group.folio, context.sheet, group.anchor);
        }
        // Les codes d'installation et d'emplacement viennent de l'appareil :
        // ils identifient l'armoire, pas la famille. Le secteur et la boucle
        // aussi — un instrument porte SA boucle, c'est ce qui le relie a sa
        // carte a travers tout le dossier.
        for (const SymbolInstance *member : group.members) {
            if (context.installation.isEmpty())
                context.installation = member->fields.value(QStringLiteral("installation"));
            if (context.location.isEmpty())
                context.location = member->fields.value(QStringLiteral("location"));
            if (context.sector.isEmpty())
                context.sector = member->fields.value(QStringLiteral("sector"));
            if (context.loop.isEmpty())
                context.loop = member->fields.value(QStringLiteral("loop"));
        }
        // Le secteur retombe sur celui de la PLANCHE : une feuille de schema
        // de boucle appartient a une aire de l'usine, et le retaper sur
        // chaque instrument serait le meilleur moyen d'en oublier un. Le
        // champ pose sur l'appareil garde le dernier mot.
        if (context.sector.isEmpty() && group.folio)
            context.sector = group.folio->titleBlock.value(QStringLiteral("sector"));
        if (context.sector.isEmpty())
            context.sector = project.info.extra.value(QStringLiteral("sector"));

        QString designation;
        // Le departage se fait par une LETTRE des que le format ne porte pas
        // de numero — c'est le cas de la reference de ligne (104K, 104K-A) et
        // celui du repere d'instrument (022TT8917A). Sans cette question, un
        // format comme « %C%F%B » ferait tourner le compteur sans jamais
        // changer le texte produit : la boucle « tant que le repere est pris »
        // ne s'arreterait pas.
        if (rule.mode == DesignationRule::Mode::LineReference || !rule.usesNumber()) {
            context.number = 1;
            designation = rule.format(context);
            int suffix = 0;
            while (taken.contains(designation)) {
                context.suffix = suffixLetter(suffix++);
                context.number = suffix + 1;
                designation = rule.format(context);
            }
        } else {
            const QString counterKey = rule.perFolio
                    ? QString::number(group.folioIndex) + QLatin1Char('/') + group.prefix
                    : group.prefix;
            int &counter = counters[counterKey];
            do {
                ++counter;
                context.number = counter;
                designation = rule.format(context);
            } while (taken.contains(designation));
        }

        taken.insert(designation);
        for (SymbolInstance *symbol : group.members)
            symbol->setDesignation(designation);
        ++result.devicesDesignated;
    }

    // ---- LES BORNIERS --------------------------------------------------
    //
    // Deux regles, et elles ne se ressemblent pas.
    //
    // 1. LE BORNIER EST PARTAGE. Une borne qui porte deja un bornier le
    //    garde — c'est la meme regle que le collage de circuit (« une borne
    //    garde son bornier et change de numero »). Celles qui n'en ont pas
    //    rejoignent le bornier du FOLIO : un folio correspond a une fonction,
    //    et ses bornes partent dans le meme cable. Un seul compteur pour tout
    //    le dossier, de sorte que le folio 1 donne -X1 et le folio 2 -X2.
    //
    // 2. LE BORNIER SE REMPLIT, IL NE SE RENUMEROTE PAS. Une borne qui porte
    //    deja un numero le garde, et une borne neuve prend le premier numero
    //    libre du bornier. Renumeroter d'office un bornier deja cable est une
    //    faute, pas un service : le cableur a le plan de l'an dernier dans les
    //    mains. Renumeroter reste possible, mais c'est un geste explicite —
    //    le bouton « Renumeroter 1, 2, 3… » de l'editeur de borniers.
    if (!bornes.isEmpty()) {
        std::sort(bornes.begin(), bornes.end(), [](const Borne &a, const Borne &b) {
            if (a.folioIndex != b.folioIndex)
                return a.folioIndex < b.folioIndex;
            return readingOrder(a.symbol->placement.position, b.symbol->placement.position);
        });

        // Le bornier par defaut de chaque folio, attribue a la demande.
        QHash<int, QString> bornierDuFolio;
        int compteurBornier = 0;

        // Les numeros deja pris dans chaque bornier, pour que le remplissage
        // ne double jamais un numero existant.
        QHash<QString, QSet<QString>> numerosPris;
        for (const Borne &borne : std::as_const(bornes)) {
            const QString bloc = borne.symbol->designation();
            const QString numero = borne.symbol->fields.value(QStringLiteral("terminal"));
            if (!bloc.isEmpty() && !numero.isEmpty())
                numerosPris[bloc].insert(numero);
        }

        QHash<QString, int> prochain;
        for (const Borne &borne : std::as_const(bornes)) {
            QString bloc = borne.symbol->designation();
            if (bloc.isEmpty()) {
                auto it = bornierDuFolio.find(borne.folioIndex);
                if (it == bornierDuFolio.end()) {
                    DesignationContext context;
                    context.family = borne.prefix;
                    if (borne.folio) {
                        context.sheet = borne.folio->number.isEmpty()
                                ? QString::number(borne.folioIndex + 1)
                                : borne.folio->number;
                        context.lineReference = lineReferenceOf(*borne.folio, context.sheet,
                                                                borne.symbol->placement.position);
                    }
                    QString designation;
                    do {
                        ++compteurBornier;
                        context.number = compteurBornier;
                        designation = rule.format(context);
                    } while (taken.contains(designation));
                    taken.insert(designation);
                    it = bornierDuFolio.insert(borne.folioIndex, designation);
                }
                bloc = *it;
                borne.symbol->setDesignation(bloc);
                ++result.devicesDesignated;
            }

            if (!borne.symbol->fields.value(QStringLiteral("terminal")).isEmpty())
                continue; // deja numerotee : on ne la bouscule pas

            QSet<QString> &pris = numerosPris[bloc];
            int &suivant = prochain[bloc];
            QString numero;
            do {
                ++suivant;
                numero = QString::number(suivant);
            } while (pris.contains(numero));
            pris.insert(numero);
            borne.symbol->fields.insert(QStringLiteral("terminal"), numero);
            ++result.terminalsNumbered;
        }
    }

    return result;
}

} // namespace

NumberingResult Numbering::designateDevices(Project &project, const Profile &profile)
{
    QVector<DesignationTarget> targets;
    const auto folios = project.folios();
    for (int folioIndex = 0; folioIndex < int(folios.size()); ++folioIndex) {
        Folio *folio = folios[std::size_t(folioIndex)];
        for (SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>())
            targets.append({ symbol, folio, folioIndex });
    }
    return designateTargets(targets, project, profile.designation, usedDesignations(project));
}

NumberingResult Numbering::designateNew(const Project &project, const Profile &profile,
                                        const QVector<SymbolInstance *> &symbols,
                                        const Folio *destination)
{
    // Le rang du folio sert a la designation par folio et a l'ordre de
    // lecture ; hors projet, le lot se comporte comme s'il etait sur le
    // premier folio plutot que de n'etre nulle part.
    const int index = destination ? project.indexOf(destination->id()) : -1;

    QVector<DesignationTarget> targets;
    targets.reserve(symbols.size());
    for (SymbolInstance *symbol : symbols)
        targets.append({ symbol, destination, std::max(0, index) });

    return designateTargets(targets, project, profile.designation, allDesignations(project));
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
