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
- **Fil multiple (bus)** — `WireTools::busPaths` et `BusSpec`. L1/L2/L3 se
  tracent d'un seul geste : la boîte arme le canevas, qui trace ensuite
  **comme un fil ordinaire** — le bus hérite ainsi d'ortho, des accrochages et
  de la cote tapée sans une ligne de code en plus. Trois décisions :
  **le décalage est un vrai parallèle** (`offsetPolyline` : chaque segment
  glisse le long de sa normale, les sommets sont les intersections des droites
  décalées) — translater la polyligne entière garderait la forme mais pas le
  parallélisme, et cela se voit au premier coude ; **le côté ne dépend pas du
  sens du tracé** (vers le bas d'une horizontale, vers la droite d'une
  verticale), sans quoi le même geste donnerait deux dessins ; et **chaque
  conducteur porte son nom**, ce qui fait que la netlist raccorde L1 à L1 et
  jamais à L2 — l'appariement par nom existait déjà dans `NetlistBuilder`,
  le bus ne fait que s'en servir. Le bus reste armé tant que l'outil Fil l'est,
  comme le type de fil, et tombe au changement d'outil.
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
- **ÉCHELLE (SCALE)** — `Placement::scale`, et `Entity::scale(base, facteur)`
  virtuelle à côté de `translate`. Chaque type sait ce que grossir veut dire
  pour lui : un symbole change de facteur de placement (ses **broches suivent
  donc toutes seules**, peintre et netlist passant par la même
  transformation), un fil déplace ses sommets, un texte grandit sa **hauteur
  de capitale** — pas son facteur de placement, sinon les deux se
  multiplieraient. Le facteur est **uniforme** : un symbole aplati n'est plus
  le symbole normalisé qu'un lecteur reconnaît.
  **L'épaisseur de trait ne suit pas l'échelle** (décision utilisateur,
  2026-09-02 : *« je ne veux pas épaissir les fils, juste des dimensions plus
  grosses »*). Grossir change les dimensions, pas la plume : un cercle deux
  fois plus grand reste tracé au même trait, comme sur une planche. Sur un
  schéma l'épaisseur porte en plus un sens — puissance ou commande — qu'un
  agrandissement n'a pas à modifier. Deux endroits le tiennent :
  `Primitive::scale` laisse `lineWidth` intact, et `FolioPainter::paintSymbol`
  divise ses stylos par `placement.scale`, puisque la transformation posée
  juste avant porte déjà ce facteur. Le test mesure **l'encre déposée** : en
  doublant un symbole elle doit doubler, pas quadrupler — sans le correctif le
  rapport valait 3,99. L'annulation restaure l'état
  figé plutôt que d'appliquer l'homothétie inverse — dix aller-retours ne
  doivent pas éloigner le symbole de sa taille.
- **`core/edittools.*`** — RÉSEAU (ARRAY, rectangulaire seulement),
  ALIGNER/RÉPARTIR, JOINDRE, COUPER. RÉPARTIR aligne les **centres**, pas les
  bords : des éléments de tailles différentes paraissent sinon mal espacés.
  JOINDRE refuse deux types de fils différents, sous peine de perdre une
  couleur en silence.
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
  utilisée d'AutoCAD Electrical. Annuler à l'insertion défait la pose : c'est
  pour cela qu'elle est ouverte juste après le `AddEntityCommand`, jamais
  avant. AutoCAD l'ouvre à chaque insertion ; **nous non** (décision
  utilisateur, 2026-09-02) : poser un symbole est le geste le plus répété de
  la journée, et une boîte modale à chaque pose le coupe en deux. Le
  double-clic ouvre la même boîte quand on veut vraiment régler quelque chose.
  Le réglage reste dans le menu, et le choix est retenu.
- **Le cadre barré d'un symbole introuvable porte son identifiant.** Il disait
  qu'il manquait un symbole, pas lequel — un dessinateur qui tombe dessus a
  besoin du nom pour savoir quelle bibliothèque rouvrir.
