// Deplacer un appareil sans le debrancher.
//
// Sur un schema, un symbole n'est jamais seul : des fils viennent mourir sur
// ses broches. Le deplacer en laissant les fils derriere lui casse le schema
// en silence — la netlist s'en apercoit, l'oeil non. Ces outils font suivre
// les extremites de fil posees sur les broches, comme le fait AutoCAD
// Electrical avec Scoot et Move Component.
#pragma once

#include "folio.h"
#include "symbollibrary.h"

#include <QSet>
#include <QVector>

#include <optional>

namespace dsn {

class SymbolInstance;

class ComponentTools
{
public:
    // Une extremite de fil posee sur une broche : le fil et le rang du sommet
    // concerne. On retient le rang plutot que le point, parce que c'est lui
    // qui reste valable une fois la geometrie deplacee.
    struct WireEnd {
        QString wireId;
        int vertex = 0;
    };

    // Extremites de fil raccordees aux broches d'un symbole. Seules les
    // extremites comptent : un fil qui passe au milieu d'une broche sans y
    // finir n'y est pas raccorde, il la croise.
    static QVector<WireEnd> attachedWireEnds(const Folio &folio, const SymbolLibrary &library,
                                             const SymbolInstance &symbol);

    // Axe de glissement d'un appareil : la direction du ou des fils qui s'y
    // raccordent. Rien quand ils ne s'accordent pas — glisser un appareil
    // raccorde dans deux directions n'aurait pas de sens unique.
    static std::optional<QPointF> scootAxis(const Folio &folio, const SymbolLibrary &library,
                                            const SymbolInstance &symbol);

    // Coupure d'un fil a l'insertion d'un appareil. AutoCAD Electrical le fait
    // tout seul : poser une borne ou un contact sur un fil doit brancher
    // l'appareil, pas le poser par-dessus en laissant le fil passer derriere.
    struct WireSplit {
        QString wireId;
        QVector<QPointF> before;  // du debut du fil jusqu'a la premiere broche
        QVector<QPointF> after;   // de la seconde broche jusqu'a la fin
    };

    // Coupure a realiser pour brancher ce symbole, ou rien. Il faut un
    // appareil a deux broches, pose sur un fil et aligne avec lui : tout le
    // reste se pose a cote du fil, pas dessus.
    static std::optional<WireSplit> splitForInsertion(const Folio &folio,
                                                      const SymbolLibrary &library,
                                                      const SymbolInstance &symbol);

    // Recouture du fil au retrait d'un appareil. C'est le symetrique exact de
    // splitForInsertion : poser un appareil de passage sur un fil le coupe et
    // le rebranche, l'enlever doit refermer le trace. Sans cela, effacer un
    // contact laisse deux fils qui pointent vers du vide — le dessin parait
    // juste et le circuit est ouvert.
    struct WireHeal {
        QString keepWireId;      // le fil qui survit, avec son repere et son type
        QString removeWireId;    // celui qui est absorbe
        QVector<QPointF> points; // le trace recousu
    };

    // La recouture a faire en retirant ce symbole, ou rien. `alsoRemoved`
    // porte ce qui part dans le meme geste : effacer un depart entier ne doit
    // pas recoudre des fils qu'on est en train d'effacer aussi.
    static std::optional<WireHeal> healOnRemoval(const Folio &folio, const SymbolLibrary &library,
                                                 const SymbolInstance &symbol,
                                                 const QSet<QString> &alsoRemoved = {});

    // Remplacer un symbole pose, sans le debrancher.
    //
    // Un contact NO devient un contact NF, un disjoncteur change de calibre :
    // le geste existe chez AutoCAD Electrical, et sans lui il faut effacer,
    // reposer, retaper le repere et refaire les fils. Trois choses sont
    // gardees, et ce sont les trois qu'on perdrait a la main : le REPERE et
    // les champs, la POSITION avec son orientation, et les RACCORDEMENTS.
    struct PinMove {
        QString wireId;
        int vertex = 0;
        QPointF to;
    };

    struct SwapPlan {
        bool valid = false;       // faux si une des deux definitions manque
        QVector<PinMove> moves;   // extremites de fil a suivre
        int orphaned = 0;         // extremites qu'aucune broche neuve ne reprend
    };

    // Ce qu'il faut deplacer pour que ce symbole devienne `newDefinitionId`.
    // L'appariement des broches se fait d'abord par NUMERO — un 13/14 reste un
    // 13/14 quel que soit le dessin — puis, a defaut, par la broche neuve la
    // plus proche de l'ancienne. Une extremite qu'aucune broche ne reprend est
    // comptee, jamais deplacee au hasard : il vaut mieux le dire que laisser
    // le dessinateur decouvrir un fil en l'air.
    static SwapPlan planSwap(const Folio &folio, const SymbolLibrary &library,
                             const SymbolInstance &symbol, const QString &newDefinitionId);

    // Projette un deplacement sur l'axe : Scoot ne bouge que le long du fil,
    // c'est ce qui l'empeche de detacher l'appareil de son circuit.
    static QPointF constrainToAxis(const QPointF &delta, const QPointF &axis);
};

} // namespace dsn
