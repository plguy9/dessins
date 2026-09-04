# Étape 05 — La palette récupère la colonne, les folios deviennent des pages

**Fichiers touchés :** `src/ui/symbolpalette.h/.cpp`, `src/ui/folionavigator.h/.cpp`,
`src/ui/mainwindow.cpp`, `src/ui/dockrail.cpp`, `src/ui/theme.cpp`, nouveau
`src/ui/foliotabs.h/.cpp`.

**Risque :** moyen. Déplace un dock. La logique de folio ne change pas.

---

## Le problème

Deux panneaux très différents partagent une colonne de 200 px : la palette de symboles (un
**outil**, le panneau le plus sollicité de la journée) et le navigateur de folios (un objet de
**navigation de document**).

Le navigateur prend **193 px** pour afficher, dans le projet d'exemple, *un* folio et beaucoup
de vide. La palette, elle, montre 24 vignettes avec la moitié de sa surface en air — alors que
la décision documentée du 2026-09-02 visait justement à en montrer une trentaine.

## 5.1 — Les folios sortent de la colonne

Un folio est une **page du dossier**. Sa métaphore juste est l'onglet de page, en bas du
canevas, pas la colonne latérale — 28 px au lieu de 193, et un dossier de trente pages se
parcourt mieux horizontalement.

Nouveau `src/ui/foliotabs.h` :

```cpp
// Les onglets de folio : les pages du dossier, en bas du canevas.
//
// Un folio est une page, pas un outil : sa place est celle d'un onglet de
// classeur, pas celle d'un panneau lateral. 28 px au lieu des 193 px que
// prenait la colonne — et sur un dossier de trente pages, une bande
// horizontale se parcourt a l'oeil quand une colonne se parcourt a
// l'ascenseur.
//
// Les vignettes ne disparaissent pas : elles se deplient a la demande sous les
// onglets (126 px), et c'est la que les rapports viendront se poser comme
// folios calcules (voir 07-rapports-en-folios.md).
//
// Comme le ruban, cette barre ne detient rien : chaque onglet represente un
// folio du Document et demande le changement, il ne le fait pas.
#pragma once

#include <QWidget>

class QTabBar;

namespace dsn {

class Document;
class FolioNavigator;

class FolioTabs : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kTabHeight = 28;
    static constexpr int kStripHeight = 126;

    explicit FolioTabs(Document *document, QWidget *parent = nullptr);

    void refresh();
    bool stripVisible() const { return m_stripVisible; }
    void setStripVisible(bool visible);

Q_SIGNALS:
    void folioChosen(const QString &folioId);
    void addFolioRequested();
    void pageSetupRequested();

private:
    Document *m_document = nullptr;
    QTabBar *m_tabs = nullptr;
    FolioNavigator *m_strip = nullptr;   // les vignettes, repliees par defaut
    bool m_stripVisible = false;
};

} // namespace dsn
```

Chaque onglet porte le numéro en chasse fixe puis le titre : `1  Circuit de puissance`.
Le numéro au troisième niveau d'encre, le titre au premier — deux niveaux, comme une case de
cartouche.

L'onglet actif se distingue par un **filet d'accent de 2 px au bord du canevas** (donc en
haut) et le plan `surface` : le même motif que l'onglet de ruban actif et la bascule d'état en
marche. Trois endroits, un seul motif à apprendre.

`FolioNavigator` est conservé tel quel et devient le contenu de la bande dépliée : ses
vignettes sont le vrai rendu du folio, ce qui reste le bon choix et vaut d'autant plus une
fois qu'elles s'accordent avec le canevas (étape 01).

Dans `mainwindow.cpp` : retirer le dock des folios, poser `FolioTabs` dans le conteneur
vertical du canevas, sous `FolioView`. Garder les commandes `NOUVFOLIO` / `FOLIOSUIVANT` /
`FOLIOPRECEDENT` (`NF` / `FS` / `FP`) inchangées — elles pilotent maintenant la barre.

## 5.2 — La palette, seule dans sa colonne

Hauteur disponible sur un écran 1080 : `1080 − 114 (ruban) − 32 (menus) − 62 (commande) − 30
(état)` = **842 px** pour le rail entier.

Répartition :

