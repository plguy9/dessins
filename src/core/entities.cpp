#include "entities.h"
#include "jsonutils.h"

namespace dsn {

namespace {

QRectF boundsOf(const QVector<QPointF> &points, double margin = 0.0)
{
    if (points.isEmpty())
        return QRectF();
    QRectF r(points.first(), points.first());
    for (const QPointF &p : points) {
        r.setLeft(std::min(r.left(), p.x()));
        r.setTop(std::min(r.top(), p.y()));
        r.setRight(std::max(r.right(), p.x()));
        r.setBottom(std::max(r.bottom(), p.y()));
    }
    return margin > 0.0 ? r.adjusted(-margin, -margin, margin, margin) : r;
}

void translatePoints(QVector<QPointF> &points, const QPointF &delta)
{
    for (QPointF &p : points)
        p += delta;
}

} // namespace

// --------------------------------------------------------------------------
// SymbolInstance

EntityPtr SymbolInstance::clone() const { return std::make_unique<SymbolInstance>(*this); }

bool SymbolInstance::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const SymbolInstance *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF SymbolInstance::boundingBox() const { return placement.mapRect(m_localBounds); }

void SymbolInstance::translate(const QPointF &delta) { placement.position += delta; }

QJsonObject SymbolInstance::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("def")] = definitionId;
    o[QStringLiteral("placement")] = placementToJson(placement);
    if (!fields.isEmpty())
        o[QStringLiteral("fields")] = stringMapToJson(fields);
    if (!deviceGroup.isEmpty()) {
        o[QStringLiteral("group")] = deviceGroup;
        o[QStringLiteral("block")] = blockIndex;
    }
    if (designationLocked)
        o[QStringLiteral("designationLocked")] = true;
    return o;
}

bool SymbolInstance::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    definitionId = object.value(QStringLiteral("def")).toString();
    placement = placementFromJson(object.value(QStringLiteral("placement")));
    fields = stringMapFromJson(object.value(QStringLiteral("fields")));
    deviceGroup = object.value(QStringLiteral("group")).toString();
    blockIndex = object.value(QStringLiteral("block")).toInt(0);
    designationLocked = object.value(QStringLiteral("designationLocked")).toBool(false);
    return !definitionId.isEmpty();
}

// --------------------------------------------------------------------------
// Wire

EntityPtr Wire::clone() const { return std::make_unique<Wire>(*this); }

bool Wire::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const Wire *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF Wire::boundingBox() const { return boundsOf(points, 0.5); }

void Wire::translate(const QPointF &delta) { translatePoints(points, delta); }

QString Wire::conductorName(int index) const
{
    if (conductors.isEmpty())
        return QString();
    if (index < 0 || index >= conductors.size())
        return QString();
    return conductors.at(index);
}

double Wire::length() const
{
    double total = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        const QPointF d = points.at(i) - points.at(i - 1);
        total += std::hypot(d.x(), d.y());
    }
    return total;
}

bool Wire::isDegenerate() const { return points.size() < 2 || length() <= kConnectTolerance; }

QJsonObject Wire::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("points")] = pointsToJson(points);
    if (!conductors.isEmpty())
        o[QStringLiteral("conductors")] = stringListToJson(conductors);
    if (!number.isEmpty())
        o[QStringLiteral("number")] = number;
    if (numberLocked)
        o[QStringLiteral("numberLocked")] = true;
    if (!style.isEmpty())
        o[QStringLiteral("style")] = style;
    if (!wireType.isEmpty())
        o[QStringLiteral("wireType")] = wireType;
    return o;
}

bool Wire::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    points = pointsFromJson(object.value(QStringLiteral("points")));
    conductors = stringListFromJson(object.value(QStringLiteral("conductors")));
    number = object.value(QStringLiteral("number")).toString();
    numberLocked = object.value(QStringLiteral("numberLocked")).toBool(false);
    style = object.value(QStringLiteral("style")).toString();
    wireType = object.value(QStringLiteral("wireType")).toString();
    return points.size() >= 2;
}

// --------------------------------------------------------------------------
// Junction

