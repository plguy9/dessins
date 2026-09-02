#include "circuitcopy.h"

#include "core/entities.h"
#include "core/folio.h"
#include "numbering.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace dsn {

namespace {

bool isTerminal(const Project &project, const SymbolInstance &symbol)
{
    const SymbolDefinition *definition = project.library.definition(symbol.definitionId);
    return definition && definition->deviceKind == QLatin1String("terminal");
}

// Cle d'unicite d'une borne : bornier + numero. C'est celle de l'audit, et
// les deux doivent dire la meme chose de la meme paire.
QString terminalKey(const QString &block, const QString &number)
{
    return block + QLatin1Char('/') + number;
}

QSet<QString> terminalKeysOf(const Project &project)
{
    QSet<QString> keys;
    for (const Folio *folio : project.folios()) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            if (!isTerminal(project, *symbol))
                continue;
            keys.insert(terminalKey(symbol->designation(),
                                    symbol->fields.value(QStringLiteral("terminal"))));
        }
    }
    return keys;
}

QSet<QString> signalNamesOf(const Project &project)
{
    QSet<QString> names;
    for (const Folio *folio : project.folios()) {
        for (const Label *label : folio->entitiesOfType<Label>()) {
            if (label->isSignalArrow() && !label->name.isEmpty())
                names.insert(label->name);
        }
    }
    return names;
}

// Cle d'occupation d'un emplacement d'automate. Deux cartes dans le meme
// rack au meme emplacement se recouvrent, quel que soit leur modele.
QString slotKey(int rack, int slot)
{
    return QString::number(rack) + QLatin1Char(':') + QString::number(slot);
}

} // namespace

// --------------------------------------------------------------------------

QString CircuitCopyResult::summary() const
{
    QStringList parts;
    if (devicesRetagged > 0) {
        parts << QCoreApplication::translate("CircuitCopy", "%n appareil(s) re-repéré(s)", "",
                                             devicesRetagged);
    }
    if (terminalsRenumbered > 0) {
        parts << QCoreApplication::translate("CircuitCopy", "%n borne(s) renumérotée(s)", "",
                                             terminalsRenumbered);
    }
    if (modulesMoved > 0) {
        parts << QCoreApplication::translate("CircuitCopy", "%n carte(s) déplacée(s)", "",
                                             modulesMoved);
    }
    if (signalsRenamed > 0) {
        parts << QCoreApplication::translate("CircuitCopy", "%n renvoi(s) renommé(s)", "",
                                             signalsRenamed);
    }
    if (wiresReleased > 0) {
        parts << QCoreApplication::translate("CircuitCopy", "%n repère(s) de fil libéré(s)", "",
                                             wiresReleased);
    }
    return parts.join(QStringLiteral(", "));
}

// --------------------------------------------------------------------------

QString CircuitCopy::nextTerminalNumber(const QString &block, const QSet<QString> &taken)
{
    for (int n = 1; n <= 9999; ++n) {
        const QString candidate = QString::number(n);
        if (!taken.contains(terminalKey(block, candidate)))
            return candidate;
    }
    return QString();
}

QString CircuitCopy::nextSignalName(const QString &name, const QSet<QString> &taken)
{
    if (name.isEmpty())
        return name;
    // On repart toujours du nom d'origine : sans cela, un troisieme collage
    // donnerait DEMARRAGE_2_2 au lieu de DEMARRAGE_3.
    QString base = name;
    const int cut = base.lastIndexOf(QLatin1Char('_'));
    if (cut > 0) {
        bool digits = false;
        const QString tail = base.mid(cut + 1);
        tail.toInt(&digits);
        if (digits && !tail.isEmpty())
            base = base.left(cut);
    }
    for (int n = 2; n <= 999; ++n) {
        const QString candidate = base + QLatin1Char('_') + QString::number(n);
        if (!taken.contains(candidate))
            return candidate;
    }
    return name;
}