- **`DesignationRule::tagFormat`** — `%F%N` et ses variantes. Deux modes :
  séquentiel, ou basé sur la référence de ligne (`104K` = folio 1, colonne 4),
  où deux appareils au même endroit se départagent par une lettre. Le format
  vit dans le **projet** (`Project::designationFormat`), pas dans le profil :
  c'est une convention de bureau d'études, pas une norme. Il y est stocké en
  texte, parce que `core/` ne dépend pas de `rules/`.
- **`rules/catalog.*`** — le catalogue fabricant, embarqué par ressource
  comme les symboles et complété par les fichiers du poste.
- **`rules/plc.*`** — les automates. On choisit une carte dans la base des
  constructeurs, on donne son adresse de départ, et chaque point porte déjà
  son adresse : `I:3/00`, `%I0.0`, `%I0.2.5`. Trois décisions :
  1. **Un module posé est un `SymbolInstance` ordinaire.** Sa définition est
     engendrée à l'insertion et rangée dans la bibliothèque du **projet**,
     qui voyage dans le fichier. Il se déplace, se copie, s'annule et se
     câble comme le reste ; ni le peintre ni la netlist n'apprennent rien.
  2. **L'adresse n'est jamais stockée** — elle se recalcule depuis l'adresse
     de départ et le rang du point. Changer l'emplacement réadresse les
     seize points d'un coup, sans risque d'en oublier un.
  3. **Un seul compteur de points.** `%B` (octet) et `%b` (bit) sont des
     vues dérivées de `%P`, pas une seconde numérotation : c'est ce qui fait
     que Siemens groupe par 8 et Omron par 16 sans code séparé.
- **`rules/circuitcopy.*`** — copier un circuit, comme le *Copy Circuit*
  d'AutoCAD Electrical. Coller un départ moteur en gardant `KM1` fait un
  dessin juste et une nomenclature fausse, et l'erreur ne se voit qu'au
  câblage. Quatre décisions :
  1. **Le re-repérage se fait sur les copies, avant leur entrée dans le
     document.** Rien n'y entre en double, même le temps d'une commande : le
     collage reste une seule annulation et l'audit ne voit jamais l'état
     intermédiaire.
  2. **Le repère vient de `Numbering::designateNew`**, pas d'un incrément
     maison : même format, même ordre de lecture, même départage que la
     régénération globale. Elles ne diffèrent que par ce qu'elles s'interdisent
     de bousculer — la globale évite les repères verrouillés, celle du lot les
     évite tous.
  3. **Ce qui nomme un potentiel n'est pas re-repéré.** Huit départs se
     branchent tous sur L1/L2/L3 : renommer l'étiquette de la copie
     débrancherait le circuit qu'on vient de copier. Les flèches de signal font
     exception — deux sources du même code sont une faute — et une paire
     source/destination copiée ensemble reçoit **un seul** nouveau code.
  4. **Une borne garde son bornier et change de numéro.** Copier cinq bornes de
     X1, c'est cinq bornes de plus dans X1, jamais cinq borniers.
  Le repère de fil est **libéré**, pas recalculé : il dépend du potentiel, donc
  du dessin une fois les copies posées. `Coller à l'identique` (Ctrl+Maj+V)
  existe pour le geste inverse — déplacer un circuit d'un folio à l'autre, où
  l'appareil doit garder son identité.
- **`rules/audit.*`** — l'audit électrique. Deux règles : **tout constat
  porte un lieu** (folio, entité, zone du cadre) — un message qui dit
  « repère en double » sans dire lequel ni où coûte plus de temps qu'il n'en
  fait gagner ; et **rien n'est vérifié deux fois** — les constats que la
  netlist produit en se construisant sont repris tels quels, jamais
  recalculés. `humanize()` réaccentue au passage les messages du cœur, qui
  s'écrivent sans accents par convention.
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
- **`FolioView::setPendingSymbol` accepte un prototype** : ses champs et son
  repère sont recopiés sur l'instance posée. C'est ce qui permet à une boîte
  d'insertion de tout régler avant la pose, donc de tenir dans une seule
  annulation. Un prototype vaut pour **une** pose — deux cartes d'automate
  au même emplacement seraient une erreur, pas un raccourci.
