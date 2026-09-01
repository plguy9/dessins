# Dessins — guide pour les sessions Claude

Logiciel de dessin électrique (Qt 6, C++20). Le brief d'architecture est dans
`docs/BRIEF.md`, le format de fichier dans `docs/FORMAT.md` — les lire avant
toute décision structurante.

## Compiler et tester

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure        # exige QT_QPA_PLATFORM=offscreen hors CI
./build/bin/dessins_sample examples               # vérification de bout en bout + projet d'exemple
QT_QPA_PLATFORM=offscreen ./build/bin/dessins --screenshot=/tmp/fenetre.png examples/demarrage-direct.dsn
```

Dépendances Debian/Ubuntu : `qt6-base-dev qt6-svg-dev libgl1-mesa-dev cmake
ninja-build zlib1g-dev catch2`. Sans Catch2, les tests sont désactivés sans
erreur. `-DDESSINS_BUILD_GUI=OFF` compile le cœur seul.

## Architecture — la règle qui gouverne tout

Chaque couche ne connaît que celles situées sous elle :

```
core/  →  symbols/, rules/, render/  →  io/  →  ui/  →  app/
```

- `core/` ne référence que **Qt6::Core**. Ni QtGui ni QtWidgets — c'est ce qui
  le rend testable sans écran. QTransform et QUndoStack vivent dans QtGui :
  c'est pourquoi `Transform2D` et `CommandStack` sont écrits maison.
- `render/FolioPainter` est le **seul** endroit qui sait dessiner un schéma.
  L'écran, l'aperçu, le PDF et les vignettes passent tous par lui. Ne jamais
  dupliquer un trace ailleurs : c'est la garantie « écran = papier ».
- Le canevas (`ui/folioview.cpp`) est un QWidget, **pas** une QGraphicsView —
  conséquence directe du point précédent.

## Invariants à ne pas casser

1. **Ce que l'utilisateur a saisi à la main n'est jamais écrasé** par un
   automatisme (`numberLocked`, `designationLocked`). Sur un potentiel, un
   repère verrouillé gouverne tout le potentiel.
2. **Toute modification du document passe par une commande** (`Document::push`).
   Rien ne modifie une entité directement depuis l'interface.
3. **Une commande restaure l'état dans l'entité existante** (`Entity::assign`),
   jamais en remplaçant l'objet : les panneaux détiennent des pointeurs.
4. **La netlist n'est jamais stockée** — recalculée depuis la géométrie,
   invalidée à chaque modification (`Document::invalidateNetlist`).
5. **Les coordonnées sont locales au folio.** Toute recherche spatiale doit
   être enfermée dans son folio (voir `NetlistBuilder::touchesNear`).
6. **Un fil porte n conducteurs** dès le modèle (`Wire::conductors`), n = 1 par
   défaut. Ne pas l'aplatir : c'est ce qui rend l'unifilaire possible.
7. **Le repérage est reproductible** : relancé sur un dessin inchangé, il
   redonne exactement les mêmes repères (test dédié).
8. Un document d'une version **plus récente est refusé net** ; une entité
   inconnue dans un document lisible est **ignorée** sans bloquer l'ouverture.

## Bibliothèque de symboles

- Source de vérité : les JSON de `libraries/`, embarqués dans le binaire par
  ressource Qt. `tools/gen_builtin_library.py` n'a servi qu'à les amorcer —
  il reste utilisable, mais après régénération il faut recompiler (ressource).
- `logicalId` relie les variantes CEI/ANSI d'un même symbole ; `resolve()` se
  rabat sur l'autre norme si une variante manque.
- Convention de tracé : origine au centre, y vers le bas, module 2,5 mm,
  la broche dessine son propre trait (le graphisme ne dessine que le corps).
- Angles d'arc : convention Qt — sens trigonométrique **visuel**, origine à
  3 h. Sous le renversement d'axe du DXF, les angles passent inchangés.

## Conventions

- Code et commentaires en français (sans accents dans les .cpp/.h du cœur,
  chaînes `tr()` accentuées côté interface). Messages de commit en français.
- Tests Catch2 : noms de test en français décrivant le comportement, avec un
  commentaire disant *pourquoi* l'exigence existe.
- `clang-format` n'est pas encore configuré : imiter le style en place
  (4 espaces, ~100 colonnes, accolades K&R).
- Vérifier avant tout push : `cmake --build build && ctest --test-dir build`.

## Points ouverts (décisions utilisateur en attente)

Licence (libre ou propriétaire), reprise de fichiers existants (ferait
remonter l'import DXF), mono-poste ou partagé. Voir `docs/BRIEF.md`,
section « Questions ouvertes ».

Tranché : **OS prioritaire = Windows** (décision utilisateur, 2026-09-01).
La distribution se fait par la page GitHub Releases.

## Prochaines étapes envisagées (dans l'ordre de valeur)

1. **Empaquetage Windows** — priorité de la prochaine session. Plan :
   workflow GitHub Actions `release.yml` déclenché sur tag `v*` ;
   runner `windows-latest`, Qt via `jurplel/install-qt-action`,
   compilation Ninja + MSVC, `windeployqt` dans un dossier autonome,
   zip portable `dessins-<version>-windows-x64.zip`, publication en
   Release. Binaire non signé : documenter le contournement SmartScreen
   dans les notes de version. Premier tag : `v0.1.0`.
   Vérification : dérouler le zip dans un job Windows vierge et lancer
   `dessins.exe --screenshot=...` pour prouver qu'il démarre hors de
   l'arbre de compilation.
2. Import DXF (l'export existe : `io/dxfexport.cpp`).
3. Unifilaires M8 : symboles de distribution + bilan de puissance
   (le modèle multi-conducteurs est prêt).
4. Électronique M9 : export netlist SPICE / KiCad.
5. AppImage Linux et .dmg macOS, quand le besoin se présentera.
