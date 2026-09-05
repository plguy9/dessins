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
  **coiffé de son nom** dans un bandeau de zone. Un ou deux gros boutons par
  panneau (icône 32 px + libellé), le reste en grille de petites icônes sur
  deux rangées — la hauteur d'un panneau ne dépend donc pas de son contenu.
  Voir « Le ruban imprime le clavier », plus bas.
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
  Le cartouche sous le curseur, pendant une désignation, porte l'accent et est
  **rabattu dans la vue** — près d'un bord il se coupait, or c'est au bord
  qu'on désigne le dernier fil.
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
  Un **troisième** compare les *silhouettes* plutôt que les dessins, panneau
  par panneau : deux glyphes différents peuvent rester indiscernables à 20 px,
  et c'est dans un panneau que l'œil compare. Une seule paire y échappe, et
  elle se déclare — « Trait continu » et « Trait pointillé » sont le **même
  objet dans deux états**, comme les marqueurs d'accrochage ; leur donner deux
  dessins ferait croire à deux commandes.
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
  devient injoignable. Un test le vérifie sur tous les jetons.
- **Une commande de menu doit avoir un nom à taper.** Deux essais l'ont
  trouvée en défaut (`SUPPRIMER` au bloc C, `FORMATREPERE` au bloc D) : le
  menu propose, la palette trouve, et la ligne de commande répond « commande
  inconnue ». Aucun test ne le tient encore — le lien entre un libellé de
  menu et un nom de commande n'est pas mécanique.

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
6. **Le papier est le seul blanc pur du logiciel** (refonte 2026-09-04,
   `design/etapes/01-jetons.md`). Aucune surface du chrome n'atteint `#ffffff`,
   dans aucun thème. C'est ce qui fait flotter la feuille en clair comme en
   sombre — un panneau de la couleur d'une feuille fait s'effondrer la
   hiérarchie au dernier centimètre, là où l'œil travaille.
   **Et c'est ce qui permet au dessin de n'avoir qu'UNE palette au lieu de
   deux** : puisque le papier est blanc des deux côtés, l'encre est la même
   des deux côtés. `paper` et `ink` sont donc des jetons du thème, au même
   rang que les quatre plans.

Chiffres et coordonnées passent par `Theme::monoFont()` : un nombre qui
change ne doit pas déplacer ses voisins.

**La barre d'état est le cartouche de la fenêtre** (refonte 2026-09-04,
`design/etapes/02-barre-etat.md`). Six bascules — `RESOL` `GRILLE` `ORTHO`
`POLAIRE` `ACCROBJ` `REPOBJ` — portaient un **aplat d'accent en permanence**,
dans le coin le plus visible de la fenêtre : la règle 3 dit que l'accent ne
désigne que ce qui est actif, et là il disait « actif » sans interruption,
donc il n'informait plus de rien. Elles portent maintenant le **même filet de
2 px** que l'onglet de ruban actif — un seul motif à apprendre pour deux
endroits — et rien d'autre. Cinq décisions :

1. **Une case porte une valeur, pas une phrase.** Libellé gravé à gauche,
   valeur en chasse fixe à droite, filet de 1 px entre deux cases : c'est la
   grammaire d'un cartouche. `zoom 58 %` devient `58 %`, `aucune sélection`
   devient `—`, et deux cases apparaissent qui manquaient — `FORMAT` et
   `FOLIO` — plus la case `RÉV` en creux, empruntée à la case *RÉV.* d'une
   planche.
2. **La chasse fixe ne suffit pas à empêcher une case de danser.**
   « 0,00 » et « -184,50 » n'ont pas le même nombre de caractères : chaque
   mouvement de souris poussait les voisines. Chaque case déclare donc son
   **gabarit** — la valeur la plus large qu'elle aura à porter — et la
   largeur est figée une fois pour toutes.
3. **La barre se replie plutôt que de se faire rogner.** Qt ne dit rien quand
   une barre d'état déborde : il coupe la fin, et `420x29` ment au lieu de
   manquer. `MainWindow::fitStatusBar` cache les cases dans un **ordre
   déclaré** — format, folio, zone, indice, zoom — jusqu'à ce que le reste
   tienne. La position, la sélection et les six bascules ne partent jamais :
   elles ne sont nulle part ailleurs sous les yeux pendant qu'on dessine.
   Mesuré : 1517 px pour tout montrer, 1560 px à l'ouverture ; rien n'est
   rogné jusqu'à 860 px.
4. **`FORMAT`, `FOLIO` et `RÉV` se lisent par `TitleBlock::values()`**, donc
   par le même chemin que le cartouche imprimé — invariant 15. Lire
   `ProjectInfo::revision` directement ferait mentir la barre dès qu'une
   planche porterait son propre indice, et c'est le plan que le câbleur a en
   main. Un test le tient, et il échoue quand on court-circuite le chemin.
