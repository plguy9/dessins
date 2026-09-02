// Commandes de modification du document.
//
// Les modifications sont exprimees par instantanes clones plutot que par
// deltas : c'est un peu plus de memoire par entite, mais l'annulation devient
// impossible a desynchroniser du document, ce qui vaut largement l'echange.
#pragma once

#include "command.h"
#include "entity.h"
#include "folio.h"
#include "edittools.h"
#include "project.h"

#include <QPair>
#include <QStringList>

#include <utility>
#include <vector>

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

// ETIRER (STRETCH). La commande la plus specifique de l'edition 2D : ce n'est
// pas l'entite qui est deplacee, ce sont ses sommets pris dans la fenetre de
// capture. Un fil dont une seule extremite est prise s'allonge ; un fil pris
// en entier se deplace. C'est ainsi qu'on rallonge un barreau d'echelle sans
// detacher ce qui y est raccorde.
//
// Les sommets concernes sont figes a la construction : les recalculer a
// l'annulation les chercherait dans la geometrie deja deplacee, et le
// retablissement ne rendrait pas le meme dessin.
class StretchEntitiesCommand : public Command
{
public:
    StretchEntitiesCommand(Project &project, QString folioId, const QRectF &windowMm,
                           QPointF delta);

    void redo() override;
    void undo() override;
    QString text() const override;
    bool mergeWith(const Command &other) override;

    // Nombre d'entites touchees. Zero signifie que la fenetre n'a rien pris :
    // l'appelant ne doit alors rien empiler.
    int affectedCount() const;

private:
    void apply(const QPointF &delta);

    Project &m_project;
    QString m_folioId;
    QPointF m_delta;
    // Sommets pris, par entite. Une liste vide signifie « entite entiere ».
    QVector<QPair<QString, QVector<int>>> m_targets;
};

// Deplace un appareil en emmenant les extremites de fil posees sur ses
// broches. Sans elles, l'appareil se retrouve debranche sans que rien ne le
// montre : le trace reste beau, la netlist est fausse.
//
// Comme pour l'etirement, les sommets concernes sont figes a la construction.
// Les rechercher a l'annulation les chercherait dans la geometrie deja
// deplacee, et le retablissement ne rendrait pas le meme dessin.
class MoveComponentCommand : public Command
{
public:
    MoveComponentCommand(Project &project, QString folioId, QString symbolId, QPointF delta,
                         const SymbolLibrary &library);

    void redo() override;
    void undo() override;
    QString text() const override;
    bool mergeWith(const Command &other) override;

    // Nombre d'extremites de fil emmenees. Zero signifie un appareil isole :
    // le deplacement reste valable, il ne traine simplement rien.
    int attachedCount() const { return int(m_wireEnds.size()); }

private:
    void apply(const QPointF &delta);

    Project &m_project;
    QString m_folioId;
    QString m_symbolId;
    QPointF m_delta;
    QVector<QPair<QString, int>> m_wireEnds;
};

// Renomme un folio : son numero et son titre, ce que porte le cartouche.
class RenameFolioCommand : public Command
{
public:
    RenameFolioCommand(Project &project, QString folioId, QString number, QString title);
    void redo() override;
    void undo() override;
    QString text() const override;

private:
    void apply(const QString &number, const QString &title);

    Project &m_project;
    QString m_folioId;
    QString m_beforeNumber;
    QString m_beforeTitle;
    QString m_afterNumber;
    QString m_afterTitle;
};

// Remplace le jeu de types de fils du projet. Les fils ne changent pas : ils
// referencent un identifiant, et c'est ce que l'identifiant designe qui bouge.
class ChangeWireTypesCommand : public Command
{
public:
    ChangeWireTypesCommand(Project &project, WireTypeSet after);
    void redo() override;
    void undo() override;
    QString text() const override;

private:
    Project &m_project;
    WireTypeSet m_before;
    WireTypeSet m_after;
};

// ECHELLE (SCALE). Grossir ou reduire une selection autour d'un point de
// base. Chaque entite sait ce que grossir veut dire pour elle : un symbole
// change de facteur de placement, un fil deplace ses sommets, un texte
// grandit sa hauteur de capitale.
//
// L'etat d'avant est fige a la construction, comme pour l'etirement. Rejouer
// l'homothetie inverse a l'annulation accumulerait l'erreur d'arrondi a
// chaque aller-retour, et un symbole grossi puis annule dix fois ne
// reviendrait pas exactement a sa taille.
class ScaleEntitiesCommand : public Command
{
public:
    ScaleEntitiesCommand(Project &project, QString folioId, QStringList entityIds, QPointF base,
                         double factor);

    void redo() override;
    void undo() override;
    QString text() const override;

    int affectedCount() const { return int(m_before.size()); }

private:
    Project &m_project;
    QString m_folioId;
    QPointF m_base;
    double m_factor = 1.0;
    // std::vector et non QVector : un unique_ptr ne se copie pas, et les
    // conteneurs implicitement partages de Qt l'exigent.
    std::vector<std::pair<QString, EntityPtr>> m_before;
};

// RESEAU (ARRAY). Une matrice de copies, rectangulaire ou polaire. La
// commande pose les copies et rien d'autre : l'original ne bouge pas, et
// tout se defait d'une seule annulation.
class ArrayEntitiesCommand : public Command
{
public:
    ArrayEntitiesCommand(Project &project, QString folioId, const QStringList &entityIds,
                         const ArraySpec &spec);

    void redo() override;
    void undo() override;
    QString text() const override;

    int addedCount() const { return int(m_added.size()); }

private:
    Project &m_project;
    QString m_folioId;
    // Les copies sont construites une fois, a la construction : les
    // reconstruire au retablissement leur donnerait de nouveaux identifiants,
    // et tout ce qui les designe — selection, panneaux — pointerait a vide.
    std::vector<EntityPtr> m_copies;
    QStringList m_added;
};

// ALIGNER et REPARTIR. Un deplacement par entite, calcule une fois puis
// applique tel quel : c'est une translation, donc l'annulation est exacte.
class AlignEntitiesCommand : public Command
{
public:
    AlignEntitiesCommand(Project &project, QString folioId, const QStringList &entityIds,
                         AlignMode mode);

    void redo() override;
    void undo() override;
    QString text() const override;

    int affectedCount() const { return int(m_offsets.size()); }

private:
    void apply(double sign);

    Project &m_project;
    QString m_folioId;
    AlignMode m_mode;
    QVector<QPair<QString, QPointF>> m_offsets;
};

} // namespace dsn
