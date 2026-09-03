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
QT_QPA_PLATFORM=offscreen ./build/bin/arcus_ui_tests "[essai]"   # le logiciel conduit à la main
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
- **Cotations** (`DimensionItem`) — voir « Le bloc C », plus bas. La règle en
  une ligne : **une cote mesure, elle ne récite pas**.
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
- **Un rail sur le bord garde la flèche** (`ui/dockrail.*`, décision
  utilisateur, 2026-09-02 : *« la flèche doit rester pour pouvoir la
  rouvrir ? »*). Le chevron qui tasse un panneau vit dans **sa** barre de
  titre : il part avec lui, et le seul retour devenait un menu ou un
  raccourci — rien à l'endroit même où le panneau venait de disparaître. Le
  rail est une bande de 24 px collée au bord du canevas : **rien tant que
  tout est ouvert** (c'est ce qui permet de le laisser en place), un onglet
  par panneau tassé — chevron tourné vers le dessin, nom gravé à la
  verticale. Le nom compte autant que le chevron : deux panneaux se tassent
  dans la même colonne, et deux chevrons identiques ne disent pas lequel
  ramène la palette. L'onglet **demande**, il n'ouvre pas : tout passe par
  `MainWindow::setDockVisible`, seul endroit qui repose la largeur retenue —
  deux chemins finiraient par diverger. Seuls les panneaux de la colonne de
  gauche y prennent un onglet ; les rapports partent fermés à dessein, et un
  onglet permanent pour eux encombrerait un bord que personne n'a fermé.
- **La ligne de commande n'en est qu'une** — un utilisateur a signalé « deux
  fois une ligne de commande ». C'était l'historique et le champ, l'un
  au-dessus de l'autre, tous deux **stylés comme des champs de saisie** et
  portant le **même conseil**. L'historique est maintenant du texte posé
  (`[commandHistory="true"]` : ni fond, ni bordure, ni coins arrondis), il
  part vide — le champ porte déjà l'invite — et il **se replie tant qu'il n'a
  rien à dire**, puis grandit jusqu'à trois lignes.
- **La ligne de commande conduit le geste** (bloc A4). Trois règles, et
  aucune exception :
  1. **L'invite est déduite de l'état, jamais poussée.**
     `FolioView::currentPrompt()` la calcule depuis `m_typing`, `m_pending`,
     les modes armés et l'outil ; `refreshPrompt()` la signale depuis
     `paintEvent`, seul point par lequel tout changement passe — chacun
     appelle `update()`. Il y avait **soixante-quinze** endroits qui
     poussaient une invite dans la barre d'état : pousser depuis chacun
     revient toujours à en oublier un, et **une invite qui survit au geste
     qu'elle décrit est pire que pas d'invite**. Vingt de ces envois ont
     disparu, remplacés par une fonction qu'on lit d'un bout à l'autre pour
     savoir ce que le logiciel dit.
  2. **La barre d'état ne porte plus de dialogue.** Elle garde ses états
     permanents — coordonnées, zone, zoom, sélection, bascules. Tout compte
     rendu passe par `MainWindow::report`, donc par l'historique de la ligne
     de commande, qui le **garde** : un message qui s'efface au bout de six
     secondes dans le coin bas de la fenêtre n'est pas lu, et quand il l'est,
     il est déjà parti. Seul le repli du bandeau fait retomber le message
     dans la barre d'état — le critère est `isHidden()`, pas `isVisible()` :
     un panneau d'une fenêtre pas encore affichée n'est pas replié.
  3. **Le ruban et les menus écrivent aussi dans la ligne de commande**
     (`echoMenuCommands`). Elle devient le journal de la séance quel que soit
     le chemin pris, et **un bouton cliqué enseigne le nom à taper**. Les
     bascules s'y écrivent par leur nom court (`ORTHO : activé`), celui
     qu'AutoCAD écrit.
  L'invite porte l'**accent** (`QLabel[commandPrompt="true"]`) : c'est la
  réserve d'usage de la règle 3 du thème, puisque rien d'autre ne désigne ce
  qui est actif pendant un geste. Le cartouche sous le curseur, pendant une
  désignation, porte le même accent et est **rabattu dans la vue** — près
  d'un bord il se coupait, or c'est au bord qu'on désigne le dernier fil.
- **Un seul abandon** (`FolioView::abandonGesture`). Échap et le clic droit
  avaient chacun leur code, et ils ne couvraient pas les mêmes états : Échap
  laissait le panoramique armé, quittait le zoom fenêtre sans un mot et
  gardait la continuation d'une désignation abandonnée. L'abandon défait
  **une couche à la fois** — la cote, puis le geste, puis le tracé, puis
  l'outil, puis la sélection — pour qu'une frappe de trop ne rende pas
  l'outil par-dessus le marché, et **il dit ce qu'il vient de défaire**.
  Le clic droit s'arrête à l'outil (`includeSelection = false`) : aller
  jusqu'à la sélection viderait ce sur quoi le menu contextuel allait porter.
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
- **Neuf menus** : Fichier, Édition, **Modification**, **Dessin**, Outils,
  Affichage, Projet, Symboles, Aide. Modification est le groupe « Modifier »
  du ruban d'AutoCAD ; il était dilué dans Édition, où il voisinait le
  presse-papiers. Dessin porte les outils de tracé et le style de trait.
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
taille suivrait le zoom serait illisible une fois sur deux. Elle est rabattue
dans la vue : près d'un bord elle se coupait, or c'est au bord qu'on désigne
le dernier fil.

**4. La ligne de commande conduit le geste.** Le second chiffre du diagnostic
— 75 invites vers la barre d'état contre 5 vers la ligne de commande — est
traité à sa racine : l'invite n'est plus poussée du tout, elle se **déduit**
de l'état (`FolioView::currentPrompt`), et la barre d'état ne porte plus une
seule phrase. Le détail est dans « Interface », plus haut ; la règle tient en
une ligne : **un seul endroit où le logiciel parle, et il y parle toujours**.

## L'essai de reproduction (2026-09-02)

L'utilisateur a demandé : *« teste le logiciel toi-même pour te rendre compte
de la fluidité »* et a fourni la photo d'un vrai schéma — le raccordement de
deux appareils Valmet à un automate PLC152, armoire 047BJ0152B. Il a été
refait **entièrement par événements de souris et de clavier**, via
`tests/test_essai.cpp` : la palette au clavier, les clics sur le canevas, la
ligne de commande, les boîtes modales. Rien n'y court-circuite l'interface.

**Ce que cela mesure.** 132 entités, **557 gestes**, **0 boîte modale**,
0 fil en biais, 40 constats d'audit dont aucune erreur. Les boîtes modales
étaient 48 au premier passage — une par texte et par étiquette ; c'est le
chiffre qui a déclenché la saisie sur place, décrite plus bas. Ces chiffres
sont le résultat de l'essai, pas le succès du test.

**Réserve, et elle compte.** Cet essai conduit les mêmes widgets par les mêmes
événements qu'une main, ce qui trouve de vrais défauts — quatre ci-dessous.
Il ne dit rien de ce qu'on ressent : la latence, la fatigue, le geste qu'on
refait dix fois. Seul un dessinateur devant l'écran le dira.

### Les quatre défauts trouvés, et corrigés

1. **On ne pouvait pas tracer un cadre en pointillé.** `Primitive` portait une
   épaisseur mais pas de style de trait — or le contour d'une armoire, d'un
   coffret, d'une boîte de jonction se trace en pointillé sur tous les schémas
   industriels : c'est une convention de lecture, et sans elle **l'enveloppe
   se confond avec le circuit**. Le schéma photographié en porte trois.
   `Primitive::Stroke` (continu, pointillé, fin, mixte) traverse maintenant le
   peintre, le fichier et l'export DXF (table LTYPE déclarée, sans quoi
   AutoCAD rendrait le trait plein sans rien dire). Le motif est imposé **en
   millimètres** et non en multiples de l'épaisseur comme Qt le fait par
   défaut : un cadre au trait fin et un autre au trait épais doivent porter le
   même pointillé. Le réglage suit la mécanique du type de fil — armé pour ce
   qu'on va tracer, appliqué tout de suite à ce qui est désigné. Le test
   compte **l'encre déposée** : c'est la seule mesure qui distingue un vrai
   pointillé d'un trait plein ou d'un trait invisible.
