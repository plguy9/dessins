// La ligne de commande.
//
// C'est le trait le plus reconnaissable d'AutoCAD, et le plus utile a qui
// vient de la : on tape « L » puis Entree, sans quitter le clavier ni chercher
// un bouton. Les alias sont ceux d'AutoCAD, en francais comme en anglais,
// parce qu'un dessinateur les a dans les doigts et non dans la tete.
#pragma once

#include <QColor>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QCompleter;
class QFrame;
class QLabel;
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

    // QUATRE VOIX, et la couleur les distingue sans qu'on ait a lire :
    //
    //   writeSource  QUI parle — le nom de la commande, au troisieme niveau
    //                d'encre. Le ruban et les menus ecrivent par la, et c'est
    //                ce qui rend l'historique lisible en diagonale : on voit
    //                qui parle avant de lire quoi.
    //   write        le fil normal            — textMuted
    //   writeOk      ce qui a abouti          — success
    //   writeWarning ce qui merite un regard  — warning
    //   writeError   ce qui a echoue          — danger
    //
    // `success`, `warning` et `danger` etaient dans les jetons du theme et ne
    // se voyaient nulle part : un compte rendu qui a la meme couleur qu'une
    // erreur oblige a lire les deux pour savoir lequel est lequel.
    void writeSource(const QString &line);
    void write(const QString &line);
    void writeOk(const QString &line);
    void writeWarning(const QString &line);
    void writeError(const QString &line);
    void writePrompt(const QString &line);

    // L'INVITE — ce que la commande en cours attend. Elle reste affichee tant
    // que le geste dure, contrairement a un message d'historique qui defile.
    // C'est ce qui fait d'une ligne de commande un fil conducteur plutot qu'un
    // lanceur : chez AutoCAD, elle dit en permanence ou l'on en est.
    //
    // Tant qu'elle est posee, un filet d'accent de 2 px s'allume a sa gauche :
    // c'est le seul signal « le logiciel t'attend » de toute la fenetre, et il
    // s'eteint avec le geste, donc il ne peut pas mentir.
    void setPrompt(const QString &prompt);
    QString prompt() const;

    // Les couleurs des voix sont FIGEES dans le document au moment ou la
    // ligne est ecrite : Qt n'y range que des teintes, pas des jetons. Il faut
    // donc retracer l'historique au changement de theme, sinon la moitie des
    // lignes garde les couleurs de l'autre.
    void applyTheme();

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

    // La voix d'une ligne : ce qui decide de sa couleur. On la garde a cote du
    // texte pour pouvoir retracer l'historique au changement de theme.
    enum class Voice { Source, Plain, Ok, Warning, Error };
    void append(Voice voice, const QString &line);
    QColor colourOf(Voice voice) const;

    QLabel *m_prompt = nullptr;
    QPlainTextEdit *m_history = nullptr;
    QLineEdit *m_input = nullptr;
    // Le filet d'accent, allume tant qu'une invite est posee.
    QFrame *m_waiting = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;

    QVector<QPair<Voice, QString>> m_lines;

    QVector<CommandDefinition> m_commands;
    QStringList m_typedHistory;
    int m_historyCursor = -1;
    QString m_lastCommand;
};

} // namespace dsn
