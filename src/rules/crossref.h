// Renvois de folio : ou va un signal, et d'ou il vient.
//
// C'est la moitie invisible des fleches de signal d'AutoCAD Electrical. Une
// fleche qui porte seulement un nom de code oblige a feuilleter le dossier
// pour trouver l'autre bout ; celle qui porte « → 2/A3 » se lit d'un coup
// d'oeil, et c'est tout l'interet du renvoi.
//
// Le renvoi n'est jamais stocke dans le document : il se deduit du dessin, et
// un dessin qui bouge doit le voir changer. Il est donc calcule ici puis
// pousse dans le peintre, comme les broches en l'air.
#pragma once

#include "core/netlist.h"
#include "core/project.h"

#include <QHash>
#include <QString>

namespace dsn {

class CrossReference
{
public:
    // Texte de renvoi par etiquette, indexe par identifiant d'entite. Les
    // etiquettes sans autre bout n'apparaissent pas : mieux vaut rien
    // afficher qu'une fleche vers nulle part.
    static QHash<QString, QString> resolve(const Project &project, const Netlist &netlist);

    // Repere « folio/zone » d'un point, ex. « 2/A3 ». Vide hors du cadre.
    static QString locationOf(const Folio &folio, const QPointF &point);
};

} // namespace dsn
