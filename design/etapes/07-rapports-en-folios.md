# Étape 07 — Les rapports deviennent des folios calculés

**Fichiers touchés :** `src/core/folio.h/.cpp`, `src/core/project.cpp`, `src/io/dsnfile.cpp`,
`src/rules/reportplacer.h/.cpp`, `src/render/foliopainter.cpp`, `src/ui/reportpanel.h/.cpp`,
`src/ui/foliotabs.cpp`, `src/render/pdfexport.cpp`, `tests/`.

**Risque : élevé — c'est le seul des sept chantiers qui touche au modèle et au format de
fichier.** À faire **en dernier**, dans son propre jalon, une fois les six autres en place.

---

## Le problème

Neuf tableaux — Récapitulatif, Nomenclature, Composants, Bornier, Fils, Câbles, Câblage
De/Vers, E/S automate, Contrôles — dans un `QTabWidget` posé dans un dock étroit en bas à
gauche.

Élargir le panneau ne réglera rien : un tableau de nomenclature à cinq colonnes a besoin de la
largeur d'une feuille, pas de 320 px. Et neuf onglets dans un panneau tronqué restent neuf
onglets tronqués.

## L'idée

Une nomenclature n'est pas un outil. **C'est une page du dossier** : elle s'imprime, elle se
relit, elle se livre. Arcus a déjà l'objet qu'il faut pour ça — le folio.

Un rapport posé prend un numéro de folio, un cartouche, un format et un indice de révision.
Il entre dans le PDF multi-folios sans manipulation, il s'imprime avec le reste, et le client
reçoit un dossier de cinq pages au lieu de deux plans plus trois CSV envoyés à part. **C'est
ce que fait l'atelier de toute façon** ; le logiciel arrête de le lui laisser faire à la main.

## La règle du dépôt est préservée — et même renforcée

> `reportpanel.h` : « Les tableaux sont recalculés depuis le document, jamais stockés. Un
> rapport qui diverge du schéma est pire que pas de rapport du tout. »

Le folio calculé ne stocke pas son contenu non plus. **Il stocke le fait qu'on veut cette
page.** Son contenu est repeint à l'ouverture, exactement comme les repères de fil et le
cartouche le sont déjà. Le badge `CALCULÉ` le dit à l'écran pour que personne ne cherche à
l'éditer.

C'est le point à ne pas rater à la relecture : si le tableau finit sérialisé dans le `.dsn`,
le chantier a échoué.

## 7.1 — `src/core/folio.h`

```cpp
// Un folio est soit DESSINE — l'utilisateur y pose des symboles et des fils —
// soit CALCULE : sa geometrie est produite a la volee depuis la netlist par
// rules/reportplacer.
//
// Le folio calcule ne stocke PAS son tableau. Il stocke le fait qu'on veut
// cette page, et son identifiant de rapport. Le contenu est repeint a
// l'ouverture, comme les reperes de fil et le cartouche le sont deja : un
// rapport qui diverge du schema est pire que pas de rapport du tout.
enum class FolioKind {
    Drawn,     // le defaut, et le seul avant la version 3 du format
    Computed,
};

class Folio
{
public:
    FolioKind kind() const { return m_kind; }
    void setKind(FolioKind kind);

    // Vide pour un folio dessine. Pour un folio calcule : « bom »,
    // « terminals », « wires »… — la clef stable du rapport, jamais son titre
    // traduit, qui changerait avec la langue.
    QString reportId() const { return m_reportId; }
    void setReportId(const QString &id);

    // Un folio calcule n'accepte aucune entite posee a la main. Le refus est
    // ici, dans le coeur : ni FolioView ni la ligne de commande n'ont a le
    // savoir, et aucun chemin ne peut donc l'oublier.
    bool acceptsEntities() const { return m_kind == FolioKind::Drawn; }

private:
    FolioKind m_kind = FolioKind::Drawn;
    QString m_reportId;
};
```

`acceptsEntities()` dans le cœur est le choix important : le refus est en un seul endroit, et
les commandes de dessin le consultent au lieu de chacune se souvenir de la règle.

## 7.2 — Le format `.dsn`

Version du format à incrémenter. Le folio gagne deux clefs **facultatives** :

```json
{
  "id": "folio-4",
  "number": 4,
  "title": "Nomenclature",
  "format": "A3",
  "kind": "computed",
  "report": "bom"
}
```

