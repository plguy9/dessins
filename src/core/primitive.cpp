#include "primitive.h"
#include "jsonutils.h"

namespace dsn {

QString Primitive::kindTag(Kind k)
{
    switch (k) {
    case Kind::Line: return QStringLiteral("line");
    case Kind::Polyline: return QStringLiteral("polyline");
    case Kind::Rect: return QStringLiteral("rect");
    case Kind::Circle: return QStringLiteral("circle");
    case Kind::Arc: return QStringLiteral("arc");
    case Kind::Text: return QStringLiteral("text");
    }
    return QStringLiteral("line");
}

Primitive::Kind Primitive::kindFromTag(const QString &tag)
{
    if (tag == QLatin1String("polyline")) return Kind::Polyline;
    if (tag == QLatin1String("rect")) return Kind::Rect;
    if (tag == QLatin1String("circle")) return Kind::Circle;
    if (tag == QLatin1String("arc")) return Kind::Arc;
    if (tag == QLatin1String("text")) return Kind::Text;
    return Kind::Line;
}

QString Primitive::strokeTag(Stroke s)
{
    switch (s) {
    case Stroke::Dashed: return QStringLiteral("dashed");
    case Stroke::Dotted: return QStringLiteral("dotted");
    case Stroke::DashDot: return QStringLiteral("dashdot");
    case Stroke::Solid: break;
    }
    return QStringLiteral("solid");
}

Primitive::Stroke Primitive::strokeFromTag(const QString &tag)
{
    if (tag == QLatin1String("dashed")) return Stroke::Dashed;
    if (tag == QLatin1String("dotted")) return Stroke::Dotted;
    if (tag == QLatin1String("dashdot")) return Stroke::DashDot;
    return Stroke::Solid;
}

QString Primitive::alignTag(Align a)
{
    switch (a) {
    case Align::Center: return QStringLiteral("center");
    case Align::Right: return QStringLiteral("right");
    case Align::Left: break;
    }
    return QStringLiteral("left");
}

Primitive::Align Primitive::alignFromTag(const QString &tag)
{
    if (tag == QLatin1String("center")) return Align::Center;
    if (tag == QLatin1String("right")) return Align::Right;
    return Align::Left;
}

QRectF Primitive::bounds() const
{
    switch (kind) {
    case Kind::Circle:
    case Kind::Arc: {
        const QPointF c = points.isEmpty() ? QPointF() : points.first();
        return QRectF(c.x() - radius, c.y() - radius, radius * 2.0, radius * 2.0)
                .adjusted(-lineWidth / 2, -lineWidth / 2, lineWidth / 2, lineWidth / 2);
    }
    case Kind::Text: {
        const QPointF a = points.isEmpty() ? QPointF() : points.first();
        const double w = textHeight * 0.62 * std::max(1, int(text.size()));
        QRectF r(a.x(), a.y() - textHeight, w, textHeight * 1.35);
        if (align == Align::Center)
            r.moveLeft(a.x() - w / 2.0);
        else if (align == Align::Right)
            r.moveLeft(a.x() - w);
        return r;
    }
    default: {
        if (points.isEmpty())
            return QRectF();
        QRectF r(points.first(), points.first());
        for (const QPointF &p : points) {
            r.setLeft(std::min(r.left(), p.x()));
            r.setTop(std::min(r.top(), p.y()));
            r.setRight(std::max(r.right(), p.x()));
            r.setBottom(std::max(r.bottom(), p.y()));
        }
        const double h = lineWidth / 2.0;
        return r.adjusted(-h, -h, h, h);
    }
    }
}

void Primitive::translate(const QPointF &delta)
{
    for (QPointF &p : points)
        p += delta;
}

void Primitive::scale(const QPointF &base, double factor)
{
    if (fuzzyEqual(factor, 1.0) || factor <= kEpsilon)
        return;
    for (QPointF &p : points)
        p = scaledAbout(p, base, factor);
    radius *= factor;
    textHeight *= factor;
    // L'EPAISSEUR DE TRAIT NE SUIT PAS. Grossir un objet change ses
    // dimensions, pas la plume qui le dessine : un cercle deux fois plus grand
    // reste trace au meme trait, comme sur une planche. La mettre a l'echelle
    // faisait grossir le trait avec la forme, ce qu'un dessinateur ne demande
    // jamais — et sur un schema, l'epaisseur porte un sens (puissance,
    // commande) qu'un agrandissement n'a pas a modifier.
}

QJsonObject Primitive::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("kind")] = kindTag(kind);
    o[QStringLiteral("points")] = pointsToJson(points);
    if (kind == Kind::Circle || kind == Kind::Arc)
        o[QStringLiteral("radius")] = roundStorage(radius);
    if (kind == Kind::Arc) {
        o[QStringLiteral("startAngle")] = roundStorage(startAngle);
        o[QStringLiteral("spanAngle")] = roundStorage(spanAngle);
    }
    if (kind == Kind::Text) {
        o[QStringLiteral("text")] = text;
        o[QStringLiteral("textHeight")] = roundStorage(textHeight);
        if (align != Align::Left)
            o[QStringLiteral("align")] = alignTag(align);
    }
    if (!fuzzyEqual(lineWidth, 0.25))
        o[QStringLiteral("lineWidth")] = roundStorage(lineWidth);
    if (stroke != Stroke::Solid)
        o[QStringLiteral("stroke")] = strokeTag(stroke);
    if (filled)
        o[QStringLiteral("filled")] = true;
    return o;
}

