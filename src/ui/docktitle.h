// La barre de titre d'un panneau ancre.
//
// Qt en dessine une par defaut, mais elle ne porte qu'une croix minuscule que
// la feuille de style du theme efface. L'utilisateur a demande un bouton
// visible pour tasser la palette de symboles et rendre la place au dessin :
// c'est celui-ci.
//
// Le titre reste grave — petites capitales espacees, filet dessous — parce
// qu'un panneau se separe par un trait, jamais par une boite. Le bouton est
// discret jusqu'au survol : il ne doit pas attirer l'oeil plus que le nom du
// panneau, qui est ce qu'on lit.
#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace dsn {

class DockTitle : public QWidget
{
    Q_OBJECT

public:
    // `hint` explique comment rouvrir le panneau une fois ferme : un bouton
    // qui fait disparaitre un panneau sans dire comment le retrouver coute
    // plus cher qu'il ne rend.
    DockTitle(const QString &title, const QString &hint, QWidget *parent = nullptr);

    void applyTheme();

Q_SIGNALS:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_label = nullptr;
    QToolButton *m_close = nullptr;
};

} // namespace dsn
