# Le schéma de boucle — relevé sur trois planches réelles

Relevé fait le 2026-09-03 sur **trois planches de production** fournies par
l'utilisateur : deux d'un même bureau d'études québécois (1999, tracées en 2009
au format lettre) et une d'un autre bureau (2025, format A2, « POUR
CONSTRUCTION »). Papeterie, secteur pâtes et papiers.

> **Aucune donnée client dans ce document.** Ni nom de firme, ni nom de personne,
> ni numéro d'affaire, ni description de procédé. Ce qui est relevé ici, ce sont
> des **conventions de dessin** — la forme, pas le contenu. Les planches
> elles-mêmes ne sont pas versionnées et ne le seront pas : le dépôt est public.

---

## Le constat qui compte

**Les trois planches sont des schémas de boucle** (*diagramme de connexions*,
*loop diagram*) : une boucle d'instrumentation par feuille, suivie de bout en
bout — capteur au champ → boîte de jonction → armoire → carte d'automate.

Arcus a été construit sur le modèle d'AutoCAD **Electrical** : échelle de
commande, contacteurs, bobines, contacts. Or **le travail quotidien de
l'utilisateur est le schéma de boucle**, qui est un autre document : autre
structure de page, autre bibliothèque, autres rapports.

Deux choses donnent du poids à ce constat :

1. **Seize ans et deux bureaux d'études séparent la planche la plus ancienne de
   la plus récente, et la mise en page est la même** — bandes verticales
   nommées, même découpage de cartouche, même façon de nommer les conducteurs.
   Ce n'est pas l'habitude d'un dessinateur : c'est une convention de métier.
2. Rien de ce qui est décrit ci-dessous n'est *incompatible* avec ce qui existe.
   Les fils, les symboles, les bornes, les renvois, la netlist servent tels
   quels. Ce qui manque est au-dessus : la **structure de la planche** et la
   **bibliothèque**.

---

## 1. La page est découpée en bandes verticales nommées

C'est la caractéristique la plus visible, et Arcus n'a rien qui y ressemble.

La feuille est coupée sur toute sa hauteur par des traits pleins, en deux à
trois **bandes**, chacune portant son nom dans un bandeau d'en-tête :

```
┌───────────────────────┬──────────────────────────┐
│        CHAMP          │   CABINET 037BJ0151      │   ← bandeau, trait plein dessous
├───────────────────────┼──────────────────────────┤
│                       │                          │
│   capteur, vanne,     │   bornes, carte auto.,   │
│   manomètres…         │   adresse                │
```

Sur la planche de 2025, trois bandes : `PROCEDE` | `SUR LE CHANTIER` |
`028BJ0151-A1 (022PLC0154)` — et la troisième est **coupée en deux
horizontalement**, la moitié basse portant son propre en-tête
`028BJ0151-A7 (022PLC0154)`.

**Ce que la bande veut dire.** Elle n'est pas décorative : c'est la
**localisation** de tout ce qu'elle contient. Un capteur est *au champ*, une
borne est *dans l'armoire 037BJ0151*. C'est exactement ce qu'un rapport de
câblage doit imprimer dans ses colonnes « de » et « vers ».

**Conséquence pour nous.** La bande est à un schéma de boucle ce que l'échelle
de commande (`rules/ladder.*`) est à un schéma de commande : la structure qui
organise la page. Elle se pose une fois, elle se redimensionne, et
l'appartenance d'une entité se **déduit de son abscisse** — comme
`Folio::zoneAt()` déduit déjà la zone du cadre. Rien à stocker sur l'entité.

---

## 2. Le repérage du cadre va de **droite à gauche**

Sur les trois planches :

- les lettres de zone se lisent `E D C B A` de gauche à droite — **la zone A
  est à droite**, du côté du cartouche ;
- les chiffres se lisent `4 3 2 1` de haut en bas — **le 1 est en bas**.

Arcus fait l'inverse dans les deux directions (A en haut à gauche, 1 à gauche).
Ce n'est pas un détail d'affichage : **tous les renvois du dossier en
dépendent**, et une planche reproduite avec l'autre convention renvoie vers la
mauvaise case.

À prévoir dans le profil de dessin : sens des lettres (gauche→droite ou
droite→gauche) et sens des chiffres (haut→bas ou bas→haut), indépendamment.