2. **Le numéro d'une borne ne s'affichait pas.** `fields["terminal"]` était
   écrit par l'éditeur de borniers et imprimé par le rapport de câblage —
   et **jamais dessiné**. Le plan et le rapport se contredisaient : un câbleur
   lisait « X1:4 » sur sa feuille et ne trouvait sur le schéma qu'une borne
   anonyme. Pire, l'audit réclamait un numéro que le dessin était incapable de
   montrer : les quinze constats « Borne sans numéro » de l'essai portaient
   sur des bornes que rien ne permettait de numéroter visiblement. Il se lit
   maintenant du côté de la valeur — une borne n'a pas de calibre à montrer —
   et suit le même interrupteur que le repère, parce que c'est son identité.
3. **La résolution s'appliquait à l'annotation.** Un sommet de fil tombe sur
   le pas de 2,5 mm : c'est ce qui aligne un schéma tout seul. Un texte, non —
   le forcer sur ce pas **interdit d'écrire deux lignes de 1,7 mm l'une sous
   l'autre**, ce que le renvoi d'une voie d'automate demande à chaque ligne.
   Dans le premier jet, `%R07S04C005` et `IDM PLC152 12 (CH5)` se
   superposaient. `FolioView::snapAnnotation` garde l'accrochage aux **objets**
   — une étiquette doit toujours pouvoir se poser au bout d'un fil — et laisse
   tomber la grille. La règle : **la résolution tient le courant, pas
   l'annotation**.
