# Étape 03 — Le ruban : bandeau de zone et raccourcis imprimés

**Fichiers touchés :** `src/ui/ribbon.h`, `src/ui/ribbon.cpp`, `src/ui/mainwindow.cpp`
(registre de commandes + construction du ruban), `src/ui/theme.cpp`, `tests/test_ui.cpp`.

**Risque :** moyen. Les hauteurs du ruban sont mesurées par des tests, et l'invariant
« tout ce qui est au ruban est au menu » est tenu par un autre. Aucun des deux ne doit céder.

---

## Le problème, correctement posé

L'onglet Accueil porte 47 commandes, dont **40 en icône de 20 px sans étiquette**. Le ruban
n'est pas en cause — les icônes muettes le sont.

Le dépôt a déjà un garde-fou : un test vérifie « qu'aucun glyphe n'est vide et qu'aucun n'en
répète un autre ». C'est une garantie **d'unicité**, pas de **reconnaissance**. Deux dessins
peuvent être différents et rester indiscernables à 20 px — c'est exactement le cas des huit
icônes du panneau FILS, dont les silhouettes ne diffèrent qu'à l'intérieur.

### Ce qui ne marche pas : étiqueter les petits boutons

Calcul fait, à ne pas refaire : un petit bouton étiqueté (icône 18 + libellé + alias) mesure
~112 px. Pour les 40 petits boutons de l'onglet Accueil en deux rangées, il faut **2 762 px de
ruban**. On en a 1 720 à 1920×1080. Ça ne tient pas, et ça ne tient pas non plus à 2560.

### Ce qui marche : imprimer l'alias

L'alias fait deux caractères, il se pose dans le coin de la case existante, il coûte **zéro
pixel de large**, et il résout le problème par l'information au lieu de l'image : `J` `RV`
`ET` `PT` sont discernables même si les glyphes se brouillent. C'est déjà la doctrine du
dépôt, écrite dans `dockrail.h` — *« le rail enseigne le clavier, comme le ruban enseigne le
nom des commandes »* — et appliquée pour l'instant à un rail de 24 px.

## 3.1 — Porter l'alias sur la `QAction`

Le registre de commandes existe déjà (`mainwindow.cpp` ~1870–2165, les appels `simple(...)` /
`add(...)`). L'alias y est, mais il ne va pas jusqu'au bouton. Une propriété suffit, et elle
respecte l'invariant : rien ne se connecte, le bouton lit son action.

Dans la fonction qui enregistre une commande, après avoir résolu l'action :

```cpp
// L'alias voyage avec l'action, comme le mnemonique et le raccourci. Le ruban,
// le rail et la palette de commandes le liront sans qu'aucun d'eux ne detienne
// la table — l'invariant « le ruban ne detient aucune commande » est intact.
if (action && !aliases.isEmpty())
    action->setProperty("alias", aliases.first());
```

Le **premier** alias est le bon : la table est écrite dans cet ordre (`LIGNE` → `{ "L",
"FIL" }`, `PIVOTER` → `{ "RO", "RT" }`), le premier est la forme courte.

### Table de contrôle — les alias réels du dépôt

À afficher tels quels. **Ne jamais inventer un alias** : c'est le seul point où ce mouvement
perd toute sa valeur.

| Commande | Alias | | Commande | Alias |
|---|---|---|---|---|
| LIGNE | `L` | | RECTANGLE | `REC` |
| JONCTION | `J` | | CERCLE | `CE` |
| ETIQUETTE | `ET` | | ARC | `A2` |
| RENVOI | `RV` | | POLYLIGNE | `PL` |
| POTENTIEL | `PT` | | COTATION | `CT` |
| SOURCE | `SO` | | COTATIONH / V | `CTH` / `CTV` |
| DESTINATION | `DE` | | TEXTE | `T` |
| APPLIQUERTYPE | `APT` | | TRAIT | `TR2` |
| TYPEFIL | `TF` | | JOINDRE | `JO` |
| FILMULTIPLE | `BUS` | | COUPURE | `COU` |
| DEPLACER | `DP` | | DISTANCE | `DI` |
| PIVOTER | `RO` | | ALIGNER | `AL` |
| MIROIR | `MI` | | REPARTIR | `REP` |
| COPIER | `CP` | | ETIRER | `ETI` |
| COLLER | `CC` | | GLISSER | `GL` |
| COLLERIDENT | `CCI` | | ECHELLE | `EC` |
| COPIERPROP | `CPR` | | RESEAU | `RE` |
| DECALER | `DC` | | INSERER | `I` |
| AJUSTER | `AJ` | | COMPOSANT | `EDC` |
| PROLONGER | `PR` | | REMPLACERSYMBOLE | `RS` |
| FIXERREPERE | `FR` | | BORNIER | `BO` |
| LIBERERREPERE | `LR` | | AUTOMATE | `API` |
| REPERAGE | `RN` | | SURFER | `SF` |
| AUDIT | `CONTROLE` | | CARTOUCHE | `CA` |
| RAPPORTS | `BOM` | | FORMATREPERE | `FORMAT` |
| ORTHO | `OR` | | POLAIRE | `PO` |
| ACCROBJ | `OS` | | GRILLE | `GR` |
| **RESOL** | *(aucun)* | | PARAMDESSIN | `PD` |
| ZOOMFENETRE | `ZF` | | ZOOMPRECEDENT | `ZP` |
| PANORAMIQUE | `PAN` | | REGEN | `RG` |

