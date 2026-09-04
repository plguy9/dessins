# Étape 01 — Les jetons : `paper` et `ink` au premier rang

**Fichiers touchés :** `src/ui/theme.h`, `src/ui/theme.cpp`, `src/render/renderstyle.h`,
`src/render/renderstyle.cpp`, `src/ui/mainwindow.cpp`, `src/ui/appearance.h/.cpp`,
`tests/test_render.cpp`.

**Risque :** faible. Aucun changement de structure, deux champs ajoutés, une dérivation
remplace un pont d'une ligne.

---

## Le problème

Arcus a **deux palettes qui ne se connaissent pas**. `ThemeColors` (15 couleurs, le chrome)
et `RenderStyle` (~20 couleurs, le dessin), reliées par **une seule ligne** dans tout le
dépôt :

```cpp
// mainwindow.cpp:2727
style.pageBackground = Theme::colors().canvas;
```

Conséquence observable, visible dans `docs/images/application.png` : la vignette de folio est
peinte avec `screen()` — feuille blanche — et le canevas avec `screenDark()` — feuille noire.
La même page, deux couleurs, à quinze centimètres l'une de l'autre.

## ⚠ La contrainte d'architecture à respecter

`BRIEF.md` §3 : « chaque couche ne connaît que celles situées sous elle ». `src/render/` est
**sous** `src/ui/`. Donc :

> **`renderstyle.cpp` ne doit PAS inclure `ui/theme.h`.**

La dérivation se fait par **injection depuis la couche `ui`**, pas par dépendance. C'est le
seul point de cette étape où une implémentation naïve casserait l'architecture.

## 1.1 — `src/ui/theme.h`

Ajouter deux champs à `ThemeColors`, avec la règle en commentaire :

```cpp
// Un plan par usage, et un seul. « canvas » est le vide derriere la feuille,
// « window » le chrome, « surface » les panneaux, « elevated » ce qui flotte
// au-dessus. Le fond du dessin est plus profond que le chrome : c'est ce qui
// fait flotter la feuille au lieu de la poser sur un gris etranger.
//
// REGLE 6 — le papier est le seul blanc pur du logiciel. Aucune surface du
// chrome n'atteint #ffffff, dans aucun theme. C'est ce qui fait flotter la
// feuille en clair comme en sombre, et c'est ce qui permet au dessin d'avoir
// UNE SEULE palette au lieu de deux : puisque le papier est blanc des deux
// cotes, l'encre est la meme des deux cotes.
struct ThemeColors {
    QColor canvas;
    QColor window;
    QColor surface;
    QColor elevated;

    // La feuille et son encre. Ce sont les couleurs du SUJET du logiciel :
    // elles vivaient dans RenderStyle, c'est-a-dire nulle part pour le theme.
    QColor paper;
    QColor ink;

    QColor border;
    QColor borderStrong;
    QColor text;
    QColor textMuted;
    QColor textFaint;
    QColor accent;
    QColor accentHover;
    QColor accentText;
    QColor danger;
    QColor success;
    QColor warning;
    bool dark = true;
};
```

## 1.2 — `src/ui/theme.cpp`, jeu sombre

Ajouter les deux valeurs là où la palette sombre est construite :

```cpp
    c.paper = QColor(0xFF, 0xFF, 0xFF);   // regle 6 : le seul blanc pur
    c.ink   = QColor(0x15, 0x1A, 0x18);
```

## 1.3 — `src/ui/theme.cpp`, jeu clair révisé

Trois corrections, et deux ajouts. Les deux premières corrections sont **la raison pour
laquelle le thème clair était le parent pauvre** — ce n'était pas un manque de réglages, mais
deux valeurs qui annulaient les règles 1 et 2.