4. **Il manquait le relais d'interface.** `iec:interface-relay` : bobine
   A1/A2 **et** contact 11/14 dans un seul bloc, avec la liaison mécanique en
   pointillé fin. C'est le composant d'une armoire de marshalling — le relais
   embrochable qu'on tient dans la main, avec les quatre bornes de son
   étiquette. Le dessiner en bobine + contact séparés est juste en CEI mais
   oblige à les relier par un groupe d'appareil pour qu'ils partagent un
   repère : deux symboles, deux poses, une boîte modale.

### La saisie de texte sur place, et le piège qu'elle a découvert

**48 boîtes modales pour un folio** était le chiffre le plus laid de l'essai :
une par texte, une par étiquette. La boîte coupe le dessin en deux, s'ouvre au
milieu de l'écran loin du point visé, et cache justement l'endroit où le texte
va se poser. On tape désormais **où l'on a cliqué, à la taille réelle, sur le
dessin** — c'est le DTEXT d'AutoCAD, et c'est le seul moyen de voir si le
texte tient dans la place avant de le valider. Entrée valide, Échap annule.
La hauteur est retenue d'un texte au suivant (série ISO 3098 : 1,8 · 2,5 ·
3,5 · 5 mm) et s'applique à la sélection, sinon il faudrait retaper le texte.
Compteur : **48 → 0**, et 27 gestes de moins pour le même dessin.

**Et cela a découvert le piège que la boîte modale cachait.** Les outils
portent des raccourcis d'une lettre à **portée application** (S, W, L, T, O…).
La boîte les bloquait ; en tapant sur le dessin, chaque lettre qui est un
raccourci partait comme commande au lieu d'entrer dans le texte :

```
   « BJ RM5 »            →  « 5 »
   « VALMET »            →  « V »
   « POWER SUPPLY »      →  « O U »
   « RELAIS OMRON 24 V » →  « I MRON 24 V »   + la boîte Décaler qui s'ouvre
```

Qt a une réponse exacte : **`QEvent::ShortcutOverride`**. Un widget qui
l'accepte reçoit la touche comme une frappe ordinaire au lieu de la laisser
déclencher le raccourci. `FolioView::event` l'accepte pendant une saisie —
texte ou cote — et **seulement pour les touches sans Ctrl ni Alt**, pour que
Ctrl+Z et Ctrl+S continuent de marcher pendant qu'on écrit.

Le test de non-régression a d'abord été **écrit faux** : il passait avec le
correctif désactivé. Il manquait un `QApplication::processEvents()` après
`show()` — sans lui la fenêtre n'est pas active, une portée application ne
déclenche rien, et le test ne prouvait rien. **Un test de raccourci doit
échouer sans le correctif ; le vérifier est la seule façon de le savoir.**

### Le dernier symbole reste sous la main

Changer d'outil désarme le symbole : poser un texte au milieu d'une série de
bornes obligeait à retourner dans la palette, chercher la borne et la
reprendre. C'est **l'INSERT d'AutoCAD** qui a raison — il propose toujours le
dernier bloc inséré. Revenir à l'outil Symbole les mains vides reprend donc le
dernier posé, **avec son orientation** : l'avoir fait pivoter trois fois ne
doit pas être à refaire. La commande `INSERER` (alias `I`) fait de même, et ne
va ouvrir la palette que s'il n'y a rien à reprendre.