5. **Le creux vient de la feuille de style, pas d'une QPalette.** La palette
   **fige** la couleur au moment où on la pose : la case aurait gardé le gris
   de l'autre thème au premier basculement — et c'est le seul élément peint
   de la barre, donc le seul qui puisse mentir sans qu'aucune valeur soit
   fausse. Piège au passage : **un QWidget nu ne peint aucun fond de feuille
   de style** tant qu'il n'a pas `Qt::WA_StyledBackground`.

Trois choses ont failli partir dans la refonte et sont restées : la **zone du
cadre** (le pas de bandeau du paquet de design ne la portait pas — c'est la
position dite dans la grammaire du cadre, et c'est ce qu'un renvoi imprime),
le **nom court** des bascules (`setDefaultAction` aurait posé le libellé de
menu — « Résolution — accrochage à la grille » — et doublé la largeur de la
barre ; le nom court est en plus **le nom à taper**, règle 3 de la ligne de
commande), et `setFocusPolicy(Qt::NoFocus)` sur les bascules (sans lui elles
entrent dans la chaîne de tabulation et volent le focus au canevas).

**Les six tests mesurent des pixels, pas des règles de feuille de style.**
Qt ignore **sans un mot** une déclaration qu'il ne comprend pas : le pas de
design écrivait `%ACCENT_HOVER%` là où le jeton s'appelle `%ACCENTHOVER%`, et
vérifier que la règle est présente aurait laissé passer la faute. Les six ont
été **contre-essayés** en désactivant leur correctif un par un — c'est la
seule façon de savoir qu'ils ne prouvent pas rien.

**La ligne de commande a quatre voix, et l'accent passe au filet** (refonte
2026-09-04, `design/etapes/04-ligne-de-commande.md`). C'est le seul endroit où
le logiciel parle, et il y parlait en 11 px gris sous un en-tête gravé
« LIGNE DE COMMANDE » qui dépensait une rangée entière à nommer l'évidence.
`success`, `warning` et `danger` étaient dans les jetons du thème **et ne se
voyaient nulle part** : un compte rendu d'automatisme avait la même encre
qu'une erreur, et il fallait lire la phrase entière pour savoir lequel était
lequel. Cinq décisions :

1. **Plus d'en-tête.** Un champ où l'on tape n'a pas besoin qu'on lui dise son
   nom. La rangée gagnée va à l'invite. Le chevron de repli part avec la barre
   de titre : **Ctrl+9 devient une bascule cochée**, comme Ctrl+3 et Ctrl+4, et
   c'est le seul chemin de retour. Le rail ne prend pas d'onglet ici — il est
   vertical et collé au bord **gauche** du canevas, et un bandeau du bas n'y a
   pas sa place.
2. **Quatre voix, plus la source.** `writeSource` dit **qui** parle — le nom de
   la commande, au troisième niveau d'encre, écrit par le ruban et les menus ;
   `write` le fil normal, `writeOk` ce qui a abouti, `writeWarning` ce qui
   mérite un regard, `writeError` ce qui a échoué. Le repérage, l'audit,
   l'enregistrement et l'export PDF sont branchés dessus : ce sont les
   automatismes dont on veut le compte rendu. « 2 saisies manuelles
   préservées » vaut d'être dit en vert — c'est l'invariant 1 rendu visible, et
   un dessinateur qui le voit cesse de se méfier de l'automatisme.
3. **L'accent passe de l'invite à un filet de 2 px.** Ce guide disait que
   l'invite portait l'accent, faute d'autre chose à désigner. Un filet le fait
   mieux : l'accent **désigne**, il ne colore pas une phrase entière — une
   phrase entièrement accentuée se lit moins bien et dépense la seule couleur
   du logiciel sur ce qu'un trait de deux pixels dit aussi bien. Le filet
   s'allume avec l'invite et s'éteint avec elle : il ne peut pas mentir.
   L'invite passe au **corps de l'interface** (`uiFont(10)`) — c'est une phrase
   adressée au dessinateur ; seule la saisie garde la chasse fixe, parce
   qu'elle porte des coordonnées.
