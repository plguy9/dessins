# Arcus — guide pour les sessions Claude

Logiciel de dessin électrique (Qt 6, C++20). Le brief d'architecture est dans
`docs/BRIEF.md`, le format de fichier dans `docs/FORMAT.md` — les lire avant
toute décision structurante.

## Compiler et tester

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure        # exige QT_QPA_PLATFORM=offscreen hors CI
./build/bin/arcus_sample examples               # vérification de bout en bout + projet d'exemple
QT_QPA_PLATFORM=offscreen ./build/bin/arcus --screenshot=/tmp/fenetre.png examples/demarrage-direct.arcus
```

Dépendances Debian/Ubuntu : `qt6-base-dev qt6-svg-dev libgl1-mesa-dev cmake
ninja-build zlib1g-dev catch2`. Sans Catch2, les tests sont désactivés sans
erreur. `-DARCUS_BUILD_GUI=OFF` compile le cœur seul.

## Architecture — la règle qui gouverne tout

Chaque couche ne connaît que celles situées sous elle :

```
core/  →  symbols/, rules/, render/  →  io/  →  ui/  →  app/
```

- `core/` ne référence que **Qt6::Core**. Ni QtGui ni QtWidgets — c'est ce qui
  le rend testable sans écran. QTransform et QUndoStack vivent dans QtGui :
  c'est pourquoi `Transform2D` et `CommandStack` sont écrits maison.
- `render/FolioPainter` est le **seul** endroit qui sait dessiner un schéma.
  L'écran, l'aperçu, le PDF et les vignettes passent tous par lui. Ne jamais
  dupliquer un trace ailleurs : c'est la garantie « écran = papier ».
- Le canevas (`ui/folioview.cpp`) est un QWidget, **pas** une QGraphicsView —
  conséquence directe du point précédent.

## Ce qui vient d'AutoCAD (et pourquoi)

L'utilisateur veut un outil qui ressemble à AutoCAD Electrical. Les emprunts
sont délibérés et documentés dans le code :

- **`core/snapengine.*`** — les onze modes d'accrochage (OSNAP) avec leurs
  marqueurs normatifs : carré = extrémité, triangle = milieu, cercle =
  centre, cercle barré = nodal, losange = quadrant, croix = intersection.
  Les formes sont aussi normatives que les symboles.
- **Priorité d'accrochage** : un point du dessin prime sur le repérage
  d'alignement, qui prime sur la contrainte d'angle, qui prime sur la
  grille. Le classement mêle distance et importance du mode.
- **Repérage d'accrochage** (OTRACK, F11) — survoler un point un instant le
  **retient** ; des traits pointillés en partent et le curseur s'y pose.
  Le cas qui compte est le croisement d'un alignement avec la direction
  contrainte du tracé : « à l'aplomb du milieu de ce fil, sur ma
  horizontale ». Un croisement désigne un point, une projection seulement
  une direction : le croisement gagne même un peu plus loin du curseur.
  On n'acquiert que pendant une commande, et les repères sont relâchés à
  la fin — sinon des alignements sans rapport survivraient au geste.
- **`core/wiretools.*`** — AJUSTER (TRIM) et PROLONGER (EXTEND). Attention :
  un obstacle colinéaire ne « croise » rien géométriquement, or c'est le cas
  courant (prolonger jusqu'à une borne alignée) — traité à part.
- **`rules/ladder.*`** — l'échelle de commande d'AutoCAD Electrical.
- **`ui/commandline.*`** — la ligne de commande et ses alias (L, E, CP, Z…).
- **`core/wiretype.*`** — le *Wire Type Manager* : couleur, section, calque
  d'export et style de trait, réglés une fois pour tout le projet. Un fil
  référence un identifiant de type, jamais une couleur — changer la couleur
  d'un potentiel se fait alors en un endroit.
- **DÉCALER (OFFSET) et DÉPLACER (MOVE)** : `FolioView::beginOffset` et
  `beginMoveSelection`. Ce sont des gestes en deux clics, comme la ligne de
  commande d'AutoCAD ; l'état vit dans `FolioView::Pending`.
- **ÉTIRER (STRETCH)** : `StretchEntitiesCommand`. Les sommets pris dans la
  fenêtre de capture sont figés **à la construction** de la commande ; les
  recalculer à l'annulation les chercherait dans la géométrie déjà déplacée.
- **Flèches de signal** (`Label::Role`) — source et destination portent le
  même nom de code et deviennent un seul potentiel. Leur renvoi
  (« → 2/A3 ») est calculé par `rules/crossref.*` et **poussé dans le
  peintre**, jamais stocké : il se déduit du dessin, comme la netlist.
- **`rules/reportplacer.*`** — poser un rapport dans le dessin. La table est
  faite d'entités ordinaires, pas d'un type « table » à part : elle se
  déplace, se copie et s'annule comme le reste, et le peintre n'apprend rien.
  Les largeurs de colonnes viennent de `FolioPainter::textWidthMm` quand
  l'interface les mesure — `rules/` ne sait qu'estimer.
- **`core/componenttools.*`** — déplacer un appareil sans le débrancher. Les
  extrémités de fil posées sur ses broches suivent ; un fil qui **croise** une
  broche sans y finir ne suit pas. Scoot contraint le déplacement à l'axe des
  fils raccordés, et ne propose rien quand ils tirent dans des sens
  différents. Comme pour ÉTIRER, les sommets sont figés à la construction de
  la commande.
- **Insertion sur un fil** — poser un appareil de passage sur un fil le
  **coupe** et le rebranche sur ses bornes (`ComponentTools::splitForInsertion`).
  Il faut deux broches, posées sur le tracé et alignées avec lui : tout le
  reste se pose à côté du fil, pas dessus. Les morceaux héritent du repère,
  des conducteurs et du type — brancher ne fait pas perdre son identité au fil.
- **`ui/terminalstripdialog.*`** — l'éditeur de borniers. Il ne dessine rien :
  il modifie les repères de borne par commandes annulables, en une macro.
- **`ui/surferdialog.*`** — le Surfer d'AutoCAD : ce qui est lié à un élément
  **dans tout le dossier**, et le saut vers là-bas. Il travaille au projet,
  pas au folio — c'est tout son intérêt.
- **`ui/componentdialog.*`** — la boîte « Insérer/Éditer composant », la plus
  utilisée d'AutoCAD Electrical. Elle s'ouvre à la pose **et** au double-clic.
  Annuler à l'insertion défait la pose : c'est pour cela qu'elle est ouverte
  juste après le `AddEntityCommand`, jamais avant.
- **`DesignationRule::tagFormat`** — `%F%N` et ses variantes. Deux modes :
  séquentiel, ou basé sur la référence de ligne (`104K` = folio 1, colonne 4),
  où deux appareils au même endroit se départagent par une lettre. Le format
  vit dans le **projet** (`Project::designationFormat`), pas dans le profil :
  c'est une convention de bureau d'études, pas une norme. Il y est stocké en
  texte, parce que `core/` ne dépend pas de `rules/`.
- **`rules/catalog.*`** — le catalogue fabricant, embarqué par ressource
  comme les symboles et complété par les fichiers du poste.
- **`ReportScope`** — chaque rapport commence par la question d'AutoCAD :
  tout le projet, ou le folio actif. Un seul point de filtrage
  (`foliosInScope`), pour qu'aucun rapport ne puisse l'oublier.
- **Menu contextuel au clic droit**, sur le canevas et sur la liste des
  folios. Le clic droit désigne d'abord ce qu'il survole : sans cela la
  moitié du menu s'appliquerait à une autre sélection.
- **Sélection fenêtre (bleu plein) vs capture (vert pointillé)**, poignées
  bleues qui rougissent au survol, réticule pleine vue, saisie dynamique.
- **`core/coordinateentry.*`** — la saisie de cote au clavier : `50`,
  `50<45`, `@10,5`, `#120,80`. La virgule sépare les coordonnées (convention
  de toute la CAO), le point est décimal, le point-virgule est accepté comme
  séparateur pour qui tient à sa virgule décimale. Une distance seule n'a de
  sens qu'avec une direction visée — sans elle on ne pose rien plutôt que
  d'inventer un défaut.
