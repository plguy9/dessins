// Palette de symboles : recherche, categories, apercu.
//
// C'est le panneau le plus sollicite de la journee. Trois choses comptent.
//
// 1. **La recherche doit trouver sur le nom courant comme sur le mot-cle
//    metier**, sans quoi il faut connaitre le nom exact de ce qu'on cherche.
//
// 2. **L'apercu doit etre le vrai trace du symbole**, pas une icone
//    approchante : c'est a sa forme qu'un electricien reconnait un contact NF
//    d'un contact NO, et une approximation lui ferait poser le mauvais.
//
// 3. **La densite.** Decision utilisateur (2026-09-02) : *« j'aimerais que ce
//    soit plus discret, cela prend trop de place »*. En liste a une colonne,
//    cinq symboles sur cent trois etaient visibles — chercher voulait dire
//    faire defiler. La grille de vignettes en montre une trentaine dans la
//    meme place ; c'est l'Icon Menu d'AutoCAD Electrical, et c'est le bon
//    modele parce qu'on reconnait un symbole a sa forme plus vite qu'a son
//    nom. La liste reste disponible pour qui lit les noms, et le choix est
//    retenu.
#pragma once

#include "core/symbollibrary.h"
#include "render/renderstyle.h"

#include <QIcon>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;

namespace dsn {

class SymbolPalette : public QWidget
{
    Q_OBJECT

public:
    explicit SymbolPalette(QWidget *parent = nullptr);

    void setLibrary(const SymbolLibrary *library);
    void setNorm(const QString &norm);
    QString norm() const { return m_norm; }

    QString currentDefinitionId() const;

    // Un symbole vient d'etre pose : il passe en tete des recents. C'est le
    // seul signal fiable — un symbole seulement selectionne dans la palette
    // n'a peut-etre jamais servi.
    void noteUsed(const QString &definitionId);
    QStringList recent() const { return m_recent; }

    bool gridMode() const { return m_grid; }
    void setGridMode(bool grid);

    // Nombre de symboles montres, pour les tests : c'est la mesure de ce que
    // la recherche et la categorie ont retenu.
    int visibleCount() const;

    // Combien de vignettes tiennent A L'ECRAN, sans faire defiler. C'est le
    // vrai chiffre de la densite — `visibleCount` compte ce que le filtre a
    // retenu, pas ce que l'oeil voit. Publique pour qu'un test verrouille le
    // gain au lieu de compter des pixels.
    int gridCapacity() const;

    // La bande des recents : ce qu'on repose toute la journee, en permanence
    // au-dessus de la grille plutot qu'en categorie a choisir. Sur cent trois
    // symboles dont on en repose dix, c'est le raccourci le plus rentable du
    // panneau — et `noteUsed` l'alimentait deja sans que rien ne le montre.
    int recentVisibleCount() const;

    // Apercu d'une definition, dessine avec le meme peintre que le canevas.
    static QIcon renderIcon(const SymbolDefinition &definition, int pixels,
                            const RenderStyle &style);

    // Clef de la categorie « Recents ». Publique parce qu'un test la vise.
    static QString recentCategory();

Q_SIGNALS:
    void symbolChosen(const QString &definitionId);
    void symbolActivated(const QString &definitionId);

protected:
    // La fleche vers le bas depuis la recherche entre dans la grille : on tape
    // trois lettres puis on descend, sans lacher le clavier. Sans cela il faut
    // reprendre la souris entre chaque symbole, ce qui est exactement ce qu'on
    // fait cent fois par jour.
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildCategories();
    void rebuildList();
    void applyViewMode();

    const SymbolLibrary *m_library = nullptr;
    QString m_norm = QStringLiteral("IEC");
    QStringList m_recent;
    bool m_grid = true;

    void rebuildRecent();
    void refreshSelectionCard();

    QLineEdit *m_search = nullptr;
    QComboBox *m_category = nullptr;
    QToolButton *m_viewToggle = nullptr;
    QListWidget *m_list = nullptr;
    // La bande des recents, une rangee, au-dessus de la grille.
    QWidget *m_recentBand = nullptr;
    QListWidget *m_recentList = nullptr;
    QLabel *m_allBand = nullptr;
    // La case de selection, en bas : elle repond a la question que la grille
    // pose forcement — « quelle variante ai-je ? » — et qui coute un survol.
    QLabel *m_pickName = nullptr;
    QLabel *m_pickDetail = nullptr;
};

} // namespace dsn
