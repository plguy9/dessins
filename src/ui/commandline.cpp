#include "commandline.h"

#include "theme.h"

#include <QCompleter>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStringListModel>
#include <QVBoxLayout>

namespace dsn {

CommandLine::CommandLine(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_history = new QPlainTextEdit(this);
    m_history->setReadOnly(true);
    m_history->setMaximumBlockCount(400);
    m_history->setFrameShape(QFrame::NoFrame);
    m_history->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_history->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(9.0);
    m_history->setFont(mono);
    layout->addWidget(m_history, 1);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Entrez une commande — tapez ? pour la liste"));
    m_input->setFont(mono);
    m_input->installEventFilter(this);
    layout->addWidget(m_input);

    m_completionModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completionModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_input->setCompleter(m_completer);
    // La liste montre ce que fait chaque commande, pas seulement son nom :
    // c'est ainsi qu'on apprend le repertoire en s'en servant, au lieu
    // d'aller lire une aide. Seul le nom est insere a la validation.
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated), this,
            [this](const QString &entry) {
                m_input->setText(entry.section(QStringLiteral("  —  "), 0, 0).trimmed());
            });

    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_input->text().trimmed();
        m_input->clear();
        if (text.isEmpty()) {
            // Entree sur une ligne vide relance la derniere commande :
            // c'est le reflexe d'AutoCAD, et il fait gagner un temps reel
            // quand on pose vingt symboles a la suite.
            if (!m_lastCommand.isEmpty())
                execute(m_lastCommand);
            return;
        }
        m_typedHistory.append(text);
        m_historyCursor = m_typedHistory.size();
        execute(text);
    });

    writePrompt(tr("Prêt. Tapez ? pour la liste des commandes."));
}

void CommandLine::registerCommand(CommandDefinition command)
{
    m_commands.append(std::move(command));
    refreshCompletion();
}

void CommandLine::refreshCompletion()
{
    QStringList entries;
    for (const CommandDefinition &command : std::as_const(m_commands)) {
        const QString description = command.description;
        entries.append(description.isEmpty()
                               ? command.name
                               : command.name + QStringLiteral("  —  ") + description);
        for (const QString &alias : command.aliases) {
            // L'alias rappelle le nom complet : c'est comme cela qu'on passe
            // du raccourci appris par hasard a la commande qu'il abrege.
            entries.append(alias + QStringLiteral("  —  ") + description
                           + QStringLiteral("  (") + command.name + QLatin1Char(')'));
        }
    }
    entries.sort(Qt::CaseInsensitive);
    m_completionModel->setStringList(entries);
}

const CommandDefinition *CommandLine::find(const QString &token) const
{
    for (const CommandDefinition &command : m_commands) {
        if (command.name.compare(token, Qt::CaseInsensitive) == 0)
            return &command;
        for (const QString &alias : command.aliases) {
            if (alias.compare(token, Qt::CaseInsensitive) == 0)
                return &command;
        }
    }
    return nullptr;
}

void CommandLine::write(const QString &line)
{
    m_history->appendPlainText(line);
    m_history->verticalScrollBar()->setValue(m_history->verticalScrollBar()->maximum());
}

void CommandLine::writeError(const QString &line)
{
    const QColor color = Theme::colors().danger;
    m_history->appendHtml(QStringLiteral("<span style='color:%1'>%2</span>")
                                  .arg(color.name(), line.toHtmlEscaped()));
    m_history->verticalScrollBar()->setValue(m_history->verticalScrollBar()->maximum());
}

void CommandLine::writePrompt(const QString &line)
{
    const QColor color = Theme::colors().textMuted;
    m_history->appendHtml(QStringLiteral("<span style='color:%1'>%2</span>")
                                  .arg(color.name(), line.toHtmlEscaped()));
    m_history->verticalScrollBar()->setValue(m_history->verticalScrollBar()->maximum());
}

bool CommandLine::execute(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    write(QStringLiteral("› ") + trimmed);

    const QStringList parts = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString token = parts.first();

    if (token == QLatin1String("?")) {
        writePrompt(tr("Commandes disponibles :"));
        QVector<CommandDefinition> sorted = m_commands;
        std::sort(sorted.begin(), sorted.end(),
                  [](const CommandDefinition &a, const CommandDefinition &b) {
                      return a.name < b.name;
                  });
        for (const CommandDefinition &command : std::as_const(sorted)) {
            const QString aliases = command.aliases.isEmpty()
                    ? QString()
                    : QStringLiteral(" (%1)").arg(command.aliases.join(QStringLiteral(", ")));
            write(QStringLiteral("   %1%2 — %3")
                          .arg(command.name.leftJustified(14), aliases, command.description));
        }
        return true;
    }

    const CommandDefinition *command = find(token);
    if (!command) {
        // Le message reprend la formulation d'AutoCAD, qui a le merite de
        // dire exactement ce qui n'a pas ete reconnu.
        writeError(tr("Commande inconnue « %1 ». Tapez ? pour la liste.").arg(token));
        return false;
    }

    m_lastCommand = command->name;
    if (command->handler)
        command->handler(parts.mid(1));
    Q_EMIT commandExecuted(command->name);
    return true;
}

bool CommandLine::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_input || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    switch (key->key()) {
    case Qt::Key_Escape:
        // Echap vide la saisie puis rend la main au dessin : on ne reste
        // jamais coince dans la ligne de commande.
        if (m_input->text().isEmpty())
            Q_EMIT escapePressed();
        m_input->clear();
        return true;

    case Qt::Key_Up:
        if (!m_typedHistory.isEmpty() && m_historyCursor > 0) {
            --m_historyCursor;
            m_input->setText(m_typedHistory.at(m_historyCursor));
        }
        return true;

    case Qt::Key_Down:
        if (m_historyCursor < m_typedHistory.size() - 1) {
            ++m_historyCursor;
            m_input->setText(m_typedHistory.at(m_historyCursor));
        } else {
            m_historyCursor = m_typedHistory.size();
            m_input->clear();
        }
        return true;

    case Qt::Key_Space:
        // Espace vaut Entree quand la saisie forme deja une commande :
        // c'est le comportement d'AutoCAD, et il evite de chercher la
        // touche Entree en plein trace.
        if (find(m_input->text().trimmed())) {
            const QString text = m_input->text().trimmed();
            m_input->clear();
            m_typedHistory.append(text);
            m_historyCursor = m_typedHistory.size();
            execute(text);
            return true;
        }
        break;

    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void CommandLine::focusInput()
{
    m_input->setFocus(Qt::ShortcutFocusReason);
    m_input->selectAll();
}

} // namespace dsn