EntityPtr Junction::clone() const { return std::make_unique<Junction>(*this); }

bool Junction::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const Junction *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF Junction::boundingBox() const
{
    const double r = diameter / 2.0;
    return QRectF(point.x() - r, point.y() - r, diameter, diameter);
}

void Junction::translate(const QPointF &delta) { point += delta; }

QJsonObject Junction::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("at")] = pointToJson(point);
    if (!fuzzyEqual(diameter, 1.2))
        o[QStringLiteral("diameter")] = roundStorage(diameter);
    return o;
}

bool Junction::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    point = pointFromJson(object.value(QStringLiteral("at")));
    diameter = object.value(QStringLiteral("diameter")).toDouble(1.2);
    return true;
}

// --------------------------------------------------------------------------
// TextItem

EntityPtr TextItem::clone() const { return std::make_unique<TextItem>(*this); }

bool TextItem::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const TextItem *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF TextItem::boundingBox() const
{
    // Estimation sans metrique de police : le coeur ne charge pas de police.
    // La couche d'affichage recalcule la boite exacte quand elle en a une.
    const double w = height * 0.62 * std::max(1, int(text.size()));
    QRectF local(0, -height, w, height * 1.35);
    switch (align) {
    case Align::Center: local.moveLeft(-w / 2.0); break;
    case Align::Right: local.moveLeft(-w); break;
    case Align::Left: break;
    }
    return placement.mapRect(local);
}

void TextItem::translate(const QPointF &delta) { placement.position += delta; }

QJsonObject TextItem::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("placement")] = placementToJson(placement);
    o[QStringLiteral("height")] = roundStorage(height);
    if (align != Align::Left)
        o[QStringLiteral("align")] = align == Align::Center ? QStringLiteral("center")
                                                            : QStringLiteral("right");
    return o;
}

bool TextItem::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    text = object.value(QStringLiteral("text")).toString();
    placement = placementFromJson(object.value(QStringLiteral("placement")));
    height = object.value(QStringLiteral("height")).toDouble(2.5);
    const QString a = object.value(QStringLiteral("align")).toString();
    align = a == QLatin1String("center") ? Align::Center
          : a == QLatin1String("right")  ? Align::Right
                                         : Align::Left;
    return true;
}

// --------------------------------------------------------------------------
// GraphicItem

EntityPtr GraphicItem::clone() const { return std::make_unique<GraphicItem>(*this); }

bool GraphicItem::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const GraphicItem *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF GraphicItem::boundingBox() const { return shape.bounds(); }

void GraphicItem::translate(const QPointF &delta) { shape.translate(delta); }

QJsonObject GraphicItem::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("shape")] = shape.toJson();
    return o;
}

bool GraphicItem::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    shape = Primitive::fromJson(object.value(QStringLiteral("shape")));
    return true;
}

// --------------------------------------------------------------------------
// Label

EntityPtr Label::clone() const { return std::make_unique<Label>(*this); }

bool Label::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const Label *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF Label::boundingBox() const
{
    const double w = height * 0.62 * std::max(1, int(name.size())) + height;
    return QRectF(point.x() - w / 2.0, point.y() - height, w, height * 2.0);
}

void Label::translate(const QPointF &delta) { point += delta; }

QJsonObject Label::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("at")] = pointToJson(point);
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("dir")] = toDegrees(direction);
    o[QStringLiteral("scope")] = scope == Scope::Project ? QStringLiteral("project")
                                                         : QStringLiteral("folio");
    o[QStringLiteral("height")] = roundStorage(height);
    return o;
}

bool Label::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    point = pointFromJson(object.value(QStringLiteral("at")));
    name = object.value(QStringLiteral("name")).toString();
    direction = directionFromDegrees(object.value(QStringLiteral("dir")).toInt(0));
    scope = object.value(QStringLiteral("scope")).toString() == QLatin1String("project")
            ? Scope::Project
            : Scope::Folio;
    height = object.value(QStringLiteral("height")).toDouble(2.0);
    return !name.isEmpty();
}

} // namespace dsn
