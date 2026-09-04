# `design/` — la refonte d'interface

Ce dossier est l'endroit où vivent les **maquettes et les étapes de refonte**
de l'interface d'Arcus, produites avec Claude Design. Il est lu par les
sessions Claude Code qui implémentent : les chemins cités dans les étapes
(`design/Arcus - fenetre principale v2.dc.html`) sont relatifs à la racine du
dépôt, donc les maquettes se déposent **ici, à la racine de `design/`**.

```
design/
├─ README.md                 ce fichier — la carte du dossier et les garde-fous
├─ contexte.md               à quoi sert le logiciel, qui s'en sert, ce qui est
│                            déjà tranché — le brief donné à Claude Design
├─ *.dc.html                 LES MAQUETTES (à déposer ici)
├─ etapes/                   LES ÉTAPES DE REFONTE (à déposer ici), numérotées
└─ etat-actuel/              ce à quoi l'interface ressemble AUJOURD'HUI
   ├─ *.png                  captures de l'application réelle
   └─ arcus-*.html           l'interface transposée en HTML, avec les vrais
                             jetons du thème — support de retouche visuelle
```

## Où déposer quoi

| Ce que vous avez | Où ça va |
|---|---|
| Une maquette `.dc.html` | `design/` (racine) — c'est le chemin que les étapes citent |
| Une étape de refonte `.md` | `design/etapes/`, préfixée de son numéro |
| Une capture de l'existant | `design/etat-actuel/` |
| Une image de référence (AutoCAD, planche…) | `design/references/` — à créer au besoin |

**Le dépôt est public.** Rien de ce qui identifie un client, un bureau
d'études ou une affaire ne doit entrer ici. Les captures présentes ne portent
que des repères déjà publiés dans `docs/BOUCLES.md` et dans les essais.

## L'état actuel, en un coup d'œil

| Fichier | Ce qu'il montre |
|---|---|
| `etat-actuel/accueil.png` | l'écran d'accueil |
| `etat-actuel/fenetre-schema-de-commande.png` | le plan de travail, schéma de commande dense |
| `etat-actuel/fenetre-schema-de-boucle.png` | le plan de travail, schéma de boucle |
| `etat-actuel/boite-cartouche.png` | une boîte de dialogue — la plus dense |
| `etat-actuel/panneau-rapports.png` | le panneau des rapports, ouvert |
| `etat-actuel/arcus-accueil.html` | l'accueil en HTML, jetons réels, deux thèmes |
| `etat-actuel/arcus-fenetre.html` | la fenêtre principale en HTML, idem |

Les deux HTML ne sont **pas** le code du logiciel : Arcus est une application
Qt 6 en C++ et son canevas est peint à la main. Ils reproduisent les couleurs
(`src/ui/theme.cpp`), les espacements, les hauteurs du ruban (`src/ui/ribbon.h`)
et tous les libellés, pour qu'une maquette parte du vrai. Leurs **icônes sont
des substituts** — dans le logiciel chaque commande a son propre dessin,
généré à l'exécution.

## Les garde-fous — ce qu'une refonte ne peut pas casser

Ils sont tenus par des tests. Une étape qui les contredit est à corriger
avant d'être implémentée, pas après.

1. **Les menus sont la source de vérité.** Le ruban ne détient aucune
   commande : chaque bouton représente une `QAction` déjà posée dans un menu.
   La palette de commandes se remplit en parcourant les menus — une commande
   qui ne serait qu'au ruban serait introuvable. Test : `[ui][ruban]`.
2. **Toute commande de menu porte une icône**, et deux commandes ne partagent
   jamais un glyphe. Tests : `[ui][icones]`, `[ui][icones][menus]`.
3. **Un seul accent, tenu en réserve** : il ne désigne que ce qui est actif ou
   sélectionné. Quatre plans de profondeur, des filets jamais des cadres,
   trois niveaux d'encre, un seul pas d'espacement (4 px × n).
   Tests : `[ui][theme]`.
4. **Aucun bouton de barre d'outils masqué** quand la fenêtre rétrécit — Qt
   masque la fin d'une rangée sans rien dire.
5. **Aucun `min-height` sur un bouton dans la feuille de style** : Qt s'en
   sert pour réécrire la taille minimale du widget, et un `setMinimumHeight`
   posé sur le bouton est effacé sans bruit. Piège payé trois fois, documenté
   dans `ribbon.h` et `theme.cpp`.
6. **Le clavier prime.** Chaque commande a un nom tapable ; les raccourcis
   d'une lettre et les touches de fonction sont à portée application.
7. **L'interface n'existe qu'en français**, boutons standards de Qt compris.

Le reste des décisions déjà prises — le ruban reste, pas de panneau de
propriétés ancré, pas de boîte modale à chaque pose, la palette en grille —
est dans `contexte.md` et dans `CLAUDE.md`.

## Avancement

| Étape | État |
|---|---|
| 01 — jetons `paper` / `ink` | **faite** (2026-09-04) |
| 02 — barre d'état | à faire |
| 04 — ligne de commande | à faire |
| 03 — ruban | à faire — voir la réserve ci-dessous |
| 05 — palette et folios | à faire |
| 06 — boîtes de dialogue | à faire |
| 07 — rapports en folios | jalon séparé |

Ordre conseillé par le paquet : 01 → 02 → 04 → 03 → 05 → 06, puis 07.

## Relu contre le code — l'étape « ruban »

L'étape 03, qui imprime l'alias de chaque commande dans le coin de son
bouton, a été relue contre le code avant d'être implémentée. Sa table d'alias
a été comparée au registre de commandes
(`src/ui/mainwindow.cpp`) : **57 alias sur 60 sont exacts**, et `RESOL` n'a
bien aucun alias. Mais l'étape dit d'afficher le **premier** alias de la
table, et pour trois commandes le premier alias du code n'est pas celui
annoncé :

| Commande | Alias annoncé | Alias réels, dans l'ordre du code |
|---|---|---|
| `RAPPORTS` | `BOM` | `NOMENCLATURE`, puis `BOM` |
| `COMPOSANT` | `EDC` | `CO2`, puis `EDC` |
| `SURFER` | `SF` | `SUR`, puis `SF` |

Pour `RAPPORTS`, appliquer la règle telle qu'elle est écrite imprimerait
**`NOMENCLATURE`** — douze caractères dans un bouton de 26 px, ce qui ruine
la prémisse même du mouvement (« l'alias fait deux caractères, il coûte zéro
pixel de large »).

Deux façons de s'en sortir, au choix :

- **Prendre l'alias le plus court** plutôt que le premier — règle mécanique,
  aucune table à tenir à jour, et elle donne `BOM`, `CO2`, `SUR` ;
- **Réordonner les trois alias dans le registre** pour que la forme courte
  vienne en premier, et garder la règle « le premier ». C'est un changement
  visible : le premier alias est aussi celui que la ligne de commande
  suggère.

L'étape écrit elle-même la bonne consigne — *« ne jamais inventer un
alias »* : c'est pour cela que l'écart est relevé ici plutôt que tranché tout
seul.

*(Vérification refaite à volonté : lire les appels `simple(...)` de
`src/ui/mainwindow.cpp` et comparer le premier alias de chaque commande.)*
