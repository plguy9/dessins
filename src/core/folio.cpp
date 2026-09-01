#include "folio.h"
#include "entities.h"
#include "jsonutils.h"

#include <QJsonArray>

#include <algorithm>

namespace dsn {

QJsonObject SheetFrame::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("margin")] = roundStorage(margin);
    o[QStringLiteral("bindingMargin")] = roundStorage(bindingMargin);
    o[QStringLiteral("columns")] = columns;
    o[QStringLiteral("rows")] = rows;
    o[QStringLiteral("showZoneLabels")] = showZoneLabels;
    o[QStringLiteral("titleBlockWidth")] = roundStorage(titleBlockWidth);
    o[QStringLiteral("titleBlockHeight")] = roundStorage(titleBlockHeight);
    return o;
}

SheetFrame SheetFrame::fromJson(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    SheetFrame f;
    f.margin = o.value(QStringLiteral("margin")).toDouble(f.margin);
    f.bindingMargin = o.value(QStringLiteral("bindingMargin")).toDouble(f.bindingMargin);
    f.columns = std::max(1, o.value(QStringLiteral("columns")).toInt(f.columns));
    f.rows = std::max(1, o.value(QStringLiteral("rows")).toInt(f.rows));
    f.showZoneLabels = o.value(QStringLiteral("showZoneLabels")).toBool(f.showZoneLabels);
    f.titleBlockWidth = o.value(QStringLiteral("titleBlockWidth")).toDouble(f.titleBlockWidth);
    f.titleBlockHeight = o.value(QStringLiteral("titleBlockHeight")).toDouble(f.titleBlockHeight);
    return f;
}

Folio::Folio() : m_id(newId()) {}

Folio::Folio(QString id) : m_id(std::move(id)) {}

Folio::~Folio() = default;

Folio::Folio(const Folio &other)
    : number(other.number), title(other.title), sheet(other.sheet), frame(other.frame),
      titleBlock(other.titleBlock), m_id(other.m_id)
{
    m_entities.reserve(other.m_entities.size());
    for (const EntityPtr &e : other.m_entities)
        m_entities.push_back(e->clone());
}

Folio &Folio::operator=(const Folio &other)
{
    if (this == &other)
        return *this;
    m_id = other.m_id;
    number = other.number;
    title = other.title;
    sheet = other.sheet;
    frame = other.frame;
    titleBlock = other.titleBlock;
    m_entities.clear();
    m_entities.reserve(other.m_entities.size());
    for (const EntityPtr &e : other.m_entities)
        m_entities.push_back(e->clone());
    return *this;
}

Entity *Folio::addEntity(EntityPtr entity)
{
    if (!entity)
        return nullptr;
    Entity *raw = entity.get();
    m_entities.push_back(std::move(entity));
    return raw;
}

EntityPtr Folio::takeEntity(const QString &entityId)
{
    auto it = std::find_if(m_entities.begin(), m_entities.end(),
                           [&](const EntityPtr &e) { return e->id() == entityId; });
    if (it == m_entities.end())
        return nullptr;
    EntityPtr taken = std::move(*it);
    m_entities.erase(it);
    return taken;
}

bool Folio::removeEntity(const QString &entityId) { return takeEntity(entityId) != nullptr; }

int Folio::indexOfEntity(const QString &entityId) const
{
    for (std::size_t i = 0; i < m_entities.size(); ++i) {
        if (m_entities[i]->id() == entityId)
            return int(i);
    }
    return -1;
}

bool Folio::replaceEntity(EntityPtr entity)
{
    if (!entity)
        return false;
    const int index = indexOfEntity(entity->id());
    if (index < 0)
        return false;
    m_entities[std::size_t(index)] = std::move(entity);
    return true;
}

Entity *Folio::insertEntity(int index, EntityPtr entity)
{
    if (!entity)
        return nullptr;
    index = std::clamp(index, 0, int(m_entities.size()));
    Entity *raw = entity.get();
    m_entities.insert(m_entities.begin() + index, std::move(entity));
    return raw;
}

Entity *Folio::entity(const QString &entityId)
{
    auto it = std::find_if(m_entities.begin(), m_entities.end(),
                           [&](const EntityPtr &e) { return e->id() == entityId; });
    return it == m_entities.end() ? nullptr : it->get();
}

