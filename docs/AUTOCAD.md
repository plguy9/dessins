# Ce qu'AutoCAD fait, et où nous en sommes

Relevé de **342 fonctionnalités** d'AutoCAD et d'AutoCAD Electrical, issu de huit
recherches web indépendantes (accrochage, aides au dessin, mise en page, édition
2D, composants, fils, gestion de projet, conventions d'interface).

Ce document sert de référence de travail : il dit ce qui existe chez AutoCAD,
ce que nous en avons repris, et ce qui reste. Il n'est pas une liste de tâches —
beaucoup de fonctions d'AutoCAD n'ont aucun sens dans un logiciel de schéma
électrique, et les copier serait une erreur.

> Source : recherche automatisée du 2026-09-01. Les comportements décrits sont
> ceux rapportés par les agents ; ceux que nous implémentons sont vérifiés au
> cas par cas contre la documentation Autodesk avant d'être codés.

## Journal des reprises

Les décomptes « déjà en place » de chaque section datent du relevé. Ce qui a
été ajouté depuis :

**Lot 2 — 2026-09-01**

| Repris d'AutoCAD | Chez nous |
|---|---|
| Types de fils (couleur / calibre / calque), `Create/Edit Wire Type` | `core/wiretype.*`, `ui/wiretypedialog.*`, commande `TYPEFIL` / `TF`. Un type = une couleur, une section, un style de trait et un calque d'export DXF. Comme chez AutoCAD, le calque est ce qui structure l'export. |
| Changer / convertir le type de fil | Sélecteur dans la barre d'outils pour les fils à venir, liste déroulante dans l'inspecteur pour un fil déjà tracé. |
| Décalage parallèle, `OFFSET` | `FolioView::beginOffset`, commande `DECALER` / `DC` / `O`, touche <kbd>O</kbd>. Distance saisie puis côté cliqué, avec aperçu ; la copie ne reprend pas le repère du fil. |
| Déplacer, `MOVE` | `FolioView::beginMoveSelection`, commande `DEPLACER` / `DP` / `M`, touche <kbd>D</kbd>. Point de base puis point d'arrivée, tous deux accrochables, avec fantôme de la sélection. |
| Menu contextuel du clic droit, `SHORTCUTMENU` | Sur le canevas (contenu selon la sélection) et sur la liste des folios (ajouter, dupliquer, renommer, monter, descendre, mise en page, supprimer). |

Reste notamment : gestionnaire de projet multi-dossiers, rapport de composants,
Wire From/To, entrées-sorties API, palette de propriétés (Ctrl+1), ÉTIRER
(`STRETCH`), flèches de signal source/destination, éditeur de borniers.

## Accrochage aux objets (Object Snap / OSNAP) d'AutoCAD — modes d'accrochage, marqueurs AutoSnap et reglages associes
31 fonctionnalités relevées — **23 déjà en place**, 8 restantes.

**En place chez nous :** Accrochage aux objets 3D, Accrochages permanents (jeu courant), Aimant, Bascule generale des accrochages, Centre, Centre geometrique, Cycle entre points d'accrochage, Extremite, Infobulle AutoSnap, Insertion, Intersection, Intersection apparente, Le plus proche, Marqueur AutoSnap, Menu contextuel d'accrochage, Milieu, Milieu entre 2 points, Noeud, Perpendiculaire, Prolongement, Quadrant, Remplacement ponctuel, Reperage d'accrochage aux objets

| Fonctionnalité | Commande AutoCAD | Comportement |
|---|---|---|
| Boite de dialogue Parametres de dessin | `DSETTINGS (Drafting Settings dialog box)` | Ouverte par DSETTINGS (alias DS, SE, ou OSNAP qui ouvre directement le bon onglet). Boite a onglets : (0) Resolution et grille, (1) Reperage polaire, (2) Accrochage aux objets, (3) Saisie dy |

<details><summary>2 fonctionnalités importantes (non essentielles)</summary>

- **Aucun (desactivation ponctuelle)** — `None (NON)`
- **Boite d'ouverture** — `Aperture box (APERTURE / APBOX)`

</details>

## Aides au dessin d'AutoCAD (Drafting Aids) : reperage, accrochage, saisie dynamique, grille et barre d'etat
35 fonctionnalités relevées — **23 déjà en place**, 12 restantes.

**En place chez nous :** Accrochage aux objets, Accrochage polaire (increments de distance le long d'un angle polaire), Acquisition et abandon d'un point de reperage, Angle d'increment polaire, Angle et origine de la grille d'accrochage, Angles polaires supplementaires, Bascules de dessin de la barre d'état, Comportement de la grille (adaptative, subdivision, au-dela des limites), Grille, Grille et resolution rectangulaires ou isometriques, Inversion temporaire du mode orthogonal, Mesure d'angle polaire : absolue ou relative au dernier segment, Mode orthogonal, Pas d'accrochage X et Y, Pas de grille et lignes majeures, Personnalisation de la barre d'état, Reperage orthogonal seul ou selon tous les angles polaires, Reperage par accrochage aux objets, Reperage polaire, Resolution (accrochage a la grille), Retour visuel des accrochages (AutoSnap), Saisie de cote (cotes dynamiques), Saisie dynamique

| Fonctionnalité | Commande AutoCAD | Comportement |
|---|---|---|
| Saisie au pointeur (coordonnees a l'ecran) | `Pointer Input — variables DYNPIVIS, DYNPIFOR` | Affiche la position du reticule sous forme de coordonnees dans une info-bulle pres du curseur ; quand une commande demande un point, on peut taper les valeurs directement dans l'info-bulle.  |
| Saisie de distance directe | `Direct Distance Entry` | Methode de saisie qui fait le lien entre toutes les aides de reperage : plutot que de taper des coordonnees, on oriente le curseur dans la direction voulue — direction verrouillee par Ortho, |
| Barre d'état (principe general) | `The Status Bar` | Bande situee en bas de la fenetre, qui regroupe l'affichage des coordonnees et une serie de boutons donnant un acces immediat aux outils qui gouvernent l'environnement de dessin. La plupart  |

<details><summary>6 fonctionnalités importantes (non essentielles)</summary>

- **Invites dynamiques** — `Dynamic Prompts — variable DYNPROMPT`
- **Dessin isometrique et bascule d'isoplan** — `Isometric Drafting — commande ISODRAFT, `
- **Sélection cyclique** — `Selection Cycling — variable SELECTIONCY`
- **Touches de remplacement temporaire** — `Temporary Override Keys`
- **Affichage des coordonnées dans la barre d'état** — `Coordinates (Status Bar Button) — variab`
- **Boîte de dialogue Paramètres de dessin** — `Drafting Settings Dialog Box — commande `

</details>

## AutoCAD Electrical — Gestion de projet, opérations projet, rapports, audits, automates (PLC) et implantation d'armoire (Panel Layout)
66 fonctionnalités relevées — **18 déjà en place**, 48 restantes.

**En place chez nous :** Bornier graphique dans l'armoire, Copier / archiver / renommer un projet, Export de rapport vers fichier, Insertion d'automate en bloc complet, Insertion d'automate paramétrique, Insertion d'empreinte depuis le menu d'icônes, Insertion d'empreintes depuis la liste du schéma, Lien bidirectionnel schéma ↔ implantation, Mise à jour des cartouches sur tout le projet, Mise à jour des renvois (références croisées), Onglet et environnement Panel Layout, Rails, goulottes et assemblages d'armoire, Rapport de nomenclature (schéma), Rapport de nomenclature manquante, Rapports d'implantation d'armoire, Renumérotation des repères de fils sur tout le projet, Tracer / publier tout le projet, Édition et copie d'empreintes

| Fonctionnalité | Commande AutoCAD | Comportement |
|---|---|---|
| Gestionnaire de projet (palette) | `Project Manager` | Palette ancrable listant tous les projets ouverts sous forme d'arbre. Un seul projet est « actif » à la fois (affiché en gras) ; c'est lui qui sert de périmètre à toutes les opérations dites |
| Fichier de projet .wdp | `Project File (.wdp)` | Fichier texte ASCII qui EST le projet : il ne contient pas de dessin, seulement (a) des lignes de réglages de projet (préfixées par des marqueurs de type ===...) et (b) la liste ordonnée des |
| Liste et ordre des dessins | `Drawing List / Reorder Drawings` | L'ordre des dessins dans la liste n'est pas cosmétique : il détermine l'ordre de parcours pour la numérotation séquentielle des fils, le repérage séquentiel des composants, la numérotation d |
| Propriétés du dessin | `Drawing Properties` | Boîte de dialogue à onglets définissant, folio par folio, tout le comportement d'AutoCAD Electrical sur ce dessin : (1) Drawing Settings — codes d'installation et de localisation par défaut, |
| Données de liste de dessins (métadonnées de folio) | `Drawing List Data / Drawing Properties > Des` | Champs descriptifs attachés à chaque dessin du projet et exploités par le cartouche et par le Drawing List Report : Description 1, Description 2, Description 3, numéro de folio (Sheet), numé |
| Propriétés du projet | `Project Properties` | Mêmes onglets que les propriétés de dessin, mais au niveau projet : ils servent de valeurs par défaut héritées par tout nouveau dessin ajouté, et peuvent être poussés en bloc sur les dessins |
| Ajouter, retirer, remplacer des dessins | `Add Drawings / Add Active Drawing / Remove /` | Ajout d'un ou plusieurs .dwg existants au projet (sélection multiple), ajout du dessin actuellement ouvert, création d'un nouveau dessin à partir d'un gabarit directement dans le projet (ave |
| Mise à jour / repérage à l'échelle du projet | `Project-Wide Update/Retag` | Boîte de dialogue centrale des opérations en lot, avec des cases à cocher que l'on combine puis applique à tout le projet ou à une sélection de dessins : repérage des composants (retag), num |
| Repérage des composants sur tout le projet | `Retag Components` | Réattribue les repères des composants sur l'ensemble du projet selon le format défini dans les propriétés (séquentiel : KM1, KM2… par famille ; ou basé sur la référence de ligne : 102KM pour |
| Rapport de composants | `Component (Schematic Reports)` | Liste de tous les composants du projet avec leur repère, type/famille, descriptions (DESC1..3), folio et ligne d'implantation, codes installation et localisation, données catalogue, calibre/ |
| Rapport de câblage de/vers | `Wire From/To` | Le rapport de câblage central : une ligne par liaison physique, avec origine (composant + broche + folio/ligne) et destination (composant + broche + folio/ligne), repère du fil, couleur, sec |
| Rapport de numéros de bornes | `Terminal Numbers` | Liste des bornes du projet : bornier (repère du bloc de jonction), numéro de borne, repère du fil raccordé, composant et broche à chaque extrémité, folio/ligne, référence catalogue de la bor |
| Rapport d'adresses et descriptions d'E/S automate | `PLC I/O Address and Descriptions` | Table des points d'entrée/sortie d'automate du projet : adresse (rack/slot/point ou adresse absolue), type (TOR entrée, TOR sortie, analogique…), les lignes de description associées au point |
| Générateur de rapports (fenêtre de résultat) | `Report Generator` | Fenêtre commune à tous les rapports affichant le résultat en table avant toute sortie. Elle offre : le tri par colonne, le changement de format de rapport à la volée, l'ajout/retrait de colo |
| Paramétrage du format de rapport | `Report Format Setup` | Outil de définition, par type de rapport, de la présentation : choix des champs disponibles à inclure et leur ordre en colonnes, intitulé et largeur de chaque colonne, justification, niveaux |
| Poser un rapport dans le dessin (table) | `Put on Drawing / Table Generation Setup` | Insère le rapport comme table dans un dessin, au choix sous forme d'objet TABLE AutoCAD (avec style de table) ou de blocs de lignes de rapport. Le dialogue de génération règle : le titre, le |
| Portée d'un rapport (projet / dessin / sélection) | `Report Scope: Project / Active Drawing` | Chaque rapport commence par le choix du périmètre : tout le projet, le dessin actif seulement, ou une sélection de dessins prise dans la liste du projet (avec possibilité de choisir une sect |
| Audit électrique | `Electrical Audit` | Contrôle de cohérence électrique du projet, présenté en une fenêtre à onglets/catégories avec le nombre d'anomalies par famille et la possibilité de sauter au dessin concerné (surf) pour cor |
| Adressage des points d'E/S | `PLC I/O Addressing (Rack / Slot / Point)` | À l'insertion, on définit l'adresse de départ du module et le logiciel adresse automatiquement chaque point par incrément, selon le format d'adressage du constructeur (par exemple I:3/0, %I0 |

<details><summary>20 fonctionnalités importantes (non essentielles)</summary>

- **Sections et sous-sections de projet** — `Add Section Marker / Sub-section Marker`
- **Rapport de liste de dessins** — `Drawing List Report`
- **Utilitaires projet (modifications graphiques en lot)** — `Project-Wide Utilities`
- **Navigation par surf (suivi de composant/potentiel)** — `Surfer`
- **Base de données de projet (reconstruction)** — `Project Database / Rebuild`
- **Rapport de raccordement par composant** — `Component Wire List`
- **Rapport de plan de bornier** — `Terminal Plan`
- **Rapport de connecteurs (synthèse)** — `Connector Summary`
- **Rapport de connecteurs (détail)** — `Connector Detail`
- **Rapport de câbles (synthèse)** — `Cable Summary`
- **Rapport de câbles de/vers** — `Cable From/To`
- **Rapport de raccordement des E/S automate** — `PLC I/O Component Connection`
- **Rapport des modules automate utilisés** — `PLC Modules Used So Far`
- **Sélection automatique de rapports (lot)** — `Automatic Report Selection`
- **Audit de dessin (DWG Audit)** — `DWG Audit`
- **Base de données des modules automate** — `PLC Database File (plc.mdb) / PLC Databa`
- **Génération des folios d'E/S depuis un tableur** — `Spreadsheet to PLC I/O Utility`
- **Édition d'un module d'automate posé** — `PLC Module Edit / Add or Remove I/O Poin`
- **Base de correspondance catalogue → empreinte** — `Footprint Lookup Database`
- **Bulles et numéros d'article** — `Balloons / Assign Item Numbers`

</details>

## AutoCAD Electrical — fils (wires), reperage des fils (wire numbers), fleches de signal, echelles/ladders, borniers et cables
36 fonctionnalités relevées — **21 déjà en place**, 15 restantes.

**En place chez nous :** Afficher les fils, Ajouter un barreau d'echelle, Bus multi-fils, Coupure / boucle de croisement de fils, Deplacer / copier / supprimer les reperes de fil, Editer un repere de fil, Etiquettes en ligne (in-line), Etirer un fil, Format de repere par calque, Format du repere de fil, Inserer un fil, Inserer une echelle (ladder), Insertion du bornier graphique, Permuter deux reperes de fil, Reperage a l'echelle du projet, Reperage automatique des fils, Repere de fil sur ligne de repere, Reviser l'echelle / renumeroter les references de ligne, Rogner un fil, Styles de referencement X-Y Grid et X-Zones, Verifier / reparer les pointeurs de coupure

| Fonctionnalité | Commande AutoCAD | Comportement |
|---|---|---|
| Types de fils (couleur / calibre / calque) | `Create/Edit Wire Type (boite de dialogue Cre` | Definit la bibliotheque des types de fils du dessin. Chaque type est en fait un calque AutoCAD. Dans la grille, on saisit une valeur dans la colonne 'Wire Color' et une dans la colonne 'Size |
| Repere de fil fixe (verrouille) | `Fix — commande AEFIXWIRENO` | Fige un ou plusieurs reperes de fil a leur valeur courante : un passage ulterieur du reperage automatique laisse ces reperes inchanges. Techniquement, l'attribut du repere est renomme et bas |
| Fleche de signal source | `Source Signal Arrow (Source Arrow)` | Marque un reseau de fils comme origine d'un signal qui se poursuit ailleurs. On insere la fleche a l'extremite du reseau et on lui attribue un NOM DE CODE (source code name). A l'insertion,  |
| Fleche de signal destination | `Destination Signal Arrow (Destination Arrow)` | Marque un reseau de fils comme continuation d'un signal source. On lui donne le MEME nom de code que la source : les deux reseaux sont alors consideres comme un seul et meme potentiel et par |
| Editeur de borniers | `Terminal Strip Editor — commande AETSE (boit` | Editeur tabulaire de tous les borniers du projet. On part de la boite 'Terminal Strip Selection' qui liste les borniers detectes, puis Edit (bornier existant) ou New, ce qui ouvre l'editeur. |
| Inserer une borne | `Insert Terminal (symbole de borne, via Inser` | Insere un symbole de bloc de jonction sur un fil. La borne porte un repere de bornier (numero de bornier) et un numero de borne ; elle coupe le fil en deux reseaux distincts du point de vue  |

<details><summary>4 fonctionnalités importantes (non essentielles)</summary>

- **Changer / convertir le type de fil** — `Change/Convert Wire Type`
- **Etiquettes couleur/calibre de fil** — `Insert Wire Color/Gauge Labels (fichier `
- **Sequence de connexion d'un reseau de fils** — `Edit Wire Sequence — commande AEEDITWIRE`
- **Reperes de cable** — `Cable Markers (marqueur de cable parent)`

</details>

## AutoCAD Electrical — fonctionnalites liees aux composants (insertion depuis la bibliotheque, reperage/tagging, relations parent/enfant et references croisees, catalogue fabricant, edition et manipulation des composants)
40 fonctionnalités relevées — **12 déjà en place**, 28 restantes.

**En place chez nous :** Copie d'affectation catalogue, Copie des codes Installation / Emplacement / Montage / Groupe, Copier un composant, Fichier de menu d'icones (.dat), Format graphique de reference croisee, Format tableau de reference croisee, Format texte de reference croisee, Insertion de composant depuis le menu d'icones, Insertion sur un fil : coupure et reconnexion automatiques, Liste des references d'un composant, Mise a jour des references croisees en temps reel, References croisees parent/enfant

| Fonctionnalité | Commande AutoCAD | Comportement |
|---|---|---|
| Boite de dialogue Inserer/Editer composant | `Insert/Edit Component (AEEDITCOMPONENT)` | Boite unique qui s'ouvre juste apres l'insertion d'un symbole parent et lors de son edition (clic droit sur le composant > Edit Component, ou AEEDITCOMPONENT puis selection). Elle permet d'a |
| Reperage sequentiel | `Sequential (mode de Component TAG Format)` | Mode de generation automatique des reperes defini dans les proprietes de dessin/projet (onglet Components). A l'insertion, le logiciel part de la valeur de depart definie pour le dessin et r |
| Reperage base sur la reference (Reference-Based / Line Reference) | `Line Reference (mode de Component TAG Format` | Second mode de reperage : le numero du repere derive de la position du composant — numero de reference de ligne (ou de zone) sur lequel il se trouve, et non d'un compteur. Le composant place |
| Format de repere avec parametres remplacables | `Component TAG Format (%F %N %S %D %G %X %P %` | Le format du repere est une chaine a parametres, par defaut '%F%N'. Le repere comporte au minimum deux informations : un code de famille et un numero alphanumerique de reference (par ex. 'CR |
| Repere fige / verrouille | `Fixed tag (attribut TAG1F)` | Un composant peut etre marque comme portant un repere fige : le nom de l'attribut TAG1 est alors automatiquement renomme avec le suffixe F, soit TAG1F. Consequence : les outils de reperage a |
| Relation parent / enfant (bobine et contacts) | `Parent component / Child component` | Modele fondamental : un symbole parent (la bobine de relais, le corps du contacteur) et des symboles enfants (les contacts associes) representent le meme appareil physique. Les enfants peuve |
| Boite de dialogue Inserer/Editer composant enfant | `Insert/Edit Child Component` | S'ouvre a l'insertion ou a l'edition d'un symbole enfant. Le rattachement au parent se fait de quatre facons : 1) bouton 'Parent/Sibling' — si le parent (ou un contact frere deja pose) est v |
| Base de donnees catalogue fabricant | `Catalog database (default_cat.mdb) / AECATAL` | Base de donnees (fichier Access .mdb par defaut) contenant les references fabricant. Chaque type de composant primaire ou autonome peut disposer de sa propre table dans la base — decoupage f |
| Recherche catalogue depuis le composant | `Catalog Lookup (Component Catalog Lookup Dia` | Depuis la boite Insert/Edit Component, section Catalog Data, le bouton 'Lookup' ouvre la recherche dans la table catalogue correspondant a la famille du symbole, pour choisir la reference fo |
| Surfer (navigation vers les references croisees) | `Surfer (AESURF)` | Outil de navigation projet, et non seulement dessin : on designe un element et le Surfer liste toutes les references liees dans l'ensemble du jeu de dessins du projet, puis permet de sauter  |
| Scoot (glissement contraint le long du fil) | `Scoot (AESCOOT)` | Ruban : onglet Schematic > panneau Edit Components > liste deroulante Modify Components > Scoot. Repositionnement rapide, contraint a un seul axe par construction (pour eviter de deplacer un |
| Deplacer un composant | `Move Component` | Deplacement libre d'un composant vers un nouvel emplacement, y compris sur un autre fil ou une autre zone du folio, avec prise en charge intelligente du cablage : les fils sous-jacents sont  |

<details><summary>12 fonctionnalités importantes (non essentielles)</summary>

- **Assistant de menu d'icones (personnalisation de la bibliotheque)** — `Icon Menu Wizard`
- **Liste de suffixes pour reperes dupliques** — `Suffix list (Component TAG Format)`
- **Mise a jour / reperage projet complet** — `Project-wide Update/Retag (AEPROJUPDATE)`
- **Listes de broches et mappage des contacts** — `Pin List (_PINLIST table, PINLIST_TYPE, `
- **Navigateur de catalogue** — `Catalog Browser (AECATALOGOPEN)`
- **Affectations catalogue multiples** — `Multiple Catalog (Multiple Catalog Part `
- **Assemblages catalogue (code d'assemblage)** — `ASSEMBLYCODE / ASSEMBLYLIST (champs de l`
- **Codes Installation et Emplacement sur le composant** — `Installation code / Location code (Inser`
- **Aligner des composants** — `Align (AEALIGN)`
- **Supprimer un composant** — `Delete Component`
- **Echanger / mettre a jour le bloc d'un composant** — `Swap Block / Update Block / Library Swap`
- **Numeros de broches du composant** — `Pins / List (Connector Pin Numbers in Us`

</details>

## Conventions d'interface AutoCAD — ligne de commande, curseur reticule, barre d'etat, ViewCube et barre de navigation, palettes ancrables, ruban et onglets contextuels, menus contextuels et Quick Access Toolbar, zoom/panoramique, raccourcis clavier
52 fonctionnalités relevées — **20 déjà en place**, 32 restantes.

**En place chez nous :** Alignement du reticule : SCU, angle de reseau, isometrie, 3D, Bascules de la barre d'etat, Cible d'accrochage (aperture), Commandes transparentes, Comportement generique des palettes ancrables, Correction automatique et recherche en milieu de chaine, Couleurs du reticule, du fond et des elements d'ecran, Curseur reticule (crosshair), DesignCenter, Fenetre de commande (ligne de commande), Menu de remplacement d'accrochage aux objets, Panoramique a la molette, Poignees (grips) multifonctions, Saisie dynamique (Dynamic Input), Selection par fenetre, capture et lasso, Zoom Etendu, Zoom Fenetre, Zoom Precedent, Zoom a la molette, Zoom temps reel et panoramique

| Fonctionnalité | Commande AutoCAD | Comportement |
|---|---|---|
| AVERTISSEMENT GLOBAL — aucune source verifiee | `(meta)` | Aucune verification en ligne n'a pu etre effectuee pendant cette session. Le budget WebSearch etait epuise (200/200 appels consommes) et le proxy d'egress a refuse (HTTP 403) tous les domain |
| Autocompletion de la saisie | `AUTOCOMPLETE / INPUTSEARCHOPTIONS` | Des la premiere lettre tapee, une liste deroulante apparait AU-DESSUS de la ligne de commande (elle remonte, elle ne descend pas) et propose les commandes et variables systeme correspondante |
| Historique des commandes | `Fenetre de texte / TEXTSCR / Recent Input` | Trois mecanismes distincts. (1) F2 developpe l'historique : en ligne de commande flottante il ouvre une fenetre d'historique defilante au-dessus du champ ; en mode ancre il bascule la fenetr |
| Alias de commandes | `acad.pgp / ALIASEDIT / REINIT` | Chaque commande peut etre invoquee par un alias de 1 a 3 lettres, defini dans le fichier texte acad.pgp (section des alias, syntaxe 'ALIAS,*COMMANDE'). L'utilisateur edite ce fichier directe |
| Options de commande entre crochets, cliquables | `(convention d'invite)` | Une invite de commande expose ses options entre crochets, separees par des barres obliques, avec les lettres-cles en majuscules : par ex. 'Specifiez le point suivant ou [Arc/Fermer/Demi-larg |
| Barre d'etat | `STATUSBAR` | Bande horizontale en bas de la fenetre applicative, sous la zone de dessin et la ligne de commande. Depuis AutoCAD 2015 elle est entierement iconographique : chaque bascule est une petite ic |
| Palette Proprietes | `PROPERTIES (alias PR, anciennement MO/DDMODI` | Palette la plus utilisee d'AutoCAD. En tete, une liste deroulante indique le type d'objet selectionne ; si la selection est heterogene, elle affiche 'Tout (n)' et deroule le detail par type  |
| Ruban (Ribbon) | `RIBBON / RIBBONCLOSE / RIBBONSTATE` | Bande d'outils en haut de la fenetre, organisee en ONGLETS (Debut/Home, Insertion, Annoter, Parametrique, Vue, Gerer, Sortie, Modules complementaires, Collaborer, Express Tools, Applications |
| Onglets contextuels du ruban | `RIBBONCONTEXTSELECT / RIBBONSELECTMODE` | Un onglet supplementaire apparait AUTOMATIQUEMENT et devient l'onglet actif quand un objet particulier est selectionne ou qu'une commande d'edition est lancee ; il se referme des que la sele |
| Menu contextuel du clic droit | `SHORTCUTMENU` | Le clic droit dans la zone de dessin ouvre un menu dont le CONTENU depend strictement du contexte, ce qui en fait un accelerateur central : (1) Menu PAR DEFAUT, sans selection ni commande ac |
| Touches de fonction F1 a F12 | `(raccourcis clavier)` | Rangee de bascules memorisee par tous les utilisateurs : F1 Aide ; F2 developper/reduire l'historique des commandes (fenetre de texte) ; F3 Accrochage aux objets actif/inactif ; F4 Accrochag |
| Raccourcis Ctrl | `(raccourcis clavier)` | Ctrl+A tout selectionner ; Ctrl+B resolution (grille) ; Ctrl+C copier ; Ctrl+Maj+C copier avec point de base ; Ctrl+D SCU dynamique ; Ctrl+E rotation des isoplans ; Ctrl+F accrochage aux obj |
| Touches d'edition et gestes de base | `(conventions clavier/souris)` | Entree et BARRE D'ESPACE valident indifferemment une invite et, au prompt vide, relancent la derniere commande — la barre d'espace est le validateur usuel car elle tombe sous le pouce. Echap |

<details><summary>9 fonctionnalités importantes (non essentielles)</summary>

- **Cible de selection (pickbox)** — `PICKBOX`
- **Menu de personnalisation de la barre d'etat** — `(bouton 'Personnalisation', trois barres`
- **ViewCube** — `NAVVCUBE / NAVVCUBEDISPLAY / NAVVCUBEOPA`
- **Barre de navigation** — `NAVBAR / NAVBARDISPLAY`
- **Palettes d'outils** — `TOOLPALETTES (Ctrl+3) / TOOLPALETTESCLOS`
- **Clic droit temporise** — `Options > Preferences utilisateur > Pers`
- **Quick Access Toolbar (barre d'outils d'acces rapide)** — `QAT / QATTOGGLE (a verifier)`
- **Onglets de fichiers et onglets de presentation** — `FILETAB / FILETABCLOSE ; onglets Objet e`
- **Espaces de travail** — `WSCURRENT / WSSAVE / WSSETTINGS`

</details>

## Edition 2D AutoCAD : commandes de modification les plus utilisees, poignees (grips) et modes de selection Fenetre/Capture — comportement detaille pour reimplementation dans un logiciel de dessin
30 fonctionnalités relevées — **17 déjà en place**, 13 restantes.

**En place chez nous :** Ajuster (couper a une limite), Copier, Copier les proprietes (pinceau), Echelle (homothetie), Miroir (symetrie axiale), Poignees multifonctions (menu contextuel de poignee), Poignees — affichage, etats et couleurs, Poignees — ajouter/supprimer un sommet, convertir en arc, Poignees — modes d'edition cycliques sur poignee chaude, Poignees — selection multiple et point de base, Poignees — variables de comportement et limites, Prolonger, Reseau polaire (circulaire), Rotation, Selection Capture (Crossing) — rectangle vert, Selection Fenetre (Window) — rectangle bleu, Selection au lasso (fenetre et capture a main levee)

| Fonctionnalité | Commande AutoCAD | Comportement |
|---|---|---|
| Decalage (parallele) | `OFFSET` | Cree un objet parallele a distance constante : ligne parallele, cercle concentrique, polyligne homothetique suivant la forme. Sequence : 'Specifier la distance de decalage ou [Par/Effacer/Ca |
| Etirer | `STRETCH` | La commande la plus specifique du lot : elle depend entierement du mode de designation. Sequence : 'Selectionner les objets a etirer par une fenetre de capture ou un polygone de capture...', |
| Deplacer | `MOVE` | Sequence : 'Selectionner les objets', Entree ; 'Specifier le point de base ou [Deplacement] <Deplacement>' ; 'Specifier le second point ou <utiliser le premier point comme deplacement>'. Les |

<details><summary>6 fonctionnalités importantes (non essentielles)</summary>

- **Raccord (conge, arrondi de coin)** — `FILLET`
- **Reseau (matrice de copies)** — `ARRAY`
- **Reseau rectangulaire** — `ARRAYRECT`
- **Couper / Briser (creer une coupure)** — `BREAK`
- **Joindre** — `JOIN`
- **Decomposer** — `EXPLODE`

</details>

## Formats de page, mise en page et cartouches (AutoCAD / AutoCAD Electrical)
52 fonctionnalités relevées — **30 déjà en place**, 22 restantes.

**En place chez nous :** Apercu avant trace, Cartouche : bloc a attributs, Cartouches pilotes par le jeu de feuilles, Champs dynamiques dans les attributs, Choix du peripherique de trace, Creation d'un cartouche Electrical, Decalage de trace, Echelle de fenetre et verrouillage, Echelle de trace, Espace papier trace en dernier, Gestion des presentations, Gestionnaire des mises en page, Mise a jour du cartouche a l'echelle du projet, Mise a l'echelle des epaisseurs de ligne, Mise en page nommee, Objets annotatifs et echelle d'annotation, Options de fenetre ombree, Orientation du dessin, Parametrage du cartouche (Electrical), Proprietes du dessin (AutoCAD Electrical), Styles de trace dependants de la couleur (CTB), Styles de trace nommes (STB), Table des styles de trace, Tampon de trace, Trace et publication par lot, Tracer avec les styles de trace, Tracer la transparence, Tracer les epaisseurs des objets, Visibilite des calques par fenetre, Zone de trace

| Fonctionnalité | Commande AutoCAD | Comportement |
|---|---|---|
| Format papier | `Paper size` | Liste deroulante des formats standard disponibles pour le peripherique selectionne. Les noms incluent la famille et les dimensions, par exemple 'ISO A3 (420.00 x 297.00 MM)'. Familles couram |
| Formats ISO serie A | `ISO A0 / A1 / A2 / A3 / A4` | Serie ISO 216, ratio 1:racine(2), chaque format etant la moitie du precedent coupe perpendiculairement au grand cote. Dimensions : A0 = 841 x 1189 mm, A1 = 594 x 841 mm, A2 = 420 x 594 mm, A |
| Epaisseur de trait par calque | `Lineweight ByLayer / Lineweight Settings (LW` | Chaque objet porte une epaisseur qui vaut par defaut ByLayer : l'objet herite de l'epaisseur affectee a son calque. La boite Lineweight Settings regle l'unite d'affichage (millimetres ou pou |
| Zone imprimable et marges | `Printable area — Modify Standard Paper Sizes` | Chaque format papier possede une zone imprimable plus petite que le papier, definie par des marges Haut / Bas / Gauche / Droite issues du materiel : le traceur ne peut imprimer ni la ou il p |
| Onglets Objet et Presentation | `Model tab / Layout tabs (espace objet / espa` | Un dessin possede un unique espace objet (onglet Model), ou est construite la geometrie a l'echelle 1:1 en unites reelles, et un nombre quelconque de presentations (onglets Layout), chacune  |
| Gabarits de dessin | `Fichiers .DWT (drawing template)` | Un gabarit DWT preconfigure calques, styles de texte et de cote, unites, mises en page nommees, presentations, cartouche insere, table de styles de trace et variables systeme. Tout nouveau d |

<details><summary>8 fonctionnalités importantes (non essentielles)</summary>

- **Import de mises en page depuis un autre fichier** — `PSETUPIN / -PSETUPIN`
- **Affichage de la feuille en presentation** — `Options > Display > Layout elements`
- **Edition et synchronisation des attributs** — `EATTEDIT / BATTMAN / ATTSYNC / ATTEDIT /`
- **Gabarits AutoCAD Electrical** — `ACAD_ELECTRICAL.dwt, ACAD_ELECTRICAL_IEC`
- **Bloc de configuration du dessin** — `WD_M (bloc invisible insere en 0,0)`
- **Fichier de projet et liste des dessins** — `Fichier .WDP (project file) — Project Ma`
- **Numerotation des feuilles en lot** — `Export/Update Drawing List Data (Export `
- **Presentations multiples en AutoCAD Electrical** — `Limitation : un dessin = une feuille`

</details>
