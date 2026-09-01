#include "project.h"
#include "entities.h"
#include "jsonutils.h"

#include <QJsonArray>

#include <algorithm>

namespace dsn {

QJsonObject ProjectInfo::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("reference")] = reference;
    o[QStringLiteral("client")] = client;
    o[QStringLiteral("author")] = author;
    o[QStringLiteral("revision")] = revision;
    if (date.isValid())
        o[QStringLiteral("date")] = date.toString(Qt::ISODate);
    if (!notes.isEmpty())
        o[QStringLiteral("notes")] = notes;
    if (!extra.isEmpty())
        o[QStringLiteral("extra")] = stringMapToJson(extra);
    return o;
}

ProjectInfo ProjectInfo::fromJson(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    ProjectInfo i;
    i.title = o.value(QStringLiteral("title")).toString();
    i.reference = o.value(QStringLiteral("reference")).toString();
    i.client = o.value(QStringLiteral("client")).toString();
    i.author = o.value(QStringLiteral("author")).toString();
    i.revision = o.value(QStringLiteral("revision")).toString(QStringLiteral("A"));
    const QString d = o.value(QStringLiteral("date")).toString();
    i.date = d.isEmpty() ? QDate::currentDate() : QDate::fromString(d, Qt::ISODate);
    i.notes = o.value(QStringLiteral("notes")).toString();
    i.extra = stringMapFromJson(o.value(QStringLiteral("extra")));
    return i;
}

Project::Project() = default;
Project::~Project() = default;

Project::Project(const Project &other)
    : info(other.info), profileId(other.profileId), wireTypes(other.wireTypes),
      library(other.library)
{
    m_folios.reserve(other.m_folios.size());
    for (const auto &f : other.m_folios)
        m_folios.push_back(std::make_unique<Folio>(*f));
}

Project &Project::operator=(const Project &other)
{
    if (this == &other)
        return *this;
    info = other.info;
    profileId = other.profileId;
    wireTypes = other.wireTypes;
    library = other.library;
    m_folios.clear();
    m_folios.reserve(other.m_folios.size());
    for (const auto &f : other.m_folios)
        m_folios.push_back(std::make_unique<Folio>(*f));
    return *this;
}

Folio *Project::folioAt(int index)
{
    if (index < 0 || index >= int(m_folios.size()))
        return nullptr;
    return m_folios[std::size_t(index)].get();
}

const Folio *Project::folioAt(int index) const
{
    return const_cast<Project *>(this)->folioAt(index);
}

Folio *Project::folio(const QString &id)
{
    auto it = std::find_if(m_folios.begin(), m_folios.end(),
                           [&](const std::unique_ptr<Folio> &f) { return f->id() == id; });
    return it == m_folios.end() ? nullptr : it->get();
}

const Folio *Project::folio(const QString &id) const
{
    return const_cast<Project *>(this)->folio(id);
}

int Project::indexOf(const QString &folioId) const
{
    for (std::size_t i = 0; i < m_folios.size(); ++i) {
        if (m_folios[i]->id() == folioId)
            return int(i);
    }
    return -1;
}

Folio *Project::addFolio(const QString &title)
{
    auto folio = std::make_unique<Folio>();
    folio->title = title;
    folio->number = QString::number(m_folios.size() + 1);
    Folio *raw = folio.get();
    m_folios.push_back(std::move(folio));
    return raw;
}

Folio *Project::insertFolio(int index, std::unique_ptr<Folio> folio)
{
    if (!folio)
        return nullptr;
    index = std::clamp(index, 0, int(m_folios.size()));
    Folio *raw = folio.get();
    m_folios.insert(m_folios.begin() + index, std::move(folio));
    return raw;
}

std::unique_ptr<Folio> Project::takeFolio(int index)
{
    if (index < 0 || index >= int(m_folios.size()))
        return nullptr;
    auto it = m_folios.begin() + index;
    std::unique_ptr<Folio> taken = std::move(*it);
    m_folios.erase(it);
    return taken;
}