int CircuitCopy::nextFreeSlot(const Project &project, int rack, const QSet<QString> &takenKeys)
{
    QSet<QString> occupied = takenKeys;
    for (const Folio *folio : project.folios()) {
        for (const SymbolInstance *symbol : folio->entitiesOfType<SymbolInstance>()) {
            if (PlcModule::isModule(*symbol))
                occupied.insert(slotKey(PlcModule::rack(*symbol), PlcModule::slot(*symbol)));
        }
    }
    // Un rack physique depasse rarement seize emplacements ; on cherche un peu
    // plus loin pour ne pas rendre -1 sur une configuration inhabituelle.
    for (int slot = 0; slot < 64; ++slot) {
        if (!occupied.contains(slotKey(rack, slot)))
            return slot;
    }
    return -1;
}

CircuitCopyResult CircuitCopy::retag(std::vector<EntityPtr> &copies, const Project &project,
                                     const Profile &profile, const PlcDatabase &plc,
                                     const Folio *destination)
{
    CircuitCopyResult result;

    // ---- les groupes d'appareils ------------------------------------------
    //
    // Une bobine et ses contacts auxiliaires partagent un groupe. La copie
    // doit garder le partage et changer le groupe : sans nouveau groupe, les
    // contacts copies rejoindraient la bobine d'origine et prendraient sa
    // designation.
    QHash<QString, QString> renamedGroups;
    for (const EntityPtr &entity : copies) {
        auto *symbol = dynamic_cast<SymbolInstance *>(entity.get());
        if (!symbol || symbol->deviceGroup.isEmpty())
            continue;
        if (!renamedGroups.contains(symbol->deviceGroup))
            renamedGroups.insert(symbol->deviceGroup, newId());
        symbol->deviceGroup = renamedGroups.value(symbol->deviceGroup);
    }

    // ---- appareils ---------------------------------------------------------
    QVector<SymbolInstance *> devices;
    QVector<SymbolInstance *> terminals;
    for (const EntityPtr &entity : copies) {
        auto *symbol = dynamic_cast<SymbolInstance *>(entity.get());
        if (!symbol)
            continue;
        // Un symbole dont la definition a disparu garde son repere : la
        // designation le sauterait, et l'avoir vide avant l'appel l'aurait
        // efface pour rien. Un dessin incomplet ne doit pas devenir anonyme.
        if (!project.library.definition(symbol->definitionId))
            continue;
        if (isTerminal(project, *symbol))
            terminals.append(symbol);
        else
            devices.append(symbol);
    }

    for (SymbolInstance *symbol : devices) {
        // Le verrou dit « ce repere est le mien, n'y touche pas ». Il porte
        // sur l'original, pas sur la copie : personne n'a saisi le repere de
        // la copie, donc le re-reperer n'ecrase aucune saisie.
        symbol->designationLocked = false;
        symbol->setDesignation(QString());
    }
    const NumberingResult designated =
            Numbering::designateNew(project, profile, devices, destination);
    result.devicesRetagged = designated.devicesDesignated;

    // ---- bornes ------------------------------------------------------------
    //
    // Une borne garde son bornier : cinq bornes copiees sont cinq bornes de
    // plus dans X1, pas cinq borniers.
    if (!terminals.isEmpty()) {
        QSet<QString> takenTerminals = terminalKeysOf(project);
        // Ordre de lecture : les numeros attribues doivent se suivre comme les
        // bornes se suivent sur le folio, pas comme la selection a ete faite.
        std::sort(terminals.begin(), terminals.end(),
                  [](const SymbolInstance *a, const SymbolInstance *b) {
                      const QPointF pa = a->placement.position;
                      const QPointF pb = b->placement.position;
                      if (!fuzzyEqual(pa.y(), pb.y(), 0.5))
                          return pa.y() < pb.y();
                      return pa.x() < pb.x();
                  });
        for (SymbolInstance *terminal : terminals) {
            const QString block = terminal->designation();
            const QString number = terminal->fields.value(QStringLiteral("terminal"));
            if (block.isEmpty())
                continue;
            if (!takenTerminals.contains(terminalKey(block, number)) && !number.isEmpty()) {
                takenTerminals.insert(terminalKey(block, number));
                continue;
            }
            const QString fresh = nextTerminalNumber(block, takenTerminals);
            if (fresh.isEmpty())
                continue;
            terminal->fields.insert(QStringLiteral("terminal"), fresh);
            takenTerminals.insert(terminalKey(block, fresh));
            ++result.terminalsRenumbered;
        }
    }

    // ---- cartes d'automate -------------------------------------------------
    //
    // Deux cartes au meme emplacement portent les memes adresses : l'erreur ne
    // se voit pas sur le folio, elle se voit a la mise en service.
    QSet<QString> takenSlots;
    QSet<QString> usedAddresses;
    bool addressesGathered = false;

    for (SymbolInstance *symbol : devices) {
        if (!PlcModule::isModule(*symbol))
            continue;

        if (!addressesGathered) {
            for (const Folio *folio : project.folios()) {
                for (const SymbolInstance *placed : folio->entitiesOfType<SymbolInstance>()) {
                    if (!PlcModule::isModule(*placed))
                        continue;
                    for (const PlcPoint &point : PlcModule::points(*placed, plc))
                        usedAddresses.insert(point.address);
                }
            }
            addressesGathered = true;
        }

        bool moved = false;
        const int rack = PlcModule::rack(*symbol);
        const int slot = nextFreeSlot(project, rack, takenSlots);
        if (slot >= 0) {
            takenSlots.insert(slotKey(rack, slot));
            if (slot != PlcModule::slot(*symbol)) {
                symbol->fields.insert(PlcModule::slotKey(), QString::number(slot));
                moved = true;
            }
        }

        // Tous les formats ne citent pas l'emplacement : « %I%B.%b » de
        // Siemens ne compte que des octets, si bien que deux cartes a la meme
        // adresse de depart se recouvrent quel que soit leur emplacement
        // physique. On decale alors l'adresse de depart d'un module entier —
        // jamais d'un point, qui laisserait les deux cartes a cheval.
        QVector<PlcPoint> points = PlcModule::points(*symbol, plc);
        for (int attempt = 0; attempt < 64 && !points.isEmpty(); ++attempt) {
            const bool clash = std::any_of(points.cbegin(), points.cend(),
                                           [&](const PlcPoint &p) {
                                               return usedAddresses.contains(p.address);
                                           });
            if (!clash)
                break;
            symbol->fields.insert(PlcModule::firstPointKey(),
                                  QString::number(PlcModule::firstPoint(*symbol)
                                                  + int(points.size())));
            points = PlcModule::points(*symbol, plc);
            moved = true;
        }
        for (const PlcPoint &point : points)
            usedAddresses.insert(point.address);

        if (moved)
            ++result.modulesMoved;
    }

    // ---- renvois de signal --------------------------------------------------
    //
    // Une etiquette ordinaire n'est PAS renommee : huit departs moteur se
    // branchent tous sur L1, et renommer l'etiquette de la copie
    // debrancherait le circuit qu'on vient de copier. Une fleche de signal,
    // elle, designe un renvoi unique — deux sources du meme code sont une
    // faute. Les deux bouts d'un meme renvoi copies ensemble recoivent le
    // meme nouveau code, sinon la copie perd son renvoi.
    QSet<QString> takenSignals = signalNamesOf(project);
    QHash<QString, QString> renamedSignals;
    for (const EntityPtr &entity : copies) {
        auto *label = dynamic_cast<Label *>(entity.get());
        if (!label || !label->isSignalArrow() || label->name.isEmpty())
            continue;
        auto it = renamedSignals.constFind(label->name);
        if (it == renamedSignals.constEnd()) {
            const QString fresh = nextSignalName(label->name, takenSignals);
            takenSignals.insert(fresh);
            it = renamedSignals.insert(label->name, fresh);
        }
        if (label->name != it.value()) {
            label->name = it.value();
            ++result.signalsRenamed;
        }
    }

    // ---- reperes de fil -----------------------------------------------------
    //
    // Le repere est libere, pas recalcule : il depend du potentiel, donc de la
    // netlist, donc du dessin une fois les copies posees. Un repere invente
    // ici serait faux des le premier fil qui touche la copie ; un repere vide
    // dit la verite — ce fil reste a numeroter.
    for (const EntityPtr &entity : copies) {
        auto *wire = dynamic_cast<Wire *>(entity.get());
        if (!wire || wire->number.isEmpty())
            continue;
        wire->number.clear();
        wire->numberLocked = false;
        ++result.wiresReleased;
    }

    return result;
}

} // namespace dsn
