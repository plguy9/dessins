#include "commandline.h"

#include "theme.h"

#include <QCompleter>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>

namespace dsn {

CommandLine::CommandLine(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_history = new QPlainTextEdit(this);
    // L'historique n'est pas un champ de saisie : la feuille de style lui
    // retire fond, bordure et coins arrondis. Sans cela il ressemble trait
    // pour trait au champ qui le suit, et le bandeau parait porter deux
    // lignes de commande.
    m_history->setProperty("commandHistory", true);
    m_history->setReadOnly(true);
    m_history->setMaximumBlockCount(400);
    m_history->setFrameShape(QFrame::NoFrame);
    m_history->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_history->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(9.0);
    m_history->setFont(mono);
    // L'historique n'existe visuellement que s'il a quelque chose a dire :
    // vide, il est masque et le bandeau se reduit au champ. Sinon il reserve
    // en permanence la place de trois lignes qui ne viendront peut-etre
    // jamais — ce qui est exactement ce qu'on reprochait au bandeau.
    m_history->hide();
    layout->addWidget(m_history, 1);

    // L'invite et le champ partagent une rangee coiffee d'un filet d'accent.
    // Le filet est le seul signal « le logiciel t'attend » de la fenetre : il
    // s'allume avec l'invite et s'eteint avec elle, donc il ne peut pas
    // mentir. C'est la reserve d'usage de la regle 3 du theme — l'accent
    // DESIGNE, il ne colore pas une phrase entiere.
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    m_waiting = new QFrame(this);
    m_waiting->setProperty("commandWaiting", true);
    m_waiting->setFixedWidth(2);
    m_waiting->hide();
    row->addWidget(m_waiting);

    auto *column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // L'invite se pose juste au-dessus du champ, la ou l'oeil descend deja
    // pour taper. Masquee tant qu'aucune commande n'attend rien : une ligne
    // vide en permanence prendrait la place sans rien dire.
    //
    // Elle est au CORPS DE L'INTERFACE, pas en chasse fixe : c'est une phrase
    // adressee au dessinateur — « Point suivant ou [Cote / Annuler] » — et non
    // une donnee. Seule la saisie garde la chasse fixe, parce qu'elle porte
    // des coordonnees.
    m_prompt = new QLabel(this);
    m_prompt->setProperty("commandPrompt", true);
    m_prompt->setFont(Theme::uiFont(10));
    // Fixe en hauteur : sans cela l'invite avale toute la place libre du
    // bandeau et se retrouve centree au milieu de soixante pixels de vide,
    // loin du champ qu'elle commente.
    //
    // Et elle RESTE POSEE, vide, quand rien n'attend. Ce guide disait
    // l'inverse — « masquee tant qu'aucune commande n'attend rien : une ligne
    // vide en permanence prendrait la place sans rien dire ». C'etait vrai
    // quand le bandeau valait 108 px en dur et ne bougeait jamais. Depuis que
    // l'en-tete est parti et qu'il vaut sa taille naturelle, la masquer fait
    // GRANDIR le bandeau de dix-neuf pixels des qu'un geste commence — donc
    // reculer la feuille au moment precis ou l'on vise un point. Une rangee
    // reservee qui ne dit rien coute moins qu'un dessin qui saute sous le
    // curseur. Mesure : 43 px au repos contre 62 avec l'invite.
    m_prompt->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // La hauteur est celle d'UNE ligne de texte, posee ici et pas deduite du
    // contenu : une etiquette vide et une etiquette pleine ne mesurent pas
    // tout a fait pareil, et ce pixel-la se voit — c'est le dessin qui bouge.
    // Le +4 est le rembourrage que la feuille de style pose (3 en haut, 1 en
    // bas).
    m_prompt->setFixedHeight(QFontMetrics(Theme::uiFont(10)).height() + 4);
    column->addWidget(m_prompt);

    m_input = new QLineEdit(this);
    m_input->setProperty("commandInput", true);
    m_input->setPlaceholderText(tr("Entrez une commande — tapez ? pour la liste"));
    m_input->setFont(Theme::monoFont(9));
    m_input->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_input->installEventFilter(this);
    column->addWidget(m_input);

    row->addLayout(column, 1);
    layout->addLayout(row);

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

    // Pas de message d'accueil dans l'historique : le champ porte deja le
    // meme conseil sous forme d'invite. Ecrire les deux, c'est dire deux fois
    // la meme chose sur deux lignes — et donner l'impression qu'il y a deux
    // lignes de commande. L'historique reste vide jusqu'a la premiere
    // reponse, qui est justement ce qu'on veut y lire.
}

void CommandLine::fitHistory()
{
    const int blocks = m_history->document()->blockCount();
    const bool empty = m_history->toPlainText().trimmed().isEmpty();
    m_history->setVisible(!empty);
    if (empty)
        return;

    // Trois lignes au plus, comme la ligne de commande d'AutoCAD : elle
    // informe sans manger la place du dessin. Au-dela, on fait defiler.
    const QFontMetrics metrics(m_history->font());
    const int lines = std::clamp(blocks, 1, 3);
    m_history->setFixedHeight(lines * metrics.lineSpacing() + 6);
}

void CommandLine::setPrompt(const QString &prompt)
{
    if (m_prompt->text() == prompt)
        return;
    m_prompt->setText(prompt);
    // Seul le filet bascule : l'invite garde sa rangee, vide, pour que le
    // bandeau ne change jamais de hauteur en plein geste.
    m_waiting->setVisible(!prompt.isEmpty());
}

QString CommandLine::prompt() const { return m_prompt->text(); }

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

QColor CommandLine::colourOf(Voice voice) const
{
    const ThemeColors &c = Theme::colors();
    switch (voice) {
    case Voice::Source: return c.textFaint;
    case Voice::Ok: return c.success;
    case Voice::Warning: return c.warning;
    case Voice::Error: return c.danger;
    case Voice::Plain: break;
    }
    return c.textMuted;
}

void CommandLine::append(Voice voice, const QString &line)
{
    m_lines.append({ voice, line });
    // Le document est borne a 400 blocs : la memoire des voix l'est aussi,
    // sinon une seance de dessin la ferait grandir sans fin.
    while (m_lines.size() > 400)
        m_lines.removeFirst();

    QTextCharFormat format;
    format.setForeground(colourOf(voice));
    QTextCursor cursor(m_history->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_history->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(line, format);

    fitHistory();
    m_history->verticalScrollBar()->setValue(m_history->verticalScrollBar()->maximum());
}

void CommandLine::applyTheme()
{
    // Qt ne range dans un document que des teintes, jamais des jetons : les
    // lignes deja ecrites garderaient les couleurs de l'autre theme. On
    // retrace donc, en gardant la voix de chaque ligne — c'est pour cela
    // qu'elle est memorisee a cote du texte.
    const QVector<QPair<Voice, QString>> lignes = m_lines;
    m_lines.clear();
    m_history->clear();
    for (const auto &ligne : lignes)
        append(ligne.first, ligne.second);
    m_prompt->setFont(Theme::uiFont(10));
    m_input->setFont(Theme::monoFont(9));
}

void CommandLine::writeSource(const QString &line) { append(Voice::Source, line); }
void CommandLine::write(const QString &line) { append(Voice::Plain, line); }
void CommandLine::writeOk(const QString &line) { append(Voice::Ok, line); }
void CommandLine::writeWarning(const QString &line) { append(Voice::Warning, line); }
void CommandLine::writeError(const QString &line) { append(Voice::Error, line); }
void CommandLine::writePrompt(const QString &line) { append(Voice::Plain, line); }

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
