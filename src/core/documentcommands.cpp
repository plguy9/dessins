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

void ModifyEntityCommand::apply(const EntityPtr &state)
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio || !state)
        return;
    // L'etat est recopie dans l'entite existante plutot que de la remplacer :
    // les vues et les panneaux gardent des pointeurs vers elle, et les
    // invalider provoquerait un plantage a la modification suivante.
    if (Entity *target = folio->entity(state->id()))
        target->assign(*state);
    else
        folio->addEntity(state->clone());
}

void ModifyEntityCommand::redo() { apply(m_after); }

void ModifyEntityCommand::undo() { apply(m_before); }

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

ChangeFolioLayoutCommand::ChangeFolioLayoutCommand(Project &project, QString folioId,
                                                   SheetFormat sheet, SheetFrame frame)
    : m_project(project), m_folioId(std::move(folioId)), m_afterSheet(std::move(sheet)),
      m_afterFrame(frame)
{
    if (const Folio *folio = m_project.folio(m_folioId)) {
        m_beforeSheet = folio->sheet;
        m_beforeFrame = folio->frame;
    }
}

void ChangeFolioLayoutCommand::apply(const SheetFormat &sheet, const SheetFrame &frame)
{
    if (Folio *folio = m_project.folio(m_folioId)) {
        folio->sheet = sheet;
        folio->frame = frame;
    }
}

void ChangeFolioLayoutCommand::redo() { apply(m_afterSheet, m_afterFrame); }

void ChangeFolioLayoutCommand::undo() { apply(m_beforeSheet, m_beforeFrame); }

QString ChangeFolioLayoutCommand::text() const { return QStringLiteral("Mise en page"); }

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

// --------------------------------------------------------------------------
// StretchEntitiesCommand

namespace {

// Sommets d'une entite qu'une fenetre de capture peut saisir un par un. Une
// liste vide rend l'entite indivisible : elle se deplacera en bloc si son
// point d'ancrage est pris.
QVector<QPointF> stretchablePoints(const Entity &entity)
{
    switch (entity.type()) {
    case EntityType::Wire:
        return QVector<QPointF>(static_cast<const Wire &>(entity).points);
    case EntityType::Graphic:
        return static_cast<const GraphicItem &>(entity).shape.points;
    default:
        return {};
    }
}

QPointF anchorOf(const Entity &entity)
{
    switch (entity.type()) {
    case EntityType::Symbol:
        return static_cast<const SymbolInstance &>(entity).placement.position;
    case EntityType::Junction:
        return static_cast<const Junction &>(entity).point;
    case EntityType::Text:
        return static_cast<const TextItem &>(entity).placement.position;
    case EntityType::Label:
        return static_cast<const Label &>(entity).point;
    default:
        return entity.boundingBox().center();
    }
}

void moveVertex(Entity &entity, int index, const QPointF &delta)
{
    if (auto *wire = dynamic_cast<Wire *>(&entity)) {
        if (index >= 0 && index < wire->points.size())
            wire->points[index] += delta;
        return;
    }
    if (auto *graphic = dynamic_cast<GraphicItem *>(&entity)) {
        if (index >= 0 && index < graphic->shape.points.size())
            graphic->shape.points[index] += delta;
    }
}

} // namespace

StretchEntitiesCommand::StretchEntitiesCommand(Project &project, QString folioId,
                                               const QRectF &windowMm, QPointF delta)
    : m_project(project), m_folioId(std::move(folioId)), m_delta(delta)
{
    const Folio *folio = m_project.folio(m_folioId);
    if (!folio)
        return;
    const QRectF window = windowMm.normalized();

    for (const EntityPtr &entity : folio->entities()) {
        const QVector<QPointF> points = stretchablePoints(*entity);
        if (points.isEmpty()) {
            // Entite indivisible : elle suit si son point d'ancrage est pris.
            if (window.contains(anchorOf(*entity)))
                m_targets.append({ entity->id(), {} });
            continue;
        }

        QVector<int> taken;
        for (int i = 0; i < points.size(); ++i) {
            if (window.contains(points.at(i)))
                taken.append(i);
        }
        if (taken.isEmpty())
            continue;
        // Tous les sommets pris : l'entite se deplace en bloc, comme le fait
        // AutoCAD d'un objet entierement compris dans la fenetre.
        if (taken.size() == points.size())
            taken.clear();
        m_targets.append({ entity->id(), taken });
    }
}

int StretchEntitiesCommand::affectedCount() const { return int(m_targets.size()); }

void StretchEntitiesCommand::apply(const QPointF &delta)
{
    Folio *folio = m_project.folio(m_folioId);
    if (!folio)
        return;
    for (const auto &target : m_targets) {
        Entity *entity = folio->entity(target.first);
        if (!entity)
            continue;
        if (target.second.isEmpty()) {
            entity->translate(delta);
            continue;
        }
        for (int index : target.second)
            moveVertex(*entity, index, delta);
    }
}

void StretchEntitiesCommand::redo() { apply(m_delta); }

void StretchEntitiesCommand::undo() { apply(-m_delta); }

QString StretchEntitiesCommand::text() const
{
    return m_targets.size() == 1 ? QStringLiteral("Etirer un element")
                                 : QStringLiteral("Etirer %1 elements").arg(m_targets.size());
}

bool StretchEntitiesCommand::mergeWith(const Command &other)
{
    const auto *o = dynamic_cast<const StretchEntitiesCommand *>(&other);
    if (!o || o->m_folioId != m_folioId || o->m_targets != m_targets)
        return false;
    m_delta += o->m_delta;
    return true;
}

RenameFolioCommand::RenameFolioCommand(Project &project, QString folioId, QString number,
                                       QString title)
    : m_project(project), m_folioId(std::move(folioId)), m_afterNumber(std::move(number)),
      m_afterTitle(std::move(title))
{
    if (const Folio *folio = m_project.folio(m_folioId)) {
        m_beforeNumber = folio->number;
        m_beforeTitle = folio->title;
    }
}

void RenameFolioCommand::apply(const QString &number, const QString &title)
{
    if (Folio *folio = m_project.folio(m_folioId)) {
        folio->number = number;
        folio->title = title;
    }
}

void RenameFolioCommand::redo() { apply(m_afterNumber, m_afterTitle); }

void RenameFolioCommand::undo() { apply(m_beforeNumber, m_beforeTitle); }

QString RenameFolioCommand::text() const { return QStringLiteral("Renommer un folio"); }

ChangeWireTypesCommand::ChangeWireTypesCommand(Project &project, WireTypeSet after)
    : m_project(project), m_before(project.wireTypes), m_after(std::move(after))
{
}

void ChangeWireTypesCommand::redo() { m_project.wireTypes = m_after; }

void ChangeWireTypesCommand::undo() { m_project.wireTypes = m_before; }

QString ChangeWireTypesCommand::text() const { return QStringLiteral("Modifier les types de fils"); }

} // namespace dsn
