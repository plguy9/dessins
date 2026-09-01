// Ecran d'accueil.
//
// Un logiciel de CAO qui ouvre sur une feuille blanche muette ne montre rien
// de ce qu'il sait faire — et tout le travail qu'il y a dedans reste
// invisible. Cet ecran donne un point de depart : les dossiers recents, le
// projet d'exemple, et les gestes qui valent la peine d'etre appris.
//
// Il ne remplace aucun menu : il abrege le premier pas.
#pragma once

#include <QDialog>

class QListWidget;

namespace dsn {

class StartPage : public QDialog
{
    Q_OBJECT

public:
    explicit StartPage(const QStringList &recentFiles, const QString &examplePath,
                       QWidget *parent = nullptr);

    // Vrai quand l'utilisateur a demande a ne plus revoir cet ecran.
    bool dismissed() const { return m_dismissed; }

Q_SIGNALS:
    void openRequested(const QString &path);
    void newProjectRequested();
    void browseRequested();

private:
    bool m_dismissed = false;
    QListWidget *m_recent = nullptr;
};

} // namespace dsn
