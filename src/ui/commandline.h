// La ligne de commande.
//
// C'est le trait le plus reconnaissable d'AutoCAD, et le plus utile a qui
// vient de la : on tape « L » puis Entree, sans quitter le clavier ni chercher
// un bouton. Les alias sont ceux d'AutoCAD, en francais comme en anglais,
// parce qu'un dessinateur les a dans les doigts et non dans la tete.
#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QCompleter;
class QLineEdit;
class QPlainTextEdit;
class QStringListModel;

namespace dsn {

struct CommandDefinition {
    QString name;         // « LIGNE »
    QStringList aliases;  // { « L » }
    QString description;
    std::function<void(const QStringList &arguments)> handler;
};

class CommandLine : public QWidget
{
    Q_OBJECT

public:
    explicit CommandLine(QWidget *parent = nullptr);

    void registerCommand(CommandDefinition command);
    QVector<CommandDefinition> commands() const { return m_commands; }

    // Echo dans l'historique. `write` pour le fil normal, `writeError` pour
    // ce qui a echoue : la couleur distingue les deux sans avoir a lire.
    void write(const QString &line);
    void writeError(const QString &line);
    void writePrompt(const QString &line);

    void focusInput();
    QString lastCommand() const { return m_lastCommand; }

    // Execute une ligne saisie. Publique pour que les tests puissent
    // eprouver le repertoire sans passer par le clavier.
    bool execute(const QString &text);

Q_SIGNALS:
    void commandExecuted(const QString &name);
    void escapePressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    const CommandDefinition *find(const QString &token) const;
    void refreshCompletion();
    // L'historique se replie quand il est vide et grandit avec ce qu'il
    // porte, jusqu'a trois lignes.
    void fitHistory();

    QPlainTextEdit *m_history = nullptr;
    QLineEdit *m_input = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;

    QVector<CommandDefinition> m_commands;
    QStringList m_typedHistory;
    int m_historyCursor = -1;
    QString m_lastCommand;
};

} // namespace dsn
