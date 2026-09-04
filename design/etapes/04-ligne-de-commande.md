# Étape 04 — La ligne de commande : trois voix, même hauteur

**Fichiers touchés :** `src/ui/commandline.h`, `src/ui/commandline.cpp`,
`src/ui/mainwindow.cpp` (le dock), `src/ui/theme.cpp`.

**Risque :** faible.

---

## Le problème

L'intention est juste, et elle est écrite dans `commandline.h` : l'invite reste tant que le
geste dure, *« c'est ce qui fait d'une ligne de commande un fil conducteur plutôt qu'un
lanceur »*. C'est le seul endroit où le logiciel parle.

À l'écran : 11 px, gris, sous un en-tête gravé « LIGNE DE COMMANDE » qui dépense une rangée
entière à nommer l'évidence. `writeError` existe, `danger` et `success` sont dans les jetons,
et on ne les voit nulle part.

**Plus de présence, pas plus de place.** La hauteur totale reste 62 px.

## 4.1 — Supprimer l'en-tête du dock

« LIGNE DE COMMANDE » vient de la barre de titre du dock (`docktitle.cpp`). Un champ où l'on
tape n'a pas besoin qu'on lui dise son nom.

Dans `mainwindow.cpp`, là où le dock de la ligne de commande est créé :

```cpp
// Pas de barre de titre : le champ dit ce qu'il est des qu'on y tape, et la
// rangee gagnee va a l'invite. Le chevron de repli part avec la barre de
// titre — c'est DockRail qui garde la languette de retour, et il l'a deja.
m_commandDock->setTitleBarWidget(new QWidget(m_commandDock));
m_commandDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
m_dockRail->watch(m_commandDock, tr("Commande"), tr("F2"));
```

Vérifier que `DockRail::watch` est bien appelé pour ce dock : sans barre de titre, le rail
devient **le seul** chemin de retour visible, et c'est précisément ce que `dockrail.h`
documente.

## 4.2 — Les trois voix

`commandline.h` :

```cpp
    // Echo dans l'historique. Trois voix, et la couleur les distingue sans
    // qu'on ait a lire :
    //   write      le fil normal          — textMuted
    //   writeOk    ce qui a abouti        — success
    //   writeError ce qui a echoue        — danger
    void write(const QString &line);
    void writeOk(const QString &line);
    void writeError(const QString &line);
    void writePrompt(const QString &line);

    // L'INVITE — ce que la commande en cours attend. Elle reste affichee tant
    // que le geste dure, contrairement a un message d'historique qui defile.
    // Tant qu'elle est posee, un filet d'accent de 2 px s'allume a sa gauche :
    // c'est le seul signal « le logiciel t'attend » de toute la fenetre.
    void setPrompt(const QString &prompt);
```

`commandline.cpp` — chaque voix pose sa couleur en `QTextCharFormat` :

```cpp
namespace {
void appendLine(QPlainTextEdit *history, const QString &line, const QColor &colour)
{
    QTextCharFormat format;
    format.setForeground(colour);
    QTextCursor cursor(history->document());
    cursor.movePosition(QTextCursor::End);
    if (!history->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(line, format);
    history->verticalScrollBar()->setValue(history->verticalScrollBar()->maximum());
}
} // namespace

void CommandLine::write(const QString &line)
{ appendLine(m_history, line, Theme::colors().textMuted); fitHistory(); }

void CommandLine::writeOk(const QString &line)
{ appendLine(m_history, line, Theme::colors().success); fitHistory(); }

void CommandLine::writeError(const QString &line)
{ appendLine(m_history, line, Theme::colors().danger); fitHistory(); }
```

### Où poser `writeOk`

Les automatismes sont les meilleurs candidats — ce sont eux dont on veut le compte rendu :

| Commande | Message |
|---|---|
| `REPERAGE` / `RN` | `14 fils repérés · 2 repères manuels conservés` |
| `AUDIT` / `CONTROLE` | `aucune anomalie` ou `3 anomalies — voir Contrôles` (celui-là en `warning`) |
| `ENREGISTRER` | `demarrage-direct.dsn enregistré` |
| `EXPORTPDF` | `5 folios exportés` |

