// Echelle de commande — l'equivalent de l'Insert Ladder d'AutoCAD Electrical.
//
// Un circuit de commande se lit comme une echelle : deux rails verticaux
// portant le potentiel d'alimentation, et des barreaux horizontaux numerotes
// entre eux. Le numero de barreau est ce qui rend un schema de commande
// navigable — « le contact est en ligne 12 » se dit et se retrouve.
//
// Le generateur vit dans rules/ : c'est une convention metier, pas de la
// geometrie, et elle differe d'une norme a l'autre.
#pragma once

#include "core/entity.h"

#include <QPointF>
#include <QString>
#include <QVector>

namespace dsn {

struct LadderSpec {
    QPointF origin{ 40.0, 40.0 }; // sommet du rail de gauche
    double width = 150.0;         // ecart entre les deux rails
    int rungs = 10;
    double rungSpacing = 18.0;
    int firstRungNumber = 1;
    int rungNumberStep = 1;

    QString leftRailName = QStringLiteral("L1");
    QString rightRailName = QStringLiteral("N");

    // Types de fils des deux rails. Une echelle de commande se lit d'abord a
    // ses couleurs : la phase et le neutre ne peuvent pas sortir identiques.
    QString leftRailType = QStringLiteral("l1");
    QString rightRailType = QStringLiteral("n");
    QString rungType;   // vide = type par defaut pour les barreaux

    // Les barreaux sont optionnels : beaucoup de dessinateurs posent les
    // rails seuls puis tracent chaque ligne au fur et a mesure.
    bool drawRungs = false;
    bool numberRungs = true;
    double numberHeight = 2.5;

    double height() const { return (rungs - 1) * rungSpacing; }
    QPointF leftBottom() const { return origin + QPointF(0.0, height()); }
    QPointF rightTop() const { return origin + QPointF(width, 0.0); }
    QPointF rungLeft(int index) const { return origin + QPointF(0.0, index * rungSpacing); }
    int rungNumber(int index) const { return firstRungNumber + index * rungNumberStep; }
};

class LadderBuilder
{
public:
    // Produit les entites de l'echelle : deux rails, les etiquettes de
    // potentiel qui les nomment, les numeros de ligne, et les barreaux si
    // demandes.
    static std::vector<EntityPtr> build(const LadderSpec &spec);

    // Verifie que l'echelle tient dans la zone utile du folio. Renvoie un
    // message d'avertissement, ou une chaine vide si tout tient.
    static QString fitWarning(const LadderSpec &spec, const QRectF &frameRect);
};

} // namespace dsn
