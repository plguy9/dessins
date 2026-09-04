# Étape 02 — La barre d'état, en grammaire de cartouche

**Fichiers touchés :** `src/ui/theme.cpp` (feuille de style), `src/ui/mainwindow.cpp`
(`createStatusBar`), `src/ui/mainwindow.h`.

**Risque :** très faible. Une règle de feuille de style et une fonction de construction.
Aucune peinture, aucun widget nouveau.

**Pourquoi commencer par là :** c'est le plus petit élément du chrome, il contient le pire
défaut du logiciel, et il vérifie la direction en une séance.

---

## Le problème

Six bascules — `RESOL` `GRILLE` `ORTHO` `POLAIRE` `ACCROBJ` `REPOBJ` — portent le même aplat
bleu en permanence, dans le coin le plus visible de la fenêtre. La règle 3 dit que l'accent ne
désigne que ce qui est actif ; ici il dit « actif » sans interruption, donc il n'informe plus
de rien, et il crie là où rien d'important ne se passe.

Accessoirement, le tiers gauche de la barre est vide et les coordonnées flottent au milieu.

## 2.1 — `src/ui/theme.cpp`, la feuille de style

Remplacer les règles existantes (~ lignes 487–520) :

```css
QStatusBar {
    background: %WINDOW%;
    border-top: 1px solid %BORDER%;
    min-height: 30px;
}
QStatusBar::item { border: none; }

/* Une case de cartouche : libelle grave, valeur en chasse fixe, filet de
   1 px. Pas de rayon — un cartouche n'a pas de coin arrondi. */
QStatusBar QLabel { color: %MUTED%; padding: 0 0; }
QStatusBar QLabel[cellLabel="true"] {
    color: %FAINT%;
    letter-spacing: 1.6px;
    padding: 0 0 0 16px;
}
QStatusBar QLabel[cellValue="true"] {
    color: %TEXT%;
    padding: 0 16px 0 9px;
}
QStatusBar QFrame[cellRule="true"] { color: %BORDER%; }

/* Regle 3 tenue : la bascule eteinte est au troisieme niveau d'encre et ne
   porte AUCUN aplat. En marche, elle prend le meme filet de 2 px que
   l'onglet de ruban actif — un seul motif a apprendre pour trois endroits. */
QToolButton[statusToggle="true"] {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    color: %FAINT%;
    padding: 4px 13px;
}
QToolButton[statusToggle="true"]:hover {
    background: transparent;
    color: %TEXT%;
}
QToolButton[statusToggle="true"]:checked {
    background: transparent;
    color: %TEXT%;
    border-bottom: 2px solid %ACCENT%;
}
QToolButton[statusToggle="true"]:checked:hover {
    color: %TEXT%;
    border-bottom: 2px solid %ACCENT_HOVER%;
}
```

Si `%FAINT%` et `%ACCENT_HOVER%` n'existent pas dans la table de substitution de `theme.cpp`,
les ajouter à côté de `%MUTED%` / `%ACCENT%`.

**Attention au piège déjà payé :** ne pas poser de `font-weight` sur ces boutons. Qt calcule
leur taille dans l'état normal et le texte se retrouve rogné à l'état coché. La distinction
se fait par le **filet** et par la **couleur**, jamais par la graisse.

## 2.2 — `src/ui/mainwindow.h`

```cpp
private:
    void createStatusBar();
    // Une case de cartouche : le libelle grave, la valeur en chasse fixe, et
    // le filet qui la separe de la suivante. Renvoie le QLabel de valeur pour
    // que l'appelant le garde et le mette a jour.
    QLabel *addStatusCell(const QString &label, const QString &initialValue);
    void addStatusRule();

    QLabel *m_cellPosition = nullptr;
    QLabel *m_cellZoom = nullptr;
    QLabel *m_cellSelection = nullptr;
    QLabel *m_cellFormat = nullptr;
    QLabel *m_cellFolio = nullptr;
    QLabel *m_cellRevision = nullptr;
```

## 2.3 — `src/ui/mainwindow.cpp`

