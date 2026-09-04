# Reprise de design — Arcus « la planche est la marque »

Paquet de reprise pour **plguy9/dessins**, branche `claude/electrical-drawing-software-urtd5k`,
état lu à l'arbre `fea0f92`. Application Qt 6 / C++20, widgets, interface en français.

---

## 1. Ce que contient ce paquet

| Fichier | Contenu |
|---|---|
| `README.md` | ce document — la direction, l'ordre des étapes, les jetons, les critères d'acceptation |
| `01-jetons.md` | `paper` / `ink` au premier rang, palette claire révisée, `RenderStyle` dérivé du thème |
| `02-barre-etat.md` | la barre d'état en grammaire de cartouche, et l'accent qui cesse de mentir |
| `03-ruban.md` | bandeau de zone, raccourcis imprimés, réglages sortis de la grille d'actions |
| `04-ligne-de-commande.md` | l'en-tête supprimé, l'invite au corps de l'interface, trois voix |
| `05-palette-et-folios.md` | la palette récupère la colonne, les folios deviennent des onglets de page |
| `06-boites-de-dialogue.md` | `ZoneBox` + `NumberField`, appliqués au compositeur de cartouche puis aux cinq autres |
| `07-rapports-en-folios.md` | les rapports deviennent des folios calculés — le seul chantier qui touche au modèle |
| `design/*.dc.html` | les maquettes HTML (**références visuelles**, voir §3) |

## 2. La direction, en une page

Le système visuel actuel est bien tenu — quatre plans, un accent, trois encres, un pas de
quatre — mais il ne dit rien de particulier. Un logiciel de CAO sombre à accent bleu, il en
existe cent.

La seule chose qui n'appartient qu'à Arcus est déjà à l'écran, et rien ne s'en sert comme
d'une identité : **la planche**. Le cadre, les bandeaux de zone, les repères de colonne et
de rangée, le cartouche et ses cases, la lettre de révision, les cotes au trait fin. Un
vocabulaire normé, centenaire, qui vient du métier et non d'un kit d'interface. Aucun
concurrent ne l'exploite : tous traitent le chrome comme du logiciel et la feuille comme du
contenu.

Aujourd'hui la marque d'Arcus vit dans le bandeau bleu de l'écran d'accueil — trois secondes
par session — et disparaît là où le dessinateur passe huit heures. C'est ce qu'on retourne.

### Les trois mouvements

1. **Un cinquième plan : le papier.** `paper` et `ink` remontent dans `ThemeColors` ;
   `RenderStyle` les reçoit au lieu de porter ses propres constantes. Vignette, canevas,
   aperçu et PDF cessent de se contredire.
2. **Le chrome parle la langue du dessin.** Les noms de panneau deviennent des bandeaux de
   zone ; la barre d'état prend la grammaire des cases de cartouche (libellé gravé + valeur
   en chasse fixe + filet de 1 px) ; le folio courant se lit comme une case, lettre de
   révision comprise.
3. **Le clavier s'imprime.** Le raccourci va sur le bouton, au troisième niveau d'encre,
   comme une cote sur une planche. C'est déjà la doctrine du dépôt — écrite dans
   `dockrail.h` — appliquée pour l'instant à un rail de 24 px.

### La règle 6, qui s'ajoute aux cinq existantes

> **Le papier est le seul blanc pur du logiciel.**
> Aucune surface du chrome n'atteint `#ffffff`, dans aucun thème.

Conséquence non évidente et c'est là tout l'intérêt : puisque le papier est blanc dans les
deux thèmes, **les couleurs d'encre sont les mêmes dans les deux thèmes**. `screenDark()`
cesse d'être une structure parallèle à maintenir et devient une préférence d'utilisateur.
`lightenDarkWires` n'a plus de raison d'exister.

### La discipline

« Épuré » ne veut pas dire moins de commandes. Arcus en a cent quatre-vingts et en aura
toujours. Ce qui doit baisser, c'est le nombre de choses qui réclament l'attention **en même
temps** : un accent qui ne mente jamais, trois niveaux d'encre réellement tenus, un seul
foyer visuel — la feuille.