- **Un point désigné passe par `FolioView::placeAt`**, que le clic ou la
  frappe l'ait produit : deux chemins finiraient par diverger.
- **Touches de fonction** : F3 accrochage objets, F7 grille, F8 ortho,
  F9 résolution, F10 polaire, F11 repérage d'accrochage, F12 paramètres.
  F1 la palette de commandes, F2 éditer le composant, F4 le Surfer. Portée application, parce
  qu'on lâche une touche de fonction sans regarder où est le curseur.

## Interface — ce qui la distingue d'AutoCAD

AutoCAD 2026 a supprimé ses barres d'outils au profit du ruban : plus
découvrable, mais lent et encombrant. Sa ligne de commande est l'inverse.
Nous prenons les deux bouts sans le ruban :

- **`ui/commandpalette.*`** (Ctrl+Maj+P, F1) — tout ce que le logiciel sait
  faire, cherché en français, avec le raccourci et le menu où la commande
  se trouve. Elle est **remplie à chaque ouverture** depuis les menus et la
  ligne de commande : une liste figée mentirait sur ce qui est activé.
  La recherche est floue mais **groupée** — sans cette contrainte, « bor »
  remonte « Basculer le mode ortho » et la liste se remplit de bruit.
- **`ui/startpage.*`** — l'écran d'accueil. Un logiciel de CAO qui ouvre sur
  une feuille blanche muette ne montre rien de ce qu'il sait faire.