4. **L'invite garde sa rangée, vide, quand rien n'attend.** Ce guide disait
   l'inverse — « une ligne vide en permanence prendrait la place sans rien
   dire ». C'était vrai quand le bandeau valait 108 px en dur. Depuis qu'il
   vaut sa taille naturelle, la masquer le fait **grandir de dix-neuf pixels au
   premier geste** (43 px mesurés au repos contre 62 avec l'invite) : la
   feuille recule au moment précis où l'on vise un point. Une rangée réservée
   qui ne dit rien coûte moins qu'un dessin qui saute sous le curseur.
5. **L'historique se retrace au changement de thème.** C'est le seul endroit du
   logiciel où une couleur du thème est **figée** au moment où on l'écrit — Qt
   ne range dans un document que des teintes, jamais des jetons. Chaque ligne
   garde donc sa *voix* à côté de son texte, et le basculement les redessine.

**Le défaut que ça a trouvé** : `setDockVisible` retenait `dock->width()` et le
reposait tel quel. Pour un panneau du **bas**, la dimension est la hauteur :
1560 px de largeur reposés comme hauteur ramenaient le bandeau plaqué au
plafond de 320 px. Il revenait — mais pas à sa taille, et en mangeant le tiers
du dessin. Le défaut était inatteignable tant que le bandeau ne pouvait pas se
fermer ; il l'est devenu à la ligne près où Ctrl+9 est devenu une bascule.

**Le ruban imprime le clavier** (refonte 2026-09-04,
`design/etapes/03-ruban.md`). L'onglet Accueil porte 47 commandes, dont **40
en icône de 20 px sans étiquette**. Le ruban n'était pas en cause — les icônes
muettes l'étaient. Le dépôt avait déjà un garde-fou qui refuse deux glyphes
**identiques**, mais l'unicité n'est pas la reconnaissance : deux dessins
différents restent indiscernables à 20 px, ce qui est exactement le cas des
huit icônes du panneau FILS. Étiqueter les petits boutons ne tient pas —
mesure faite, il faudrait **2 762 px** de ruban pour l'onglet Accueil et on en
a 1 720. Quatre décisions :

1. **Le bouton imprime son alias**, au troisième niveau d'encre, dans un coin
   de la case existante : **zéro pixel de large**, et `J` `RV` `ET` `PT` se
   distinguent même quand les glyphes se brouillent. Le petit bouton réserve
   sa bande du bas par le **rembourrage de la feuille de style** — le même
   mécanisme que le piège déjà payé trois fois, retourné à notre avantage — et
   l'alias se pose **centré sous l'icône** : calé à droite il paraissait
   appartenir à la case voisine. Mesuré : **78 boutons sur 100** portent leur
   marque.
2. **C'est le PREMIER alias qui tient**, dans l'ordre du registre. L'ordre
   porte l'intention — la forme française d'abord, celle d'AutoCAD ensuite :
   `DECALER` vaut `DC` puis `O`. Prendre le plus court trahirait ce choix et
   afficherait `O` pour Décaler, `M` pour Déplacer, `BR` pour Couper un fil.
   Et **ne rien imprimer vaut mieux qu'un jeton rogné** : « CONTRO » pour
   « CONTROLE » enseignerait un nom que la ligne de commande refuse. Un test
   vérifie que toute marque affichée se retrouve **telle quelle dans le
   registre** — l'inventer serait la seule faute qui viderait ce mouvement de
   sa valeur.
3. **Le nom du panneau monte au-dessus des boutons**, dans une bande filetée
   en haut et en bas, les filets verticaux la traversant : c'est le bandeau de
   zone d'une planche. Gravé dessous, il obligeait à balayer les icônes puis à
   lire pour savoir ce qu'on venait de survoler.
4. **Le réglage sort de la grille d'actions** (`RibbonPanel::addSetting`). Le
   sélecteur de type de fil est un **état**, pas un geste : posé au milieu de
   boutons d'action on le clique par erreur. Il est désormais précédé d'un
   filet, coiffé de `TYPE POSÉ`, et il montre sa valeur.

**Et le ruban a maigri : 121 px → 114.** Treize pixels étaient pris au dessin
**en permanence** pour une barre de défilement qui n'apparaît que sur une
fenêtre étroite. `Ribbon::scrollReserve()` la paie quand elle arrive, et pas
avant — ce qui n'était possible qu'une fois le nom gravé passé en haut, hors
de sa portée.

**Ce que ça a demandé, et la réserve qui va avec.** Rien ne reliait
mécaniquement une commande du registre à son action de menu : ni le libellé,
ni l'info-bulle — **mesure : 15 des 85 descriptions correspondent**. Le lien
est donc écrit à la main dans `kCommandBridge`, en un seul endroit, et un test
refuse toute ligne dont l'un des deux côtés a disparu. **Le bon dessin serait
que l'action déclare son nom de commande à sa naissance et que le registre se
remplisse en parcourant les menus** — comme le fait déjà la palette de
commandes ; c'est un remaniement des 85 enregistrements, plus large que cette
étape.

**Et un piège qui n'en était pas un.** On attendait que le rembourrage de la
feuille de style écrase l'icône du petit bouton, comme il avait écrasé celle
du bouton de repli. Il ne le fait pas : `setIconSize` **fixe** la taille de
l'icône, et Qt la dessine à cette taille quelle que soit la boîte de contenu.
Le test écrit pour le tenir passait avec le rembourrage porté à 20 px — il ne
prouvait rien, et il a été **retiré** plutôt qu'assoupli.

**`MainWindow::buildRenderStyle()` est le seul endroit qui construit un style
de rendu d'écran.** Sept endroits le faisaient chacun à sa façon — le canevas,
la vignette de folio, l'aperçu de mise en page, la palette, l'éditeur de
symboles — et ils ne tombaient pas d'accord : en thème clair, la vignette
peignait la feuille avec `print()` pendant que le canevas la peignait avec
`screen()`. La même page, deux couleurs, à quinze centimètres l'une de l'autre.

Deux conséquences à ne pas défaire :

- **`render/` ne connaît pas le thème.** `RenderStyle::applyTheme(paper, ink,
  vide)` reçoit les jetons par **injection depuis `ui/`** ; une dépendance
  montante casserait la règle de couches. C'est le seul point de cette refonte
  où une implémentation naïve casse l'architecture.
- **Le fond de dessin sombre est une préférence, pas une conséquence du
  thème** (`display/darkSheet`, décoché par défaut — décision utilisateur,
  2026-09-04). Les deux étaient liés : prendre une interface sombre imposait
  une feuille sombre, pendant que la vignette, l'aperçu et le PDF continuaient
  de montrer du papier blanc. **Basculer la préférence oublie la couleur de
  feuille épinglée** : `Appearance::save` l'écrit à chaque validation de la
  boîte de paramètres, même sans y toucher, et `load` passe en dernier — sans
  cet oubli, cocher la case n'aurait plus aucun effet visible dès la première
  visite dans les paramètres.
- **Une vignette de symbole n'a pas de papier sous elle** : elle est peinte à
  même le panneau, donc son encre suit le **chrome**, pas le papier. La faire
  suivre le papier vide la palette sans un message d'erreur — payé une fois.

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

### L'essai du bloc C

Les quatre commandes ont été conduites **à la main** (`tests/test_essai.cpp`,
`[essai][blocC]`) : ligne de commande, palette, clics, boîtes. 31 gestes,
**0 boîte modale**, 0 accroc — le contact effacé referme son fil, le symbole
remplacé garde son repère et ses raccordements, la recherche trouve ses
occurrences, la cote mesure 150 et traverse le fichier.

**Ce qu'il a trouvé.** Le menu et la palette de commandes disent
« Supprimer », et la commande s'appelait `EFFACER` : taper le mot qu'on vient
de lire répondait « commande inconnue ». C'est la règle 3 de la ligne de
commande — *un bouton cliqué enseigne le nom à taper* — prise en défaut.
`SUPPRIMER` est devenu un alias.

```sh
QT_QPA_PLATFORM=offscreen ./build/bin/arcus_ui_tests "[blocC]"
```

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
13. **Le peintre ne connaît aucun champ de cartouche par son nom.** Il pose
    des cases et demande leur valeur à `TitleBlock::values()`. Écrire un champ
    en dur ici rendrait le cartouche non modifiable, ce qui est le défaut
    qu'on vient de corriger.
14. **Tout champ ajouté à `Folio` doit être repris dans son constructeur de
    copie ET dans `operator=`.** Ils sont écrits à la main ; l'oublier perd le
    champ à la première copie, en silence (payé sur `Folio::tables`).
15. **Ce que le rapport imprime, le dessin le montre.** Un champ qu'une boîte
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

`v0.10.0` est publiée (2026-09-03) — elle porte les blocs C et D. Pour la
suivante :

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

## Le schéma de boucle — ce que trois planches réelles ont montré (2026-09-03)

L'utilisateur a fourni **trois planches de production** : deux d'un bureau
d'études (1999), une d'un autre (2025). Le relevé complet est dans
`docs/BOUCLES.md` — conventions seulement, aucune donnée client, et les
planches ne sont pas versionnées.

**Le constat qui compte.** Les trois sont des **schémas de boucle**
(diagrammes de connexions) : une boucle d'instrumentation par feuille, du
capteur au champ jusqu'à la carte d'automate. Arcus est bâti sur le modèle
d'AutoCAD **Electrical** — échelle de commande, contacteurs, bobines. Le
travail quotidien de l'utilisateur est un **autre document** : autre structure
de page, autre bibliothèque, autres rapports. Seize ans et deux bureaux
séparent la plus ancienne planche de la plus récente et la mise en page est la
même : c'est une convention de métier, pas l'habitude d'un dessinateur.

Rien n'est incompatible avec l'existant — fils, symboles, bornes, renvois et
netlist servent tels quels. **Ce qui manque est au-dessus** : la structure de
la planche et la bibliothèque.

Les cinq manques, dans l'ordre de valeur (bloc D proposé, `docs/BOUCLES.md`).
**Les cinq sont faits** — chacun a sa section plus bas. Ils restent listés ici
parce que c'est le relevé d'origine :

1. **Les bandes de localisation.** La feuille est coupée en bandes verticales
   nommées (`CHAMP` | `CABINET 037BJ0151`), chacune avec son bandeau. La bande
   est à un schéma de boucle ce que l'échelle est à un schéma de commande, et
   elle porte un sens : c'est la **localisation** de ce qu'elle contient, donc
   les colonnes « de » et « vers » du rapport de câblage. L'appartenance se
   **déduit de l'abscisse**, comme `Folio::zoneAt()` déduit la zone.
   Au passage : **leur repérage de cadre va de droite à gauche et de bas en
   haut** (zone A à droite, 1 en bas). Le sens doit devenir un réglage de
   profil — tous les renvois du dossier en dépendent. → D1
2. **Le cartouche est une structure de données, pas un dessin.** Trois tables
   qui grandissent (Cheminement, Références, Révisions — les lignes s'ajoutent
   vers le haut), un sceau et un logo (des **images**, que nous ne savons pas
   poser), et **quatre** lignes libres de description, pas trois. → D2
3. **La bibliothèque d'instrumentation** : la bulle ISA S5.1 et ses variantes
   normatives (cercle nu = au champ, barré = façade de panneau, dans un carré =
   système partagé), la borne à vis ⊘ avec sa borne de blindage, la boîte de
   jonction en cadre pointillé nommé, vanne + positionneur + I/P, manomètres,
   débitmètre. → D3
4. **Le câble n'est pas un fil.** `1PR#16CU`, `2PR#16CU`, `12PR#16CU` : n
   paires, calibre AWG, blindage. Notre `WireType` porte une section en mm² et
   rien de tout cela ; et la liste des **câbles** n'existe pas — or c'est le
   câble qu'on commande, pas le fil. → D4
5. **Le nommage.** Le repère est `secteur + fonction ISA + boucle + suffixe`
   (`022TT8917A`) — `tagFormat` ne sait pas le composer. Le conducteur porte
   **sa couleur en lettre** (`(N)` noir, `(B)` blanc…) : c'est le code qui
   s'imprime, et notre type de fil n'a qu'une teinte d'écran. L'adresse
   d'automate est `%N04R07S07C016` (Nœud/Rack/Slot/Canal) — `rules/plc.*` n'a
   pas la notion de **nœud**, pourtant écrite en toutes lettres à côté de
   chaque carte. → D5

### D1 — Les bandes de localisation (2026-09-03)

Une planche de schéma de boucle est coupée en **bandes verticales nommées** —
`CHAMP` | `BOÎTE DE JONCTION` | `CABINET 037BJ0151` — chacune coiffée de son
bandeau. La bande est à ce document ce que l'échelle de commande est à un
schéma de commande : la structure qui fait tenir la page.

Elle porte surtout un **sens** : c'est la **localisation** de ce qu'elle
contient. Un câbleur ne cherche pas la case du cadre, il cherche l'endroit où
aller visser — d'où les colonnes « De / Emplacement » et « Vers /
Emplacement » du rapport de câblage, qui n'apparaissent **que si le dossier a
des bandes** : les ajouter partout donnerait deux colonnes vides à tous les
schémas de commande.

Quatre décisions :

1. **L'appartenance se déduit de l'abscisse** (`Folio::bandAt`), exactement
   comme `zoneAt()` déduit la zone. Rien n'est stocké sur l'entité : déplacer
   un appareil d'une bande à l'autre change sa localisation, et c'est bien ce
   qu'on veut — sinon le rapport dirait « CHAMP » pour un appareil qu'on
   vient de tirer dans l'armoire.
2. **La dernière bande s'étire jusqu'au bord droit du cadre**, quelle que soit
   la largeur déclarée. Sans cela, changer de format de feuille laisserait une
   lisière sans nom, et une entité posée dedans n'aurait pas de localisation.
3. **Le sens du repérage du cadre est un réglage**
   (`SheetFrame::columnsRightToLeft`, `rowsBottomToTop`). Les trois planches
   relevées numérotent de droite à gauche et de bas en haut (zone A à droite,
   1 en bas) — et **tous les renvois du dossier en dépendent** : `zoneAt()` et
   `columnAt()` lisent les deux drapeaux, donc un renvoi, une référence de
   ligne et un repère de fil suivent sans une ligne de code en plus.
4. **Les bandes se saisissent en texte**, une par ligne, `nom = largeur`
   (`PageSetupDialog::parseBands`, publique et pure pour qu'un test lise la
   règle). Le **dernier** `=` sépare : une bande peut s'appeler
   « CABINET = ARMOIRE 3 ». Une ligne sans `=` garde son nom et une largeur
   par défaut — on tape d'abord les noms, on ajuste les largeurs ensuite.

### D2 — Le cartouche est une structure de données (2026-09-03)

**Décision utilisateur** : *« La cartouche doit être améliorée et modifiable.
Nous devons pouvoir créer une cartouche perso. »* Et sur la part des schémas de
boucle : *« Je dirais 50/50 et voir plus… AutoCAD font les deux, tu peux tout
faire ça. »* — donc les deux documents, pas l'un à la place de l'autre.

Le cartouche était dessiné en dur dans le peintre : trois bandes, six textes,
aucune prise. C'est pourtant ce qu'un bureau d'études regarde en premier, et le
seul endroit du dessin qui porte **son** identité.

`core/titleblock.*` — quatre décisions :

1. **Le gabarit vit dans le projet et voyage dans le fichier**, comme la
   bibliothèque de symboles et les types de fils. Un dossier rouvert ailleurs
   garde son cartouche, même si le poste ne connaît pas le gabarit du bureau
   qui l'a tiré. Vide = le gabarit standard : un ancien fichier se tire comme
   avant.
2. **Une cellule ne connaît qu'une CLEF, jamais une donnée.** Elle dit « écris
   ici la valeur de `client` » ; `TitleBlock::values()` sait où la prendre.
   **Le peintre ne connaît plus aucun champ par son nom** — sans cela, ajouter
   un champ demanderait de toucher le peintre, et le gabarit ne serait pas
   modifiable par l'utilisateur.
3. **Une clef inconnue écrit du vide, jamais son nom.** Un cartouche qui
   affiche « projectTitle » en toutes lettres part à l'impression sans que
   personne ne le remarque : c'est pire qu'une case vide (test dédié).
4. **Le gabarit porte sa taille, et le cadre la suit.** Changer de cartouche
   repose sa taille sur *chaque* folio, dans la même commande : les séparer
   laisserait un cartouche de 330 mm serré dans un cadre qui n'en réserve que
   180. Le peintre garde un facteur d'échelle **uniforme** comme garde-fou —
   il vaut 1 en usage normal, et n'existe que pour qu'un ancien fichier serre
   son cartouche au lieu de déborder sur le dessin.

**Les tables grandissent vers le haut.** L'intitulé des colonnes est en bas, la
révision 0 juste au-dessus, la 1 encore au-dessus : c'est l'ordre dans lequel on
relit l'historique d'une planche, et ce que font les trois planches relevées.
Une table est **une clef et des lignes de texte** (`Folio::tables`) ; c'est le
gabarit qui dit ses colonnes. Ce choix rend une table maison gratuite — ajouter
« ESSAIS EN USINE » à son cartouche ne demande pas une ligne de code.

**Les images sont embarquées**, jamais référencées par un chemin : un logo
pointé sur le disque disparaît dès que le fichier change de poste, et personne
ne s'en aperçoit avant l'impression.

**`ui/titleblockeditor.*`** — l'éditeur. Deux choses le distinguent d'un
formulaire :
- **l'aperçu est peint par le VRAI peintre** (`FolioPainter::paintTitleBlock`)
  sur une copie du projet : régler sur un dessin approché rouvrirait l'écart
  entre l'écran et le papier que tout ce logiciel existe pour fermer ;
- **on déplace les cases à la souris**, au demi-millimètre (un cartouche se
  compose sur une trame ; une case posée à 12,37 mm ne s'aligne avec rien). Le
  formulaire reste, pour poser un chiffre exact quand on le veut.

L'aperçu prend toute la largeur, au-dessus du reste : un cartouche est un objet
large et plat — 330 mm sur 35 — et le mettre dans une colonne le réduisait à un
timbre.

Deux gabarits sont livrés : le standard (l'ancien tracé en dur, transposé case
par case) et **Schéma de boucle**, calqué sur les planches relevées. Le second
ne sert pas qu'à offrir un choix : il **prouve que le mécanisme suffit à
décrire un cartouche réel**. Si un gabarit réel n'y rentrait pas, ce serait le
mécanisme qu'il faudrait reprendre.

**Ce que ça a trouvé au passage.** `Folio` a un constructeur de copie écrit à la
main — les entités se clonent une à une, un `unique_ptr` ne se copiant pas. Il
ne reprenait pas le champ neuf : les tables du cartouche se perdaient à la
première copie (une annulation, un collage, un aperçu) sans que rien ne le dise.
**Tout champ ajouté à `Folio` doit être repris dans `Folio(const Folio&)` ET
dans `operator=`** — un commentaire le dit maintenant à cet endroit, et un test
le tient.

Menu Projet ▸ Cartouche du dossier, commande `CARTOUCHE` (alias `CA`).

### D3 — La bibliothèque d'instrumentation (2026-09-03)

`libraries/iec/instrumentation.json`, dix-huit symboles. La **bulle ISA
S5.1** et ses variantes, qui sont aussi normatives que les formes des
marqueurs d'accrochage : cercle nu = instrument au champ, barré d'un trait
plein = façade de panneau, barré d'un trait pointillé = derrière le panneau,
dans un carré = système partagé, losange = fonction d'automate. Avec elles la
borne à vis ⊘ et sa borne de blindage, la boîte de jonction en cadre
pointillé nommé, vanne + positionneur + convertisseur I/P, filtre-détendeur,
manomètres, débitmètre magnétique, l'étiquette de câble en stade, et quatre
symboles de ventilation.

**Ce que ça a trouvé.** Quatre de ces symboles n'ont **aucune broche** — une
étiquette de câble ne se raccorde pas, un manomètre non plus — et le test de
cohérence de la bibliothèque refusait un symbole sans broche. La tentation
était de relâcher le test ; c'est le contraire qui a été fait :
`SymbolDefinition::noConnections` **le déclare**, et le test refuse
maintenant les deux fautes — un symbole muet qui ne se déclare pas, et un
symbole qui se déclare muet en portant des broches. Un test qu'on assouplit
ne trouve plus rien.

### D4 — Le câble n'est pas un fil (2026-09-03)

`1PR#16CU`, `2PR#16CU`, `12PR#16CU` : n paires, calibre AWG, cuivre, blindé.
C'est ce qu'on commande, ce qu'on tire et ce qu'on repère sur le chemin de
câbles — et rien dans notre `WireType` ne le disait. Trois décisions :

1. **`pairs` et `shielded` sur le type de fil**, et `pairs == 0` veut dire
   « ce type n'est pas un câble ». C'est le cas par défaut : un schéma de
   commande ne paie rien.
2. **Le code se COMPOSE** (`WireType::cableCode`), il ne se saisit pas. Un
   type à deux paires pourrait sinon s'appeler « 3PR » sans que rien ne le
   relève, et c'est le bon de commande qui serait faux.
3. **`Wire::cable` nomme le câble qui porte le fil**, et la liste des câbles
   (`Reports::cableList`) se construit **sur les fils, pas sur la netlist** :
   deux paires d'un même câble portent deux potentiels différents et restent
   un seul câble — c'est même tout l'intérêt de la chose. Une liste de fils
   sur un dossier d'instrumentation compte trois cents lignes ; la liste des
   câbles en compte quarante.

Les deux bouts d'un câble sont dits par leur **bande** (D1) : d'où à où, pas
dans quelle case du cadre.

### D5 — Le nommage (2026-09-03)

Trois manques, et ils ne se ressemblent pas.

**1. Le repère d'instrument se compose.** `022TT8917A` = secteur + fonction
ISA + boucle + suffixe. `DesignationContext` gagne `sector` (`%C`) et `loop`
(`%B`) ; ni l'un ni l'autre ne se déduit de la position de l'appareil sur la
planche, contrairement à la référence de ligne. Deux décisions :

- **Le secteur retombe sur celui de la planche** (`Folio::titleBlock["sector"]`,
  puis le projet). Une feuille de schéma de boucle appartient à une aire de
  l'usine ; le retaper sur chaque instrument serait le meilleur moyen d'en
  oublier un. Le champ posé sur l'appareil garde le dernier mot — un
  instrument peut être physiquement dans un autre secteur que sa planche.
- **Un format sans `%N` se départage par une LETTRE**
  (`DesignationRule::usesNumber`), comme le fait déjà le mode référence de
  ligne. **Et c'est un garde-fou, pas un raffinement** : le repérage
  incrémentait un compteur jusqu'à trouver un repère libre, or `%C%F%B` ne
  contient pas ce compteur — deux instruments de la même boucle faisaient
  **tourner la boucle sans fin**, et le logiciel se figeait. Le défaut
  existait déjà : `tagFormat = "%F"` suffisait à le déclencher. Le test le
  démontre, et il **fige le logiciel quand on retire le correctif** — c'est
  ainsi qu'on sait qu'il ne prouve pas rien.

**2. La couleur qui s'imprime est une lettre.** `WireType::colorCode` :
`N` noir, `B` blanc, `R` rouge… `rgb` sert à l'écran, le **code** est ce que
le câbleur cherche dans le faisceau, et c'est le seul moyen qu'un dossier tiré
en noir et blanc garde ses couleurs. Il se lit entre parenthèses à côté du
repère de fil (`WireType::colorTag`) **et** dans les rapports — invariant 15,
ce que le rapport imprime, le dessin le montre. La colonne « Couleur » du
rapport De/Vers portait `#202020`, ce qui n'apprend rien à personne ; elle
porte maintenant le code, et retombe sur la teinte tant qu'aucun code n'est
réglé.
**Il est vide par défaut, y compris dans les jeux livrés** : un code que
personne n'a demandé apparaîtrait sur chaque fil de chaque schéma déjà
dessiné, et la table des lettres est une convention de bureau (« N » pour
noir en français, « BK » pour black ailleurs) — l'inventer serait deviner à
la place de l'utilisateur. Le gestionnaire de types de fils a une colonne
« Code », et son infobulle donne la table relevée.

**3. L'automate a un nœud.** `%N04R07S07C016` = Nœud / Rack / Slot / Canal.
`PlcAddress::format` gagne le jeton `%N`, et **le nœud passe en premier
paramètre** : c'est l'ordre de la hiérarchie, celui dans lequel l'adresse
s'écrit. Il vit dans les champs de l'instance (`PlcModule::nodeKey`) comme le
rack et l'emplacement, donc **l'adresse reste recalculée** — changer de nœud
réadresse les seize points d'un coup. Trois modules génériques adressés par
nœud sont livrés dans la base, ce qui prouve que le format y rentre. Sans le
nœud, deux cartes de deux automates portent le même rack et le même
emplacement : deux adresses identiques pour deux bornes différentes, et c'est
le câbleur qui le découvre une fois le fil tiré.

**Ce qui n'a demandé aucun code.** Le conducteur porte aussi sa **polarité**
(`+` / `−`) sur les planches relevées. `Wire::conductors` la nomme déjà : une
paire est un fil à deux conducteurs nommés `+` et `−`, et la netlist les
apparie par nom comme elle apparie L1 à L1. Le relevé le listait comme un
manque ; il n'en était pas un.

### L'essai du bloc D — une planche de boucle, à la main (2026-09-03)

`docs/BOUCLES.md` promettait la preuve : refaire une des planches relevées
par événements de souris et de clavier. C'est `[essai][blocD]`, et il
traverse les cinq morceaux d'un seul geste — bandes, cartouche, symboles ISA,
câble, nommage — parce que c'est ainsi qu'un dessinateur les rencontre :
ensemble, sur la même feuille.

```sh
QT_QPA_PLATFORM=offscreen ARCUS_ESSAI_CAPTURES=/tmp \
    ./build/bin/arcus_ui_tests "[blocD]"
```

**Ce qu'il mesure.** 32 gestes, 4 boîtes modales — toutes des réglages
voulus (mise en page, cartouche, format de repère, carte d'automate), aucune
par objet posé —, 0 fil en biais, 5 constats d'audit dont aucune erreur. La
planche sort avec ses trois bandes coiffées de leur bandeau, son repérage de
droite à gauche, son cartouche de schéma de boucle et ses trois tables, la
bulle `022TT8917` au champ, la borne à vis dans la boîte de jonction, la
bulle d'automate `022TY8917` dans l'armoire, la carte adressée
`%N04R07S07C000` et le code couleur `(N)` écrit le long du fil.

**Les cinq défauts qu'il a trouvés.**

1. **La mise en page perdait les bandes.** `ChangeFolioLayoutCommand` ne
   portait que le format et le cadre : la boîte les faisait saisir, et elles
   n'entraient jamais dans le document — ni dans l'annulation. C'est le
   défaut le plus grave du lot, et invisible autrement : la boîte se referme
   sans rien dire. Les bandes entrent maintenant dans **la même commande**
   que le cadre, parce qu'elles se posent au même endroit et se lisent
   ensemble.
2. **« Format des repères… » n'avait pas de nom à taper.** Le menu le
   propose, la palette le trouve, et la ligne de commande répondait
   « commande inconnue ». C'est exactement ce que l'essai du bloc C avait
   trouvé sur `EFFACER` / `SUPPRIMER` : la règle 3 de la ligne de commande —
   *un bouton cliqué enseigne le nom à taper* — prise en défaut une seconde
   fois. `FORMATREPERE` (alias `FORMAT`) existe.
3. **Le repère d'un instrument était hors d'atteinte.** La lettre de famille
   ne venait que de la définition du symbole — la même pour tous les
   instruments au champ. Un transmetteur de température et un débitmètre
   portaient donc la même lettre, et `%C%F%B` ne pouvait pas écrire
   `022TT8917`. Le champ `family` posé sur l'appareil prime désormais sur la
   définition, comme partout ailleurs, et la boîte du composant le propose.
4. **Le tiret de tête n'était réglable que par le profil métier.** Un repère
   d'instrument n'en porte pas ; il aurait fallu changer de profil pour une
   question de ponctuation. C'est une convention de bureau, comme le format :
   une case à cocher dans la même boîte, retenue dans le projet
   (`Project::designationDash`).
5. **Un texte de table débordait de sa colonne.** Une description un peu
   longue s'écrivait par-dessus la voisine, et c'est à l'impression qu'on le
   découvrait — deux textes superposés dans un cartouche ne se lisent ni l'un
   ni l'autre. On coupe maintenant, avec des points de suspension : la coupe
   se voit, et le dessinateur raccourcit lui-même. **Le test a d'abord été
   écrit faux** — il visait un point que le texte débordé n'atteignait pas et
   passait sans le correctif. Vérifié en retirant le correctif, comme les
   trois fois précédentes.

**Ce qu'il a appris sans qu'on le corrige.** Les bulles ISA portent leurs
broches en haut et en bas, comme le veut la convention. Sur un schéma de
boucle le signal traverse la feuille horizontalement : on fait donc pivoter
la bulle d'un quart de tour avant de la poser — c'est le geste du
dessinateur, pas un défaut du symbole, mais il faut le savoir, et l'essai le
fait au grand jour.

## Prochaines étapes envisagées (dans l'ordre de valeur)

0. Reste du relevé AutoCAD (`docs/AUTOCAD.md`) : gestionnaire de projet
   multi-dossiers, configuration des colonnes de rapport, métadonnées de
   folio (Description 1/2/3) et rapport de liste de dessins.
1. Import DXF (l'export existe : `io/dxfexport.cpp`).
2. Unifilaires M8 : symboles de distribution + bilan de puissance
   (le modèle multi-conducteurs est prêt).
3. Électronique M9 : export netlist SPICE / KiCad.
4. AppImage Linux et .dmg macOS, quand le besoin se présentera.
