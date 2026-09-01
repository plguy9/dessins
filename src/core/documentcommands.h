// Commandes de modification du document.
//
// Les modifications sont exprimees par instantanes clones plutot que par
// deltas : c'est un peu plus de memoire par entite, mais l'annulation devient
// impossible a desynchroniser du document, ce qui vaut largement l'echange.
#pragma once

#include "command.h"
#include "entity.h"
#include "folio.h"
#include "project.h"

#include <QStringList>

namespace dsn {

// Identifiants de fusion des commandes successives.
enum CommandMergeId {
    NoMerge = -1,
    MergeMove = 1,
    MergeEditField = 2,
};

class AddEntityCommand : public Command
{
public:
    AddEntityCommand(Project &project, QString folioId, EntityPtr entity, QString text = {});
    void redo() override;
    void undo() override;
    QString text() const override { return m_text; }
    QString entityId() const { return m_entityId; }

private:
    Project &m_project;
    QString m_folioId;
    QString m_entityId;
    EntityPtr m_stored; // detenue quand l'entite n'est pas dans le document
    QString m_text;
};

class RemoveEntityCommand : public Command
{
public:
    RemoveEntityCommand(Project &project, QString folioId, QString entityId, QString text = {});
    void redo() override;
    void undo() override;
    QString text() const override { return m_text; }

private:
    Project &m_project;
    QString m_folioId;
    QString m_entityId;
    EntityPtr m_stored;
    int m_index = -1; // restaure l'ordre de trace
    QString m_text;
};

// Remplacement complet d'une entite par sa version modifiee.
class ModifyEntityCommand : public Command
{
public:
    ModifyEntityCommand(Project &project, QString folioId, EntityPtr before, EntityPtr after,
                        QString text = {});
    void redo() override;
    void undo() override;
    QString text() const override { return m_text; }
    int mergeId() const override { return m_mergeId; }
    bool mergeWith(const Command &other) override;
    void setMergeId(int id) { m_mergeId = id; }

private:
    void apply(const EntityPtr &state);

    Project &m_project;
    QString m_folioId;
    EntityPtr m_before;
    EntityPtr m_after;
    QString m_text;
    int m_mergeId = NoMerge;
};

// Deplacement d'un lot d'entites. Fusionne avec le deplacement precedent pour
// qu'un glisser a la souris ne produise qu'une seule entree annulable.
class MoveEntitiesCommand : public Command
{
public:
    MoveEntitiesCommand(Project &project, QString folioId, QStringList entityIds, QPointF delta);
    void redo() override;
    void undo() override;
    QString text() const override;
    int mergeId() const override { return MergeMove; }
    bool mergeWith(const Command &other) override;

private:
    Project &m_project;
    QString m_folioId;
    QStringList m_entityIds;
    QPointF m_delta;
};

class AddFolioCommand : public Command
{
public:
    AddFolioCommand(Project &project, std::unique_ptr<Folio> folio, int index = -1);
    void redo() override;
    void undo() override;
    QString text() const override;
    QString folioId() const { return m_folioId; }

private:
    Project &m_project;
    std::unique_ptr<Folio> m_stored;
    QString m_folioId;
    int m_index = -1;
};

class RemoveFolioCommand : public Command
{
public:
    RemoveFolioCommand(Project &project, QString folioId);
    void redo() override;
    void undo() override;
    QString text() const override;

private:
    Project &m_project;
    QString m_folioId;
    std::unique_ptr<Folio> m_stored;
    int m_index = -1;
};

class MoveFolioCommand : public Command
{
public:
    MoveFolioCommand(Project &project, int from, int to);
    void redo() override;
    void undo() override;
    QString text() const override;

private:
    Project &m_project;
    int m_from = 0;
    int m_to = 0;
};

// Mise en page d'un folio : format de feuille et cadre. Le contenu n'est pas
// touche — un dessin qui depassait depassera encore, et c'est a l'utilisateur
// de le voir dans l'apercu avant d'appliquer.
class ChangeFolioLayoutCommand : public Command
{
public:
    ChangeFolioLayoutCommand(Project &project, QString folioId, SheetFormat sheet,
                             SheetFrame frame);
    void redo() override;
    void undo() override;
    QString text() const override;

private:
    void apply(const SheetFormat &sheet, const SheetFrame &frame);

    Project &m_project;
    QString m_folioId;
    SheetFormat m_beforeSheet;
    SheetFrame m_beforeFrame;
    SheetFormat m_afterSheet;
    SheetFrame m_afterFrame;
};

class ChangeProjectInfoCommand : public Command
{
public:
    ChangeProjectInfoCommand(Project &project, ProjectInfo after);
    void redo() override;
    void undo() override;
    QString text() const override;

private:
    Project &m_project;
    ProjectInfo m_before;
    ProjectInfo m_after;
};

} // namespace dsn