- **Le folio vide enseigne** : quatre gestes avec leurs touches, pas un
  simple « c'est vide ».
- Les icônes sont dessinées à l'exécution (`Icons::Glyph`) : deux commandes
  différentes ne doivent jamais partager un glyphe, sinon la barre d'outils
  devient illisible.

## Le système visuel (`ui/theme.*`)

Tout l'habillage sort d'un seul fichier, et il tient en cinq règles. Elles
sont vérifiées par des tests (`[ui][theme]`) : ce ne sont pas des goûts.

1. **Quatre plans, dans cet ordre** — `canvas` (le vide derrière la feuille),
   `window` (le chrome), `surface` (les panneaux), `elevated` (ce qui flotte).
   Le fond du dessin est **plus profond que le chrome** : c'est ce qui fait
   flotter la feuille au lieu de la poser sur un gris étranger. Les vignettes
   de folios et les aperçus de symboles suivent le thème pour la même raison.
2. **Des filets, pas des boîtes** — un panneau se sépare par une ligne de
   1 px, jamais par un cadre. Ni les panneaux ancrables, ni les listes, ni
   les regroupements n'ont de bordure complète.
3. **Un seul accent, tenu en réserve** — il ne désigne que ce qui est actif
   ou sélectionné. Le seul aplat coloré du logiciel est le bandeau de l'écran
   d'accueil, parce qu'il ne contient aucune commande à lire.
4. **Trois niveaux d'encre** — `text` porte, `textMuted` accompagne,
   `textFaint` s'efface. Les étiquettes gravées (titres de panneaux,
   en-têtes, sections) sont au troisième : petites capitales espacées.
   `Theme::engravedFont()` porte la mise en capitales, car **Qt n'a pas de
   `text-transform` en feuille de style**.
5. **Un seul pas d'espacement** — `Theme::space(n)` = 4 n. `ArcusStyle`
   (un `QProxyStyle`) impose les marges de disposition par le style plutôt
   que boîte par boîte : une disposition qui ne demande rien respire pareil
   partout.

Chiffres et coordonnées passent par `Theme::monoFont()` : un nombre qui
change ne doit pas déplacer ses voisins.

Deux pièges de Qt, payés une fois :

- La feuille de style de l'application déclare un `min-height` pour les
  boutons, et Qt s'en sert pour **réécrire la taille minimale du widget** :
  un `setMinimumHeight` posé sur un bouton est effacé sans bruit. Passer par
  `sizeHint()` et une politique verticale `Fixed` (voir `ActionCard`).
- Un `font-weight` posé sur `QPushButton:default` rogne le texte : Qt
  calcule la taille du bouton dans son état normal. Le bouton par défaut se
  distingue par son aplat, pas par sa graisse.

## Invariants à ne pas casser

1. **Ce que l'utilisateur a saisi à la main n'est jamais écrasé** par un
   automatisme (`numberLocked`, `designationLocked`). Sur un potentiel, un
   repère verrouillé gouverne tout le potentiel.
2. **Toute modification du document passe par une commande** (`Document::push`).
   Rien ne modifie une entité directement depuis l'interface.
3. **Une commande restaure l'état dans l'entité existante** (`Entity::assign`),
   jamais en remplaçant l'objet : les panneaux détiennent des pointeurs.
4. **La netlist n'est jamais stockée** — recalculée depuis la géométrie,
   invalidée à chaque modification (`Document::invalidateNetlist`).
5. **Les coordonnées sont locales au folio.** Toute recherche spatiale doit
   être enfermée dans son folio (voir `NetlistBuilder::touchesNear`).