Primitive Primitive::fromJson(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    Primitive p;
    p.kind = kindFromTag(o.value(QStringLiteral("kind")).toString());
    p.points = pointsFromJson(o.value(QStringLiteral("points")));
    p.radius = o.value(QStringLiteral("radius")).toDouble(0.0);
    p.startAngle = o.value(QStringLiteral("startAngle")).toDouble(0.0);
    p.spanAngle = o.value(QStringLiteral("spanAngle")).toDouble(360.0);
    p.lineWidth = o.value(QStringLiteral("lineWidth")).toDouble(0.25);
    p.stroke = strokeFromTag(o.value(QStringLiteral("stroke")).toString());
    p.filled = o.value(QStringLiteral("filled")).toBool(false);
    p.text = o.value(QStringLiteral("text")).toString();
    p.textHeight = o.value(QStringLiteral("textHeight")).toDouble(2.0);
    p.align = alignFromTag(o.value(QStringLiteral("align")).toString());
    return p;
}

Primitive Primitive::line(const QPointF &a, const QPointF &b, double width)
{
    Primitive p;
    p.kind = Kind::Line;
    p.points = { a, b };
    p.lineWidth = width;
    return p;
}

Primitive Primitive::polyline(const QVector<QPointF> &pts, double width)
{
    Primitive p;
    p.kind = Kind::Polyline;
    p.points = pts;
    p.lineWidth = width;
    return p;
}

Primitive Primitive::rect(const QRectF &r, double width, bool filled)
{
    Primitive p;
    p.kind = Kind::Rect;
    p.points = { r.topLeft(), r.bottomRight() };
    p.lineWidth = width;
    p.filled = filled;
    return p;
}

Primitive Primitive::dashedRect(const QRectF &r, double width)
{
    Primitive p = rect(r, width);
    p.stroke = Stroke::Dashed;
    return p;
}

Primitive Primitive::circle(const QPointF &centre, double radius, double width, bool filled)
{
    Primitive p;
    p.kind = Kind::Circle;
    p.points = { centre };
    p.radius = radius;
    p.lineWidth = width;
    p.filled = filled;
    return p;
}

Primitive Primitive::arc(const QPointF &centre, double radius, double startAngle,
                         double spanAngle, double width)
{
    Primitive p;
    p.kind = Kind::Arc;
    p.points = { centre };
    p.radius = radius;
    p.startAngle = startAngle;
    p.spanAngle = spanAngle;
    p.lineWidth = width;
    return p;
}

Primitive Primitive::label(const QPointF &at, const QString &text, double height, Align align)
{
    Primitive p;
    p.kind = Kind::Text;
    p.points = { at };
    p.text = text;
    p.textHeight = height;
    p.align = align;
    return p;
}

} // namespace dsn
