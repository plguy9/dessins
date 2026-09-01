# Dessins — brief d'architecture

Logiciel de dessin électrique. Qt 6 / C++20, application de bureau native.

> Un « AutoCAD complet » représente des centaines d'années-personnes. Un logiciel de
> schématique électrique réellement utilisable — noyau CAO 2D, bibliothèque de symboles,
> folios, connectivité, nomenclature — est atteignable de façon incrémentale.
> Ce document fixe l'architecture qui rend ce chemin tenable.

## 1. Périmètre

Trois domaines retenus (commande/puissance, unifilaires de distribution, électronique),
deux jeux de normes (CEI et ANSI), quatre capacités en V1. Périmètre large : il ne devient
réaliste qu'à une condition, **ne jamais construire trois logiciels** (voir §2).

**Ce qu'on construit**

- Un éditeur de schémas **orienté connectivité** : symboles à broches, liaisons, potentiels calculés.
- Un projet **multi-folios** : cartouche, renvois de folio, formats A4→A0 et ANSI A→E.
- Une **bibliothèque à double norme** CEI 60617 / ANSI, commutable par projet.
- Des **automatismes** : repérage des fils et des composants, bornier, nomenclature.
- Des **sorties de dossier** : impression et PDF multi-folios, CSV, DXF.

**Ce qu'on ne construit pas**

- Pas de CAO 3D, ni de modeleur solide, ni d'espace-objet/espace-papier à la AutoCAD.
- Pas de DWG natif : aucune bibliothèque libre fiable n'existe (§6). DXF uniquement.
- Pas de note de calcul réglementaire certifiée — aide au dimensionnement seulement.
- Pas de routage PCB : sur le volet électronique on s'arrête au schéma et à la netlist.
- Pas de collaboration temps réel en V1 ; le format est conçu pour l'accueillir plus tard.

## 2. Un seul moteur, trois métiers

Un schéma de commande, un unifilaire et un schéma électronique paraissent être trois
logiciels. Réduits à leur modèle de données, c'est **exactement la même chose** :

```
Symboles  →  Liaisons  →  Netlist  →  Rapports
(broches)    (fils,       (union-find   (repérage, bornier,
             jonctions,    sur les       nomenclature,
             étiquettes)   broches)      exports)
```

Ce qui change d'un métier à l'autre est **de la donnée et des règles** : bibliothèque de
symboles, convention de repérage (CEI 81346 `-K1`, ANSI `K1`, électronique `R1`),
représentation, rapports produits. Le tout est encapsulé dans un **profil métier** chargé
depuis des fichiers de données. Ajouter un métier est un travail de bibliothécaire, pas de
développeur.

### La décision à ne pas rater dès le départ

Un unifilaire condense plusieurs conducteurs en un trait : un départ triphasé porte
L1/L2/L3 + N + PE. Si la liaison est mono-conducteur au départ, l'unifilaire (M8) impose une
réécriture du cœur. On modélise donc **toute liaison comme porteuse de _n_ conducteurs dès
M3**, avec _n_ = 1 comme cas courant. Coût aujourd'hui : quelques heures. Plus tard :
plusieurs semaines.

## 3. Architecture en couches

| Couche | Rôle |
|---|---|
| `app/` | Point d'entrée, fenêtre principale, assemblage des couches, préférences. |
| `ui/` | Scène de dessin, machine à états des outils, palette de symboles, inspecteur, navigateur de folios. |
| `rules/` | Profils métier : normes CEI/ANSI, règles de repérage, gabarits de feuille, numérotation, rapports. |
| `io/` | Format natif `.dsn`, import/export DXF, impression et PDF, exports CSV. |
| `symbols/` | Format de définition de symbole, chargement des bibliothèques, variantes de norme, rendu des primitives. |
| **`core/`** | **Sans dépendance graphique.** Géométrie en mm, modèle de document (projet → folios → entités), graphe de connectivité, pile d'annulation, notifications. |

Règle de dépendance : chaque couche ne connaît que celles situées sous elle. `core/` ne
référence que `Qt6::Core` — ni widgets ni GUI — donc testable sans écran en CI et
réutilisable pour un futur outil en ligne de commande.