### Ce que l'essai a appris sans qu'on le corrige

- **Un symbole doit poser ses broches sur le module de 2,5 mm.** Le relais
  d'interface a d'abord été dessiné avec ses broches à ±4 mm : elles ne
  rencontraient jamais un sommet de fil accroché à la résolution, et chaque
  fil partait en biais sans qu'on comprenne pourquoi. La convention de tracé
  n'était énoncée que pour le module ; elle vaut d'abord pour les broches.
- **L'accrochage aux objets prime sur la grille, y compris quand on ne le veut
  pas.** Un clic à 2,5 mm d'une extrémité s'y pose ; un clic sur une longue
  colonne attrape son **milieu**. D'où une règle de tracé : **les dérivations
  d'abord, la colonne commune ensuite** — tracée en dernier, elle s'accroche
  aux extrémités déjà posées, ce qu'on voulait.
- **Rien ne dit « cet appareil ne porte pas de repère ».** Les contacts secs
  dessinés à l'intérieur du boîtier Valmet appartiennent à cet appareil et ne
  doivent pas recevoir un repère `-K` propre ; le repérage automatique leur en
  donne un. Un repère verrouillé vide ne verrouille rien (test dédié).
- **Lier un contact à sa bobine demande la boîte du composant**, une par
  contact. AutoCAD Electrical propose le parent au moment de l'insertion.

**Le dessin se régénère, il n'est pas versionné** — comme le projet d'exemple,
et pour la même raison : c'est une sortie, et le test en est la source.

```sh
QT_QPA_PLATFORM=offscreen ARCUS_ESSAI_CAPTURES=/tmp \
    ./build/bin/arcus_ui_tests "[valmet]"
```

Le `.arcus` et le PDF partent dans un répertoire temporaire ; les deux captures
vont où `ARCUS_ESSAI_CAPTURES` le dit, et nulle part si la variable est vide.
Le fichier se rouvre dans l'application avec ses cadres pointillés et son
relais d'interface : la définition engendrée voyage dedans.

## Le bloc B — un dossier, pas un folio (2026-09-02)

Un folio prouve qu'on sait dessiner une planche. Un **dossier** prouve qu'on
sait faire un projet, et c'est là que vit tout ce qu'une planche seule ne
touche jamais : les renvois d'un folio à l'autre, le repérage à l'échelle du
projet, le bornier, la nomenclature, le PDF multi-pages. Le bloc B est donc un
second essai conduit à la main, sur **deux folios reliés** — alimentation puis
commande — avec flèches de signal, bornier et moteur.

**Ce qui a marché sans rien toucher** : les renvois se calculent depuis le
dessin (`→ 2/B1`, `← 1/C4`) et pointent la bonne zone du bon folio ; le
repérage porte sur tout le projet ; nomenclature, liste de fils et liste de
bornes sortent ; le dossier traverse le fichier et sort en PDF multi-pages.
141 gestes, zéro boîte modale, zéro accroc.

**Le défaut qu'il a trouvé, et il est gros.** Trois bornes côte à côte
recevaient **-X1, -X2, -X3** : trois borniers d'une borne chacun. Sur un
dossier de soixante bornes, cela fait soixante borniers, et l'éditeur de
borniers devient inutilisable. La cause : le repérage traitait une borne comme
un appareil. **Une borne n'est pas un appareil — c'est une place dans un
bornier.** Deux règles, et elles ne se ressemblent pas :

1. **Le bornier est partagé.** Une borne qui en porte un le garde — même règle
   que le collage de circuit, qui la formulait déjà (« une borne garde son
   bornier et change de numéro »). Celles qui n'en ont pas rejoignent le
   bornier **du folio** : un folio correspond à une fonction, et ses bornes
   partent dans le même câble.
2. **Le bornier se remplit, il ne se renumérote pas.** Une borne qui porte
   déjà un numéro le garde ; une borne neuve prend le premier numéro **libre**.
   Renuméroter d'office un bornier déjà câblé est une faute, pas un service :
   le câbleur a le plan de l'an dernier dans les mains. Renuméroter reste
   possible, mais c'est le geste explicite du bouton « Renuméroter 1, 2, 3… »
   de l'éditeur de borniers.

