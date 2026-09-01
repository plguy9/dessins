# Le format `.dsn`

Un fichier `.dsn` est une **archive ZIP** contenant du **JSON**. Le choix suit
celui d'ODF ou de KiCad, et pour les mêmes raisons : les symboles voyagent avec
le document, la version du format est inscrite dedans, et un fichier abîmé
reste inspectable avec un simple outil d'archivage plutôt que perdu.

```
demarrage-direct.dsn
├── mimetype        application/x-dessins-project
├── meta.json       versions du conteneur et du document, logiciel producteur
├── project.json    le projet : informations, profil, folios, entités
└── library.json    les définitions de symboles utilisées par le projet
```

Un **JSON nu** est également accepté à l'ouverture. C'est ce que produit une
récupération manuelle après un incident, et le refuser serait punir
l'utilisateur au pire moment ; le logiciel prévient simplement que la
bibliothèque embarquée est absente.

## Versions

`meta.json` porte deux numéros distincts :

- `container` — la structure de l'archive ;
- `document` — le schéma de `project.json`.

Un document écrit par une version **plus récente** est refusé net, avec un
message explicite. Mieux vaut un refus clair qu'un document silencieusement
tronqué. En revanche, une **entité inconnue** à l'intérieur d'un document
lisible est ignorée : le fichier s'ouvre en perdant cette entité plutôt que de
ne pas s'ouvrir du tout.

## `project.json`

```json
{
  "version": 1,
  "profile": "iec",
  "wireTypes": [
    { "id": "default", "name": "Fil standard", "color": "#0a5c9e",
      "width": 0.35, "layer": "FILS" },
    { "id": "l1", "name": "L1 — phase 1", "color": "#7a4a2b", "width": 0.35,
      "crossSection": "2,5 mm²", "layer": "FILS_L1" }
  ],
  "info": {
    "title": "Démarrage direct d'un moteur",
    "reference": "2026-014",
    "client": "Atelier mécanique Beauport",
    "author": "Dessins",
    "revision": "A",
    "date": "2026-09-01"
  },
  "folios": [ … ]
}
```

`profile` désigne le profil métier — `iec`, `ansi` ou `electronic`. Il porte la
norme de symboles, le pas de grille, le format de feuille par défaut et les
règles de repérage.

`designationFormat` et `designationMode` portent la convention de repérage du
projet et priment sur celle du profil : `sequential` compte par famille (`K1`,
`K2`), `lineReference` fait dire au repère où trouver l'appareil (`104K`).
Absents, c'est le profil qui décide.

`wireTypes` est la bibliothèque des types de fils du projet. Un fil ne stocke
jamais sa couleur : il référence un `id` de type. La couleur s'écrit `#rrggbb`
et `style` vaut `solid` (défaut), `dashed`, `dotted` ou `dashdot`. Le type
`default` existe toujours — c'est le repli de tout identifiant inconnu, et il
est reconstruit à la lecture s'il manquait. Un document **antérieur** aux types
de fils n'a pas la clé : il reçoit alors le jeu de sa norme, pas une liste
vide.

## Un folio

```json
{
  "id": "3f2a91c4",
  "number": "1",
  "title": "Circuit de puissance",
  "sheet": "A3",
  "sheetWidth": 420.0,
  "sheetHeight": 297.0,
  "frame": { "margin": 10, "bindingMargin": 20, "columns": 10, "rows": 6 },
  "entities": [ … ]
}
```

La taille est écrite **en plus** de l'identifiant de format : un format retiré
d'une version ultérieure reste alors relisible tel qu'il a été dessiné.

Les coordonnées sont en **millimètres**, en nombres à virgule, arrondies au
millième à l'écriture. La précision utile est le centième ; tronquer évite des
fichiers pollués par des flottants à quinze décimales qui rendent tout diff
illisible.

## Les six types d'entités

