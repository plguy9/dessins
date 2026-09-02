// Le rail des panneaux tasses.
//
// Un panneau ferme doit rester joignable la ou il etait. Le chevron qui le
// tasse vit dans sa propre barre de titre : il part avec lui, et le seul
// chemin de retour devenait un menu ou un raccourci — invisible depuis
// l'endroit ou le panneau vient de disparaitre (signale a l'usage,
// 2026-09-02 : « la fleche doit rester pour pouvoir la reouvrir ? »).
//
// Le rail est une bande etroite collee au bord du canevas. Il ne porte rien
// tant que tous les panneaux sont ouverts — c'est ce qui permet de le laisser
// en place en permanence — et un onglet par panneau tasse : chevron tourne
// vers le dessin, nom grave a la verticale. La fleche reste donc, et elle
// reste ou l'oeil la cherche.
//
// Le rail ne montre ni ne cache lui-meme : il demande, et MainWindow le fait.
// C'est ce qui garde un seul chemin (setDockVisible), celui qui retient la
// largeur du panneau — deux chemins finiraient par diverger.
#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

class QDockWidget;
class QVBoxLayout;

namespace dsn {

class RailTab;

class DockRail : public QWidget
{
    Q_OBJECT

public:
    explicit DockRail(QWidget *parent = nullptr);

    // `hint` dit ce que fait l'onglet et par quel raccourci on y arrive
    // autrement : le rail enseigne le clavier, comme le ruban enseigne le nom
    // des commandes.
    void watch(QDockWidget *dock, const QString &title, const QString &hint);

    void applyTheme();

    // Les onglets actuellement offerts, dans l'ordre. Un test lit cette liste
    // plutot que de compter des pixels.
    QStringList tabs() const;

Q_SIGNALS:
    void openRequested(QDockWidget *dock);

private:
    void paintEvent(QPaintEvent *event) override;

    void refresh();

    struct Entry {
        QDockWidget *dock = nullptr;
        RailTab *tab = nullptr;
    };

    QList<Entry> m_entries;
    QVBoxLayout *m_layout = nullptr;
};

} // namespace dsn
