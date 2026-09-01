#include "catalog.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

#include <algorithm>

namespace dsn {

QString CatalogItem::searchText() const
{
    return QStringList{ manufacturer, partNumber, description, rating, voltage, note }
            .join(QLatin1Char(' '));
}

QJsonObject CatalogItem::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("deviceKind")] = deviceKind;
    o[QStringLiteral("manufacturer")] = manufacturer;
    o[QStringLiteral("partNumber")] = partNumber;
    if (!description.isEmpty())
        o[QStringLiteral("description")] = description;
    if (!rating.isEmpty())
        o[QStringLiteral("rating")] = rating;
    if (!voltage.isEmpty())
        o[QStringLiteral("voltage")] = voltage;
    if (!note.isEmpty())
        o[QStringLiteral("note")] = note;
    return o;
}

CatalogItem CatalogItem::fromJson(const QJsonValue &value)
{
    const QJsonObject o = value.toObject();
    CatalogItem item;
    item.deviceKind = o.value(QStringLiteral("deviceKind")).toString();
    item.manufacturer = o.value(QStringLiteral("manufacturer")).toString();
    item.partNumber = o.value(QStringLiteral("partNumber")).toString();
    item.description = o.value(QStringLiteral("description")).toString();
    item.rating = o.value(QStringLiteral("rating")).toString();
    item.voltage = o.value(QStringLiteral("voltage")).toString();
    item.note = o.value(QStringLiteral("note")).toString();
    return item;
}

// --------------------------------------------------------------------------

void Catalog::insert(const CatalogItem &item)
{
    if (!item.isValid())
        return;
    // Un meme article charge deux fois — catalogue livre puis catalogue du
    // poste — ne doit pas apparaitre en double dans la liste de choix.
    for (CatalogItem &existing : m_items) {
        if (existing.partNumber == item.partNumber
            && existing.manufacturer == item.manufacturer) {
            existing = item;
            return;
        }
    }
    m_items.append(item);
}

QList<CatalogItem> Catalog::forDeviceKind(const QString &deviceKind) const
{
    if (deviceKind.isEmpty())
        return m_items;
    QList<CatalogItem> out;
    for (const CatalogItem &item : m_items) {
        if (item.deviceKind == deviceKind)
            out.append(item);
    }
    return out;
}

QList<CatalogItem> Catalog::search(const QString &text, const QString &deviceKind) const
{
    const QList<CatalogItem> pool = forDeviceKind(deviceKind);
    const QString needle = text.trimmed();
    if (needle.isEmpty())
        return pool;
    QList<CatalogItem> out;
    for (const CatalogItem &item : pool) {
        if (item.searchText().contains(needle, Qt::CaseInsensitive))
            out.append(item);
    }
    return out;
}

QStringList Catalog::deviceKinds() const
{
    QStringList kinds;
    for (const CatalogItem &item : m_items) {
        if (!item.deviceKind.isEmpty() && !kinds.contains(item.deviceKind))
            kinds.append(item.deviceKind);
    }
    kinds.sort();
    return kinds;
}

bool Catalog::readJson(const QByteArray &json, QString *error)
{
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError) {
        if (error)
            *error = parse.errorString();
        return false;
    }
    // Le fichier accepte les deux formes : un tableau nu, ou un objet portant
    // une cle « items ». Un catalogue exporte d'ailleurs a souvent la seconde.
    const QJsonArray array = document.isArray()
            ? document.array()
            : document.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : array)
        insert(CatalogItem::fromJson(value));
    return true;
}

QByteArray Catalog::toJson() const
{
    QJsonArray array;
    for (const CatalogItem &item : m_items)
        array.append(item.toJson());
    QJsonObject root;
    root[QStringLiteral("items")] = array;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Catalog Catalog::builtin()
{
    Catalog catalog;
    QFile file(QStringLiteral(":/catalog/catalogue.json"));
    if (file.open(QIODevice::ReadOnly))
        catalog.readJson(file.readAll());
    return catalog;
}

QStringList Catalog::userSearchPaths()
{
    QStringList paths;
    for (const QString &base :
         QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)) {
        paths.append(base + QStringLiteral("/catalogues"));
    }
    return paths;
}

Catalog Catalog::loadAll()
{
    Catalog catalog = builtin();
    for (const QString &path : userSearchPaths()) {
        QDir dir(path);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files,
                                                QDir::Name);
        for (const QString &name : files) {
            QFile file(dir.filePath(name));
            if (file.open(QIODevice::ReadOnly))
                catalog.readJson(file.readAll());
        }
    }
    return catalog;
}

} // namespace dsn