## 4. Décisions techniques

| Sujet | Choix | Motif | Écarté |
|---|---|---|---|
| Langage & cadre | C++20 · Qt 6.4 LGPLv3 | Vérifié opérationnel dans l'environnement. Liaison dynamique : compatible libre comme propriétaire. | Qt 5 (fin de support), C++17 |
| Construction | CMake + Ninja | Voie officielle de Qt 6, CI et compilation croisée simples. | qmake (déprécié) |
| Rendu | QGraphicsView / QPainter | Sélection, survol, zoom, panoramique fournis. Surtout : **le même code de peinture produit l'écran et le PDF**. | Scène OpenGL sur mesure (à reconsidérer au-delà de ~50 000 entités) |
| Unités internes | millimètres, `double` | L'impression et les formats de feuille sont métriques ; les pouces deviennent une conversion de présentation. | Pixels ou entiers (perte de précision au zoom) |
| Annulation | `QUndoStack` + commandes | Macro-commandes, fusion d'actions, pile visible offertes. | Instantanés du document (coût mémoire) |
| Format natif | `.dsn` = ZIP + JSON | Symboles embarqués, champ de version, chaîne de migration. Récupérable et inspectable. | XML maison, format binaire |
| PDF & impression | `QPdfWriter` + `QPainter` | Zéro code de rendu en double : conséquence directe du choix de rendu. | Générateur PDF tiers |
| DXF | Écriture maison, lecture via libdxfrw | DXF ASCII est documenté ; libdxfrw (LGPL) fait tourner LibreCAD depuis des années. | SDK ODA (payant), DWG natif |
| Tests | Catch2, cœur sans écran | Connectivité, numérotation et migrations de format se testent hors GUI. | Tests manuels seuls |

## 5. Le moteur d'automatismes

Ce qui sépare un vrai outil électrique d'un logiciel de dessin générique. Tout dérive de la netlist.

- **Extraction des potentiels** — les broches connectées par fils et jonctions sont fusionnées
  par union-find. Les étiquettes de potentiel et les renvois de folio fusionnent en plus des
  groupes de même nom à travers le projet : la continuité inter-folios devient réelle et non
  graphique.
- **Repérage automatique des fils** — stratégie configurable par projet (séquentiel, par
  folio et colonne, par potentiel). Un fil repéré manuellement est verrouillé et jamais
  écrasé par une régénération ; sans cette garantie les utilisateurs désactivent
  l'automatisme dès la première mauvaise surprise.
- **Désignation des composants** — préfixe issu du profil (`-K`, `-Q`, `-F` en CEI 81346 ;
  `K`, `CB`, `FU` en ANSI), compteur à portée projet ou folio, appareils multi-blocs
  (un contacteur et ses contacts auxiliaires partagent une désignation).
- **Nomenclature et bornier** — agrégation par référence fabricant, quantités, liste des
  câbles, plan de bornier. Export CSV en V1, XLSX ensuite.

## 6. Feuille de route

| Jalon | Titre | Contenu |
|---|---|---|
| M0 | Socle | Dépôt, CMake/Ninja, CI Linux, géométrie, modèle de document, lecture/écriture `.dsn`, tests du cœur. Livrable : une fenêtre qui ouvre et enregistre un projet vide. |
| M1 | Canevas | Scène, grille et magnétisme, zoom/panoramique, sélection, annulation/rétablissement, formats de feuille et cartouche paramétrable. |
| M2 | Symboles | Format de symbole, éditeur intégré, premier jeu CEI (~80 symboles), palette, placement, rotation, miroir, accrochage aux broches. |
| M3 | Connectivité | Fils orthogonaux, jonctions, **liaisons multi-conducteurs**, extraction des potentiels, mise en évidence d'un potentiel, renvois de folio. |
| M4 | Automatismes | Numérotation des fils, désignation des composants, plan de bornier, nomenclature, export CSV. |
| M5 | Sortie | Impression et export PDF multi-folios avec cartouche et aperçu. |
| **V1** | **Premier logiciel utilisable** | Un électricien peut produire un dossier complet de schémas de commande et de puissance, du premier trait au PDF. |
| M6 | Interopérabilité DXF | Export (symboles → blocs, champs → attributs, calques) puis import. La sémantique électrique ne survit pas au DXF : échange graphique, et l'interface le dira. |
| M7 | Profil ANSI | Second jeu de symboles, repérage nord-américain, formats ANSI, unités impériales, commutation par projet. |
| M8 | Unifilaires | Symboles de distribution, représentation condensée sur liaisons multi-conducteurs, bilan de puissance et aide au dimensionnement. |
| M9 | Électronique | Bibliothèque de composants, repérage `R1`/`C2`, export de netlist vers SPICE et KiCad. |