**Et l'audit a immédiatement attrapé le corollaire** : pour lui, trois bornes
portant `-X1` étaient trois appareils se disputant un repère. Pour une borne,
l'identité c'est le bornier **et** le numéro ; le doublon, c'est deux fois
`-X1:4` — et celui-là est une vraie faute, le câbleur ne sait pas où visser.
C'est exactement le travail qu'on attend d'un audit : refuser un changement
qui se contredit lui-même.

**L'effet sur l'essai précédent** est la meilleure preuve que les trois
correctifs forment une chaîne : le folio Valmet passe de **40 constats
d'audit à 25**, les quinze « Borne sans numéro » ont disparu — les bornes sont
numérotées toutes seules, et le dessin les montre enfin.

## Les quatre défauts trouvés à l'usage réel (2026-09-02)

L'utilisateur a installé la v0.9.1 et s'en est servi. Ce qu'il a rapporté vaut
tous les essais automatiques du monde.

### 1. Les cadres barrés rouges — la bibliothèque du projet se vidait

Signalé trois fois, introuvable deux fois. Le correctif d'affichage — écrire
l'identifiant sous le cadre barré — a donné la réponse au troisième
signalement : `iec:opamp`, `iec:connector-plug`, `iec:meter-ammeter`. Des
symboles qui **existent**. Donc le projet ne les avait plus.

```cpp
m_document->newProject(m_document->project().library);   // ← la cause
```

La bibliothèque passée **est** celle du projet. `newProject` appelle
`Project::clear()`, qui la vide, puis l'affecte à elle-même : du vide sur du
vide. **104 symboles → 0.** La palette, elle, garde les vignettes déjà
construites et continue d'offrir cent trois symboles qui n'existent plus ;
tout ce qu'on pose ensuite est un cadre barré.

Le geste qui le déclenche : écran d'accueil → *Nouveau projet* → poser un
symbole. C'est pourquoi aucun test ne l'avait vu — aucun ne repartait d'un
projet neuf.

**Le correctif est la signature** : `newProject` prend la bibliothèque **par
valeur**. La copie est faite à l'appel, donc avant le `clear()` : l'aliasing
devient impossible à écrire. Un commentaire dans le `.h` dit pourquoi, sinon
quelqu'un « optimisera » en remettant une référence const.

### 2. Un panneau tassé ne revenait pas

Le chevron cache le panneau, la commande d'affichage doit le ramener. Elle le
rendait visible mais **large de zéro pixel** : Qt restitue un dock caché avec
la largeur qu'il avait au moment où on l'a caché, et le canevas avait pris
toute la place. `MainWindow::setDockVisible` retient donc la largeur **avant**
de cacher et la repose au retour. Les deux chemins — chevron et commande —
passent par là, sinon l'un des deux oublierait.

**Réserve honnête** : introuvable en rendu hors écran, où Qt se rattrape tout
seul. Le test vérifie que le panneau revient avec de la largeur ; il passait
déjà avant le correctif sur cette plateforme.

### 3. La grille disparaissait au lieu de s'espacer

*« Il manque des carreaux, de la résolution. »* Une grille dont les marques
tombent à trois pixels l'une de l'autre n'est plus une grille, c'est un voile
gris — et le garde-fou de densité l'abandonnait alors **entièrement**, ce qui
est le pire des trois cas. `FolioPainter::displayGridStep` double le pas
jusqu'à ce qu'il respire (7 px au moins).

Deux décisions dans cette fonction :

- **On double, on ne multiplie pas par un facteur quelconque.** Chaque marque
  tracée reste ainsi sur le pas nominal, donc sur un point d'accrochage : la
  grille ne montre jamais un point où l'on ne peut pas se poser. Un facteur
  1,5 mettrait une marque sur deux entre deux points.
- **On ne subdivise pas** sous le pas de la résolution en zoom avant : ce
  serait montrer des points où l'accrochage ne se pose pas.

La fonction est **pure et publique** pour qu'un test lise la règle au lieu de
compter des pixels. La première version du test comptait des pixels et
passait sans le correctif — elle ne prouvait rien. **Deuxième fois dans ce
projet qu'un test est écrit faux et rattrapé en le désactivant exprès.**

### 4. Une cote peut porter son unité