`RESOL` n'a pas d'alias dans la table — son bouton n'affiche donc rien, et c'est correct.
Noter aussi que le libellé de la bascule est **`RESOL`**, sans accent : ne pas le renommer.

## 3.2 — `KeyButton` : le bouton qui imprime son alias

Nouveau, dans `ribbon.cpp` (anonyme) ou `src/ui/keybutton.h` si d'autres surfaces le
réutilisent — le rail et la palette de commandes sont candidats.

```cpp
// Un bouton de ruban qui imprime son alias de commande dans un coin, au
// troisieme niveau d'encre. Comme une cote sur une planche : presente,
// discrete, jamais dans le chemin.
//
// L'alias est lu sur l'action a chaque peinture : il n'est jamais copie, donc
// il ne peut pas divergier du registre.
class KeyButton : public QToolButton
{
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);

        const QAction *action = defaultAction();
        if (!action)
            return;
        const QString alias = action->property("alias").toString();
        if (alias.isEmpty())
            return;

        QPainter p(this);
        p.setFont(Theme::monoFont(6.5));
        p.setPen(isEnabled() ? Theme::colors().textFaint
                             : Theme::colors().textFaint.darker(130));

        // En bas a droite pour un petit bouton, en haut a droite pour un
        // grand : le libelle d'un grand bouton occupe deja le bas.
        const bool large = iconSize().width() >= Ribbon::kLargeIcon;
        const QRect box = large
                ? QRect(0, 2, width() - 4, 12)
                : QRect(0, height() - 12, width() - 2, 11);
        p.drawText(box, Qt::AlignRight | Qt::AlignVCenter, alias);
    }
};
```

Puis, dans `RibbonPanel::addSmall` / `addLarge` / `addLargeMenu`, remplacer
`new QToolButton(this)` par `new KeyButton(this)`. Rien d'autre à changer : le bouton suit
toujours son action.

## 3.3 — Le nom du panneau monte, et devient un bandeau de zone

Le nom gravé passe **au-dessus** des boutons, dans une bande de 18 px filetée en haut et en
bas, avec un filet vertical entre panneaux — exactement les bandeaux `CHAMP | BOÎTE DE
JONCTION | CABINET` du cadre de folio. On lit où chercher **avant** de regarder, comme sur une
planche.

`src/ui/ribbon.h` :

```cpp
class Ribbon : public QWidget
{
public:
    static constexpr int kLargeIcon = 32;
    static constexpr int kSmallIcon = 20;
    static constexpr int kRowHeight = 58;

    // Le nom du panneau, monte au-dessus des boutons, dans une bande filetee
    // en haut et en bas : c'est le bandeau de zone d'une planche.
    static constexpr int kZoneBandHeight = 18;
    // Les boutons seuls, le nom n'etant plus dedans.
    static constexpr int kPanelHeight = 66;
    static constexpr int kTabHeight = 30;
    // L'ascenseur horizontal passe desormais sous le ruban ENTIER, pas sous
    // chaque page : il ne peut plus rogner un nom grave, donc la reserve
    // tombe a zero. C'etaient 13 px pris au dessin pour rien.
    static constexpr int kScrollAllowance = 0;
};
```

`RibbonPanel`, dans le constructeur : `QVBoxLayout` avec la bande en premier.