## 3. Statut des fichiers de `design/`

Les `.dc.html` de `design/` sont des **références visuelles produites en HTML** : des
maquettes qui montrent l'intention, les valeurs exactes et le comportement attendu. **Ce
n'est pas du code à porter.** La cible est le code Qt 6 widgets existant, avec ses patrons
établis (`Theme`, `Icons`, `QToolButton`, feuille de style Qt, peinture `QPainter`).

**Fidélité : haute.** Les couleurs, corps, graisses, interlettrages, hauteurs de bande et
espacements sont définitifs et repris des jetons réels du dépôt. Les icônes des maquettes
sont des marques simplifiées, **pas** un jeu à porter : `Icons::Glyph` reste la source des
glyphes.

| Maquette | Ce qu'elle montre | Étapes concernées |
|---|---|---|
| `Arcus - audit et direction.dc.html` | l'audit chiffré et la direction | contexte |
| `Arcus - jetons v2.dc.html` | les cinq plans, la règle 6, le C++ révisé | 01 |
| `Arcus - fenetre principale v2.dc.html` | la fenêtre entière, thème sombre, 1920×1080 | 02 · 03 · 04 · 05 |
| `Arcus - theme clair v2.dc.html` | la même fenêtre, palette claire révisée | 01 |
| `Arcus - compositeur de cartouche v2.dc.html` | la boîte de dialogue patron | 06 |
| `Arcus - rapports en folios v2.dc.html` | le folio calculé, la bande de vignettes | 07 |

Ouvrir un `.dc.html` directement dans un navigateur suffit ; `design/support.js` et
`design/docs/`, `design/scraps/` sont les dépendances locales.

## 4. Ordre d'application

L'ordre n'est pas indifférent : chaque étape compile seule, et les premières rendent les
suivantes visibles.

| # | Étape | Portée | Risque | Gain visible |
|---|---|---|---|---|
| 01 | Jetons `paper` / `ink` | `theme.*`, `renderstyle.*`, `mainwindow.cpp` | faible | vignette et canevas s'accordent aussitôt |
| 02 | Barre d'état | `theme.cpp`, `mainwindow.cpp` | très faible | l'accent redevient une information |
| 03 | Ruban | `ribbon.*`, registre de commandes | moyen | le ruban devient lisible |
| 04 | Ligne de commande | `commandline.*`, `mainwindow.cpp` | faible | le logiciel se met à parler |
| 05 | Palette et folios | `symbolpalette.*`, `folionavigator.*`, `mainwindow.cpp` | moyen | 193 px rendus au dessin |
| 06 | Boîtes de dialogue | `zonebox.*` (nouveau), 6 dialogues | moyen | six écrans d'un coup |
| 07 | Rapports en folios | `core/folio.*`, `io/`, `rules/`, `ui/` | élevé — touche au modèle | le rapport devient un livrable |

**Recommandation :** 01 → 02 → 04 → 03 → 05 → 06, et 07 en dernier, dans son propre jalon.
01 et 02 se font en une séance et transforment déjà la perception de la fenêtre.

## 5. Les jetons, valeurs définitives

### Thème sombre (inchangé, plus deux champs)

| Jeton | Valeur | Rôle |
|---|---|---|
| `canvas` | `#0a0d0f` | le vide derrière la feuille |
| `window` | `#111518` | le chrome |
| `surface` | `#161b1f` | les panneaux |
| `elevated` | `#1e2429` | ce qui flotte, et le survol |
| **`paper`** | **`#ffffff`** | **la feuille — NOUVEAU** |
| **`ink`** | **`#151a18`** | **l'encre du tracé — NOUVEAU** |
| `border` | `#232a30` | le filet |
| `borderStrong` | `#39434a` | le filet appuyé |
| `text` | `#e9eef1` | l'encre qui porte |
| `textMuted` | `#87949c` | l'encre qui accompagne |
| `textFaint` | `#5c6870` | l'encre qui s'efface — gravures, raccourcis, libellés de cellule |
| `accent` | `#1fa6e8` | actif, invite, survol — et rien d'autre |
| `accentHover` | `#51bef3` | |
| `accentText` | `#04121a` | |
| `danger` | `#e86e60` | `writeError` |
| `success` | `#74c16a` | compte rendu réussi |
| `warning` | `#e3a942` | avertissement |