| Bloc | Hauteur | Note |
|---|---|---|
| bandeau de zone `SYMBOLES` + chevron | 18 | filet en bas |
| recherche + bascule grille/liste | 36 | |
| combo de catégorie | 34 | **avec chevron visible** |
| bandeau `RÉCENTS` + compte | 16 | |
| bande des récents | 44 | une rangée de 4 |
| bandeau `TOUS` + `103` | 16 | |
| **la grille** | **~594** | **13 rangées de 4 · ≈ 52 vignettes** |
| case de sélection | 84 | nom, norme, nombre de broches |

**52 symboles visibles au lieu de 24.** La grille passe de cellules de 52 px à **44 px** et le
glyphe monte de 26 à 28 px : moins d'air, symbole plus lisible.

### Les récents, qui existent déjà dans le code

`symbolpalette.h` a `recentCategory()` et `noteUsed()`, et rien ne les montre. Sur cent trois
symboles dont on en repose dix toute la journée, c'est le raccourci le plus rentable du
logiciel.

```cpp
    // Les recents, en bande permanente au-dessus de la grille — pas comme une
    // categorie parmi les autres. `noteUsed` les alimente deja : un symbole
    // POSE passe en tete, un symbole seulement selectionne ne compte pas.
    QListWidget *m_recent = nullptr;   // une rangee, 44 px, meme delegue que la grille
```

Quatre visibles, huit gardés (le cinquième au huitième apparaissent quand la fenêtre est plus
large). La bande se masque quand la liste est vide, et **ne repousse pas** la grille : elle
occupe 60 px, récupérés sur la grille.

### La combo de catégorie

Dans la capture, « Toutes les catégories » ne montre aucun chevron : ça ressemble à un titre,
pas à un contrôle. Poser le chevron et le plan `elevated` — c'est un contrôle, il doit le dire.

Et la remplir avec la **norme du projet** en tête : `Commande CEI` plutôt que `Toutes les
catégories`, qui est vrai mais n'apprend rien.

### La case de sélection

En bas du rail, sur le plan `window`, en case de cartouche :

```
SÉLECTION
Contact NO, bobine de contacteur
CEI 60617          2 broches
```

Répond à la question que la grille pose forcément — *quelle variante ai-je ?* — et qui coûte
aujourd'hui un survol. `symbolpalette.h` insiste sur le fait qu'un électricien reconnaît un
contact NF d'un NO à sa forme ; la case ne remplace pas le dessin, elle le confirme.

## 5.3 — `DockRail`

Le rail garde la languette de la palette. Comme la ligne de commande perd aussi sa barre de
titre (étape 04), le rail devient **le seul** chemin de retour visible pour deux panneaux : ce
qui est exactement sa raison d'être telle que `dockrail.h` la documente.

Vérifier que `watch()` reçoit un `hint` utile pour chacun — le rail enseigne le clavier.

## 5.4 — `theme.cpp`

```css
/* La grille de symboles : aucun cadre, un survol en aplat leger, et surtout
   AUCUN aplat d'accent sur la selection — un filet suffit (regle 3). */
QListWidget[symbolGrid="true"] {
    background: %SURFACE%;
    border: none;
    outline: none;
}
QListWidget[symbolGrid="true"]::item { border-radius: 3px; }
QListWidget[symbolGrid="true"]::item:hover { background: %HOVER%; }
QListWidget[symbolGrid="true"]::item:selected {
    background: transparent;
    border: 1px solid %ACCENT%;
}
```

## Critères d'acceptation

1. `SymbolPalette::visibleCount()` ≥ 48 sur un rail de 200 × 842 px, catégorie « toutes ».
   Le test existant qui mesure ce compte est le bon endroit pour verrouiller le gain.
2. La bande RÉCENTS apparaît après la première pose et se remplit par `noteUsed()`.
3. Les folios occupent 28 px ; la bande de vignettes se déplie à 126 px et se replie.
4. Le folio actif porte un filet d'accent de 2 px au bord du canevas, et **aucun aplat**.
5. `NF` / `FS` / `FP` fonctionnent et la barre suit.
6. Le rail montre une languette pour la palette **et** pour la ligne de commande, chacune avec
   son indice de clavier.
7. La vignette d'un folio et la feuille au canevas ont la même couleur de papier (dépend de
   l'étape 01 — c'est le contrôle croisé des deux étapes).

**Maquette de référence :** `design/Arcus - fenetre principale v2.dc.html` pour le rail et les
onglets ; `design/Arcus - rapports en folios v2.dc.html` pour la bande de vignettes dépliée.