```cpp
    // Le vide reste plus sombre que le chrome, comme en sombre : c'est la
    // regle 1, et elle ne s'inverse pas d'un theme a l'autre.
    c.canvas       = QColor(0xC8, 0xCF, 0xD4);  // etait #dfe3e6
    c.window       = QColor(0xE8, 0xEC, 0xEF);  // etait #f2f4f6

    // Un panneau ne peut pas porter la couleur d'une feuille : sinon la
    // hierarchie que tout le reste du theme construit s'effondre au dernier
    // centimetre, la ou l'oeil travaille.
    c.surface      = QColor(0xF4, 0xF6, 0xF8);  // etait #ffffff
    c.elevated     = QColor(0xFD, 0xFD, 0xFE);  // etait #ffffff — regle 6

    c.paper        = QColor(0xFF, 0xFF, 0xFF);  // le meme qu'en sombre
    c.ink          = QColor(0x15, 0x1A, 0x18);  // le meme qu'en sombre

    // Deux pour cent d'ecart de luminosite ne font pas une separation : en
    // clair, « des filets, pas des boites » devenait « ni filets ni boites ».
    c.border       = QColor(0xCF, 0xD6, 0xDB);  // etait #e1e6ea
    c.borderStrong = QColor(0xA8, 0xB3, 0xBA);  // etait #bfc8ce

    c.text         = QColor(0x10, 0x16, 0x19);
    c.textMuted    = QColor(0x56, 0x64, 0x6C);
    c.textFaint    = QColor(0x7F, 0x8C, 0x94);
    c.accent       = QColor(0x0B, 0x76, 0xB8);  // inchange
    c.accentHover  = QColor(0x0D, 0x8B, 0xD6);  // inchange
    c.accentText   = QColor(0xFF, 0xFF, 0xFF);  // inchange
    c.danger       = QColor(0xB2, 0x34, 0x28);
    c.success      = QColor(0x2C, 0x6F, 0x33);
    c.warning      = QColor(0x8F, 0x62, 0x10);
```

## 1.4 — `src/render/renderstyle.h`

Ajouter **une méthode d'injection**. Pas d'include de `theme.h`, pas de dépendance montante.

```cpp
struct RenderStyle {
    // ... champs existants, inchanges ...

    // Applique les jetons du theme au dessin.
    //
    // C'est la couche `ui` qui appelle : `render/` ne connait pas `ui/theme.h`
    // et ne doit pas le connaitre (BRIEF.md §3). L'injection garde la regle de
    // dependance intacte tout en supprimant la divergence des deux palettes.
    //
    // Ne touche QUE ce qui depend du theme : la feuille, le vide autour, et
    // l'encre du trace. Le fil, le repere, l'etiquette, la cote, la selection
    // et le marqueur d'accrochage n'en dependent pas — le papier etant blanc
    // dans les deux themes, ils sont valables dans les deux.
    void applyTheme(const QColor &paper, const QColor &ink, const QColor &voidColor);

    static RenderStyle screen();
    static RenderStyle print();
    // Fond de dessin sombre. N'est PLUS declenche par le theme : c'est une
    // preference explicite, pour qui vient d'AutoCAD et a le noir dans les
    // yeux depuis vingt ans (reglage « display/darkSheet »).
    static RenderStyle screenDark();
};
```

## 1.5 — `src/render/renderstyle.cpp`

```cpp
void RenderStyle::applyTheme(const QColor &paper, const QColor &ink, const QColor &voidColor)
{
    sheet          = paper;
    pageBackground = voidColor;
    symbol         = ink;
    text           = ink;
    // `frame` suit l'encre sans l'egaler : un cadre n'est pas un conducteur.
    frame          = ink;
}
```

`screen()` garde ses valeurs par défaut telles quelles — elles restent le comportement de
référence pour l'export PDF et les vignettes hors contexte de thème (`pixelsPerMm == 0`).

**`lightenDarkWires` devient mort** dès que le papier est blanc partout. Le laisser dans la
structure pour l'instant (le réglage sombre explicite s'en sert encore), mais retirer sa mise
à `true` automatique au changement de thème.

## 1.6 — `src/ui/mainwindow.cpp`

Remplacer le pont d'une ligne (~2727) par la dérivation. C'est le **seul** endroit qui
construit un `RenderStyle` pour l'écran ; tout le reste — vignettes, aperçu, PDF — doit
passer par le même chemin.