### Thème clair (révisé)

| Jeton | Avant | Après | Motif |
|---|---|---|---|
| `canvas` | `#dfe3e6` | `#c8cfd4` | le vide doit rester plus sombre que le chrome |
| `window` | `#f2f4f6` | `#e8ecef` | |
| `surface` | `#ffffff` | **`#f4f6f8`** | **un panneau ne peut pas être de la couleur d'une feuille** |
| `elevated` | `#ffffff` | **`#fdfdfe`** | **règle 6 : jamais `#ffffff` hors du papier** |
| `paper` | — | `#ffffff` | le même que le thème sombre |
| `ink` | — | `#151a18` | le même que le thème sombre |
| `border` | `#e1e6ea` | **`#cfd6db`** | **2 % d'écart de luminosité ne fait pas une séparation** |
| `borderStrong` | `#bfc8ce` | `#a8b3ba` | |
| `text` | `#0f1519` | `#101619` | |
| `textMuted` | `#5b6971` | `#56646c` | |
| `textFaint` | `#8a969d` | `#7f8c94` | |
| `accent` | `#0b76b8` | `#0b76b8` | inchangé |
| `accentHover` | `#0d8bd6` | `#0d8bd6` | inchangé |
| `accentText` | `#ffffff` | `#ffffff` | inchangé |
| `danger` | `#bf382c` | `#b23428` | |
| `success` | `#2f7736` | `#2c6f33` | |
| `warning` | `#9c6b11` | `#8f6210` | |

### Couleurs de dessin — **inchangées, et désormais valables dans les deux thèmes**

`wire #0a5c9e` · `symbol`/`text` = `ink #151a18` · `tag #1f6b2e` · `label #7a4a2b` ·
`dimension #5a6260` · `selection #e88b0b` · `snapMarker #c8d81e` · `snapGuide #9aa822` ·
`crosshair #7a8886` · `pinMarker #c05020` · `highlight #e84040`

### Espacement, rayons, fontes

- Espacement : `Theme::space(n) = 4n`. Aucun nombre magique.
- Rayon : `Theme::radius() = 6` pour les boutons et champs ; **0 pour tout ce qui relève de
  la planche** — bandeaux de zone, cases de barre d'état, filets. Une case de cartouche n'a
  pas de coin arrondi.
- `Theme::uiFont(10)` ≈ 13 px — le corps de l'interface, et désormais **l'invite de la ligne
  de commande**.
- `Theme::uiFont(9)` ≈ 12 px — ligne d'accompagnement.
- `Theme::monoFont(8)` ≈ 11 px — chemins, coordonnées, valeurs de cellule, raccourcis.
- `Theme::engravedFont()` ≈ 11 px, capitales, interlettrage ~0,16 em — **tous les libellés de
  bandeau de zone et de case**.

### Hauteurs de bande (définitives)

| Bande | Hauteur | Note |
|---|---|---|
| barre de menus | 32 px | les menus restent la source de vérité |
| onglets de ruban | 30 px | `Ribbon::kTabHeight`, inchangé |
| **bandeau de zone du ruban** | **18 px** | **nouveau — filet 1 px en haut et en bas** |
| boutons du ruban | 66 px | au lieu de 78, le nom étant monté au-dessus |
| réserve d'ascenseur | **0 px** | au lieu de 13 — l'ascenseur passe sous le ruban entier |
| onglets de folio | 28 px | au lieu de 193 px de colonne |
| bande de vignettes (dépliée) | 126 px | à la demande |
| ligne de commande | 62 px | 2 lignes d'historique + l'invite, sans en-tête |
| barre d'état | 30 px | |
| rail des symboles | 200 px de large | inchangé, mais seul |

## 6. Ce qui ne change pas — à ne pas rouvrir

Décisions utilisateur déjà tranchées, et invariants du dépôt :

- **Le ruban reste.** Cinq onglets, sept panneaux nommés. Une rangée d'icônes sans hiérarchie
  oblige à survoler chaque bouton.