bool Project::moveFolio(int from, int to)
{
    const int count = int(m_folios.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return false;
    auto moved = std::move(m_folios[std::size_t(from)]);
    m_folios.erase(m_folios.begin() + from);
    m_folios.insert(m_folios.begin() + to, std::move(moved));
    return true;
}

std::vector<Folio *> Project::folios()
{
    std::vector<Folio *> out;
    out.reserve(m_folios.size());
    for (const auto &f : m_folios)
        out.push_back(f.get());
    return out;
}

std::vector<const Folio *> Project::folios() const
{
    std::vector<const Folio *> out;
    out.reserve(m_folios.size());
    for (const auto &f : m_folios)
        out.push_back(f.get());
    return out;
}

Entity *Project::findEntity(const QString &entityId, Folio **owner)
{
    for (const auto &f : m_folios) {
        if (Entity *e = f->entity(entityId)) {
            if (owner)
                *owner = f.get();
            return e;
        }
    }
    if (owner)
        *owner = nullptr;
    return nullptr;
}

const Entity *Project::findEntity(const QString &entityId, const Folio **owner) const
{
    Folio *mutableOwner = nullptr;
    const Entity *e = const_cast<Project *>(this)->findEntity(entityId, &mutableOwner);
    if (owner)
        *owner = mutableOwner;
    return e;
}

void Project::renumberFolios()
{
    int next = 1;
    for (const auto &f : m_folios) {
        bool numeric = false;
        f->number.toInt(&numeric);
        // Un numero non numerique ("=A1+B2/3") a ete voulu : on ne l'ecrase pas.
        if (numeric || f->number.isEmpty())
            f->number = QString::number(next);
        ++next;
    }
}

int Project::resolveSymbolBounds()
{
    int resolved = 0;
    for (const auto &f : m_folios) {
        for (SymbolInstance *s : f->entitiesOfType<SymbolInstance>()) {
            if (const SymbolDefinition *def = library.definition(s->definitionId)) {
                s->setLocalBounds(def->bounds());
                ++resolved;
            }
        }
    }
    return resolved;
}

QStringList Project::missingDefinitions() const
{
    QStringList missing;
    for (const auto &f : m_folios) {
        for (const SymbolInstance *s : f->entitiesOfType<SymbolInstance>()) {
            if (!library.contains(s->definitionId) && !missing.contains(s->definitionId))
                missing.append(s->definitionId);
        }
    }
    missing.sort();
    return missing;
}

void Project::clear()
{
    m_folios.clear();
    library.clear();
    wireTypes = WireTypeSet::forNorm(profileId);
    info = ProjectInfo();
}

bool Project::isEmpty() const
{
    return std::all_of(m_folios.begin(), m_folios.end(),
                       [](const std::unique_ptr<Folio> &f) { return f->entityCount() == 0; });
}

QJsonObject Project::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("version")] = kFormatVersion;
    o[QStringLiteral("info")] = info.toJson();
    o[QStringLiteral("profile")] = profileId;
    o[QStringLiteral("wireTypes")] = wireTypes.toJson();

    QJsonArray folioArray;
    for (const auto &f : m_folios)
        folioArray.append(f->toJson());
    o[QStringLiteral("folios")] = folioArray;
    return o;
}

bool Project::readJson(const QJsonObject &object)
{
    const int version = object.value(QStringLiteral("version")).toInt(0);
    if (version > kFormatVersion)
        return false; // document ecrit par une version ulterieure : refus net

    info = ProjectInfo::fromJson(object.value(QStringLiteral("info")));
    profileId = object.value(QStringLiteral("profile")).toString(QStringLiteral("iec"));
    // Un document anterieur aux types de fils n'a pas la cle : on repart alors
    // du jeu de la norme, plutot que du seul type par defaut.
    if (object.contains(QStringLiteral("wireTypes")))
        wireTypes.readJson(object.value(QStringLiteral("wireTypes")));
    else
        wireTypes = WireTypeSet::forNorm(profileId);

    m_folios.clear();
    const QJsonArray folioArray = object.value(QStringLiteral("folios")).toArray();
    m_folios.reserve(folioArray.size());
    for (const QJsonValue &v : folioArray) {
        auto f = std::make_unique<Folio>();
        if (f->readJson(v.toObject()))
            m_folios.push_back(std::move(f));
    }
    return true;
}

} // namespace dsn