- **Touches de fonction** : F3 accrochage objets, F7 grille, F8 ortho,
  F9 résolution, F10 polaire, F11 repérage d'accrochage, F12 paramètres.
  F1 la palette de commandes, F2 éditer le composant, F4 le Surfer. Portée application, parce
  qu'on lâche une touche de fonction sans regarder où est le curseur.

## Interface — le ruban, et ce qui l'entoure

**Décision révisée (utilisateur, 2026-09-02).** Ce guide disait « nous prenons
les deux bouts sans le ruban ». L'utilisateur a tranché l'inverse, capture
d'AutoCAD à l'appui : *« je trouve ça très mal organisé et pas du tout
adapté »*. Une rangée d'icônes sans hiérarchie oblige à survoler chaque bouton
pour retrouver une commande ; **un panneau nommé dit où chercher avant qu'on
ait cherché** — c'est la vraie force du ruban, et elle ne s'obtient pas
autrement. Nous gardons donc les deux bouts **et** le ruban.

- **`ui/ribbon.*`** — onglets (Accueil, Insertion, Annoter, Projet, Vue), et
  sous l'onglet actif une rangée de panneaux séparés par un filet, chacun
  portant son nom gravé dessous. Un ou deux gros boutons par panneau (icône
  32 px + libellé), le reste en grille de petites icônes sur deux rangées —
  la hauteur d'un panneau ne dépend donc pas de son contenu.
- **Le ruban ne détient aucune commande.** Chaque bouton représente une
  `QAction` déjà posée dans un menu : rien ne s'y connecte, ne s'y grise ni
  ne s'y coche, le bouton suit son action. **Les menus restent la source de
  vérité** — la palette de commandes se remplit en les parcourant, et une
  commande qui ne serait qu'au ruban serait introuvable à la palette. Un test
  (`[ui][ruban]`) le tient.
- **`m_menuBar` est retenu explicitement** : `setMenuWidget()` détache la
  barre de menus de QMainWindow, et `menuBar()` en fabrique ensuite une neuve
  et vide. La palette s'est retrouvée sans une seule commande — un test l'a
  rattrapé avant l'usage.
- **La barre d'accès rapide** (nouveau, ouvrir, enregistrer, imprimer,
  annuler, rétablir) est à gauche des onglets. Critère : les commandes qu'on
  appelle sans y penser, et qui ne doivent dépendre ni de l'onglet ouvert ni
  du repli du ruban. Sans elle, Annuler finissait en petite icône dans le
  dernier panneau — la seule vraie régression sur la barre d'outils.
- **Le clic sur l'onglet actif replie le ruban**, comme chez AutoCAD. Les
  onglets restent : replier ne doit pas le rendre introuvable.
- **Toute commande de menu porte une icône** (test `[ui][icones]`). Une action
  sans icône s'affiche en toutes lettres au milieu des icônes du ruban, et le
  panneau perd son alignement.

Et ce qui distingue toujours notre interface de celle d'AutoCAD :

- **Un bouton de repli sur chaque panneau** (`ui/docktitle.*`, décision
  utilisateur, 2026-09-02). La croix que Qt dessine sur un `QDockWidget` est
  minuscule et la feuille de style du thème l'efface : il n'y avait aucun
  moyen visible de rendre la place au dessin. Chaque panneau porte donc sa
  barre de titre — nom gravé, filet dessous, chevron de repli — et
  l'infobulle du bouton **dit le raccourci qui le ramène**. Les commandes
  d'affichage (Ctrl+3, Ctrl+4) sont devenues des **bascules cochées**, si
  bien que le ruban montre d'un coup d'œil ce qui est ouvert.
- **La ligne de commande n'en est qu'une** — un utilisateur a signalé « deux
  fois une ligne de commande ». C'était l'historique et le champ, l'un
  au-dessus de l'autre, tous deux **stylés comme des champs de saisie** et
  portant le **même conseil**. L'historique est maintenant du texte posé
  (`[commandHistory="true"]` : ni fond, ni bordure, ni coins arrondis), il
  part vide — le champ porte déjà l'invite — et il **se replie tant qu'il n'a
  rien à dire**, puis grandit jusqu'à trois lignes.
