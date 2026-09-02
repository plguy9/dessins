// COPIER UN CIRCUIT — le collage qui re-repere.
//
// Coller un depart moteur en gardant KM1, F1 et le repere de fil 104 est
// l'erreur la plus couteuse d'un logiciel de schema : le dessin est juste, la
// nomenclature est fausse, et personne ne s'en apercoit avant le cablage.
// C'est aussi le geste numero un d'un folio de puissance — huit departs
// identiques ne se dessinent pas huit fois.
//
// Trois decisions gouvernent ce fichier.
//
// 1. **Le re-reperage se fait sur les copies, avant leur entree dans le
//    document.** Rien n'y entre en double, meme le temps d'une commande : le
//    collage reste une seule annulation, et l'audit ne voit jamais l'etat
//    intermediaire. Repere apres coup, il faudrait deux commandes et une
//    annulation en laisserait la moitie.
//
// 2. **Ce qui nomme un potentiel n'est pas re-repere.** Huit departs moteur
//    se branchent tous sur L1/L2/L3 : renommer l'etiquette de la copie
//    debrancherait le circuit qu'on vient de copier. Les fleches de signal
//    font exception — deux sources du meme code sont une faute, pas un
//    raccourci — et une paire source/destination copiee ensemble recoit un
//    seul nouveau code, sinon la copie perd son renvoi.
//
// 3. **Une borne garde son bornier et change de numero.** Le reste des
//    appareils suit la regle inverse : la copie est un nouvel appareil, donc
//    un nouveau repere. Mais copier cinq bornes de X1 veut dire « cinq bornes
//    de plus dans X1 », jamais « un bornier X2 par borne ».
//
// Le re-reperage n'invente aucune regle : il appelle Numbering::designateNew,
// donc le format du projet, l'ordre de lecture et le departage sont ceux de
// la regeneration globale.
#pragma once

#include "core/entity.h"
#include "core/project.h"
#include "plc.h"
#include "profile.h"

#include <vector>

namespace dsn {

class Folio;

struct CircuitCopyResult {
    int devicesRetagged = 0;   // appareils ayant recu un nouveau repere
    int wiresReleased = 0;     // fils dont le repere a ete libere
    int signalsRenamed = 0;    // codes de renvoi remplaces
    int terminalsRenumbered = 0;
    int modulesMoved = 0;      // cartes d'automate deplacees d'emplacement

    int total() const
    {
        return devicesRetagged + wiresReleased + signalsRenamed + terminalsRenumbered
                + modulesMoved;
    }

    // Une phrase pour la barre d'etat : ce qui a change, et seulement cela.
    // Un collage qui ne dit rien de ce qu'il a re-repere laisse croire que
    // les reperes ont ete conserves.
    QString summary() const;
};

class CircuitCopy
{
public:
    // Re-repere un lot de copies destine a `destination`. Les copies portent
    // deja leurs identifiants definitifs et leur position finale : c'est de la
    // position que dependent la reference de ligne et l'ordre de lecture.
    static CircuitCopyResult retag(std::vector<EntityPtr> &copies, const Project &project,
                                   const Profile &profile, const PlcDatabase &plc,
                                   const Folio *destination);

    // Le numero libre suivant d'un bornier, en tenant compte de ce que le
    // lot vient de prendre.
    static QString nextTerminalNumber(const QString &block, const QSet<QString> &taken);

    // Le code de renvoi libre suivant : DEMARRAGE -> DEMARRAGE_2. Le suffixe
    // est explicite plutot qu'incremente dans le nom, parce qu'un code se lit
    // sur le folio et qu'un « DEMARRAGE2 » se confond avec un nom voulu.
    static QString nextSignalName(const QString &name, const QSet<QString> &taken);

    // Emplacement libre suivant dans le meme rack, pour une carte copiee.
    // `takenKeys` porte les emplacements que le lot vient de prendre, sous la
    // forme « rack:emplacement ». Rend -1 si le rack est plein.
    static int nextFreeSlot(const Project &project, int rack, const QSet<QString> &takenKeys);
};

} // namespace dsn
