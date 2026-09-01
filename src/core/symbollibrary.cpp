#include "symbollibrary.h"

#include <algorithm>

namespace dsn {

void SymbolLibrary::insert(const SymbolDefinition &definition)
{
    if (definition.id.isEmpty())
        return;
    if (m_definitions.contains(definition.id))
        remove(definition.id);
    m_definitions.insert(definition.id, definition);
    if (!definition.logicalId.isEmpty()) {
        QStringList &ids = m_byLogicalId[definition.logicalId];
        if (!ids.contains(definition.id))
            ids.append(definition.id);
    }
}

void SymbolLibrary::remove(const QString &id)
{
    auto it = m_definitions.find(id);
    if (it == m_definitions.end())
        return;
    const QString logical = it->logicalId;
    m_definitions.erase(it);
    auto lit = m_byLogicalId.find(logical);
    if (lit != m_byLogicalId.end()) {
        lit->removeAll(id);
        if (lit->isEmpty())
            m_byLogicalId.erase(lit);
    }
}

void SymbolLibrary::clear()
{
    m_definitions.clear();
    m_byLogicalId.clear();
}

bool SymbolLibrary::contains(const QString &id) const { return m_definitions.contains(id); }

const SymbolDefinition *SymbolLibrary::definition(const QString &id) const
{
    auto it = m_definitions.constFind(id);
    return it == m_definitions.constEnd() ? nullptr : &it.value();
}

const SymbolDefinition *SymbolLibrary::resolve(const QString &logicalId, const QString &norm) const
{
    if (const SymbolDefinition *exact = definition(SymbolDefinition::makeId(norm, logicalId)))
        return exact;
    auto it = m_byLogicalId.constFind(logicalId);
    if (it == m_byLogicalId.constEnd() || it->isEmpty())
        return nullptr;
    return definition(it->first());
}

const SymbolDefinition *SymbolLibrary::counterpart(const QString &id, const QString &norm) const
{
    const SymbolDefinition *source = definition(id);
    if (!source)
        return nullptr;
    if (source->norm.compare(norm, Qt::CaseInsensitive) == 0)
        return source;
    return resolve(source->logicalId, norm);
}

QStringList SymbolLibrary::ids() const { return m_definitions.keys(); }

QStringList SymbolLibrary::categories(const QString &norm) const
{
    QStringList out;
    for (const SymbolDefinition &d : m_definitions) {
        if (!norm.isEmpty() && d.norm.compare(norm, Qt::CaseInsensitive) != 0)
            continue;
        if (!d.category.isEmpty() && !out.contains(d.category))
            out.append(d.category);
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QList<const SymbolDefinition *> SymbolLibrary::byCategory(const QString &category,
                                                          const QString &norm) const
{
    QList<const SymbolDefinition *> out;
    for (auto it = m_definitions.constBegin(); it != m_definitions.constEnd(); ++it) {
        if (it->category != category)
            continue;
        if (!norm.isEmpty() && it->norm.compare(norm, Qt::CaseInsensitive) != 0)
            continue;
        out.append(&it.value());
    }
    std::sort(out.begin(), out.end(),
              [](const SymbolDefinition *a, const SymbolDefinition *b) { return a->name < b->name; });
    return out;
}

QList<const SymbolDefinition *> SymbolLibrary::search(const QString &text,
                                                      const QString &norm) const
{
    QList<const SymbolDefinition *> out;
    const QString needle = text.trimmed();
    for (auto it = m_definitions.constBegin(); it != m_definitions.constEnd(); ++it) {
        if (!norm.isEmpty() && it->norm.compare(norm, Qt::CaseInsensitive) != 0)
            continue;
        if (needle.isEmpty()) {
            out.append(&it.value());
            continue;
        }
        const bool hit = it->name.contains(needle, Qt::CaseInsensitive)
                || it->logicalId.contains(needle, Qt::CaseInsensitive)
                || it->category.contains(needle, Qt::CaseInsensitive)
                || std::any_of(it->keywords.cbegin(), it->keywords.cend(),
                               [&](const QString &k) { return k.contains(needle, Qt::CaseInsensitive); });
        if (hit)
            out.append(&it.value());
    }
    std::sort(out.begin(), out.end(),
              [](const SymbolDefinition *a, const SymbolDefinition *b) { return a->name < b->name; });
    return out;
}

QList<const SymbolDefinition *> SymbolLibrary::all() const
{
    QList<const SymbolDefinition *> out;
    out.reserve(m_definitions.size());
    for (auto it = m_definitions.constBegin(); it != m_definitions.constEnd(); ++it)
        out.append(&it.value());
    return out;
}

void SymbolLibrary::merge(const SymbolLibrary &other, bool overwrite)
{
    for (auto it = other.m_definitions.constBegin(); it != other.m_definitions.constEnd(); ++it) {
        if (!overwrite && m_definitions.contains(it.key()))
            continue;
        insert(it.value());
    }
}

} // namespace dsn