*« Dans AutoCAD nous pouvons écrire 10 m et il sera à l'échelle comparé à
3 cm. »* Le dessin se compte en millimètres, mais on ne pense pas un chemin
de câbles en millimètres. `10m` vaut 10 000, `3cm` vaut 30, `2"` vaut 50,8 ;
sans suffixe c'est le millimètre — le cas courant ne doit rien coûter à
écrire. L'ordre du tableau d'unités compte : **`mm` avant `m`**, sinon
« 10mm » se lirait « 10 m ».

## Le bloc C — les commandes qui manquaient (2026-09-03)

Le relevé « ce qu'un dessinateur venu d'AutoCAD cherchera et ne trouvera pas »
n'était pas une liste de détails d'interface : c'étaient **cinq commandes
absentes**. Le bloc C les écrit.

### C1 — Effacer un composant referme le fil

Symétrique exact de l'insertion : poser un appareil de passage sur un fil le
coupe et le rebranche, l'enlever doit **recoudre**. Sans cela, effacer un
contact laisse deux fils qui pointent vers du vide — le dessin paraît juste et
le circuit est ouvert ; l'erreur ne se voit qu'au câblage.

`ComponentTools::healOnRemoval`, quatre décisions :

1. **La recouture est calculée avant la suppression**, sur la géométrie encore
   en place — après, les broches n'existent plus et on ne sait plus où les
   fils tenaient. Elle entre dans **la même macro** : une seule annulation.
2. **Deux extrémités, deux fils.** À une seule, rien ne s'ouvre en partant ; à
   trois, il y a un nœud, et recoudre reviendrait à choisir à la place du
   dessinateur.
3. **Deux types de fils différents ne se recousent pas** — même règle que
   JOINDRE : souder ferait disparaître une couleur, donc une section, sans que
   rien ne le dise.
4. **Le survivant est celui qui porte le plus d'identité** : repère verrouillé
   d'abord, puis repère tout court. Le fil recousu est le même conducteur
   qu'avant la pose de l'appareil ; il doit en garder le nom. Et les sommets
   devenus inutiles sont retirés — un coude fantôme se verrait au premier
   déplacement.

Effacer un départ entier (appareil **et** ses fils) ne recoud rien : `alsoRemoved`
porte ce qui part dans le même geste.

### C2 — Remplacer un symbole posé

Un contact NO devient un contact NF, un disjoncteur change de calibre. Sans ce
geste il faut effacer, reposer, retaper le repère et refaire les fils — et
c'est en refaisant les fils qu'on débranche un circuit sans le voir.
`ComponentTools::planSwap` + `FolioView::swapSymbol`.

- **Trois choses survivent**, et ce sont les trois qu'on perdrait à la main :
  le **repère** et les champs, la **position** avec son orientation, les
  **raccordements**.
- **Les broches s'apparient par NUMÉRO d'abord** — un 13/14 reste un 13/14
  quel que soit le dessin du symbole. C'est le seul appariement qui ait un
  sens électrique ; la distance n'est qu'un secours. Apparier par distance
  raccorderait la borne 1 sur la 2 sans que rien ne le dise.
- **Une extrémité qu'aucune broche neuve ne reprend est comptée et dite.** Un
  fil en l'air ne se voit pas sur le tracé, il se découvre au câblage.
- Pas de `componentPlaced` : ce signal ouvre la boîte du composant quand le
  réglage l'exige, et remplacer n'est pas poser.

### C3 — Rechercher / remplacer dans tout le dossier

`rules/findreplace.*` et `ui/findreplacedialog.*`. L'affaire change de numéro,
un moteur change de repère d'atelier : cela se lit dans quarante endroits
répartis sur douze folios, et à la main on en oublie toujours un — c'est
celui-là qui part à l'atelier.

- **Tout constat porte un lieu** (folio, zone), comme dans l'audit, et la
  boîte montre *actuel → deviendrait* : remplacer à l'aveugle dans un dossier
  entier est un geste qu'on n'ose pas faire, donc qu'on ne fait pas.
- **Un double-clic saute sur place** — la boîte est aussi une recherche.
- **Tout tient dans une annulation.** C'est ce qui rend le geste sans risque.
- **« Mot entier » existe** parce que remplacer `KM1` par `KM7` transforme
  `KM10` en `KM70` : le dossier devient faux d'un clic et rien ne le montre.
  Le motif est **échappé** — on cherche du texte, pas une expression
  régulière.
