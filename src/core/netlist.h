// Extraction des potentiels.
//
// C'est la piece qui transforme un dessin en schema : sans elle, un fil n'est
// qu'un trait. Toutes les fonctions metier en decoulent — reperage des fils,
// bornier, nomenclature, renvois de folio, controles de coherence.
//
// Regles de connexion retenues, alignees sur l'usage des ateliers :
//   * une extremite de fil qui touche un autre fil connecte (piquage en T) ;
//   * un point de jonction explicite connecte tout ce qui passe par lui ;
//   * deux fils qui se croisent sans jonction ne connectent pas, meme si
//     chacun possede un sommet au point de croisement ;
//   * une broche ou une etiquette se comporte comme une extremite de fil.
//
// Les liaisons multi-conducteurs sont prises en charge d'emblee : deux fils qui
// se rejoignent apparient leurs conducteurs par nom quand les deux sont nommes,
// par rang sinon. Un element mono-conducteur s'attache au premier conducteur.
#pragma once

#include "project.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dsn {

class Netlist
{
public:
    struct PinRef {
        QString folioId;
        QString symbolId;
        QString designation;
        QString pinNumber;
        QString definitionId;
        PinType type = PinType::Passive;
        QPointF position;
    };

    struct WireRef {
        QString folioId;
        QString wireId;
        int conductor = 0;
        QString conductorName;
    };

    struct Net {
        int id = -1;
        QString name;      // nom du potentiel, issu des etiquettes
        QString number;    // repere de fil, attribue par le module rules
        QStringList labels;
        QStringList folioIds;
        QVector<PinRef> pins;
        QVector<WireRef> wires;
        QVector<QPointF> points;

        bool isNamed() const { return !name.isEmpty(); }
        int pinCount() const { return int(pins.size()); }
        bool crossesFolios() const { return folioIds.size() > 1; }
    };

    // Diagnostic de coherence, remonte a l'utilisateur par le panneau de
    // controle du projet.
    struct Diagnostic {
        enum class Severity { Info, Warning, Error };
        Severity severity = Severity::Warning;
        QString code;
        QString message;
        QString folioId;
        QString entityId;
        QPointF position;
    };

    static Netlist build(const Project &project);

    const QVector<Net> &nets() const noexcept { return m_nets; }
    int netCount() const noexcept { return int(m_nets.size()); }
    const Net *net(int id) const;

    const Net *netOfWire(const QString &wireId, int conductor = 0) const;
    const Net *netOfPin(const QString &symbolId, const QString &pinNumber) const;

    const QVector<Diagnostic> &diagnostics() const noexcept { return m_diagnostics; }

    // Nets ne comportant qu'un seul point de connexion : presque toujours un
    // fil oublie en l'air.
    QVector<const Net *> danglingNets() const;

    // Broches d'un symbole qui ne touchent rien.
    QVector<PinRef> unconnectedPins() const;

    static QString wireKey(const QString &wireId, int conductor);
    static QString pinKey(const QString &symbolId, const QString &pinNumber);

private:
    QVector<Net> m_nets;
    QVector<Diagnostic> m_diagnostics;
    QHash<QString, int> m_wireNet;
    QHash<QString, int> m_pinNet;

    friend class NetlistBuilder;
};

} // namespace dsn