6. **Un fil porte n conducteurs** dès le modèle (`Wire::conductors`), n = 1 par
   défaut. Ne pas l'aplatir : c'est ce qui rend l'unifilaire possible.
   De même, il porte un **identifiant de type** (`Wire::wireType`), pas une
   couleur : un identifiant inconnu retombe sur le type par défaut, qui ne
   peut pas être supprimé — un fil ne doit jamais devenir invisible.
7. **Le repérage est reproductible** : relancé sur un dessin inchangé, il
   redonne exactement les mêmes repères (test dédié).
8. Un document d'une version **plus récente est refusé net** ; une entité
   inconnue dans un document lisible est **ignorée** sans bloquer l'ouverture.
9. **Ortho est allumé par défaut** : un schéma se trace en traits droits.
10. Une opération qui pose ou retire plusieurs entités (ajuster, échelle,
    coller, mise en page globale) passe par **une macro** : elle doit se
    défaire d'une seule annulation.

## Bibliothèque de symboles

- Source de vérité : les JSON de `libraries/`, embarqués dans le binaire par
  ressource Qt. `tools/gen_builtin_library.py` n'a servi qu'à les amorcer —
  il reste utilisable, mais après régénération il faut recompiler (ressource).
- `logicalId` relie les variantes CEI/ANSI d'un même symbole ; `resolve()` se
  rabat sur l'autre norme si une variante manque.
- Convention de tracé : origine au centre, y vers le bas, module 2,5 mm,
  la broche dessine son propre trait (le graphisme ne dessine que le corps).
- Angles d'arc : convention Qt — sens trigonométrique **visuel**, origine à
  3 h. Sous le renversement d'axe du DXF, les angles passent inchangés.

## Conventions

- Code et commentaires en français (sans accents dans les .cpp/.h du cœur,
  chaînes `tr()` accentuées côté interface). Messages de commit en français.
- L'interface n'existe **qu'en français** : `main.cpp` impose la locale
  française aux traductions de Qt, sans quoi un « Cancel » apparaît à côté
  d'un « Appliquer ». Le workflow de release déploie `--translations fr`.
- Ne jamais déclarer une classe Qt en avant **dans** `namespace dsn` : cela
  crée un type distinct et incomplet. La déclaration va hors du namespace.
- Tests Catch2 : noms de test en français décrivant le comportement, avec un
  commentaire disant *pourquoi* l'exigence existe.
- `clang-format` n'est pas encore configuré : imiter le style en place
  (4 espaces, ~100 colonnes, accolades K&R).
- Vérifier avant tout push : `cmake --build build && ctest --test-dir build`.

## Points ouverts (décisions utilisateur en attente)

Licence (libre ou propriétaire), reprise de fichiers existants (ferait
remonter l'import DXF), mono-poste ou partagé. Voir `docs/BRIEF.md`,
section « Questions ouvertes ».

Tranché : **OS prioritaire = Windows** (décision utilisateur, 2026-09-01).
La distribution se fait par la page GitHub Releases.

## Publier une version

`v0.1.0` est publiée (2026-09-01). Pour la suivante :

1. monter `VERSION` dans le `project()` du CMakeLists racine — le workflow
   refuse un tag qui ne lui correspond pas ;
2. déclencher `release.yml` manuellement avec l'entrée `publier = vX.Y.Z`
   (le proxy de session ne pousse que la branche de travail : c'est le
   jeton d'Actions qui crée le tag et la Release), ou pousser un tag `v*`
   depuis un poste qui en a le droit ;
3. le workflow compile MSVC + Qt 6.8, déploie avec `windeployqt`, déroule
   le zip dans un dossier vierge et prouve que `arcus.exe` y démarre,
   puis publie `arcus-vX.Y.Z-windows-x64.zip` en Release.
   Sans `publier`, le déclenchement manuel est un essai à blanc.

## Prochaines étapes envisagées (dans l'ordre de valeur)

0. Reste du relevé AutoCAD (`docs/AUTOCAD.md`) : gestionnaire de projet
   multi-dossiers, entrées-sorties API, éditeur de borniers, Scoot, Surfer.
1. Import DXF (l'export existe : `io/dxfexport.cpp`).
2. Unifilaires M8 : symboles de distribution + bilan de puissance
   (le modèle multi-conducteurs est prêt).
3. Électronique M9 : export netlist SPICE / KiCad.
4. AppImage Linux et .dmg macOS, quand le besoin se présentera.
