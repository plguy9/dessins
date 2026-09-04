# Arcus — le brief donné à Claude Design

*Ce document est le point de départ de la refonte : ce qu'est le logiciel, qui
s'en sert, ce qui est déjà tranché. Les captures et les transpositions HTML
qui l'accompagnent sont dans `etat-actuel/`, les étapes qui en sont sorties
dans `etapes/`.*

## Ce que c'est

Logiciel de **dessin de schémas électriques** pour bureau d'études, sur le
modèle d'AutoCAD Electrical. Application de bureau **Qt 6 / C++ (widgets)**,
**Windows** en priorité, interface **uniquement en français**. v0.10.0 publiée.

**L'utilisateur** : un dessinateur industriel. Il passe sa journée dedans, à
la souris et au clavier, sur un grand écran. Il vient d'AutoCAD et en a les
réflexes : ligne de commande, raccourcis d'une lettre, touches F3/F7/F8.

**Deux documents**, moitié-moitié dans son travail :
- le **schéma de commande** (capture 1) — contacteurs, relais, bobines ;
- le **schéma de boucle** (capture 2) — une boucle d'instrumentation par
  feuille, du capteur au champ jusqu'à la carte d'automate, la feuille coupée
  en bandes verticales nommées (CHAMP | BOÎTE DE JONCTION | CABINET).

## Ce qu'on voit à l'écran

De haut en bas : **barre de menus** (9 menus, source de vérité de toutes les
commandes) → **barre d'accès rapide + onglets de ruban** → **panneaux du
ruban** (nommés, gravés dessous) → à gauche **la palette de symboles** (grille
de vignettes) et **la liste des folios** → au centre **le canevas** → en bas
**la ligne de commande** (historique + champ) → **la barre d'état**
(coordonnées, zoom, sélection, bascules RESOL/GRILLE/ORTHO/POLAIRE/…).

Le panneau **Rapports** s'ouvre à la demande en bas ; les propriétés n'ont pas
de panneau ancré, elles s'ouvrent en boîte au double-clic (capture 3 : le
compositeur de cartouche).

## Le système visuel actuel (5 règles, tenues par des tests)

1. **Quatre plans**, dans cet ordre de profondeur : `canvas` (le vide derrière
   la feuille) < `window` (le chrome) < `surface` (les panneaux) < `elevated`
   (ce qui flotte). Le fond du dessin est **plus sombre que le chrome** :
   c'est ce qui fait flotter la feuille.
2. **Des filets, pas des boîtes** — un panneau se sépare par une ligne de 1 px,
   jamais par un cadre fermé.
3. **Un seul accent (bleu), tenu en réserve** — il ne désigne que ce qui est
   actif ou sélectionné. Le seul aplat coloré est le bandeau de l'écran
   d'accueil.
4. **Trois niveaux d'encre** : `text` porte, `textMuted` accompagne,
   `textFaint` s'efface. Les étiquettes gravées (titres de panneaux, en-têtes)
   sont au troisième niveau : petites capitales espacées.
5. **Un seul pas d'espacement** : 4 px × n.

Chiffres et coordonnées en fonte monospace, pour qu'un nombre qui change ne
déplace pas ses voisins. Thème sombre par défaut, thème clair disponible.

## Contraintes dures (à ne pas contourner)

- **Qt 6 widgets**, pas de web, pas de QML. Ce qui est proposé doit être
  faisable avec des widgets et une feuille de style Qt.
- **Le canevas est peint à la main** par un unique peintre partagé écran /
  aperçu / PDF / vignettes : « ce qu'on voit est ce qui s'imprime ». Le dessin
  lui-même (symboles, cotes, cartouche) suit des **normes** (CEI 60617,
  ISA S5.1, ISO 129) — les formes ne sont pas des choix graphiques.
- **Tout en français**, y compris les boutons standards.
- **Le clavier prime** : chaque commande a un nom tapable et souvent un
  raccourci d'une lettre. Rien ne doit exiger la souris.

## Ce sur quoi j'aimerais un regard

1. **La densité du chrome.** Le ruban + 2 panneaux à gauche + la ligne de
   commande + la barre d'état laissent ~60 % de la fenêtre au dessin. Est-ce
   le bon partage ? Que replier, que fusionner ?
2. **La hiérarchie du ruban.** 5 onglets, ~8 panneaux par onglet, un ou deux
   gros boutons puis une grille de petites icônes. Les icônes sont dessinées
   au trait, en code, toutes distinctes — mais un dessinateur retrouve-t-il sa
   commande sans survoler ?
3. **La ligne de commande.** C'est le seul endroit où le logiciel parle :
   l'invite, les comptes rendus, l'écho des boutons cliqués. Aujourd'hui c'est
   un bandeau texte discret en bas. Mérite-t-elle plus de présence ?
4. **Le panneau Rapports** (tableaux : nomenclature, bornier, câbles, De/Vers)
   s'ouvre en bas à gauche, étroit et tronqué. À revoir.
5. **Les boîtes de dialogue** (capture 3) : formulaire à gauche, aperçu en
   haut. Beaucoup de champs, peu de hiérarchie.
6. **Le thème clair** est le parent pauvre — tout a été réglé en sombre.

## Ce qui a déjà été tranché (décisions utilisateur, ne pas rouvrir)

- **Le ruban reste** : une rangée d'icônes sans hiérarchie oblige à survoler
  chaque bouton ; un panneau nommé dit où chercher avant qu'on ait cherché.
- **Pas de panneau de propriétés ancré** : « elles ne servent à rien et
  prennent trop de place ». Double-clic → boîte.
- **Pas de boîte modale à chaque pose de symbole** : c'est le geste le plus
  répété de la journée.
- **La palette de symboles est une grille**, pas une liste : on reconnaît un
  symbole à sa forme plus vite qu'à son nom.
- Chaque panneau porte son **chevron de repli**, et un rail sur le bord du
  canevas garde une languette pour le rouvrir.