Migration : `kind` absent ⇒ `Drawn`. `kind` inconnu ⇒ `Drawn` + un avertissement à l'ouverture
(un fichier d'une version future ne doit pas perdre ses pages dessinées).

Les entités d'un folio calculé **ne sont pas écrites**. À la relecture, la page est reconstruite.

Voir `docs/FORMAT.md` : y documenter la clef, et la chaîne de migration.

## 7.3 — `rules/reportplacer`

Le fichier existe déjà et sait poser un tableau sur une feuille : c'est ce qui rend ce chantier
raisonnable. Ce qui manque :

```cpp
// Produit la geometrie d'un folio calcule : le tableau du rapport, mis en
// page dans le cadre du format demande, cartouche compris.
//
// Renvoie PLUSIEURS folios quand le tableau depasse une feuille : une
// nomenclature de trois cents lignes fait quatre pages, numerotees 4a, 4b…
// comme le fait un dossier papier. La pagination est la seule vraie
// nouveaute de cette etape ; le reste est du deja-ecrit a rebrancher.
QVector<FolioGeometry> ReportPlacer::layout(const ReportTable &table,
                                            const PageFormat &format,
                                            const TitleBlock &titleBlock,
                                            const QString &title);

// Nombre de lignes qui tiennent sur une feuille du format donne, hauteur de
// texte et cartouche compris. Sert a la pagination et a l'estimation « 2 p. »
// affichee dans la liste de choix.
int ReportPlacer::rowsPerPage(const PageFormat &format, double textHeight);
```

Colonnes de la nomenclature, largeurs en millimètres sur A3 (utiliser celles-ci, elles sont
réglées sur du contenu réel du `catalogue.json`) :

| Colonne | Largeur | Alignement | Fonte |
|---|---|---|---|
| REP. | 18 mm | gauche | chasse fixe |
| DÉSIGNATION | reste | gauche | proportionnelle |
| RÉFÉRENCE | 42 mm | gauche | chasse fixe |
| FABRICANT | 34 mm | gauche | proportionnelle |
| QTÉ | 12 mm | **droite** | chasse fixe |

En-tête à 2,4 mm, lignes à 2,0 mm, filet de séparation à 0,18 mm (`dimensionWidth` — un filet
de tableau n'est pas un conducteur), une ligne sur deux en fond très léger.

Sous le tableau, une ligne de pied au troisième niveau :
`10 articles · agrégés par référence fabricant · les appareils multi-blocs comptent une fois`.
Cette phrase désamorce la question qu'un chef d'atelier posera de toute façon en comptant les
contacteurs.

## 7.4 — `src/ui/reportpanel`

Le dock disparaît. Ce qui reste tient dans une petite boîte : la portée, et le choix des
tableaux à poser.

```cpp
// Le choix des rapports du dossier. Ce n'est plus un panneau de consultation
// — les tableaux sont devenus des pages — mais la question « quelles pages le
// dossier porte-t-il ? ».
class ReportChooser : public QDialog
{
    // ZoneBox PORTEE   : Tout le projet · Folio actif
    // ZoneBox TABLEAUX : une case par rapport, avec le nombre de pages
    //                    estime et, pour ceux deja poses, le numero de folio.
};
```

Règle à tenir : **un tableau vide ne devient pas un folio.** Le dossier ne porte pas de page
blanche. La case reste offerte, marquée `vide`, et le folio apparaît dès qu'il y a quelque
chose dedans.

`Contrôles` reste un cas à part : son compte d'anomalies doit rester visible sans ouvrir quoi
que ce soit. Il va dans la **barre d'état** — `CONTRÔLES  aucune anomalie` en `success`,
`3 anomalies` en `warning` — et non dans un onglet. C'est l'information qu'on veut voir sans
la chercher, et `reportpanel.cpp` le dit déjà en commentaire.

## 7.5 — L'affichage du folio calculé

- L'onglet de folio porte un badge `CALC` gravé, au troisième niveau d'encre.
- La barre de contexte du canevas affiche `CALCULÉ` dans un cadre filet, plus la phrase
  *« recalculé à l'ouverture — jamais enregistré »*.
- La bande de vignettes sépare les folios dessinés des folios calculés par un **filet
  vertical** : deux natures de page, un filet, pas de titre de section.
- Un clic dans le canevas d'un folio calculé ne pose rien : `acceptsEntities()` renvoie
  `false` et la ligne de commande écrit, en `warning`,
  *« ce folio est calculé — modifiez le schéma, la page suit »*.

## 7.6 — Export

`pdfexport` n'a rien à savoir de neuf : les folios calculés sont des folios, avec cadre et
cartouche, produits par le même peintre. C'est le bénéfice qui justifie tout le chantier.

Vérifier seulement que la génération se fait **avant** l'export (les pages calculées doivent
être à jour) et que la pagination est stable entre l'écran et le PDF — même
`rowsPerPage`, donc même résultat.

## 7.7 — Tests

```cpp
TEST_CASE("un folio calcule ne serialise pas son contenu")
{
    // poser un folio « bom », enregistrer, relire :
    // - le folio existe, kind == Computed, reportId == "bom"
    // - il ne porte AUCUNE entite dans le fichier
    // - apres relecture, sa geometrie est reconstruite et non vide
}

TEST_CASE("un tableau qui depasse la feuille se pagine")
{
    // 300 lignes sur A3 => plusieurs FolioGeometry, aucune ligne perdue,
    // aucune dupliquee, et l'en-tete repete sur chaque page.
}

TEST_CASE("un folio calcule refuse les entites")
{
    CHECK_FALSE(computedFolio.acceptsEntities());
}

TEST_CASE("un rapport vide ne produit pas de folio")
{
    // catalogue vide => pas de page blanche dans le dossier
}

TEST_CASE("un .dsn sans clef kind se lit en folios dessines")
{
    // migration : aucun fichier existant ne doit changer de comportement
}
```

## Critères d'acceptation

1. Poser Nomenclature, Bornier et Fils donne un dossier de cinq folios ; l'export PDF produit
   **cinq pages**, cartouche compris, sans manipulation.
2. Modifier le schéma puis rouvrir le folio Nomenclature montre le tableau à jour.
3. Aucune ligne de tableau dans le `.dsn` — vérifiable en décompressant le ZIP et en lisant le
   JSON.
4. Un `.dsn` d'une version antérieure s'ouvre sans perte, tous ses folios en `Drawn`.
5. Un tableau vide ne crée pas de folio.
6. Le compte d'anomalies de Contrôles est visible dans la barre d'état sans ouvrir de boîte.
7. Le dock des rapports n'existe plus ; `ReportChooser` le remplace.

**Maquette de référence :** `design/Arcus - rapports en folios v2.dc.html`. Les références
fabricant y sont celles de `catalog/catalogue.json`, les titres et la portée ceux de
`reportpanel.cpp` — la maquette est utilisable comme spécification de mise en page du tableau.
