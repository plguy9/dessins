// Pile d'annulation.
//
// QUndoStack a migre de QtWidgets vers QtGui en Qt 6 : l'utiliser ferait entrer
// QtGui dans le coeur et lui ferait perdre sa testabilite sans ecran. La pile
// tient en une centaine de lignes, sans QObject ni moc.
#pragma once

#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace dsn {

class Command
{
public:
    virtual ~Command();
    virtual void redo() = 0;
    virtual void undo() = 0;
    virtual QString text() const = 0;

    // Fusion des commandes successives de meme nature : un deplacement a la
    // souris ne doit produire qu'une seule entree annulable, pas une par pixel.
    virtual int mergeId() const { return -1; }
    virtual bool mergeWith(const Command &other);
};

using CommandPtr = std::unique_ptr<Command>;

// Regroupe plusieurs commandes en une seule entree annulable.
class MacroCommand : public Command
{
public:
    explicit MacroCommand(QString text);
    void redo() override;
    void undo() override;
    QString text() const override { return m_text; }

    void append(CommandPtr command);
    bool isEmpty() const { return m_children.empty(); }
    std::size_t size() const { return m_children.size(); }

private:
    QString m_text;
    std::vector<CommandPtr> m_children;
};

class CommandStack
{
public:
    CommandStack();
    ~CommandStack();

    CommandStack(const CommandStack &) = delete;
    CommandStack &operator=(const CommandStack &) = delete;

    // Execute la commande puis l'empile. Toute commande situee au-dessus de
    // l'index courant est abandonnee.
    void push(CommandPtr command);

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    QString undoText() const;
    QString redoText() const;

    void clear();
    int index() const { return m_index; }
    int count() const { return int(m_commands.size()); }

    // Les macros s'imbriquent : seule la fermeture de la plus externe empile.
    void beginMacro(const QString &text);
    void endMacro();
    bool inMacro() const { return !m_macros.empty(); }

    // Etat propre : position de la pile au dernier enregistrement.
    bool isClean() const { return m_index == m_cleanIndex; }
    void setClean() { m_cleanIndex = m_index; notify(); }
    void resetClean() { m_cleanIndex = -1; notify(); }

    // Appele apres toute modification de la pile ou du document.
    void setChangedCallback(std::function<void()> callback);

    // Empeche la fusion de la prochaine commande avec la precedente : a
    // appeler quand un geste se termine (relachement de souris).
    void breakMergeChain() { m_mergeAllowed = false; }

private:
    void notify();

    std::vector<CommandPtr> m_commands;
    std::vector<std::unique_ptr<MacroCommand>> m_macros;
    int m_index = 0;      // nombre de commandes appliquees
    int m_cleanIndex = 0;
    bool m_mergeAllowed = true;
    std::function<void()> m_changed;
};

} // namespace dsn