---

## 3. Le cartouche est une structure de données, pas un dessin

Le nôtre est dessiné en dur dans le peintre. Le leur porte **trois tables qui
grandissent** et une quinzaine de champs.

**Tables** (en bas de planche, sur toute la largeur) :

| Table | Colonnes |
|---|---|
| `CHEMINEMENT` | PAR, APP, DATE, DESCRIPTION, REV |
| `REFERENCES` | NO DESSIN, DESCRIPTION |
| `REVISIONS` | NO, ZONE, DATE, DESCRIPTION, PAR, VER, APP |

Les lignes s'ajoutent **vers le haut** : la révision 0 est en bas, la 1
au-dessus. Une ligne réelle de la table `REFERENCES` : le numéro de la planche
*Liste des dessins* du dossier — c'est-à-dire que le sommaire est référencé
depuis chaque feuille.

**Champs** (bloc de droite) :

- un pavé `APPROBATION` avec le **sceau d'ingénieur** — une **image**, que nous
  ne savons pas poser aujourd'hui ;
- `DEMANDÉ PAR` / `VÉRIFIÉ` / `APPROUVÉ`, trois noms ;
- `COORDONNÉES` : `SECTEUR`, `PROJET`, `DOSSIER`, `TRACÉ @` (l'échelle du
  tracé, `1=1`) ;
- `DESSINÉ PAR`, `ÉCHELLE`, `DATE` ;
- **quatre lignes libres de description** — sur les planches relevées : le
  procédé, la fonction mesurée, le numéro de boucle, et le type de document.
  C'est le « Description 1/2/3 » déjà noté dans le relevé AutoCAD, et il en
  faut **quatre**, pas trois ;
- le **logo** du client — encore une image ;
- le numéro de dessin, la **révision**, le **format** (A2/A3) et la feuille
  (`1/1`).

**Conséquence pour nous.** Le cartouche doit devenir un **gabarit** rangé dans
le projet : une liste de cellules (rectangle, libellé gravé, clef de champ,
alignement), plus deux tables alimentées par des données de projet
(révisions, références). Un bureau d'études ne changera jamais de logiciel s'il
ne peut pas sortir *son* cartouche.

---

## 4. Ce que la bibliothèque n'a pas

Par ordre de fréquence sur les planches relevées.

| Symbole | Ce que c'est | Remarque |
|---|---|---|
| **Bulle d'instrument** | cercle, deux lignes de texte (`TT` / `8917A`) | Le symbole le plus posé de la planche. Variantes normatives ISA S5.1 : cercle nu = au champ ; cercle barré d'un trait horizontal = en façade de panneau ; cercle dans un carré = système partagé (DCS/automate) ; trait pointillé = inaccessible. **La variante porte un sens**, comme nos marqueurs d'accrochage. |
| **Borne à vis** | ⊘ — cercle barré en diagonale, empilé en bandeau | Numérotée `1`, `2`, `SHD`. Le blindage a **sa propre borne**. |
| **Étiquette de câble** | ellipse posée *sur* le fil, portant le nom du câble | Avec le type écrit au-dessus (`1PR#16CU`). |
| **Paire torsadée** | deux conducteurs, glyphe de torsade, accolade | Et le tracé de la paire dessiné comme un **câble arrondi**, pas deux traits parallèles. |
| **Blindage** | drain en zigzag vers la borne `SH`/`SHD` | |
| **Boîte de jonction** | cadre **pointillé** nommé, contenant des bornes | Le pointillé existe depuis le bloc « essai » ; le cadre nommé, non. |
| **Vanne de réglage** | corps de vanne + servomoteur à membrane | |
| **Positionneur**, **convertisseur I/P**, **filtre-régulateur** | boîtes annotées | |
| **Manomètre** | cercle avec l'échelle (`0-60#`, `20-100 kPa`) | |
| **Débitmètre électromagnétique**, **transmetteur de débit** | corps de tuyauterie | |
| **CVC** | ventilateur, volet, serpentin, filtre | Sur la planche de 2025. |
| **Résistance de charge** | zigzag posé sur un conducteur, avec sa valeur (`500 Ω`) | |

---

## 5. Le nommage — et ce que nos formats ne savent pas écrire

### Repère d'instrument