- **`ui/symbolpalette.*` — une grille de vignettes, pas une liste** (décision
  utilisateur, 2026-09-02 : *« plus discret, cela prend trop de place »*). En
  liste à une colonne, cinq symboles sur cent trois étaient visibles :
  chercher voulait dire faire défiler. La grille en montre vingt-quatre dans
  un panneau **plus étroit** (250 px au lieu de 320) — c'est l'*Icon Menu*
  d'AutoCAD Electrical, et c'est le bon modèle parce qu'on reconnaît un
  symbole à sa forme plus vite qu'à son nom. Trois conséquences : le nom passe
  dans l'infobulle (« Disjoncteur magnétothermique tripolaire » tronqué à huit
  caractères n'apprend rien et double la hauteur de chaque case) ; la
  recherche et la catégorie tiennent sur une seule ligne ; la liste des noms
  reste disponible d'un bouton, et le choix est retenu.
  Une catégorie **Récemment utilisés** est alimentée par `noteUsed`, appelée
  sur `componentPlaced` — donc par ce qui est **réellement posé**, jamais par
  ce qui est seulement sélectionné dans la palette.
- **Pas de panneau de propriétés ancré** (décision utilisateur, 2026-09-02 :
  *« elles ne servent à rien et prennent trop de place »*). Le bandeau de
  droite occupait la fenêtre en permanence pour un réglage qu'on ne fait que
  par moments. `ui/propertiesdialog.*` porte le **même** `PropertiesPanel`
  dans une boîte, ouverte au double-clic — sur un appareil c'est la boîte du
  composant, sur un fil, un texte ou une étiquette c'est la fiche, et dans le
  vide ce sont les propriétés du folio. La boîte n'a **pas** de bouton
  Annuler : le panneau pousse chaque modification comme une commande dès la
  frappe, et c'est Ctrl+Z qui défait.
- **`ui/appearance.*`** — ce que le dessinateur règle pour son confort, rendu
  au prochain lancement : taille et couleur du réticule, carré de sélection,
  aspect de la grille (points, **carreaux**, croix), renfort tous les N pas,
  fond de la feuille et pourtour, ombre portée. Deux règles : **le thème
  fournit les défauts, le réglage explicite gagne** — d'où l'appel à
  `Appearance::load` en dernier dans `applyTheme`, sans quoi changer de thème
  effacerait les choix ; et **les couleurs sont retenues par thème, la
  géométrie ne l'est pas** — une teinte lisible sur fond noir ne l'est pas sur
  blanc, c'est aussi pourquoi AutoCAD garde un jeu de couleurs par fond.
  Le garde-fou de la grille compte des **marques**, pas des points : les
  carreaux coûtent la somme des deux directions, les points leur produit, et
  un seuil unique ferait disparaître des carreaux qu'on trace sans effort.
- **Le conseil du folio vide appartient à la feuille** (décision utilisateur,
  2026-09-02). Il était composé en pixels et ancré à la fenêtre : il se
  décalait dès qu'un panneau s'ouvrait, et surtout il paraissait énorme sur
  une feuille dézoomée et minuscule sur une feuille zoomée — *« je ne veux
  pas qu'il grossisse en zoomant dézoomant »*. Il est maintenant composé en
  **unités de dessin**, puis mis à l'échelle par le peintre : il se centre sur
  la feuille et en occupe une part fixe, `kHintSheetFraction` (un dixième de
  la hauteur, le chiffre demandé). C'est la seule constante à toucher pour le
  rendre plus ou moins discret ; tout le reste suit.
  Un seul garde-fou, qui ne joue jamais aux zooms de travail : zoomé très
  près, un dixième de la feuille déborde la fenêtre, et un conseil plus grand
  que la vue n'apprend rien. `FolioView::emptyHintRect()` expose sa géométrie
  pour qu'un test lise les deux promesses — centré, et à proportion constante
  sur trois zooms — au lieu de compter des pixels.
  **Réserve** : à un dixième de la hauteur, le texte descend à environ cinq
  pixels et n'est plus lisible ; c'est lisible vers un cinquième.
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
  devient illisible. **Un test le vérifie** en rendant chaque glyphe à 16 px
  et en comparant les images : la règle n'était tenue que par l'œil, et les
  trois exports (PDF, DXF, CSV) ont partagé le même dessin depuis le début.
  `Glyph::Count` ferme l'énumération pour que le test n'ait pas de liste à
  tenir à jour. Ce test dit que les glyphes sont **distincts**, pas qu'on ne
  les a pas **assignés** deux fois — et c'est l'assignation qui se voit. Un
  second test parcourt donc chaque panneau du ruban et refuse deux commandes
  qui y portent la même image ; il a trouvé cinq collisions que l'œil n'avait
  pas relevées.
