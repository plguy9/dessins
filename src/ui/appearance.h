// Ce que le dessinateur regle pour son confort, et qu'on lui rend au
// prochain lancement.
//
// Deux principes.
//
// 1. **Le theme fournit les defauts, le reglage explicite gagne.** On
//    reconstruit d'abord le style du theme, puis on repose par-dessus ce que
//    l'utilisateur a choisi. Sans cet ordre, changer de theme effacerait ses
//    reglages ; en sens inverse, un reglage jamais touche ne suivrait plus le
//    theme.
//
// 2. **Les couleurs sont retenues par theme, la geometrie ne l'est pas.** Une
//    couleur de reticule choisie sur fond sombre est illisible sur fond clair
//    — c'est aussi pour cela qu'AutoCAD garde un jeu de couleurs par fond. La
//    taille du reticule ou le style de grille, eux, n'ont aucune raison de
//    changer avec le fond.
#pragma once

#include "render/renderstyle.h"

namespace dsn {

namespace Appearance {

// Pose sur `style` les reglages retenus. A appeler APRES avoir construit le
// style du theme, jamais avant.
void load(RenderStyle &style, bool dark);

// Retient ce que la boite de reglages vient de rendre.
void save(const RenderStyle &style, bool dark);

// Oublie tout : le style redevient exactement celui du theme.
void reset(bool dark);

// FOND DE DESSIN SOMBRE — une preference explicite, plus une consequence du
// theme d'interface. Les deux etaient lies : choisir une interface sombre
// imposait une feuille sombre, alors que l'apercu, la vignette et le PDF
// continuaient de montrer du papier blanc. Ils sont maintenant independants.
//
// Le reglage est COMMUN AUX DEUX THEMES, comme la geometrie : il ne passe pas
// par `colorKey()`. Vouloir dessiner sur du noir ne depend pas de la couleur
// du chrome.
bool darkSheet();
void setDarkSheet(bool on);

// Y a-t-il quelque chose de retenu ? Sert au bouton « Rétablir les valeurs
// d'origine », qui n'a de sens que s'il a quelque chose a defaire.
bool hasOverrides(bool dark);

} // namespace Appearance

} // namespace dsn