La mention « 2 repères manuels conservés » vaut d'être dite : c'est la garantie documentée
dans `BRIEF.md` §5 (*« un fil repéré manuellement est verrouillé et jamais écrasé »*), et un
utilisateur qui la voit à l'écran cesse de désactiver l'automatisme par méfiance.

## 4.3 — L'invite au corps de l'interface

Elle passe de `monoFont(8)` à `uiFont(10)` — c'est une phrase adressée à l'utilisateur, pas
une donnée. Seule la **saisie** reste en chasse fixe : elle contient des coordonnées.

```cpp
    m_prompt->setFont(Theme::uiFont(10));
    m_prompt->setProperty("commandPrompt", true);

    m_input->setFont(Theme::monoFont(9));   // la saisie porte des nombres
```

Le filet d'accent, allumé tant que l'invite est posée :

```cpp
void CommandLine::setPrompt(const QString &prompt)
{
    m_prompt->setText(prompt);
    m_prompt->setVisible(!prompt.isEmpty());
    // Le seul signal « le logiciel attend une saisie » de la fenetre. Il
    // s'eteint des que la commande est finie, donc il ne peut pas mentir.
    m_waitingRule->setVisible(!prompt.isEmpty());
}
```

`m_waitingRule` est un `QFrame` de 2 px de large, `Theme::colors().accent`, posé en tête de la
rangée de saisie.

## 4.4 — Disposition finale, 62 px

```
┌──────────────────────────────────────────────────────────────────────┐
│ REPÉRAGE   14 fils repérés · 2 repères manuels conservés         20px│  success
│ JONCTION   le point demandé ne tombe pas sur un conducteur       20px│  danger
│ ▌ Point suivant ou [Cote / Annuler] : @10,5▏     ? liste…        22px│  invite
└──────────────────────────────────────────────────────────────────────┘
```

Le libellé de gauche (`REPÉRAGE`, `JONCTION`) est **la commande qui parle**, gravée sur 66 px,
au troisième niveau d'encre. C'est encore la grammaire de cartouche : libellé gravé, valeur à
côté. Il rend l'historique lisible en diagonale — on voit *qui* parle avant de lire *quoi*.

Ajout à `write` / `writeOk` / `writeError` : un premier argument facultatif `source`. Si
l'appelant ne le donne pas, `lastCommand()` fait l'affaire.

`theme.cpp` :

```css
QWidget[commandLine="true"] {
    background: %WINDOW%;
    border-top: 1px solid %BORDER%;
}
QLabel[commandPrompt="true"] { color: %TEXT%; }
QLabel[commandSource="true"] {
    color: %FAINT%;
    letter-spacing: 1.6px;
}
QLineEdit[commandInput="true"] {
    background: transparent;
    border: none;
    color: %TEXT%;
    padding: 0;
}
```

L'entrée sans cadre ni fond : la ligne de commande n'est pas un formulaire, c'est une invite.
Le filet d'accent à gauche suffit à dire où l'on tape.

## Critères d'acceptation

1. Hauteur totale = 62 px, comme avant. Aucun gain de place demandé, aucun perdu.
2. Plus d'en-tête « LIGNE DE COMMANDE » ; `DockRail` montre la languette quand le dock est
   replié, avec `F2` comme indice de clavier.
3. Un `REPERAGE` réussi écrit en `success`, une jonction hors conducteur en `danger`, le
   reste en `textMuted`.
4. Pendant une commande à plusieurs points, le filet d'accent est allumé ; il s'éteint à la
   fin ou sur Échap.
5. L'invite est au même corps que le reste de l'interface (`uiFont(10)`).
6. La saisie reste en chasse fixe et `@10,5` ne déplace pas ce qui l'entoure.
7. Les alias et le comportement du clavier sont inchangés — flèches haut/bas dans
   l'historique, `?` pour la liste, Échap pour annuler.

**Maquette de référence :** `design/Arcus - fenetre principale v2.dc.html`, la bande au-dessus
de la barre d'état.
