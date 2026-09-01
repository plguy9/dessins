#include "command.h"

#include <algorithm>

namespace dsn {

Command::~Command() = default;

bool Command::mergeWith(const Command &) { return false; }

MacroCommand::MacroCommand(QString text) : m_text(std::move(text)) {}

void MacroCommand::redo()
{
    for (auto &child : m_children)
        child->redo();
}

void MacroCommand::undo()
{
    // Ordre inverse : une macro se defait comme on remonte une pile.
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        (*it)->undo();
}

void MacroCommand::append(CommandPtr command)
{
    if (command)
        m_children.push_back(std::move(command));
}

CommandStack::CommandStack() = default;
CommandStack::~CommandStack() = default;

void CommandStack::notify()
{
    if (m_changed)
        m_changed();
}

void CommandStack::setChangedCallback(std::function<void()> callback)
{
    m_changed = std::move(callback);
}

void CommandStack::push(CommandPtr command)
{
    if (!command)
        return;

    command->redo();

    if (!m_macros.empty()) {
        m_macros.back()->append(std::move(command));
        notify();
        return;
    }

    // Abandon de la branche defaite : on repart de l'etat courant.
    if (m_index < int(m_commands.size()))
        m_commands.erase(m_commands.begin() + m_index, m_commands.end());
    if (m_cleanIndex > m_index)
        m_cleanIndex = -1; // l'etat enregistre n'est plus atteignable

    if (m_mergeAllowed && !m_commands.empty() && command->mergeId() >= 0
        && m_commands.back()->mergeId() == command->mergeId()
        && m_commands.back()->mergeWith(*command)) {
        notify();
        return;
    }

    m_commands.push_back(std::move(command));
    m_index = int(m_commands.size());
    m_mergeAllowed = true;
    notify();
}

bool CommandStack::canUndo() const { return m_index > 0 && m_macros.empty(); }

bool CommandStack::canRedo() const { return m_index < int(m_commands.size()) && m_macros.empty(); }

void CommandStack::undo()
{
    if (!canUndo())
        return;
    --m_index;
    m_commands[std::size_t(m_index)]->undo();
    m_mergeAllowed = false;
    notify();
}

void CommandStack::redo()
{
    if (!canRedo())
        return;
    m_commands[std::size_t(m_index)]->redo();
    ++m_index;
    m_mergeAllowed = false;
    notify();
}

QString CommandStack::undoText() const
{
    return canUndo() ? m_commands[std::size_t(m_index - 1)]->text() : QString();
}

QString CommandStack::redoText() const
{
    return canRedo() ? m_commands[std::size_t(m_index)]->text() : QString();
}

void CommandStack::clear()
{
    m_commands.clear();
    m_macros.clear();
    m_index = 0;
    m_cleanIndex = 0;
    m_mergeAllowed = true;
    notify();
}

void CommandStack::beginMacro(const QString &text)
{
    m_macros.push_back(std::make_unique<MacroCommand>(text));
}

void CommandStack::endMacro()
{
    if (m_macros.empty())
        return;
    auto macro = std::move(m_macros.back());
    m_macros.pop_back();
    if (macro->isEmpty()) {
        notify();
        return;
    }

    if (!m_macros.empty()) {
        m_macros.back()->append(std::move(macro));
        notify();
        return;
    }

    // La macro a deja ete executee commande par commande : on l'empile sans
    // la rejouer.
    if (m_index < int(m_commands.size()))
        m_commands.erase(m_commands.begin() + m_index, m_commands.end());
    if (m_cleanIndex > m_index)
        m_cleanIndex = -1;
    m_commands.push_back(std::move(macro));
    m_index = int(m_commands.size());
    m_mergeAllowed = false;
    notify();
}

} // namespace dsn
