#include "entity.h"
#include "entities.h"

#include <QUuid>

namespace dsn {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

Entity::Entity() : m_id(newId()) {}

Entity::Entity(QString id) : m_id(std::move(id)) {}

Entity::~Entity() = default;

QJsonObject Entity::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = typeTag();
    o[QStringLiteral("id")] = m_id;
    if (m_locked)
        o[QStringLiteral("locked")] = true;
    if (!m_group.isEmpty())
        o[QStringLiteral("group")] = m_group;
    return o;
}

bool Entity::readJson(const QJsonObject &object)
{
    const QString id = object.value(QStringLiteral("id")).toString();
    m_id = id.isEmpty() ? newId() : id;
    m_locked = object.value(QStringLiteral("locked")).toBool(false);
    m_group = object.value(QStringLiteral("group")).toString();
    return true;
}

EntityPtr createEntity(const QString &typeTag)
{
    if (typeTag == QLatin1String("symbol"))
        return std::make_unique<SymbolInstance>();
    if (typeTag == QLatin1String("wire"))
        return std::make_unique<Wire>();
    if (typeTag == QLatin1String("junction"))
        return std::make_unique<Junction>();
    if (typeTag == QLatin1String("text"))
        return std::make_unique<TextItem>();
    if (typeTag == QLatin1String("graphic"))
        return std::make_unique<GraphicItem>();
    if (typeTag == QLatin1String("label"))
        return std::make_unique<Label>();
    return nullptr;
}

} // namespace dsn