```cpp
RibbonPanel::RibbonPanel(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    m_name = new QLabel(title.toUpper(), this);
    m_name->setFixedHeight(Ribbon::kZoneBandHeight);
    m_name->setContentsMargins(Theme::space(4), 0, Theme::space(4), 0);
    m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_name->setProperty("zoneBand", true);
    Theme::engrave(m_name);
    column->addWidget(m_name);

    auto *buttons = new QWidget(this);
    buttons->setFixedHeight(Ribbon::kPanelHeight);
    m_row = new QHBoxLayout(buttons);
    m_row->setContentsMargins(Theme::space(4), 0, Theme::space(4), 0);
    m_row->setSpacing(Theme::space(2));
    column->addWidget(buttons);
}
```

`theme.cpp` :

```css
/* Le bandeau de zone : filets en haut et en bas, aucun fond, aucun rayon.
   Les filets verticaux entre panneaux sont peints par Ribbon::paintEvent et
   traversent la bande ET la rangee de boutons, comme les traits de zone
   traversent le cadre d'une planche. */
QLabel[zoneBand="true"] {
    color: %FAINT%;
    letter-spacing: 1.6px;
    border-top: 1px solid %BORDER%;
    border-bottom: 1px solid %BORDER%;
    background: transparent;
}
```

Dans `Ribbon::paintEvent`, les séparateurs de panneau montent de `kZoneBandHeight` :
ils partent du haut de la bande et descendent jusqu'au bas de la rangée de boutons.

## 3.4 — Les réglages sortent de la grille d'actions

Le sélecteur de type de fil et le style de trait sont des **états**, pas des gestes ; les
poser au milieu de boutons d'action fait cliquer au hasard. `addControl` existe déjà :
lui donner sa forme définitive.

```cpp
// Un reglage a la place d'un bouton — le selecteur de type de fil, le style
// de trait. Ce n'est pas une action : il est donc precede d'un filet, coiffe
// d'un libelle grave, et il montre sa VALEUR. On sait ce qu'on va poser sans
// cliquer pour verifier.
void RibbonPanel::addSetting(const QString &label, QWidget *widget)
{
    closeGrid();

    auto *rule = new QFrame(this);
    rule->setFrameShape(QFrame::VLine);
    rule->setFixedWidth(1);
    m_row->addWidget(rule);

    auto *cell = new QWidget(this);
    auto *box = new QVBoxLayout(cell);
    box->setContentsMargins(Theme::space(1), Theme::space(2), Theme::space(1), Theme::space(2));
    box->setSpacing(Theme::space(2));

    auto *name = new QLabel(label.toUpper(), cell);
    Theme::engrave(name);
    name->setProperty("settingLabel", true);
    box->addWidget(name);
    box->addWidget(widget);
    box->addStretch(1);

    m_row->addWidget(cell);
}
```

Appels : `panelFils->addSetting(tr("Type posé"), m_wireTypeCombo);` et
`panelDessin->addSetting(tr("Trait"), m_strokeCombo);`.

## 3.5 — Silhouettes : le garde-fou qui manque

Le test d'unicité reste. En ajouter un second, **par panneau**, qui compare les silhouettes
plutôt que les dessins : rendre chaque glyphe à 20 px, le binariser, et vérifier que deux
glyphes du même panneau ne partagent pas plus de ~92 % de leurs pixels allumés.

C'est le garde-fou qui aurait attrapé le panneau FILS. Il est court à écrire et il empêche la
régression de revenir, ce qui est exactement l'esprit des tests déjà en place.

## Critères d'acceptation

1. Hauteur totale du ruban = `kTabHeight + kZoneBandHeight + kPanelHeight` = **114 px**
   (contre 121). Les tests qui mesurent la hauteur sont à mettre à jour avec ces constantes,
   **jamais avec des nombres en dur**.
2. Chaque bouton dont l'action porte un `alias` l'affiche. `RESOL` n'affiche rien.
3. Le test « tout ce qui est au ruban est au menu » passe toujours.
4. Le nom du panneau est au-dessus, au troisième niveau d'encre, avec un filet en haut et un
   en bas, et les filets verticaux traversent les deux bandes.
5. Le sélecteur de type de fil est séparé des boutons d'action par un filet et coiffé de
   `TYPE POSÉ`.
6. Aucun `min-height` sur un bouton de ruban dans la feuille de style — le piège de
   `ribbon.h` est explicite là-dessus.
7. L'ascenseur horizontal, quand la fenêtre est étroite, ne rogne aucun nom gravé.

**Maquette de référence :** `design/Arcus - fenetre principale v2.dc.html`, les trois bandes
du haut. Les alias y sont ceux du tableau ci-dessus.
