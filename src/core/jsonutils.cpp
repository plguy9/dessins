#include "jsonutils.h"

namespace dsn {

double roundStorage(double v) { return std::round(v * 1000.0) / 1000.0; }

QJsonArray pointToJson(const QPointF &p)
{
    return QJsonArray{ roundStorage(p.x()), roundStorage(p.y()) };
}

QPointF pointFromJson(const QJsonValue &v, const QPointF &fallback)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 2)
        return fallback;
    return QPointF(a.at(0).toDouble(fallback.x()), a.at(1).toDouble(fallback.y()));
}

QJsonArray pointsToJson(const QVector<QPointF> &points)
{
    QJsonArray a;
    for (const QPointF &p : points)
        a.append(pointToJson(p));
    return a;
}

QVector<QPointF> pointsFromJson(const QJsonValue &v)
{
    QVector<QPointF> out;
    const QJsonArray a = v.toArray();
    out.reserve(a.size());
    for (const QJsonValue &item : a)
        out.append(pointFromJson(item));
    return out;
}

QJsonObject placementToJson(const Placement &p)
{
    QJsonObject o;
    o[QStringLiteral("at")] = pointToJson(p.position);
    if (p.orientation != Orientation::R0)
        o[QStringLiteral("rot")] = toDegrees(p.orientation);
    if (p.mirrored)
        o[QStringLiteral("mirror")] = true;
    // L'echelle n'est ecrite que si elle differe de un : un dossier qui
    // n'utilise pas l'homothetie doit produire exactement le meme fichier
    // qu'avant qu'elle existe.
    if (!fuzzyEqual(p.scale, 1.0))
        o[QStringLiteral("scale")] = p.scale;
    return o;
}

Placement placementFromJson(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    Placement p;
    p.position = pointFromJson(o.value(QStringLiteral("at")));
    p.orientation = orientationFromDegrees(o.value(QStringLiteral("rot")).toInt(0));
    p.mirrored = o.value(QStringLiteral("mirror")).toBool(false);
    p.scale = o.value(QStringLiteral("scale")).toDouble(1.0);
    // Une echelle nulle ou negative viderait le symbole sans rien dire : on
    // retombe sur un, comme un identifiant de type de fil inconnu retombe sur
    // le type par defaut.
    if (p.scale <= kEpsilon)
        p.scale = 1.0;
    return p;
}

QJsonObject stringMapToJson(const QMap<QString, QString> &map)
{
    QJsonObject o;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!it.value().isEmpty())
            o[it.key()] = it.value();
    }
    return o;
}

QMap<QString, QString> stringMapFromJson(const QJsonValue &v)
{
    QMap<QString, QString> map;
    const QJsonObject o = v.toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        map.insert(it.key(), it.value().toString());
    return map;
}

QJsonArray stringListToJson(const QStringList &list)
{
    QJsonArray a;
    for (const QString &s : list)
        a.append(s);
    return a;
}

QStringList stringListFromJson(const QJsonValue &v)
{
    QStringList list;
    const QJsonArray a = v.toArray();
    for (const QJsonValue &item : a)
        list.append(item.toString());
    return list;
}

} // namespace dsn
