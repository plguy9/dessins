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
};

} // namespace dsn