- **Un repère remplacé est VERROUILLÉ.** Sans cela la prochaine régénération
  le recalcule et le remplacement disparaît sans un mot — le pire résultat,
  puisqu'on l'a vu marcher.
- Les gisements sont séparés (repères, champs, textes, étiquettes, repères de
  fil) : renommer les « M1 » ne doit pas toucher la phrase « alimentation M1 »
  d'un cartouche si on ne l'a pas demandé.
- Portée : la question d'AutoCAD, par le même `ReportScope` que les rapports.

### C4 — Les cotations

`DimensionItem`, septième type d'entité. Un schéma d'armoire porte des cotes —
entraxe de deux rails, hauteur d'un jeu de barres, encombrement d'un coffret.
Sans elles on écrit « 150 » à côté d'un trait et plus rien ne garantit que le
trait mesure 150.

1. **La cote est MESURÉE, jamais saisie.** La valeur se déduit des deux points
   d'attache ; déplacer une attache change le nombre. C'est toute la
   différence avec un texte posé à côté, et c'est ce qui empêche un plan de
   mentir. `override` existe pour la rupture d'échelle et se voit comme ce
   qu'il est.
2. **La géométrie se calcule en UN endroit** (`DimensionItem::geometry`), dans
   le cœur. Le peintre et l'export DXF la lisent tous les deux ; la
   recalculer de chaque côté les ferait diverger d'un demi-millimètre, et cela
   se voit sur une flèche.
3. **Le décalage est donné par un point**, pas par une distance signée : le
   troisième clic pose la ligne où on la veut, des deux côtés, sans avoir à
   penser au signe.
4. **Alignée, horizontale, verticale.** Un schéma est fait de traits droits :
   coter l'entraxe horizontal de deux rails ne doit pas dépendre du fait qu'on
   a désigné deux points exactement à la même hauteur.
5. **Les deux premiers clics s'accrochent complètement, le troisième à l'œil.**
   La cote *mesure* le dessin : ses attaches doivent tomber exactement sur la
   géométrie. La ligne de cote, elle, ne fait que se placer — et il faut
   pouvoir la glisser de 1,2 mm pour qu'elle passe entre deux fils. C'est
   l'invariant 11 appliqué à un seul geste.
6. **Le texte se lit toujours dans le bon sens** : angle ramené dans
   `[-90, 90)`, donc une cote verticale se lit du bas vers le haut (ISO 129) —
   en tournant la planche d'un quart de tour à droite, jamais à gauche. Il est
   posé du côté « haut » du texte lui-même, quel que soit l'angle.
7. **Les flèches passent à l'extérieur quand la cote est trop courte** pour
   les contenir, et la ligne se prolonge pour les porter — sinon deux flèches
   se croisent au milieu d'une cote de 3 mm.
8. **On exporte le DESSIN de la cote en DXF, pas une entité DIMENSION.** Une
   DIMENSION dépend d'un DIMSTYLE ; un style mal repris et AutoCAD la
   redessine avec d'autres flèches, une autre hauteur, parfois une autre
   valeur — le dessin changerait en s'ouvrant, ce que « écran = papier »
   interdit. **Réserve honnête** : la cote exportée n'est plus modifiable
   comme une cote dans AutoCAD, seulement comme des traits.
9. **Elle se clique sur ses traits, pas dans la bande qu'elle mesure.** Sa
   boîte englobante couvre toute la distance cotée : la prendre pour cible
   avalerait les clics du dessin qu'elle mesure — exactement ce qu'on vise.
10. **On s'accroche à ses points d'attache, jamais à sa ligne de cote.** Trois
    entraxes alignés le long d'un rail se cotent à la suite ; s'accrocher à la
    ligne enchaînerait les cotes les unes sur les autres — on coterait la
    cotation.
11. **Ses trois points sont étirables un par un** (ÉTIRER) : c'est la
    démonstration du point 1, et le test la mesure.

### C5 — Une icône par commande, partout

Le relevé disait « une vingtaine de glyphes servent encore deux commandes ».
L'inventaire complet en a trouvé **dix-huit groupes en collision** : Quitter et
Supprimer, Pivoter et Repérage polaire, Jonction et Éditeur de borniers,
Annuler et Vue précédente… Vingt-neuf glyphes ont été dessinés ; il en reste
**trois**, et ils sont voulus — ce sont les mêmes commandes atteintes par deux
chemins (Cotation / Cote alignée, Étiquette / Étiquette de potentiel, Palette
de commandes / Toutes les commandes). Leur donner deux dessins serait pire que
le doublon : on croirait à deux commandes.

