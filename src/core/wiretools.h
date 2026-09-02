// Ajuster et prolonger les fils — les equivalents de TRIM et EXTEND.
//
// Ce sont les deux commandes d'edition les plus utilisees d'AutoCAD, et sur
// un schema elles servent tout le temps : un fil trace trop long qu'on
// raccourcit au croisement, un fil trop court qu'on amene jusqu'a la borne.
//
// Les deux vivent dans le coeur : ce sont des operations de geometrie pure,
// et une regle de decoupe fausse produit un fil silencieusement debranche.
#pragma once

#include "folio.h"
#include "symbollibrary.h"

#include <QStringList>
#include <QVector>

#include <optional>

namespace dsn {

// Un fil ajuste devient zero, un ou deux morceaux selon que la portion
// retiree touche une extremite ou le milieu.
struct TrimResult {
    QVector<QVector<QPointF>> pieces;
    QPointF cutFrom;
    QPointF cutTo;
};

// FIL MULTIPLE (BUS) — le tracé d'un depart triphase d'un seul geste.
//
// Le modele porte n conducteurs par fil depuis l'origine, mais un folio de
// puissance ne dessine pas un trait unique : il dessine L1, L2 et L3 cote a
// cote. Le bus est donc N fils paralleles, un conducteur nomme chacun — et
// c'est ce nom qui fait que la netlist raccorde L1 a L1 et jamais a L2.
struct BusSpec {
    int count = 3;
    double spacing = 5.0; // millimetres entre deux conducteurs voisins
    // Un nom par conducteur. Plus court que `count`, les derniers restent
    // anonymes plutot que de recevoir un nom invente.
    QStringList conductors{ QStringLiteral("L1"), QStringLiteral("L2"),
                            QStringLiteral("L3") };

    bool isValid() const { return count >= 2 && spacing > 0.0; }
    QString conductorAt(int index) const
    {
        return index >= 0 && index < conductors.size() ? conductors.at(index) : QString();
    }
};

class WireTools
{
public:
    // Retire la portion du fil comprise entre les deux croisements qui
    // encadrent le point vise. Sans croisement d'aucun cote, le fil entier
    // disparait — c'est le comportement d'AutoCAD, et il est previsible.
    static std::optional<TrimResult> trim(const Folio &folio, const SymbolLibrary &library,
                                          const QString &wireId, const QPointF &at);

    // Allonge une extremite jusqu'au premier obstacle rencontre dans son axe.
    // Renvoie le nouveau point, ou rien s'il n'y a rien a atteindre.
    static std::optional<QPointF> extend(const Folio &folio, const SymbolLibrary &library,
                                         const QString &wireId, bool lastEnd);

    // ---- utilitaires de polyligne, exposes pour les tests ---------------
    static double polylineLength(const QVector<QPointF> &points);
    static QPointF pointAtLength(const QVector<QPointF> &points, double distance);
    static double lengthAtPoint(const QVector<QPointF> &points, const QPointF &point);
    // Portion de polyligne entre deux abscisses curvilignes, sommets
    // intermediaires conserves.
    static QVector<QPointF> subPolyline(const QVector<QPointF> &points, double from, double to);

    // Decalage parallele. Chaque segment glisse de `distance` le long de sa
    // normale, et les sommets deviennent les intersections des droites
    // porteuses decalees.
    //
    // Translater la polyligne entiere serait plus court et faux : sur un
    // coude, la jambe qui suit resterait a la bonne distance, mais le sommet
    // se retrouverait decale de biais et les deux jambes ne seraient plus
    // paralleles a l'original. C'est visible des le premier bus triphase qui
    // tourne, et c'est pour cela que la fonction existe.
    //
    // `distance` est signe : positif vers la gauche du sens de parcours.
    static QVector<QPointF> offsetPolyline(const QVector<QPointF> &points, double distance);

    // Les traces des N conducteurs d'un bus. Le premier est le trace vise ;
    // les suivants s'en ecartent d'un pas chacun.
    //
    // Le cote ne depend PAS du sens du trace : les conducteurs vont vers le
    // bas d'une horizontale et vers la droite d'une verticale, quel que soit
    // le sens dans lequel on a tire le fil. C'est la convention du dessin, et
    // sans elle un bus tire de droite a gauche partirait vers le haut — le
    // meme geste donnerait deux resultats.
    static QVector<QVector<QPointF>> busPaths(const QVector<QPointF> &path, const BusSpec &spec);
};

} // namespace dsn
