// Reperage automatique des fils et designation des appareils.
//
// Regle qui gouverne tout ce fichier : ce que l'utilisateur a saisi a la main
// n'est jamais ecrase. Un automatisme qui detruit une saisie manuelle est
// desactive des la premiere mauvaise surprise, et l'outil perd d'un coup ce
// qui le distingue d'un logiciel de dessin generique.
#pragma once

#include "core/netlist.h"
#include "core/project.h"
#include "profile.h"

#include <QVector>

namespace dsn {

class SymbolInstance;

struct NumberingResult {
    int wiresNumbered = 0;
    int netsNumbered = 0;
    int devicesDesignated = 0;
    int keptManual = 0; // reperes et designations preserves parce que verrouilles
    int terminalsNumbered = 0; // bornes qui ont recu leur numero dans leur bornier
    QStringList notes;
};

class Numbering
{
public:
    // Attribue un repere par potentiel, puis l'inscrit sur tous les fils du
    // potentiel. Un repere verrouille quelque part sur le potentiel devient le
    // repere de tout le potentiel.
    static NumberingResult numberWires(Project &project, const Netlist &netlist,
                                       const Profile &profile);

    // Attribue les designations d'appareil dans l'ordre de lecture : folio,
    // puis de haut en bas, puis de gauche a droite.
    static NumberingResult designateDevices(Project &project, const Profile &profile);

    // Designe un lot d'appareils sans toucher au reste du projet, contre les
    // reperes deja portes. C'est ce dont le collage a besoin : les copies ne
    // sont pas encore dans le folio, et rien d'autre ne doit bouger.
    //
    // La regle appliquee est la meme que celle de designateDevices — meme
    // format, meme ordre de lecture, meme departage — parce qu'un second
    // chemin finirait par diverger du premier.
    //
    // `destination` sert a situer les appareils : sans lui, la designation
    // par reference de ligne n'a pas de folio a citer.
    static NumberingResult designateNew(const Project &project, const Profile &profile,
                                        const QVector<SymbolInstance *> &symbols,
                                        const Folio *destination);

    // Les deux d'un coup, sur une netlist recalculee entre les deux etapes :
    // la designation d'un appareil peut apparaitre dans un repere de fil.
    static NumberingResult renumberAll(Project &project, const Profile &profile);

    // Reperes deja utilises, pour eviter les collisions avec la saisie manuelle.
    static QSet<QString> usedWireNumbers(const Project &project);
    static QSet<QString> usedDesignations(const Project &project);

    // Tous les reperes portes, verrouilles ou non. La difference avec
    // usedDesignations est deliberee : une regeneration globale a le droit de
    // reattribuer ce qui n'est pas verrouille, une designation de lot n'a le
    // droit de bousculer personne.
    static QSet<QString> allDesignations(const Project &project);
};

} // namespace dsn