```cpp
// Avant :
//   RenderStyle style = Theme::isDark() ? RenderStyle::screenDark() : RenderStyle::screen();
//   style.pageBackground = Theme::colors().canvas;
//   Appearance::load(style, Theme::isDark());

// Apres — le theme ne decide plus de la couleur du papier, il la FOURNIT.
RenderStyle MainWindow::buildRenderStyle() const
{
    const QSettings settings;
    const bool darkSheet =
            settings.value(QStringLiteral("display/darkSheet"), false).toBool();

    RenderStyle style = darkSheet ? RenderStyle::screenDark() : RenderStyle::screen();

    const ThemeColors &c = Theme::colors();
    style.applyTheme(darkSheet ? style.sheet : c.paper,
                     darkSheet ? style.symbol : c.ink,
                     c.canvas);

    Appearance::load(style, Theme::isDark());
    return style;
}
```

Puis faire passer **la vignette de folio, l'aperçu d'impression et le rendu du canevas** par
`buildRenderStyle()`. Chercher les autres constructions :

```sh
grep -rn 'RenderStyle::screen\|RenderStyle::screenDark' src/ tools/
```

Chaque appel hors `print()` doit devenir `buildRenderStyle()` — c'est **exactement le
correctif de la divergence vignette / canevas**, et c'est le gain le plus immédiat de tout ce
paquet.

## 1.7 — `src/ui/appearance.cpp`

Ajouter `darkSheet` aux réglages de forme, à côté de `showSheetShadow` :

```cpp
    style.showSheetShadow =
            settings.value(shape + QStringLiteral("sheetShadow"), style.showSheetShadow).toBool();
    // Fond de dessin sombre : preference explicite, plus une consequence du
    // theme d'interface. Les deux reglages sont desormais independants.
    // (lu par MainWindow::buildRenderStyle, pas applique ici)
```

et l'écrire dans `save()`. Le réglage étant **commun aux deux thèmes** (comme les réglages de
forme), il ne passe pas par `colorKey()`.

Prévoir l'entrée d'interface correspondante dans `draftingsettingsdialog` onglet Affichage :
une case « Fond de dessin sombre », avec la phrase d'explication au deuxième niveau d'encre :
*« la feuille reste blanche à l'impression »*.

## 1.8 — `tests/`

- `tests/test_render.cpp` : les assertions qui comparent des couleurs en dur sur fond sombre
  demandent une revue. C'est le **seul chantier réel** de cette étape.
- Ajouter un test qui vaut la peine et verrouille le gain :

```cpp
TEST_CASE("la vignette, le canevas et le PDF s'accordent sur la couleur du papier")
{
    for (bool dark : { true, false }) {
        Theme::apply(app, dark);
        const RenderStyle screen = /* buildRenderStyle() equivalent */;
        const RenderStyle paper  = RenderStyle::print();
        CHECK(screen.sheet == Theme::colors().paper);
        CHECK(screen.sheet == paper.sheet);
        CHECK(screen.symbol == Theme::colors().ink);
    }
}

TEST_CASE("regle 6 : aucun plan de chrome n'est blanc pur")
{
    for (bool dark : { true, false }) {
        Theme::apply(app, dark);
        const ThemeColors &c = Theme::colors();
        for (const QColor &plane : { c.canvas, c.window, c.surface, c.elevated })
            CHECK(plane != QColor(Qt::white));
        CHECK(c.paper == QColor(Qt::white));
    }
}
```

Le second test est court et il tient la règle pour toujours — c'est le genre de garde-fou que
ce dépôt sait déjà écrire.

## Critères d'acceptation

1. `grep -rn 'ui/theme.h' src/render/` ne renvoie **rien**.
2. Vignette de folio, feuille au canevas et page du PDF portent la même couleur, en sombre
   comme en clair.
3. `grep -rn 'RenderStyle::screen' src/ui/` ne renvoie que `buildRenderStyle()`.
4. Les deux nouveaux tests passent dans les deux thèmes.
5. Le réglage « Fond de dessin sombre » est décorrélé du thème d'interface : les quatre
   combinaisons fonctionnent.

**Maquette de référence :** `design/Arcus - jetons v2.dc.html` (les cinq plans côte à côte,
les huit couleurs de dessin marquées « inchangé », le C++ révisé) et
`design/Arcus - theme clair v2.dc.html` (la même fenêtre, 497 valeurs de chrome basculées,
papier et encre intacts).
