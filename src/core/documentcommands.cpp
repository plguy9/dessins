#include "documentcommands.h"
#include "entities.h"

namespace dsn {

// --------------------------------------------------------------------------
// AddEntityCommand

AddEntityCommand::AddEntityCommand(Project &project, QString folioId, EntityPtr entity,
                                   QString text)
    : m_project(project), m_folioId(std::move(folioId)), m_stored(std::move(entity)),
      m_text(std::move(text))
{
    if (m_stored)
        m_entityId = m_stored->id();
    if (m_text.isEmpty())
        m_text = QStringLiteral("Ajouter un element");
}

void AddEntityCommand::redo()
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio || !m_stored)
        return;
    if (auto *symbol = dynamic_cast<SymbolInstance *>(m_stored.get())) {
        if (const SymbolDefinition *def = m_project.library.definition(symbol->definitionId))
            symbol->setLocalBounds(def->bounds());
    }
    folio->addEntity(std::move(m_stored));
}

void AddEntityCommand::undo()
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio)
        return;
    m_stored = folio->takeEntity(m_entityId);
}

// --------------------------------------------------------------------------
// RemoveEntityCommand

RemoveEntityCommand::RemoveEntityCommand(Project &project, QString folioId, QString entityId,
                                         QString text)
    : m_project(project), m_folioId(std::move(folioId)), m_entityId(std::move(entityId)),
      m_text(std::move(text))
{
    if (m_text.isEmpty())
        m_text = QStringLiteral("Supprimer un element");
}

void RemoveEntityCommand::redo()
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio)
        return;
    m_index = folio->indexOfEntity(m_entityId);
    m_stored = folio->takeEntity(m_entityId);
}

void RemoveEntityCommand::undo()
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio || !m_stored)
        return;
    folio->insertEntity(m_index, std::move(m_stored));
}

// --------------------------------------------------------------------------
// ModifyEntityCommand

ModifyEntityCommand::ModifyEntityCommand(Project &project, QString folioId, EntityPtr before,
                                         EntityPtr after, QString text)
    : m_project(project), m_folioId(std::move(folioId)), m_before(std::move(before)),
      m_after(std::move(after)), m_text(std::move(text))
{
    if (m_text.isEmpty())
        m_text = QStringLiteral("Modifier un element");
}

void ModifyEntityCommand::redo()
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio || !m_after)
        return;
    folio->replaceEntity(m_after->clone());
}

void ModifyEntityCommand::undo()
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio || !m_before)
        return;
    folio->replaceEntity(m_before->clone());
}

bool ModifyEntityCommand::mergeWith(const Command &other)
{
    const auto *o = dynamic_cast<const ModifyEntityCommand *>(&other);
    if (!o || m_mergeId == NoMerge || o->m_mergeId != m_mergeId)
        return false;
    if (!m_after || !o->m_after || m_after->id() != o->m_after->id())
        return false;
    // On garde l'etat initial et on adopte le dernier etat : la suite de
    // frappes dans un champ ne laisse qu'une entree annulable.
    m_after = o->m_after->clone();
    return true;
}

// --------------------------------------------------------------------------
// MoveEntitiesCommand

MoveEntitiesCommand::MoveEntitiesCommand(Project &project, QString folioId, QStringList entityIds,
                                         QPointF delta)
    : m_project(project), m_folioId(std::move(folioId)), m_entityIds(std::move(entityIds)),
      m_delta(delta)
{
}

void MoveEntitiesCommand::redo()
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio)
        return;
    for (const QString &id : m_entityIds) {
        if (Entity *e = folio->entity(id))
            e->translate(m_delta);
    }
}

void MoveEntitiesCommand::undo()
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio)
        return;
    for (const QString &id : m_entityIds) {
        if (Entity *e = folio->entity(id))
            e->translate(-m_delta);
    }
}

QString MoveEntitiesCommand::text() const
{
    return m_entityIds.size() == 1 ? QStringLiteral("Deplacer un element")
                                   : QStringLiteral("Deplacer %1 elements")
                                             .arg(m_entityIds.size());
}

bool MoveEntitiesCommand::mergeWith(const Command &other)
{
    const auto *o = dynamic_cast<const MoveEntitiesCommand *>(&other);
    if (!o || o->m_folioId != m_folioId || o->m_entityIds != m_entityIds)
        return false;
    m_delta += o->m_delta;
    return true;
}

// --------------------------------------------------------------------------
// Folios

AddFolioCommand::AddFolioCommand(Project &project, std::unique_ptr<Folio> folio, int index)
    : m_project(project), m_stored(std::move(folio)), m_index(index)
{
    if (m_stored)
        m_folioId = m_stored->id();
}

void AddFolioCommand::redo()
{
    if (!m_stored)
        return;
    if (m_index < 0)
        m_index = m_project.folioCount();
    m_project.insertFolio(m_index, std::move(m_stored));
}

void AddFolioCommand::undo()
{
    const int index = m_project.indexOf(m_folioId);
    if (index < 0)
        return;
    m_index = index;
    m_stored = m_project.takeFolio(index);
}

QString AddFolioCommand::text() const { return QStringLiteral("Ajouter un folio"); }

RemoveFolioCommand::RemoveFolioCommand(Project &project, QString folioId)
    : m_project(project), m_folioId(std::move(folioId))
{
}

void RemoveFolioCommand::redo()
{
    m_index = m_project.indexOf(m_folioId);
    if (m_index < 0)
        return;
    m_stored = m_project.takeFolio(m_index);
}

void RemoveFolioCommand::undo()
{
    if (!m_stored)
        return;
    m_project.insertFolio(m_index, std::move(m_stored));
}

QString RemoveFolioCommand::text() const { return QStringLiteral("Supprimer un folio"); }

MoveFolioCommand::MoveFolioCommand(Project &project, int from, int to)
    : m_project(project), m_from(from), m_to(to)
{
}

void MoveFolioCommand::redo() { m_project.moveFolio(m_from, m_to); }

void MoveFolioCommand::undo() { m_project.moveFolio(m_to, m_from); }

QString MoveFolioCommand::text() const { return QStringLiteral("Deplacer un folio"); }

ChangeProjectInfoCommand::ChangeProjectInfoCommand(Project &project, ProjectInfo after)
    : m_project(project), m_before(project.info), m_after(std::move(after))
{
}

void ChangeProjectInfoCommand::redo() { m_project.info = m_after; }

void ChangeProjectInfoCommand::undo() { m_project.info = m_before; }

QString ChangeProjectInfoCommand::text() const
{
    return QStringLiteral("Modifier les informations du projet");
}

} // namespace dsn
