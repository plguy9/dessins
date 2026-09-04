# Étape 06 — Les boîtes de dialogue : `ZoneBox`, puis six écrans

**Fichiers touchés :** nouveaux `src/ui/zonebox.h/.cpp` et `src/ui/numberfield.h/.cpp` ;
`src/ui/titleblockeditor.cpp` (le cas d'école) ; puis `pagesetupdialog`,
`draftingsettingsdialog`, `componentdialog`, `plcdialog`, `wiretypedialog`.

**Risque :** moyen. Deux widgets nouveaux, six dialogues à reprendre. Aucun changement de
modèle.

---

## Le problème

Le compositeur de cartouche mélange sur une même pile de quatorze champs de poids égal :
la **géométrie** (X, Y, largeur, hauteur), le **contenu** (libellé, champ, texte fixe,
colonnes), la **typographie** (deux hauteurs) et le **cadre**. Quatre natures, un seul rang.

Un champ de 590 px reçoit « 68.0 mm ». « Charger un logo… » reste offert alors que la case
sélectionnée est du texte fixe. La liste des cases met « Table references » et « REFERENCES »
au même niveau.

## 6.1 — `ZoneBox` : le groupe, en bandeau de zone

Le même motif que le ruban : un libellé gravé dans une bande de 18 px filetée, et le contenu
dessous. Un seul widget, réutilisé par les six dialogues.

`src/ui/zonebox.h` :

```cpp
// Un groupe de reglages, coiffe d'un bandeau de zone.
//
// C'est le meme motif que le nom d'un panneau de ruban et que les bandeaux
// CHAMP | BOITE DE JONCTION du cadre de folio : libelle grave dans une bande
// de 18 px filetee en haut et en bas, contenu dessous. Regle 2 — des filets,
// pas des boites : ZoneBox ne dessine JAMAIS de cadre ferme, contrairement a
// QGroupBox, et c'est toute la raison de son existence.
#pragma once

#include <QWidget>

class QFormLayout;
class QLabel;
class QVBoxLayout;

namespace dsn {

class ZoneBox : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kBandHeight = 18;

    explicit ZoneBox(const QString &title, QWidget *parent = nullptr);

    // Le conteneur du contenu. On y pose ce qu'on veut : QFormLayout pour une
    // suite de champs, QHBoxLayout pour une rangee de nombres.
    QWidget *body() const { return m_body; }
    void setBodyLayout(QLayout *layout);

    // Un groupe entier peut disparaitre selon le contexte — c'est ce qui
    // remplace le grisage.
    void setTitle(const QString &title);

private:
    QLabel *m_band = nullptr;
    QWidget *m_body = nullptr;
};

} // namespace dsn
```

`zonebox.cpp` :

```cpp
ZoneBox::ZoneBox(const QString &title, QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    m_band = new QLabel(title.toUpper(), this);
    m_band->setFixedHeight(kBandHeight);
    m_band->setContentsMargins(Theme::space(5), 0, Theme::space(5), 0);
    m_band->setProperty("zoneBand", true);   // meme regle CSS que le ruban
    Theme::engrave(m_band);
    column->addWidget(m_band);

    m_body = new QWidget(this);
    column->addWidget(m_body);
}
```

La règle de feuille de style est **celle du ruban** (`QLabel[zoneBand="true"]`, étape 03) :
un seul motif, une seule règle, deux surfaces.

## 6.2 — `NumberField` : un champ dimensionné à son contenu

La largeur d'un champ annonce ce qu'on attend dedans. 590 px pour « 68.0 mm » est un
mensonge ; 92 px est une phrase.

```cpp
// Un nombre, en millimetres, dans un champ dimensionne a son contenu.
//
// La largeur d'un champ annonce ce qu'on attend dedans : 12 caracteres pour
// une cote, pas la colonne entiere. L'unite est GRAVEE dans le champ, a
// droite, au troisieme niveau d'encre — comme l'unite d'une cote sur une
// planche, et non comme un libelle a lire.
class NumberField : public QWidget
{
    Q_OBJECT
public:
    explicit NumberField(const QString &unit = QStringLiteral("mm"),
                         QWidget *parent = nullptr);

    static constexpr int kWidth = 92;      // 12 caracteres en monoFont(8)
    static constexpr int kNarrow = 82;     // pour les hauteurs de texte

    double value() const;
    void setValue(double value);
    void setRange(double min, double max);
    void setDecimals(int decimals);

Q_SIGNALS:
    void valueChanged(double value);

private:
    QDoubleSpinBox *m_spin = nullptr;      // sans boutons flechees
    QLabel *m_unit = nullptr;
};
```

Le `QDoubleSpinBox` en `NoButtons`, `monoFont(8)`, virgule décimale française
(`QLocale::system()`), et l'unité en `engravedFont()` à droite.

## 6.3 — Le compositeur de cartouche

### La trouvaille de cette étape

**Un cartouche est un dessin.** Sa géométrie s'édite donc **sur l'aperçu**, à la poignée et à
la cote, pas dans quatre champs numériques qui se disputent la colonne. C'est la grammaire de
la planche appliquée à une boîte de dialogue, et c'est ce qui rend le groupe POSITION
secondaire au lieu de dominant.

Sur l'aperçu :

- la case sélectionnée porte un **filet d'accent de 1,6 px** et **quatre poignées de 6 px**
  aux coins (couleur `accent`) ;
- une **cote** sous la case donne sa largeur, une autre depuis le bord gauche donne son X, une
  troisième à droite donne sa hauteur — au trait fin, en couleur `dimension` (`#5a6260`), avec
  la valeur en chasse fixe ;
- tirer une poignée met à jour les champs, et saisir dans un champ déplace la poignée.

Les quatre champs `NumberField` restent — sur **une seule rangée**, en lecture rapide — mais
ils ne sont plus le seul chemin. La phrase au deuxième niveau d'encre le dit :
*« ou tirez les poignées sur l'aperçu — les cotes suivent, comme sur une planche »*.

L'aperçu reste **en haut, pleine largeur** : un cartouche fait 330 × 35 mm, soit un rapport de
9,4 pour 1. Le mettre en colonne de gauche gaspillerait la place au nom d'une symétrie que la
forme de l'objet ne justifie pas. Il est **sur papier blanc à l'échelle 1:1**, filets à
l'encre du dessin — le seul blanc pur de la boîte, comme partout ailleurs (règle 6).

### Les quatre groupes

```cpp
    auto *position = new ZoneBox(tr("Position"), this);
    // X · Y · | · Largeur · Hauteur — quatre NumberField sur UNE rangee, avec
    // un filet entre le point et la taille : deux natures de nombre.
    auto *row = new QHBoxLayout;
    row->addWidget(labelled(tr("X"), m_x));
    row->addWidget(labelled(tr("Y"), m_y));
    row->addWidget(verticalRule());
    row->addWidget(labelled(tr("Largeur"), m_width));
    row->addWidget(labelled(tr("Hauteur"), m_height));
    row->addWidget(hint(tr("ou tirez les poignées sur l'aperçu — les cotes suivent, "
                           "comme sur une planche")), 1);
    position->setBodyLayout(row);

    auto *content = new ZoneBox(tr("Contenu"), this);
    // Nature en boutons exclusifs — quatre choix courts, donc une rangee et
    // non une liste deroulante : on voit les quatre etats possibles d'un coup.
    //   Texte fixe · Champ · Table · Image

    auto *text = new ZoneBox(tr("Texte"), this);
    //   Hauteur (82) · Libellé (82) · | · Alignement (3 boutons) · Disposition

    auto *border = new ZoneBox(tr("Cadre"), this);
    //   [x] Encadrer la case    filet de 0,25 mm, comme les cases voisines
```

### La nature commande le formulaire

C'est le correctif qui compte le plus pour l'usage. Les champs des autres natures sont
**retirés, pas grisés** :

| Nature | Champs affichés |
|---|---|
| Texte fixe | Libellé *(facultatif)* · **Texte fixe** |
| Champ | Libellé *(facultatif)* · **Champ** (liste des champs du projet) |
| Table | Libellé *(facultatif)* · **Colonnes** (`NO ; DATE ; DESCRIPTION`) |
| Image | **Fichier** (`Charger une image…` + nom du fichier chargé) |

```cpp
void TitleBlockEditor::applyNature(CellNature nature)
{
    // Retirer, pas griser : « Charger un logo » sous une case de texte est
    // une question a laquelle il n'y a rien a repondre. Un champ grise laisse
    // croire qu'il existe un etat ou il servirait ici — il n'y en a pas.
    m_rowFixedText->setVisible(nature == CellNature::FixedText);
    m_rowField->setVisible(nature == CellNature::Field);
    m_rowColumns->setVisible(nature == CellNature::Table);
    m_rowImage->setVisible(nature == CellNature::Image);
    m_boxText->setVisible(nature != CellNature::Image);   // un logo n'a pas de corps
    adjustSize();
}
```

Les deux boutons « Charger un logo… » / « Charger un sceau… » fusionnent en un seul
« Charger une image… » : la distinction logo/sceau appartient à la case, pas au bouton.

### La liste des cases : `QTreeWidget`, pas `QListWidget`

Dix-neuf entrées à plat mettaient « Table references » et « REFERENCES » au même rang.
En arbre, avec le nom de code du champ aligné à droite en chasse fixe au troisième niveau
d'encre :

```
▸ Table routing
    CHEMINEMENT
▸ Table references
    REFERENCES                    ← sélectionnée, filet d'accent à gauche
▸ Table revisions
    REVISIONS
  APPROBATION
    Demandé                            requestedBy
    Vérifié                             checkedBy
    Approuvé                          approvedBy
  COORDONNÉES
    Secteur                                sector
    Projet                              reference
    Dossier                               fileRef
    Dessiné                                author
    Échelle                                 scale
    Date                                     date
  Image · sceau
  (cadre)
```

Sélection : **filet d'accent de 2 px à gauche** + plan `elevated`. Pas d'aplat bleu.

## 6.4 — Le pied de la boîte

```
Le gabarit est enregistré dans le dossier : tous les folios qui le portent
suivent, y compris à l'export PDF.              [ Annuler ]  [ Appliquer au dossier ]
```

La phrase à gauche, au troisième niveau d'encre, dit **la portée de l'action** — la question
que pose forcément un bouton nommé « Appliquer au dossier ».

Le bouton par défaut se distingue par son **aplat**, pas par sa graisse : le piège Qt est déjà
documenté dans l'écran d'accueil (un `font-weight` sur le bouton par défaut rogne son texte,
Qt calculant sa taille dans l'état normal).

## 6.5 — Puis les cinq autres

Le patron est réutilisable tel quel. Par ordre de rendement :

| Dialogue | Groupes proposés |
|---|---|
| `pagesetupdialog` | `FEUILLE` (format, orientation, marges) · `CADRE` (zones, colonnes, rangées) · `CARTOUCHE` (gabarit) |
| `draftingsettingsdialog` | `GRILLE` · `ACCROCHAGE` · `REPÉRAGE` · `AFFICHAGE` — remplace les onglets par des `ZoneBox` empilées, tout visible d'un coup |
| `componentdialog` | `REPÈRE` · `CATALOGUE` · `RATTACHEMENT` · `BROCHES` |
| `plcdialog` | `MODULE` · `ADRESSAGE` · `VOIES` |
| `wiretypedialog` | `IDENTITÉ` · `TRAIT` · `CONDUCTEURS` |

Deux règles qui valent pour les cinq :

1. **Tout nombre passe en `NumberField`.** Un champ de la largeur de la colonne pour une cote
   est le défaut le plus répandu des six boîtes.
2. **Aucun champ grisé.** Si un réglage ne s'applique pas dans le contexte courant, il
   disparaît — et si c'est tout un groupe, la `ZoneBox` disparaît avec son bandeau.

Pour `draftingsettingsdialog` en particulier : les onglets (« Repérage polaire » etc.) cachent
des réglages qui se règlent ensemble. Des `ZoneBox` empilées dans une boîte plus haute valent
mieux qu'un `QTabWidget` — on voit l'état complet du dessin d'un coup d'œil, ce qui est
justement ce qu'on vient vérifier.

## Critères d'acceptation

1. `ZoneBox` ne dessine aucun cadre fermé (règle 2) et utilise la **même** règle CSS que les
   noms de panneau du ruban.
2. Aucun champ numérique ne dépasse `NumberField::kWidth` (92 px) dans les six dialogues.
3. Changer la Nature d'une case **retire** les champs des autres natures ; aucun champ grisé
   ne subsiste.
4. La liste des cases montre la hiérarchie table → case par indentation.
5. L'aperçu du cartouche est sur papier blanc, à l'échelle, avec poignées et cotes ; tirer une
   poignée met à jour les `NumberField` et l'inverse aussi.
6. Aucun `font-weight` sur un bouton par défaut.
7. La virgule décimale suit la locale française dans tous les `NumberField`.

**Maquette de référence :** `design/Arcus - compositeur de cartouche v2.dc.html`. La bascule
« Nature » de la maquette montre le formulaire se réécrire — c'est le comportement attendu.
