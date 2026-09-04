# Amorce pour Claude Code

Colle ceci comme premier message, dans le dépôt `dessins` :

---

Tu vas appliquer une révision de design à Arcus. Le paquet complet est dans
`design_handoff_arcus/`. **Lis `README.md` en entier avant de toucher au code** — il porte la
direction, les jetons définitifs, l'ordre des étapes, et la liste de ce qui ne doit pas être
rouvert.

Ensuite, applique **une étape à la fois**, dans cet ordre :

```
01-jetons.md            → paper / ink au premier rang, palette claire révisée
02-barre-etat.md        → la barre d'état en grammaire de cartouche
04-ligne-de-commande.md → trois voix, l'invite au corps de l'interface
03-ruban.md             → bandeau de zone, raccourcis imprimés
05-palette-et-folios.md → la palette récupère la colonne, les folios en onglets
06-boites-de-dialogue.md→ ZoneBox + NumberField, puis six dialogues
07-rapports-en-folios.md→ EN DERNIER, jalon séparé : touche au modèle et au format
```

Après chaque étape : compile, lance `ctest`, et vérifie les critères d'acceptation en fin de
fichier. Ne passe à la suivante que quand ils sont tous verts.

Cinq points sur lesquels il ne faut pas improviser :

1. **`src/render/` ne doit pas inclure `src/ui/theme.h`.** L'étape 01 passe par injection
   (`RenderStyle::applyTheme`), pas par dépendance. C'est la règle de couches de
   `docs/BRIEF.md` §3.
2. **Les alias de raccourci affichés doivent venir du registre de commandes**
   (`mainwindow.cpp` ~1870–2165), jamais d'une invention. `03-ruban.md` contient la table de
   contrôle. `RESOL` n'a pas d'alias, et son libellé s'écrit sans accent.
3. **Aucun `min-height` sur un bouton de ruban dans la feuille de style**, et **aucun
   `font-weight` sur un bouton par défaut** — les deux pièges sont documentés dans
   `ribbon.h` et `startpage.cpp`, et déjà payés une fois.
4. **L'invariant « tout ce qui est au ruban est au menu » est tenu par un test.** Rien de ce
   paquet ne l'entame ; si une étape semble l'exiger, c'est que l'étape est mal comprise.
5. **À l'étape 07, aucun tableau de rapport ne doit finir sérialisé dans le `.dsn`.** Le folio
   calculé stocke *le fait qu'on veut la page*, pas son contenu.

Les `.dc.html` de `design/` sont des **références visuelles**, pas du code à porter : ouvre-les
dans un navigateur pour voir l'intention et relever les valeurs. La cible est le Qt existant,
avec ses patrons (`Theme`, `Icons`, feuille de style Qt, peinture `QPainter`).

Si une valeur manque ou qu'une consigne se contredit, arrête-toi et demande plutôt que de
choisir à ma place.