- **Sept menus** : Fichier, Édition, **Modification**, Outils, Affichage,
  Projet, Symboles, Aide. Modification est le groupe « Modifier » du ruban
  d'AutoCAD ; il était dilué dans Édition, où il voisinait le presse-papiers.
- **Quatre barres d'outils sur deux rangées**, la seconde collée au canevas
  parce que c'est celle qui touche le dessin. Une rangée unique tenait à
  vingt commandes ; à quarante elle déborde et **Qt masque la fin sans rien
  dire** — un test (`[ui][theme]`) vérifie qu'aucun bouton n'est caché.
- **Aucun nom ni alias de commande en double** — un doublon ne casse rien
  visiblement : la seconde inscription masque la première et une commande
  devient injoignable. Un test le vérifie sur les 157 jetons.

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
- Qt ne propage à un enfant que **les attributs posés sur sa fonte**. Poser
  la fonte gravée sur un panneau et la fonte d'interface sur son contenu ne
  suffit pas : la mise en capitales, que `uiFont` ne mentionnait pas,
  continuait de descendre et toute la liste des symboles se lisait en
  majuscules. `uiFont()` remet donc capitales et espacement à plat
  explicitement.
- Un `font-weight` posé sur `QPushButton:default` rogne le texte : Qt
  calcule la taille du bouton dans son état normal. Le bouton par défaut se
  distingue par son aplat, pas par sa graisse.
- **Le padding d'une feuille de style mange l'icône d'un petit bouton.** La
  règle générale `QToolButton { padding: 6px 9px }` ne laissait que deux
  pixels de large à une icône de quatorze dans un bouton fixé à vingt : le
  bouton répondait au clic et restait invisible. C'est le même piège que
  `min-height`, payé une troisième fois — tout bouton de taille fixe a besoin
  de sa propre règle (`ribbonSmall`, `dockClose`…). Un test vérifie
  désormais que l'icône est **réellement encrée**, pas seulement présente :
  compter les pixels peints est la seule vérification qui l'aurait attrapé.

## Les commandes obéissent (bloc A, 2026-09-02)

Diagnostic d'usage de l'utilisateur : *« je ne peux même pas cliquer sur
l'outil couper un fil »*. Il n'était pas cassé — il **exigeait** une sélection
préalable et refusait sinon. Deux chiffres disaient le reste : **62 actions
sur 66 ne se grisaient jamais**, et **75 invites de geste partaient vers la
barre d'état contre 5 vers la ligne de commande**. Un bouton noir qui ne
répond pas est un bouton cassé, quoi que dise le code.

**1. La désignation à la demande** (`FolioView::requestSelection`) — le
`Select objects:` d'AutoCAD. Une commande démarre **toujours** ; si elle a
besoin d'objets, elle les demande. On clique ou on encadre, **Entrée** valide,
**Échap** abandonne. Quatre décisions :

- **Un filtre qui parle.** Cliquer un appareil quand la commande attend un fil
  répond « ce n'est pas un fil » au lieu d'ignorer — un clic ignoré passe pour
  un clic raté.
- **La fenêtre ajoute et filtre.** Encadrer un départ entier pour couper un
  fil ne doit pas embarquer les appareils.
- **La commande se rappelle elle-même** via la continuation : au second
  passage la sélection convient et elle suit son chemin normal. La
  continuation est prise **puis effacée avant** d'être appelée — sinon une
  commande qui redemande une désignation écraserait la sienne en plein appel.
- **Les deux sens restent vrais.** Si la sélection convient déjà, la commande
  part directement : c'est le geste de l'habitué, et le perdre serait un
  mauvais échange.

