// Helpers de serialisation partages. Les nombres sont arrondis au millieme de
// millimetre a l'ecriture : la precision utile est le centieme, et tronquer
// evite des fichiers pollues par des flottants a quinze decimales qui rendent
// tout diff illisible.
#pragma once

#include "geometry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QVector>

namespace dsn {

double roundStorage(double v);

QJsonArray pointToJson(const QPointF &p);
QPointF pointFromJson(const QJsonValue &v, const QPointF &fallback = QPointF());

QJsonArray pointsToJson(const QVector<QPointF> &points);
QVector<QPointF> pointsFromJson(const QJsonValue &v);

QJsonObject placementToJson(const Placement &p);
Placement placementFromJson(const QJsonValue &v);

QJsonObject stringMapToJson(const QMap<QString, QString> &map);
QMap<QString, QString> stringMapFromJson(const QJsonValue &v);

QJsonArray stringListToJson(const QStringList &list);
QStringList stringListFromJson(const QJsonValue &v);

} // namespace dsn