const Entity *Folio::entity(const QString &entityId) const
{
    return const_cast<Folio *>(this)->entity(entityId);
}

void Folio::clearEntities() { m_entities.clear(); }

QRectF Folio::sheetRect() const { return QRectF(0, 0, sheet.width, sheet.height); }

QRectF Folio::frameRect() const
{
    return QRectF(frame.bindingMargin, frame.margin,
                  sheet.width - frame.bindingMargin - frame.margin,
                  sheet.height - 2 * frame.margin);
}

QRectF Folio::titleBlockRect() const
{
    const QRectF fr = frameRect();
    const double w = std::min(frame.titleBlockWidth, fr.width());
    const double h = std::min(frame.titleBlockHeight, fr.height());
    return QRectF(fr.right() - w, fr.bottom() - h, w, h);
}

QRectF Folio::zoneRect(int column, int row) const
{
    const QRectF fr = frameRect();
    const double cw = fr.width() / frame.columns;
    const double rh = fr.height() / frame.rows;
    return QRectF(fr.left() + (column - 1) * cw, fr.top() + (row - 1) * rh, cw, rh);
}

int Folio::columnAt(const QPointF &p) const
{
    const QRectF fr = frameRect();
    if (p.x() < fr.left() || p.x() > fr.right())
        return -1;
    const double cw = fr.width() / frame.columns;
    const int c = int((p.x() - fr.left()) / cw) + 1;
    return std::clamp(c, 1, frame.columns);
}

QString Folio::zoneAt(const QPointF &p) const
{
    const QRectF fr = frameRect();
    if (!fr.contains(p))
        return QString();
    const int c = columnAt(p);
    const double rh = fr.height() / frame.rows;
    const int r = std::clamp(int((p.y() - fr.top()) / rh) + 1, 1, frame.rows);
    // Lignes lettrees A, B, C... comme sur un plan cote.
    return QString(QChar(char16_t(u'A' + r - 1))) + QString::number(c);
}

QRectF Folio::contentBounds() const
{
    QRectF bounds;
    for (const EntityPtr &e : m_entities) {
        const QRectF b = e->boundingBox();
        if (b.isNull())
            continue;
        bounds = bounds.isNull() ? b : bounds.united(b);
    }
    return bounds;
}

QJsonObject Folio::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = m_id;
    o[QStringLiteral("number")] = number;
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("sheet")] = sheet.id;
    // La taille est ecrite en plus de l'identifiant : un format retire d'une
    // version ulterieure reste alors relisible tel qu'il a ete dessine.
    o[QStringLiteral("sheetWidth")] = roundStorage(sheet.width);
    o[QStringLiteral("sheetHeight")] = roundStorage(sheet.height);
    o[QStringLiteral("frame")] = frame.toJson();
    if (!titleBlock.isEmpty())
        o[QStringLiteral("titleBlock")] = stringMapToJson(titleBlock);

    QJsonArray items;
    for (const EntityPtr &e : m_entities)
        items.append(e->toJson());
    o[QStringLiteral("entities")] = items;
    return o;
}

bool Folio::readJson(const QJsonObject &object)
{
    const QString id = object.value(QStringLiteral("id")).toString();
    m_id = id.isEmpty() ? newId() : id;
    number = object.value(QStringLiteral("number")).toString();
    title = object.value(QStringLiteral("title")).toString();

    sheet = sheetFormatById(object.value(QStringLiteral("sheet")).toString());
    const double w = object.value(QStringLiteral("sheetWidth")).toDouble(0.0);
    const double h = object.value(QStringLiteral("sheetHeight")).toDouble(0.0);
    if (w > 1.0 && h > 1.0) {
        sheet.width = w;
        sheet.height = h;
    }

    frame = SheetFrame::fromJson(object.value(QStringLiteral("frame")));
    titleBlock = stringMapFromJson(object.value(QStringLiteral("titleBlock")));

    m_entities.clear();
    const QJsonArray items = object.value(QStringLiteral("entities")).toArray();
    m_entities.reserve(items.size());
    for (const QJsonValue &v : items) {
        const QJsonObject eo = v.toObject();
        EntityPtr e = createEntity(eo.value(QStringLiteral("type")).toString());
        if (!e)
            continue; // entite d'une version plus recente : ignoree, pas fatale
        if (e->readJson(eo))
            m_entities.push_back(std::move(e));
    }
    return true;
}

} // namespace dsn