```
022 TT 8917 A
 │   │   │   └── suffixe (plusieurs instruments d'une même boucle)
 │   │   └────── numéro de boucle
 │   └────────── fonction ISA (TT, FT, TCV, FCV, NT, NCV, NY…)
 └────────────── secteur
```

`DesignationRule::tagFormat` sait faire `%F%N` et la référence de ligne. Il ne
sait pas composer **secteur** + **fonction** + **boucle** + **suffixe** : il
lui manque deux champs et la notion de boucle.

### Nom de conducteur

```
022TT8917A-%N04R07S07C016+ (N)
└─ repère ─┘└── adresse ──┘│  └── couleur
                           └──── polarité
```

Deux manques :

- **la couleur est une lettre**, pas une teinte : `(N)` noir, `(B)` blanc,
  `(R)` rouge, `(G)` vert, `(O)` orange, `(RO)` rouge-orange. Notre `WireType`
  porte une couleur `0xRRGGBB` pour l'écran ; **c'est le code qui s'imprime**,
  et nous ne l'avons pas ;
- le conducteur porte **la polarité** (`+` / `−`), qui n'est pas un repère de
  fil mais un rang dans la paire.

### Adresse d'automate

```
%N04R07S07C016      Nœud 04, Rack 07, Slot 07, Canal 016
%R04S06C001         (sans nœud)
```

`rules/plc.*` sait écrire `I:3/00`, `%I0.0`, `%I0.2.5`. Il ne sait pas ce
format, et surtout il n'a **pas la notion de nœud** — or elle est écrite en
toutes lettres à côté de chaque carte :

```
MODULE:  SAM (ENTREE)
NOEUD:   04
ADRESSE: %R04S06C001
```

### Câble

```
1PR#16CU    1 paire, calibre 16 AWG, cuivre
2PR#16CU    2 paires
12PR#16CU   12 paires (le câble multipaire du tronc)
```

Notre `WireType` porte une **section en mm²**. Il lui manque le **calibre
AWG**, le **nombre de paires** et le **blindage** — c'est-à-dire tout ce qui
fait un câble d'instrumentation.

---

## 6. Ce que ça change pour les rapports

Un dossier de boucles produit naturellement :

- **la liste des boucles** — une ligne par boucle : instrument, type, carte,
  nœud, adresse, planche. Nous ne l'avons pas ;
- **la liste des câbles** — nom, type (`2PR#16CU`), de → vers, blindage. Nous
  avons une liste de **fils**, ce qui n'est pas la même chose : un câble
  regroupe n paires et c'est lui qu'on commande ;
- **la liste d'E/S** — nous l'avons déjà (`rules/plc.*`) ;
- **la liste des dessins** — référencée depuis chaque feuille, déjà notée comme
  manquante dans le relevé AutoCAD.

---

## Bloc D proposé — le schéma de boucle

Dans l'ordre de valeur. Chaque point est nécessaire pour que la planche
reproduite ressemble à la leur.

| | | Pourquoi maintenant |
|---|---|---|
| **D1** | Bandes de localisation + sens du repérage du cadre configurable | Sans elles, aucune de leurs planches n'est reproductible, et le rapport de câblage ne sait pas dire « du champ vers l'armoire ». |
| **D2** | Cartouche piloté par un gabarit + tables Révisions / Références / Cheminement + logo + 4 lignes de description | C'est la première chose qu'on regarde sur une planche, et aujourd'hui nous ne savons pas sortir le leur. |
| **D3** | Bibliothèque instrumentation : bulles ISA et leurs variantes, borne à vis, boîte de jonction, vanne + positionneur + I/P, manomètres, débitmètre | Le dessin lui-même. |
| **D4** | Le **câble** : n paires, calibre AWG, blindage, étiquette en ellipse, paire torsadée — et la **liste des câbles** | Le modèle multi-conducteurs est déjà là (`Wire::conductors`) ; il manque l'enveloppe et le rapport. |
| **D5** | Nommage : secteur + boucle dans le format de repère, code couleur des conducteurs, adresse Nœud/Rack/Slot/Canal | Ce qui fait qu'un plan est lisible par le câbleur qui l'a en main. |

**La preuve, comme pour les blocs précédents** : redessiner une des planches à
la main, par événements de souris et de clavier, et compter les accrocs.
