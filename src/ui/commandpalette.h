// Palette de commandes — la reponse moderne au probleme du ruban.
//
// AutoCAD a supprime ses barres d'outils au profit d'un ruban : plus
// decouvrable, mais lent et encombrant. Sa ligne de commande est l'inverse :
// rapide, mais il faut deja savoir ce qu'on cherche.
//
// La palette prend le meilleur des deux. Une frappe l'ouvre, on tape ce qu'on
// veut faire en francais, elle montre la commande, son raccourci et ce qu'elle
// fait. Le debutant la lit comme un menu, l'habitue s'en sert comme d'une
// ligne de commande, et personne n'a besoin d'apprendre ou se cache quoi.
//
// Elle ne remplace rien : elle donne un second acces a tout ce que les menus
// et la ligne de commande offrent deja.
#pragma once

#include <QDialog>
#include <QVector>

#include <functional>

class QLineEdit;
class QListWidget;

namespace dsn {

class CommandPalette : public QDialog
{
    Q_OBJECT

public:
    struct Entry {
        QString title;        // « Poser un rapport dans le dessin »
        QString group;        // « Projet », « Édition »…
        QString shortcut;     // « Ctrl+P »
        QString detail;       // ce que fait la commande
        QStringList keywords; // alias de la ligne de commande, mots proches
        std::function<void()> run;
        bool enabled = true;
    };

    explicit CommandPalette(QWidget *parent = nullptr);

    void setEntries(QVector<Entry> entries);
    // Ouvre la palette, champ vide et liste complete : on part toujours de
    // tout ce qui est possible, jamais de la derniere recherche.
    void open();

    // Filtrage flou : les lettres tapees doivent apparaitre dans l'ordre, pas
    // forcement cote a cote. « psrap » trouve « Poser le rapport ». Expose
    // pour les tests, parce que c'est la seule regle qui compte ici.
    static bool matches(const QString &needle, const QString &haystack);
    // Score d'une entree : plus il est bas, plus elle remonte. Negatif quand
    // l'entree ne correspond pas.
    static int score(const QString &needle, const Entry &entry);

    // Entrees actuellement visibles, pour les tests.
    QVector<Entry> visibleEntries() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    void runCurrent();

    QVector<Entry> m_entries;
    QVector<int> m_visible;   // rangs dans m_entries
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
};

} // namespace dsn
