// Rechercher et remplacer du texte dans tout le dossier.
//
// C'est la premiere commande qu'un dessinateur venu d'AutoCAD cherche et ne
// trouvait pas. Le cas courant n'a rien d'exotique : l'affaire change de
// numero, un moteur change de repere d'atelier, une reference fabricant est
// remplacee — et cela se lit dans quarante endroits repartis sur douze
// folios. A la main, on en oublie toujours un, et c'est celui-la qui part a
// l'atelier.
//
// Deux regles, reprises de l'audit parce qu'elles ont la meme raison d'etre :
//
// 1. **Tout constat porte un lieu** — folio, zone du cadre, entite. Une liste
//    qui dit « 12 occurrences » sans dire ou coute plus de temps qu'elle n'en
//    fait gagner.
// 2. **Rien n'est cherche hors de la portee demandee.** La question d'AutoCAD
//    — tout le projet ou le folio actif — se pose ici comme pour un rapport,
//    et par le meme ReportScope : un seul point de filtrage.
#pragma once

#include "core/project.h"
#include "reports.h"

#include <QString>
#include <QVector>

namespace dsn {

// Ce qu'on cherche, et ou.
struct FindQuery {
    QString needle;
    QString replacement;
    bool caseSensitive = false;
    bool wholeWord = false;
    ReportScope scope;

    // Les gisements de texte d'un schema. Ils sont separes parce qu'on ne
    // cherche pas la meme chose dans un repere et dans une annotation :
    // renommer tous les « M1 » d'un dossier ne doit pas toucher la phrase
    // « alimentation M1 » d'un cartouche si on ne l'a pas demande.
    bool inTexts = true;         // textes libres
    bool inLabels = true;        // etiquettes de potentiel et renvois
    bool inDesignations = true;  // reperes d'appareil
    bool inFields = true;        // valeur, reference, fonction, localisation...
    bool inWireNumbers = true;   // reperes de fil
};

// Une occurrence, avec son lieu. `field` n'est renseigne que pour un champ
// libre — c'est lui qui dit lequel remplacer.
struct FindHit {
    QString folioId;
    QString folioLabel;  // « 2 » ou « 2 — Commande » : de quoi s'y retrouver
    QString entityId;
    QString zone;        // reference de zone du cadre, comme dans l'audit
    QString where;       // « Repère », « Texte », « Champ Valeur »...
    QString field;       // clef du champ, vide sinon
    QString before;
    QString after;       // ce que le remplacement donnerait
};

class FindReplace
{
public:
    // Les occurrences, dans l'ordre de lecture du dossier. Une recherche a
    // vide ne renvoie rien plutot que tout : « remplacer rien par X » n'a pas
    // de sens et remplirait le dessin.
    static QVector<FindHit> find(const Project &project, const FindQuery &query);

    // Le texte avec toutes les occurrences remplacees. Fonction pure : c'est
    // elle que les tests lisent, et c'est le seul endroit qui sait ce que
    // « mot entier » veut dire.
    static QString replaced(const QString &source, const FindQuery &query);

    // Vrai si le texte contient au moins une occurrence.
    static bool matches(const QString &source, const FindQuery &query);
};

} // namespace dsn