| `type` | Rôle | Champs propres |
|---|---|---|
| `symbol` | Instance d'un symbole | `def`, `placement`, `fields`, `group`, `block` |
| `wire` | Fil, éventuellement multi-conducteurs | `points`, `conductors`, `number`, `wireType` |
| `junction` | Point de connexion dessiné | `at`, `diameter` |
| `text` | Annotation libre | `text`, `placement`, `height`, `align` |
| `graphic` | Primitive de tracé | `shape` |
| `label` | Étiquette de potentiel, renvoi de folio ou flèche de signal | `at`, `name`, `dir`, `scope`, `role` |

### Les liaisons multi-conducteurs

Un fil porte **n conducteurs** dès l'origine, avec n = 1 comme cas courant :

```json
{ "type": "wire",
  "points": [[140, 60], [140, 110]],
  "conductors": ["L1", "L2", "L3", "N", "PE"],
  "wireType": "l1",
  "number": "W12" }
```

`conductors` vide signifie un conducteur anonyme. C'est cette décision, prise
au premier jalon plutôt qu'au huitième, qui rend l'unifilaire — où un trait
unique représente L1/L2/L3 + N + PE — réalisable sans réécrire le cœur.

Deux fils qui se rejoignent apparient leurs conducteurs **par nom** quand les
deux jeux sont nommés, **par rang** sinon : l'ordre de saisie des conducteurs
d'un câble n'a pas à être le même des deux côtés d'une borne.

### Les flèches de signal

`role` vaut `source` ou `destination` sur une flèche de signal, et est absent
sur une étiquette ordinaire. Les deux bouts portent le **même nom de code** et
deviennent un seul potentiel. Une flèche est inter-folios par construction :
la lecture force `scope` à `project`, un fichier qui dirait le contraire
rendrait la flèche muette.

Le renvoi affiché — « → 2/A3 », le folio et la zone de l'autre bout — n'est
jamais écrit dans le fichier : il se déduit du dessin, comme la netlist.

### Le repérage manuel

`numberLocked` sur un fil et `designationLocked` sur un symbole marquent une
saisie manuelle. Le repérage automatique ne les écrase jamais — et sur un
potentiel, un repère verrouillé quelque part gouverne tout le potentiel, parce
qu'un même fil électrique ne peut pas porter deux repères.

### Les étiquettes

`scope` vaut `folio` ou `project`. Une étiquette de folio ne fusionne les
potentiels que dans sa page ; un renvoi de folio les fusionne à travers tout le
dossier. C'est ce qui rend la continuité électrique inter-folios réelle et non
simplement dessinée.

## `library.json`

Le même format que les fichiers de `libraries/` :

```json
{
  "format": "dessins-symbol-library",
  "version": 1,
  "symbols": [
    {
      "id": "iec:coil",
      "logicalId": "coil",
      "norm": "IEC",
      "name": "Bobine de contacteur",
      "category": "Commande",
      "prefix": "K",
      "deviceKind": "coil",
      "graphics": [ { "kind": "rect", "points": [[-5,-4],[5,4]] } ],
      "pins": [ { "number": "A1", "at": [0,-10], "dir": 270, "length": 6 } ]
    }
  ]
}
```

`logicalId` est la clé qui relie les variantes CEI et ANSI du même symbole :
deux définitions qui le partagent deviennent commutables, et un projet bascule
d'un jeu de symboles à l'autre sans toucher à ses instances.

La bibliothèque embarquée dans un document **écrase** celle du poste au
chargement : un dossier archivé doit se rouvrir exactement tel qu'il a été
dessiné, même si la bibliothèque a évolué depuis.

## Ce qui n'est pas dans le fichier

La **netlist n'est jamais enregistrée**. Les potentiels sont recalculés à
l'ouverture depuis la géométrie. Une netlist stockée finirait par diverger du
dessin, et un rapport qui diverge du schéma est pire que pas de rapport.