Neuf commandes réparées par ce seul mécanisme : Déplacer, Décaler, Échelle,
Couper, Glisser, Déplacer l'appareil, Surfer, Réseau, Copier les propriétés.

**2. Le grisage dit « impossible », jamais « rien de sélectionné »** (`Need`).
La nuance est tout le bloc : puisqu'une commande demande ses objets, l'absence
de sélection ne la grise pas. Ce qui la grise, c'est qu'il n'y ait rien dans
le folio sur quoi elle puisse porter — couper un fil dans un dossier sans un
seul fil. `make()` porte la condition, `applyNeeds()` l'applique, et le
rafraîchissement suit le **folio** et pas seulement la sélection (changer de
page peut rendre une commande possible sans qu'on ait rien touché).

**3. L'invite se lit sous le curseur**, en pixels et non en millimètres —
`paintPendingGesture` peint dans le repère du dessin, et une invite dont la
taille suivrait le zoom serait illisible une fois sur deux.

## Ce qui a été retiré, et pourquoi (bloc A, 2026-09-02)

Le logiciel avait 66 commandes de menu et une centaine d'entrées cliquables.
Un dessinateur en utilise dix par jour. **On a jeté avant de réparer**, sur un
seul critère : *personne ne s'en servira sur un schéma électrique* — jamais
« c'est cassé ».

- **Polygone régulier** — un hexagone à n côtés n'a aucun usage en schéma. Il
  était là parce qu'AutoCAD l'a.
- **Mesurer une surface** — une aire ne veut rien dire sur un schéma ; ce
  n'est pas un plan d'implantation. La mesure de distance reste (entraxes).
- **Réseau polaire** — répartir des copies en cercle : jamais sur un folio.
  `ArraySpec` n'a plus de `Kind`, et `ArrayPlacement` plus d'angle.
- **Grouper / Dégrouper** — double emploi avec `deviceGroup` (bobine + ses
  contacts), qui est le vrai concept du métier. Deux notions de « groupe »
  dans le même logiciel, c'est une confusion, pas une fonction. Le champ
  `Entity::m_group` a disparu du modèle ; un ancien fichier qui porte `group`
  voit le champ ignoré, comme n'importe quel champ inconnu.
- **Taille réelle (zoom 100 %)** — n'a de sens que sur un écran calibré en
  DPI ; sinon c'est un zoom arbitraire déguisé en certitude.

**Aucun test ne couvrait Grouper/Dégrouper** — la suite est restée verte à
leur suppression. C'est en soi le signe qu'on avait construit large et plat.

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

## Ce qu'un dessinateur venu d'AutoCAD cherchera et ne trouvera pas

Relevé lors de la revue du ruban, dans l'ordre où il s'en apercevra. Ce ne
sont pas des détails d'interface : ce sont des commandes qui manquent.

1. **Rechercher / remplacer du texte dans tout le dossier.**
2. **Remplacer un symbole posé** sans perdre son repère ni ses raccordements.
3. **Effacer un composant en refermant le fil** — l'insertion coupe et
   rebranche, la suppression devrait recoudre.
4. **Cotations.** Elles n'existent pas : ce n'est pas une case de ruban mais
   un type d'entité, un tracé, un export DXF et des accrochages.
5. **Une icône par commande, partout.** Le test `[ui][ruban][icones]` tient la
   règle **par panneau du ruban** — c'est là que deux icônes se lisent côte à
   côte. Ailleurs dans les menus, une vingtaine de glyphes servent encore deux
   commandes ; ce n'est pas ambigu à l'œil, mais ce n'est pas tenu.

## Prochaines étapes envisagées (dans l'ordre de valeur)

0. Reste du relevé AutoCAD (`docs/AUTOCAD.md`) : gestionnaire de projet
   multi-dossiers, configuration des colonnes de rapport, métadonnées de
   folio (Description 1/2/3) et rapport de liste de dessins.
1. Import DXF (l'export existe : `io/dxfexport.cpp`).
2. Unifilaires M8 : symboles de distribution + bilan de puissance
   (le modèle multi-conducteurs est prêt).
3. Électronique M9 : export netlist SPICE / KiCad.
4. AppImage Linux et .dmg macOS, quand le besoin se présentera.