## 7. Risques

**Le DWG natif est hors d'atteinte.** Aucune bibliothèque libre ne lit ou n'écrit le DWG de
façon fiable ; seul le SDK Open Design Alliance le fait, sous licence payante.
*Parade* — DXF dans les deux sens, qu'AutoCAD ouvre nativement. À annoncer d'emblée plutôt
qu'à découvrir en démonstration.

**La bibliothèque de symboles est le coût caché.** Des centaines de symboles à dessiner et
vérifier ; c'est régulièrement ce qui enlise ce type de projet — le moteur marche, mais il
n'y a rien à poser dessus. *Parade* — l'éditeur de symboles arrive en M2, pas en fin de
parcours : les symboles deviennent du contenu produit en continu, y compris par des
non-développeurs. Étudier aussi la réutilisation du fonds QElectroTech (licence à vérifier
avant tout emprunt).

**Les calculs de distribution engagent une responsabilité.** Courant de court-circuit,
sélectivité, chute de tension : un chiffre faux a des conséquences physiques et juridiques.
*Parade* — positionner M8 en *aide au dimensionnement*, hypothèses affichées, jamais de note
de calcul présentée comme certifiée.

**QGraphicsView a un plafond.** Au-delà de quelques dizaines de milliers d'entités visibles
la fluidité se dégrade. *Parade* — un folio réel en compte quelques milliers, le plafond est
loin ; le rendu reste isolé derrière une interface pour rester remplaçable sans toucher au cœur.

## 8. Structure du dépôt

```
dessins/
├── CMakeLists.txt
├── cmake/                 modules, détection de Qt, empaquetage
├── src/
│   ├── core/              modèle, géométrie, connectivité, annulation — sans GUI
│   ├── symbols/           format de symbole, chargement des bibliothèques
│   ├── io/                .dsn, DXF, PDF, CSV
│   ├── rules/             profils CEI/ANSI, repérage, rapports
│   ├── ui/                scène, outils, palette, inspecteur
│   └── app/               main.cpp, fenêtre principale
├── libraries/             symboles CEI et ANSI (données, pas du code)
├── templates/             gabarits de feuille et cartouches
├── tests/                 Catch2, cœur sans écran
├── docs/                  ce brief, format de fichier, guide de contribution
└── .github/workflows/     compilation + tests Linux, puis Windows et macOS
```

## 9. Questions ouvertes

Aucune ne bloque le démarrage de M0. Les deux premières deviennent structurantes avant M5.

1. **Système d'exploitation prioritaire ?** L'industrie est massivement sous Windows. Cela
   change l'ordre de la CI et de l'empaquetage, pas le code.
2. **Licence du projet : libre ou propriétaire ?** Qt en LGPL autorise les deux en liaison
   dynamique, mais cela conditionne quelles bibliothèques tierces et quels fonds de symboles
   sont réutilisables.
3. **Faut-il lire des fichiers existants ?** Un fonds de plans AutoCAD, EPLAN ou SEE à
   reprendre ferait remonter M6 avant M5.
4. **Mono-poste ou projets partagés ?** Un partage à plusieurs plus tard change dès
   maintenant la granularité du format de fichier.

---

*Chaîne de compilation vérifiée dans l'environnement de développement : g++ 13.3, CMake 3.28,
Ninja 1.11, Qt 6.4.2.*