Un test (`[ui][icones][menus]`) parcourt désormais **tous** les menus et refuse
tout autre partage. Il a immédiatement trouvé un piège : `m_actionGlyphs`
réapplique le glyphe au changement de thème, si bien qu'une action construite
avec la bonne icône mais **enregistrée avec une autre** retrouvait l'icône
d'une voisine au premier basculement de thème — sans que rien ne le signale.

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
11. **La résolution tient le courant, pas l'annotation.** Un sommet de fil, un
    appareil, une jonction tombent sur le pas de la grille ; un texte se pose
    à l'œil (`FolioView::snapAnnotation`). L'accrochage aux **objets**, lui,
    vaut pour tout : une étiquette doit pouvoir se poser au bout d'un fil.
12. **Une cote mesure, elle ne récite pas.** La valeur d'une `DimensionItem`
    se déduit de ses deux points d'attache et n'est jamais stockée — comme la
    netlist et les renvois. `override` est le seul échappatoire, et il se
    déclare.
13. **Ce que le rapport imprime, le dessin le montre.** Un champ qu'une boîte
    de dialogue renseigne et qu'un rapport publie doit être lisible sur la
    planche — sinon le plan et le rapport se contredisent, et c'est le plan
    que le câbleur a en main (payé sur le numéro de borne).

## Bibliothèque de symboles

- Source de vérité : les JSON de `libraries/`, embarqués dans le binaire par
  ressource Qt. `tools/gen_builtin_library.py` n'a servi qu'à les amorcer —
  il reste utilisable, mais après régénération il faut recompiler (ressource).
- `logicalId` relie les variantes CEI/ANSI d'un même symbole ; `resolve()` se
  rabat sur l'autre norme si une variante manque.
- Convention de tracé : origine au centre, y vers le bas, module 2,5 mm,
  la broche dessine son propre trait (le graphisme ne dessine que le corps).
  **Les broches d'abord** : une broche hors du module ne rencontre jamais un
  sommet de fil accroché à la résolution, et chaque fil part en biais sans
  qu'on comprenne pourquoi (payé en dessinant `iec:interface-relay`).
- Une primitive porte un **style de trait** (`Primitive::Stroke`) : continu,
  pointillé, fin, mixte. Il sert au graphisme d'un symbole comme aux formes
  d'annotation — la liaison mécanique entre une bobine et son contact se
  dessine en pointillé fin, et le contour d'une armoire en pointillé.
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

**Les cinq sont faites — c'est le bloc C (2026-09-03), décrit plus haut.**
Elles restent listées ici parce que c'est le relevé d'origine :

1. ~~Rechercher / remplacer du texte dans tout le dossier.~~ → C3
2. ~~Remplacer un symbole posé sans perdre son repère ni ses
   raccordements.~~ → C2
3. ~~Effacer un composant en refermant le fil.~~ → C1
4. ~~Cotations.~~ → C4
5. ~~Une icône par commande, partout.~~ → C5

Ce qui manque encore, dans l'ordre où un dessinateur s'en apercevra :

1. **Cotations d'angle et de rayon.** Seule la cotation linéaire existe ; sur
   un schéma c'est l'essentiel, mais un plan d'implantation en demanderait
   d'autres.
2. **Lier un contact à sa bobine à l'insertion**, comme le propose AutoCAD
   Electrical — aujourd'hui il faut la boîte du composant, une par contact.
3. **Dire qu'un appareil ne porte pas de repère** : les contacts secs dessinés
   à l'intérieur d'un boîtier reçoivent un `-K` propre que rien ne demande.

## Prochaines étapes envisagées (dans l'ordre de valeur)

0. Reste du relevé AutoCAD (`docs/AUTOCAD.md`) : gestionnaire de projet
   multi-dossiers, configuration des colonnes de rapport, métadonnées de
   folio (Description 1/2/3) et rapport de liste de dessins.
1. Import DXF (l'export existe : `io/dxfexport.cpp`).
2. Unifilaires M8 : symboles de distribution + bilan de puissance
   (le modèle multi-conducteurs est prêt).
3. Électronique M9 : export netlist SPICE / KiCad.
4. AppImage Linux et .dmg macOS, quand le besoin se présentera.