```cpp
void MainWindow::addStatusRule()
{
    auto *rule = new QFrame(this);
    rule->setFrameShape(QFrame::VLine);
    rule->setProperty("cellRule", true);
    rule->setFixedWidth(1);
    statusBar()->addPermanentWidget(rule);
}

QLabel *MainWindow::addStatusCell(const QString &label, const QString &initialValue)
{
    auto *name = new QLabel(label, this);
    name->setProperty("cellLabel", true);
    Theme::engrave(name);
    statusBar()->addPermanentWidget(name);

    auto *value = new QLabel(initialValue, this);
    value->setProperty("cellValue", true);
    value->setFont(Theme::monoFont(8));
    // La chasse fixe evite qu'un nombre qui change deplace ses voisins : c'est
    // la raison d'etre de monoFont, et une barre d'etat en est le cas type.
    statusBar()->addPermanentWidget(value);
    return value;
}

void MainWindow::createStatusBar()
{
    statusBar()->setSizeGripEnabled(false);

    // Les cases, de gauche a droite. addPermanentWidget pose a DROITE : on
    // construit donc dans l'ordre de lecture et on laisse le message
    // temporaire de statusBar() occuper la gauche — c'est la ou le logiciel
    // parle, et il ne doit rien avoir a bousculer.
    m_cellPosition  = addStatusCell(tr("POSITION"),  QStringLiteral("0,00 · 0,00 mm"));
    addStatusRule();
    m_cellZoom      = addStatusCell(tr("ZOOM"),      QStringLiteral("100 %"));
    addStatusRule();
    m_cellSelection = addStatusCell(tr("SÉLECTION"), QStringLiteral("—"));
    addStatusRule();

    // Les bascules. Elles gardent leur QAction : rien ne se connecte ici, le
    // bouton suit son action et l'etat reste juste sans une ligne de code.
    for (QAction *action : { m_snapGridAction, m_gridAction, m_orthoAction,
                             m_polarAction, m_osnapAction, m_trackingAction }) {
        auto *button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setProperty("statusToggle", true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFont(Theme::monoFont(8));
        statusBar()->addPermanentWidget(button);
        m_statusToggles.insert(button, action);
    }

    addStatusRule();
    m_cellFormat = addStatusCell(tr("FORMAT"), QStringLiteral("A3 · 420×297"));
    addStatusRule();
    m_cellFolio  = addStatusCell(tr("FOLIO"),  QStringLiteral("1/1"));
    addStatusRule();

    // La case de revision, a droite : c'est la marque, empruntee au cartouche.
    // Elle est en creux (plan `canvas`) comme la case REV. d'une planche.
    createRevisionCell();
}
```

### La case de révision

C'est le seul élément peint de cette étape, et c'est le geste de marque de la fenêtre : deux
lignes dans une case en creux, exactement comme la case `RÉV.` en bas à droite d'un cartouche.

```cpp
void MainWindow::createRevisionCell()
{
    auto *cell = new QWidget(this);
    cell->setAutoFillBackground(true);
    QPalette pal = cell->palette();
    pal.setColor(QPalette::Window, Theme::colors().canvas);   // en creux
    cell->setPalette(pal);

    auto *layout = new QVBoxLayout(cell);
    layout->setContentsMargins(Theme::space(4), 2, Theme::space(4), 2);
    layout->setSpacing(1);

    auto *label = new QLabel(tr("RÉV"), cell);
    label->setProperty("cellLabel", true);
    label->setContentsMargins(0, 0, 0, 0);
    Theme::engrave(label);
    layout->addWidget(label, 0, Qt::AlignHCenter);

    m_cellRevision = new QLabel(QStringLiteral("A"), cell);
    m_cellRevision->setFont(Theme::monoFont(8));
    layout->addWidget(m_cellRevision, 0, Qt::AlignHCenter);

    statusBar()->addPermanentWidget(cell);
}
```

## 2.4 — Le format des valeurs

Une case de cartouche porte **une valeur, pas une phrase**. Les libellés étant gravés à côté,
les valeurs perdent leur préfixe :

| Case | Avant | Après |
|---|---|---|
| position | `X 0,0   Y 0,0 mm` | `184,50 · 96,25 mm` |
| zoom | `zoom 58 %` | `58 %` |
| sélection | `aucune sélection` | `—`, puis `3 objets` |
| format | *(absent)* | `A3 · 420×297` |
| folio | *(absent)* | `2/2` |
| révision | *(absent)* | `A` |

Deux décimales pour les coordonnées : le pas de grille est à 2,5 mm et l'accrochage travaille
au dixième. `0,0` cachait de l'information que le dessinateur cherchait ailleurs.

Le séparateur `·` entre X et Y, plutôt que deux libellés `X` et `Y` : la case s'appelle
POSITION, l'ordre est évident, et deux libellés de plus feraient trois niveaux d'encre dans
une bande de 30 px.

## Critères d'acceptation

1. Au démarrage, avec `RESOL`, `GRILLE` et `ACCROBJ` en marche : **trois filets d'accent de
   2 px**, et aucun aplat.
2. `ORTHO`, `POLAIRE`, `REPOBJ` éteintes sont au troisième niveau d'encre et deviennent
   `text` au survol, sans fond.
3. Le filet de la bascule active est **visuellement identique** à celui de l'onglet de ruban
   actif — même épaisseur, même couleur.
4. Basculer `ORTHO` (touche, commande `OR`, ou clic) met à jour le filet sans reconstruction
   du widget.
5. Aucun texte de bascule n'est rogné — vérifier à l'état coché, c'est là que le piège
   `font-weight` se déclenche.
6. Les valeurs de cellule ne déplacent pas leurs voisines quand elles changent : bouger la
   souris sur le canevas ne doit produire **aucun** décalage horizontal.

**Maquette de référence :** `design/Arcus - fenetre principale v2.dc.html`, bande du bas ; et
`design/Arcus - audit et direction.dc.html` §06 qui montre l'avant et l'après côte à côte.
