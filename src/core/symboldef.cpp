#include "symboldef.h"
#include "jsonutils.h"

#include <QJsonArray>

namespace dsn {

QString pinTypeTag(PinType t)
{
    switch (t) {
    case PinType::Passive: return QStringLiteral("passive");
    case PinType::Input: return QStringLiteral("input");
    case PinType::Output: return QStringLiteral("output");
    case PinType::Bidirectional: return QStringLiteral("bidir");
    case PinType::Power: return QStringLiteral("power");
    case PinType::Ground: return QStringLiteral("ground");
    case PinType::Terminal: return QStringLiteral("terminal");
    case PinType::NotConnected: return QStringLiteral("nc");
    }
    return QStringLiteral("passive");
}

PinType pinTypeFromTag(const QString &tag)
{
    if (tag == QLatin1String("input")) return PinType::Input;
    if (tag == QLatin1String("output")) return PinType::Output;
    if (tag == QLatin1String("bidir")) return PinType::Bidirectional;
    if (tag == QLatin1String("power")) return PinType::Power;
    if (tag == QLatin1String("ground")) return PinType::Ground;
    if (tag == QLatin1String("terminal")) return PinType::Terminal;
    if (tag == QLatin1String("nc")) return PinType::NotConnected;
    return PinType::Passive;
}

QJsonObject Pin::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("number")] = number;
    if (!name.isEmpty())
        o[QStringLiteral("name")] = name;
    o[QStringLiteral("at")] = pointToJson(position);
    o[QStringLiteral("dir")] = toDegrees(direction);
    o[QStringLiteral("length")] = roundStorage(length);
    if (type != PinType::Passive)
        o[QStringLiteral("type")] = pinTypeTag(type);
    if (showName)
        o[QStringLiteral("showName")] = true;
    if (!showNumber)
        o[QStringLiteral("showNumber")] = false;
    return o;
}

Pin Pin::fromJson(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    Pin p;
    p.number = o.value(QStringLiteral("number")).toString();
    p.name = o.value(QStringLiteral("name")).toString();
    p.position = pointFromJson(o.value(QStringLiteral("at")));
    p.direction = directionFromDegrees(o.value(QStringLiteral("dir")).toInt(0));
    p.length = o.value(QStringLiteral("length")).toDouble(2.5);
    p.type = pinTypeFromTag(o.value(QStringLiteral("type")).toString());
    p.showName = o.value(QStringLiteral("showName")).toBool(false);
    p.showNumber = o.value(QStringLiteral("showNumber")).toBool(true);
    return p;
}

QString SymbolDefinition::makeId(const QString &norm, const QString &logicalId)
{
    return norm.toLower() + QLatin1Char(':') + logicalId;
}

QRectF SymbolDefinition::bodyBounds() const
{
    QRectF r;
    for (const Primitive &p : graphics) {
        const QRectF b = p.bounds();
        if (b.isNull())
            continue;
        r = r.isNull() ? b : r.united(b);
    }
    return r;
}

QRectF SymbolDefinition::bounds() const
{
    QRectF r = bodyBounds();
    for (const Pin &p : pins) {
        const QRectF pb = normalized(p.root(), p.position);
        r = r.isNull() ? pb : r.united(pb);
    }
    if (r.isNull())
        r = QRectF(-2.5, -2.5, 5.0, 5.0);
    return r;
}

const Pin *SymbolDefinition::pin(const QString &number) const
{
    for (const Pin &p : pins) {
        if (p.number == number)
            return &p;
    }
    return nullptr;
}

QJsonObject SymbolDefinition::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("logicalId")] = logicalId;
    o[QStringLiteral("norm")] = norm;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("category")] = category;
    if (!keywords.isEmpty())
        o[QStringLiteral("keywords")] = stringListToJson(keywords);
    if (!designationPrefix.isEmpty())
        o[QStringLiteral("prefix")] = designationPrefix;
    if (!deviceKind.isEmpty())
        o[QStringLiteral("deviceKind")] = deviceKind;

    QJsonArray g;
    for (const Primitive &p : graphics)
        g.append(p.toJson());
    o[QStringLiteral("graphics")] = g;

    QJsonArray pinArray;
    for (const Pin &p : pins)
        pinArray.append(p.toJson());
    o[QStringLiteral("pins")] = pinArray;

    if (!defaultFields.isEmpty())
        o[QStringLiteral("defaultFields")] = stringMapToJson(defaultFields);
    o[QStringLiteral("designationAnchor")] = pointToJson(designationAnchor);
    o[QStringLiteral("valueAnchor")] = pointToJson(valueAnchor);
    return o;
}

SymbolDefinition SymbolDefinition::fromJson(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    SymbolDefinition d;
    d.logicalId = o.value(QStringLiteral("logicalId")).toString();
    d.norm = o.value(QStringLiteral("norm")).toString(QStringLiteral("IEC"));
    d.id = o.value(QStringLiteral("id")).toString();
    if (d.id.isEmpty() && !d.logicalId.isEmpty())
        d.id = makeId(d.norm, d.logicalId);
    d.name = o.value(QStringLiteral("name")).toString();
    d.category = o.value(QStringLiteral("category")).toString();
    d.keywords = stringListFromJson(o.value(QStringLiteral("keywords")));
    d.designationPrefix = o.value(QStringLiteral("prefix")).toString();
    d.deviceKind = o.value(QStringLiteral("deviceKind")).toString();

    const QJsonArray g = o.value(QStringLiteral("graphics")).toArray();
    d.graphics.reserve(g.size());
    for (const QJsonValue &item : g)
        d.graphics.append(Primitive::fromJson(item));

    const QJsonArray pins = o.value(QStringLiteral("pins")).toArray();
    d.pins.reserve(pins.size());
    for (const QJsonValue &item : pins)
        d.pins.append(Pin::fromJson(item));

    d.defaultFields = stringMapFromJson(o.value(QStringLiteral("defaultFields")));
    d.designationAnchor = pointFromJson(o.value(QStringLiteral("designationAnchor")),
                                        QPointF(0.0, -6.0));
    d.valueAnchor = pointFromJson(o.value(QStringLiteral("valueAnchor")), QPointF(0.0, 6.0));
    return d;
}

} // namespace dsn
