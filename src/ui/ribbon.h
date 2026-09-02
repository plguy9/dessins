// Le ruban : des onglets, et sous l'onglet actif une rangee de panneaux
// nommes — la disposition d'AutoCAD, dont c'est la vraie force. Une rangee
// d'icones sans hierarchie oblige a survoler chaque bouton pour retrouver une
// commande ; un panneau nomme dit ou chercher avant qu'on ait cherche.
//
// Le ruban ne detient aucune commande. Chaque bouton represente une QAction
// deja construite et deja posee dans un menu : c'est une seconde vue sur le
// meme repertoire, comme la palette de commandes. Une action qui ne serait
// QUE dans le ruban disparaitrait de la palette, qui se remplit en parcourant
// les menus — c'est l'invariant qui gouverne ce fichier, et un test le tient.
//
// Consequence pratique : rien ici ne se connecte, ne grise ni ne coche. Le
// bouton suit son action, donc l'etat reste juste sans une ligne de code.
#pragma once

#include <QList>
#include <QSize>
#include <QString>
#include <QWidget>

class QAction;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QMenu;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace dsn {

// Un panneau : une ou deux commandes en grand, le reste en petit, et le nom
// grave dessous.
class RibbonPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RibbonPanel(const QString &title, QWidget *parent = nullptr);

    // Gros bouton : icone 32 px, libelle dessous. Le libelle vient du
    // parametre plutot que de action->text() : celui-ci porte le mnemonique,
    // les points de suspension, et pour Annuler le nom de la derniere
    // commande — la largeur du bouton changerait a chaque geste.
    QToolButton *addLarge(QAction *action, const QString &shortLabel = QString());

    // Petit bouton : icone seule. Deux rangees, remplies en colonnes : trois
    // petits font deux colonnes, jamais trois rangees — la hauteur du panneau
    // ne doit pas dependre de son contenu.
    QToolButton *addSmall(QAction *action);

    // Gros bouton a menu deroulant, pour une famille de commandes voisines
    // (les huit alignements).
    QToolButton *addLargeMenu(QAction *action, QMenu *menu, const QString &shortLabel);

    // Un reglage a la place d'un bouton — le selecteur de type de fil.
    void addControl(QWidget *widget);

    QString title() const { return m_title; }
    // Les actions posees, pour le test « tout ce qui est au ruban est au menu ».
    QList<QAction *> ribbonActions() const { return m_actions; }

private:
    void closeGrid();

    QString m_title;
    QHBoxLayout *m_row = nullptr;
    QGridLayout *m_grid = nullptr;
    int m_small = 0;
    QLabel *m_name = nullptr;
    QList<QAction *> m_actions;
};

// Un onglet deplie : des panneaux separes par un filet d'un pixel, tasses a
// gauche.
class RibbonPage : public QWidget
{
    Q_OBJECT

public:
    explicit RibbonPage(QWidget *parent = nullptr);

    RibbonPanel *addPanel(const QString &title);
    QList<RibbonPanel *> panels() const { return m_panels; }

private:
    QHBoxLayout *m_row = nullptr;
    QList<RibbonPanel *> m_panels;
};

class Ribbon : public QWidget
{
    Q_OBJECT

public:
    // Les hauteurs vivent ici et nulle part ailleurs. La feuille de style de
    // l'application ne doit declarer aucun min-height sur les boutons du
    // ruban : Qt s'en servirait pour reecrire la taille minimale du widget et
    // ces constantes ne voudraient plus rien dire — le piege deja paye par la
    // carte de l'ecran d'accueil.
    static constexpr int kLargeIcon = 32;
    static constexpr int kSmallIcon = 20;
    static constexpr int kRowHeight = 58;
    static constexpr int kPanelHeight = 78;
    static constexpr int kTabHeight = 30;
    // La barre de defilement horizontale de la page se pose SOUS les
    // panneaux : sans cette reserve, elle rogne le nom grave, et un panneau
    // sans nom n'est plus qu'une grappe d'icones.
    static constexpr int kScrollAllowance = 13;

    explicit Ribbon(QWidget *parent = nullptr);

    // La barre d'acces rapide, a gauche des onglets. Elle porte le tout petit
    // nombre de commandes qu'on appelle sans y penser — enregistrer, annuler —
    // et qui ne doivent jamais dependre de l'onglet ouvert ni du repli du
    // ruban. C'est la reponse d'AutoCAD au meme probleme, et la seule : sans
    // elle, Annuler se retrouve en petite icone dans le dernier panneau.
    QToolButton *addQuickAction(QAction *action);
    QList<QAction *> quickActions() const { return m_quick; }

    RibbonPage *addPage(const QString &title);
    RibbonPage *page(int index) const;
    RibbonPage *page(const QString &title) const;
    int pageCount() const;

    // Repli : le clic sur l'onglet actif replie, le clic sur un autre deplie
    // et bascule — c'est le geste d'AutoCAD. Seule la pile est masquee ; les
    // onglets restent, sinon on ne saurait plus comment revenir.
    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);

    // A appeler apres Theme::apply : les boutons suivent leur action, mais la
    // couleur des filets et du chevron n'appartient a aucune action.
    void applyTheme();

    // Publiques : la hauteur du ruban est ce qu'il prend au dessin, et c'est
    // la seule chose qu'un test a besoin de mesurer.
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void collapsedChanged(bool collapsed);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_pages = nullptr;
    QToolButton *m_fold = nullptr;
    QHBoxLayout *m_quickRow = nullptr;
    QList<QAction *> m_quick;
    QList<RibbonPage *> m_pageList;
    QStringList m_titles;
    bool m_collapsed = false;
};

} // namespace dsn