- **Le ruban ne détient aucune commande.** Chaque bouton représente une `QAction` déjà posée
  dans un menu — c'est l'invariant que tient un test. Rien de ce paquet ne l'entame.
- **Pas de panneau de propriétés ancré.** Double-clic → boîte.
- **Pas de boîte modale à chaque pose de symbole.**
- **La palette de symboles est une grille**, pas une liste.
- **Chaque panneau garde son chevron de repli**, et `DockRail` garde la languette de retour.
- **`core/` ne connaît que `Qt6::Core`**, et chaque couche ne connaît que celles sous elle.
  Conséquence directe et importante : **`src/render/` ne peut pas inclure `src/ui/theme.h`.**
  L'étape 01 respecte cette contrainte par injection, pas par dépendance — voir `01-jetons.md`.
- **Tout en français**, boutons standards compris.
- **Le clavier prime.** Aucun geste de ce paquet n'exige la souris.

## 7. Critères d'acceptation, tous chantiers confondus

1. `grep -rn '#ffffff\|Qt::white' src/ui/` ne renvoie **aucune** couleur de chrome — seul le
   papier est blanc.
2. Le fond d'une vignette de folio, le fond de la feuille au canevas et le fond de la page du
   PDF sont **la même couleur**, dans les deux thèmes.
3. Dans une capture de la fenêtre, **exactement trois** éléments portent l'accent : l'onglet
   de ruban actif, les bascules d'état en marche, le folio courant. Plus l'invite quand une
   commande attend une saisie.
4. Aucun aplat d'accent dans la barre d'état.
5. Chaque petit bouton du ruban affiche son alias de commande, au troisième niveau d'encre.
   Les alias affichés sont **ceux du registre** (`mainwindow.cpp`), jamais inventés.
6. Aucune bande gravée ne dépasse 18 px ; aucun libellé gravé n'est en dessous du troisième
   niveau d'encre.
7. La suite de tests passe. `tests/test_render.cpp` et `tests/test_ui.cpp` demandent une
   revue à l'étape 01 (couleurs en dur) et 03 (hauteurs de ruban).
8. Le thème clair et le thème sombre passent les mêmes tests, avec les mêmes assertions de
   structure.

## 8. Contexte utile repris du dépôt

- `theme.h` — `ThemeColors` (15 champs), `Theme::space/gap/radius/uiFont/monoFont/engravedFont/engrave`,
  `Icons::Glyph` (~120 glyphes, unicité tenue par un test).
- `ribbon.h` — `kLargeIcon 32`, `kSmallIcon 20`, `kRowHeight 58`, `kPanelHeight 78`,
  `kTabHeight 30`, `kScrollAllowance 13`. Les hauteurs vivent là et nulle part ailleurs ;
  **la feuille de style ne doit déclarer aucun `min-height` sur les boutons du ruban** (piège
  déjà payé par la carte de l'écran d'accueil).
- `renderstyle.h` — `RenderStyle::screen() / print() / screenDark()`, ~20 couleurs,
  6 épaisseurs en millimètres.
- `commandline.h` — `setPrompt` (l'invite persiste tant que le geste dure), `write`,
  `writeError`, `fitHistory` (l'historique grandit jusqu'à trois lignes).
- `symbolpalette.h` — `recentCategory()`, `noteUsed()`, `setGridMode()`, `visibleCount()`.
- `dockrail.h` — `watch(dock, title, hint)` : « le rail enseigne le clavier, comme le ruban
  enseigne le nom des commandes ».
- `reportpanel.h` / `.cpp` — neuf tableaux : Récapitulatif, Nomenclature, Composants,
  Bornier, Fils, Câbles, Câblage De/Vers, E/S automate, Contrôles. Portée : Tout le projet /
  Folio actif. « Les tableaux sont recalculés depuis le document, jamais stockés. »
- `mainwindow.cpp:2727` — `style.pageBackground = Theme::colors().canvas;` : le seul pont
  entre les deux palettes dans tout le dépôt. L'étape 01 le remplace par une dérivation en
  règle.
