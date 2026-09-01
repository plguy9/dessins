// Primitive vectorielle unique, partagee par les annotations d'un folio et par
// le graphisme des definitions de symboles. Un seul type ici signifie un seul
// chemin de rendu dans la couche d'affichage, donc un seul endroit ou l'ecran
// et le PDF peuvent diverger : aucun.
#pragma once

#include "geometry.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace dsn {

struct Primitive {
    enum class Kind { Line, Polyline, Rect, Circle, Arc, Text };
    enum class Align { Left, Center, Right };

    Kind kind = Kind::Line;
    QVector<QPointF> points;   // Circle/Arc : [centre]. Rect : [coin, coin].
    double radius = 0.0;
    double startAngle = 0.0;   // degres, sens trigonometrique
    double spanAngle = 360.0;
    double lineWidth = 0.25;
    bool filled = false;

    QString text;              // Kind::Text uniquement
    double textHeight = 2.0;
    Align align = Align::Left;

    QRectF bounds() const;
    void translate(const QPointF &delta);

    QJsonObject toJson() const;
    static Primitive fromJson(const QJsonValue &v);

    static QString kindTag(Kind k);
    static Kind kindFromTag(const QString &tag);
    static QString alignTag(Align a);
    static Align alignFromTag(const QString &tag);

    // Constructeurs de confort, utilises par la bibliotheque de symboles.
    static Primitive line(const QPointF &a, const QPointF &b, double width = 0.25);
    static Primitive polyline(const QVector<QPointF> &pts, double width = 0.25);
    static Primitive rect(const QRectF &r, double width = 0.25, bool filled = false);
    static Primitive circle(const QPointF &centre, double radius, double width = 0.25,
                            bool filled = false);
    static Primitive arc(const QPointF &centre, double radius, double startAngle,
                         double spanAngle, double width = 0.25);
    static Primitive label(const QPointF &at, const QString &text, double height = 2.0,
                           Align align = Align::Left);
};

} // namespace dsn
