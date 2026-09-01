#!/usr/bin/env python3
"""Amorce la bibliotheque de symboles integree.

Les fichiers produits sous libraries/ sont la source de verite : ce script ne
sert qu'a les creer d'un coup au demarrage du projet. Passe ce point, les
symboles se modifient dans l'editeur integre, pas ici.

    python3 tools/gen_builtin_library.py

Conventions de trace :
  * origine au centre du symbole, y vers le bas ;
  * module de grille de 2,5 mm, longueurs de broche multiples du module ;
  * les symboles de commande et de puissance sont verticaux (schema
    developpe europeen), les symboles electroniques horizontaux ;
  * la broche porte son propre trait : le graphisme ne dessine que le corps.
"""

import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RIGHT, DOWN, LEFT, UP = 0, 90, 180, 270

# --- primitives -----------------------------------------------------------

def line(x1, y1, x2, y2, w=0.25):
    return {"kind": "line", "points": [[x1, y1], [x2, y2]], "lineWidth": w}

def poly(points, w=0.25):
    return {"kind": "polyline", "points": [list(p) for p in points], "lineWidth": w}

def rect(x, y, w, h, lw=0.25, filled=False):
    d = {"kind": "rect", "points": [[x, y], [x + w, y + h]], "lineWidth": lw}
    if filled:
        d["filled"] = True
    return d

def circle(cx, cy, r, lw=0.25, filled=False):
    d = {"kind": "circle", "points": [[cx, cy]], "radius": r, "lineWidth": lw}
    if filled:
        d["filled"] = True
    return d

def arc(cx, cy, r, start, span, lw=0.25):
    return {"kind": "arc", "points": [[cx, cy]], "radius": r,
            "startAngle": start, "spanAngle": span, "lineWidth": lw}

def text(x, y, s, h=2.0, align="center"):
    d = {"kind": "text", "points": [[x, y]], "text": s, "textHeight": h}
    if align != "left":
        d["align"] = align
    return d

def pin(number, x, y, direction, length=5.0, ptype=None, name=None, show_number=True):
    d = {"number": number, "at": [x, y], "dir": direction, "length": length}
    if ptype:
        d["type"] = ptype
    if name:
        d["name"] = name
        d["showName"] = True
    if not show_number:
        d["showNumber"] = False
    return d

# --- fabrique de definitions ---------------------------------------------

SYMBOLS = []

def sym(logical_id, name, category, prefix, kind, graphics, pins,
        norm="IEC", keywords=(), fields=None, desig=(-8.0, -2.0), value=(8.0, -2.0)):
    SYMBOLS.append({
        "id": "%s:%s" % (norm.lower(), logical_id),
        "logicalId": logical_id,
        "norm": norm,
        "name": name,
        "category": category,
        "keywords": list(keywords),
        "prefix": prefix,
        "deviceKind": kind,
        "graphics": graphics,
        "pins": pins,
        "defaultFields": fields or {},
        "designationAnchor": list(desig),
        "valueAnchor": list(value),
    })

# ==========================================================================
# Commande — contacts, bobines, organes de commande
# ==========================================================================

# Contact a fermeture : lame partant de la borne basse vers le haut a gauche,
# sans toucher la borne haute.
sym("contact-no", "Contact à fermeture", "Commande", "K", "contact",
    [line(0, 3, -3.6, -2.6)],
    [pin("13", 0, -10, UP, 7.0), pin("14", 0, 10, DOWN, 7.0)],
    keywords=["contact", "NO", "fermeture", "travail"])

# Contact a ouverture : la lame repose sur la borne haute, materialisee par
# une barre perpendiculaire.
sym("contact-nc", "Contact à ouverture", "Commande", "K", "contact",
    [line(0, 3, -3.6, -3.2), line(-4.6, -3.0, -1.2, -3.0)],
    [pin("11", 0, -10, UP, 7.0), pin("12", 0, 10, DOWN, 7.0)],
    keywords=["contact", "NC", "ouverture", "repos"])

sym("contact-changeover", "Contact inverseur", "Commande", "K", "contact",
    [line(-4, 5, -3.6, -2.8), line(-4.6, -3.0, -1.2, -3.0)],
    [pin("11", -4, -10, UP, 7.0), pin("12", -4, 10, DOWN, 7.0),
     pin("14", 4, 10, DOWN, 7.0)],
    keywords=["inverseur", "changeover"])

sym("contact-no-delayed-on", "Contact à fermeture temporisé au travail", "Commande", "K", "contact",
    [line(0, 3, -3.6, -2.6), arc(0, 0, 3.0, 0, 180)],
    [pin("17", 0, -10, UP, 7.0), pin("18", 0, 10, DOWN, 7.0)],
    keywords=["temporisé", "retard", "travail"])

sym("contact-nc-delayed-off", "Contact à ouverture temporisé au repos", "Commande", "K", "contact",
    [line(0, 3, -3.6, -3.2), line(-4.6, -3.0, -1.2, -3.0), arc(0, 0, 3.0, 180, 180)],
    [pin("15", 0, -10, UP, 7.0), pin("16", 0, 10, DOWN, 7.0)],
    keywords=["temporisé", "retard", "repos"])

sym("coil", "Bobine de contacteur", "Commande", "K", "coil",
    [rect(-5, -4, 10, 8)],
    [pin("A1", 0, -10, UP, 6.0), pin("A2", 0, 10, DOWN, 6.0)],
    keywords=["bobine", "contacteur", "commande"],
    fields={"value": "230 V CA"})

sym("relay-coil", "Bobine de relais", "Commande", "K", "coil",
    [rect(-5, -4, 10, 8), line(-5, -4, 5, 4)],
    [pin("A1", 0, -10, UP, 6.0), pin("A2", 0, 10, DOWN, 6.0)],
    keywords=["bobine", "relais"])

sym("coil-timer-on", "Bobine temporisée au travail", "Commande", "K", "coil",
    [rect(-5, -4, 10, 8), poly([(-5, 4), (-1, -4), (-1, 4)])],
    [pin("A1", 0, -10, UP, 6.0), pin("A2", 0, 10, DOWN, 6.0)],
    keywords=["temporisé", "bobine"])

sym("coil-timer-off", "Bobine temporisée au repos", "Commande", "K", "coil",
    [rect(-5, -4, 10, 8), poly([(1, -4), (1, 4), (5, -4)])],
    [pin("A1", 0, -10, UP, 6.0), pin("A2", 0, 10, DOWN, 6.0)],
    keywords=["temporisé", "bobine"])

sym("pushbutton-no", "Bouton-poussoir à fermeture", "Commande", "S", "pushbutton",
    [line(0, 3, -3.6, -2.6), line(-3.6, -2.6, -3.6, -6.0),
     line(-6.0, -6.0, -1.2, -6.0)],
    [pin("13", 0, -10, UP, 7.0), pin("14", 0, 10, DOWN, 7.0)],
    keywords=["bouton", "poussoir", "marche"])

sym("pushbutton-nc", "Bouton-poussoir à ouverture", "Commande", "S", "pushbutton",
    [line(0, 3, -3.6, -3.2), line(-4.6, -3.0, -1.2, -3.0),
     line(-3.6, -3.2, -3.6, -6.0), line(-6.0, -6.0, -1.2, -6.0)],
    [pin("11", 0, -10, UP, 7.0), pin("12", 0, 10, DOWN, 7.0)],
    keywords=["bouton", "poussoir", "arrêt"])

sym("emergency-stop", "Arrêt d'urgence coup de poing", "Commande", "S", "pushbutton",
    [line(0, 3, -3.6, -3.2), line(-4.6, -3.0, -1.2, -3.0),
     line(-3.6, -3.2, -3.6, -6.5), circle(-3.6, -7.8, 1.4)],
    [pin("11", 0, -10, UP, 7.0), pin("12", 0, 10, DOWN, 7.0)],
    keywords=["arrêt", "urgence", "coup de poing", "AU"])

sym("selector-switch", "Commutateur deux positions", "Commande", "S", "switch",
    [line(0, 3, -3.6, -2.6), line(-5.4, -4.4, -1.8, -0.8)],
    [pin("1", 0, -10, UP, 7.0), pin("2", 0, 10, DOWN, 7.0)],
    keywords=["commutateur", "sélecteur"])

sym("limit-switch-no", "Interrupteur de position à fermeture", "Commande", "S", "switch",
    [line(0, 3, -3.6, -2.6), line(-3.6, -2.6, -3.6, -5.5),
     rect(-5.0, -7.5, 2.8, 2.0)],
    [pin("13", 0, -10, UP, 7.0), pin("14", 0, 10, DOWN, 7.0)],
    keywords=["fin de course", "position"])

sym("limit-switch-nc", "Interrupteur de position à ouverture", "Commande", "S", "switch",
    [line(0, 3, -3.6, -3.2), line(-4.6, -3.0, -1.2, -3.0),
     line(-3.6, -3.2, -3.6, -5.5), rect(-5.0, -7.5, 2.8, 2.0)],
    [pin("11", 0, -10, UP, 7.0), pin("12", 0, 10, DOWN, 7.0)],
    keywords=["fin de course", "position"])

sym("thermal-contact", "Contact de relais thermique", "Commande", "F", "contact",
    [line(0, 3, -3.6, -3.2), line(-4.6, -3.0, -1.2, -3.0),
     poly([(-3.6, -3.2), (-3.6, -5.5), (-6.0, -5.5), (-6.0, -7.5)])],
    [pin("95", 0, -10, UP, 7.0), pin("96", 0, 10, DOWN, 7.0)],
    keywords=["thermique", "protection", "surcharge"])

sym("indicator-lamp", "Voyant lumineux", "Commande", "H", "lamp",
    [circle(0, 0, 4.0), line(-2.8, -2.8, 2.8, 2.8), line(-2.8, 2.8, 2.8, -2.8)],
    [pin("X1", 0, -10, UP, 6.0), pin("X2", 0, 10, DOWN, 6.0)],
    keywords=["voyant", "lampe", "signalisation"])

sym("horn", "Avertisseur sonore", "Commande", "H", "horn",
    [arc(0, 0, 4.0, -90, 180), line(0, -4, 0, 4)],
    [pin("1", 0, -10, UP, 6.0), pin("2", 0, 10, DOWN, 6.0)],
    keywords=["sirène", "klaxon", "alarme"])

# ==========================================================================
# Puissance
# ==========================================================================

sym("fuse", "Fusible", "Puissance", "F", "fuse",
    [rect(-2.5, -5, 5, 10), line(0, -5, 0, 5)],
    [pin("1", 0, -10, UP, 5.0), pin("2", 0, 10, DOWN, 5.0)],
    keywords=["fusible", "protection"], fields={"value": "10 A gG"})

sym("disconnector", "Sectionneur", "Puissance", "Q", "disconnector",
    [line(0, 5, -4.2, -3.0), line(-5.4, -5.0, -3.0, -5.0)],
    [pin("1", 0, -10, UP, 5.0), pin("2", 0, 10, DOWN, 5.0)],
    keywords=["sectionneur", "isolement"])

sym("switch-disconnector", "Interrupteur-sectionneur", "Puissance", "Q", "disconnector",
    [line(0, 5, -4.2, -3.0), line(-5.4, -5.0, -3.0, -5.0),
     circle(0, 5, 0.9, 0.25, True)],
    [pin("1", 0, -10, UP, 5.0), pin("2", 0, 10, DOWN, 5.0)],
    keywords=["interrupteur", "sectionneur", "coupure en charge"])

sym("circuit-breaker-1p", "Disjoncteur unipolaire", "Puissance", "Q", "breaker",
    [line(0, 5, -4.2, -3.0), poly([(-2.6, -4.6), (-1.4, -3.4), (-2.6, -2.2)]),
     poly([(-4.4, -4.6), (-3.2, -3.4), (-4.4, -2.2)])],
    [pin("1", 0, -10, UP, 5.0), pin("2", 0, 10, DOWN, 5.0)],
    keywords=["disjoncteur", "protection", "courbe C"],
    fields={"value": "C10"})

sym("circuit-breaker-3p", "Disjoncteur tripolaire", "Puissance", "Q", "breaker",
    [line(-7.5, 5, -11.7, -3.0), line(0, 5, -4.2, -3.0), line(7.5, 5, 3.3, -3.0),
     poly([(-2.6, -4.6), (-1.4, -3.4), (-2.6, -2.2)]),
     poly([(-4.4, -4.6), (-3.2, -3.4), (-4.4, -2.2)]),
     line(-11.7, -1.0, 3.3, -1.0, 0.15)],
    [pin("1", -7.5, -10, UP, 5.0), pin("2", -7.5, 10, DOWN, 5.0),
     pin("3", 0, -10, UP, 5.0), pin("4", 0, 10, DOWN, 5.0),
     pin("5", 7.5, -10, UP, 5.0), pin("6", 7.5, 10, DOWN, 5.0)],
    keywords=["disjoncteur", "tripolaire", "triphasé"],
    fields={"value": "C16 3P"}, desig=(-14.0, -2.0), value=(11.0, -2.0))

sym("rcd", "Interrupteur différentiel", "Puissance", "Q", "rcd",
    [line(-7.5, 5, -11.7, -3.0), line(0, 5, -4.2, -3.0),
     rect(-13.0, 6.0, 15.0, 5.0), text(-5.5, 10.0, "I∆n", 2.2)],
    [pin("1", -7.5, -10, UP, 5.0), pin("2", -7.5, 14, DOWN, 3.0),
     pin("3", 0, -10, UP, 5.0), pin("4", 0, 14, DOWN, 3.0)],
    keywords=["différentiel", "30 mA", "protection"],
    fields={"value": "30 mA"}, desig=(-16.0, -2.0), value=(6.0, -2.0))

sym("fuse-disconnector", "Sectionneur porte-fusible", "Puissance", "Q", "disconnector",
    [line(0, 5, -4.2, -3.0), line(-5.4, -5.0, -3.0, -5.0),
     rect(-6.0, -2.0, 4.0, 6.0)],
    [pin("1", 0, -10, UP, 5.0), pin("2", 0, 10, DOWN, 5.0)],
    keywords=["sectionneur", "fusible"])

sym("contactor-power-3p", "Contacteur de puissance tripolaire", "Puissance", "K", "contactor",
    [line(-7.5, 5, -11.7, -3.0), line(0, 5, -4.2, -3.0), line(7.5, 5, 3.3, -3.0),
     arc(-7.5, 3.5, 1.8, 180, 180), arc(0, 3.5, 1.8, 180, 180), arc(7.5, 3.5, 1.8, 180, 180),
     line(-11.7, -1.0, 3.3, -1.0, 0.15)],
    [pin("1", -7.5, -10, UP, 5.0), pin("2", -7.5, 10, DOWN, 5.0),
     pin("3", 0, -10, UP, 5.0), pin("4", 0, 10, DOWN, 5.0),
     pin("5", 7.5, -10, UP, 5.0), pin("6", 7.5, 10, DOWN, 5.0)],
    keywords=["contacteur", "puissance", "tripolaire"],
    desig=(-14.0, -2.0), value=(11.0, -2.0))

sym("thermal-relay-3p", "Relais thermique tripolaire", "Puissance", "F", "thermal",
    [rect(-11.0, -5.0, 18.0, 10.0),
     poly([(-7.5, -5), (-7.5, -1), (-5.5, -1), (-5.5, 1), (-7.5, 1), (-7.5, 5)]),
     poly([(0, -5), (0, -1), (2, -1), (2, 1), (0, 1), (0, 5)]),
     poly([(7.5, -5), (7.5, -1), (9.5, -1), (9.5, 1), (7.5, 1), (7.5, 5)])],
    [pin("1", -7.5, -10, UP, 5.0), pin("2", -7.5, 10, DOWN, 5.0),
     pin("3", 0, -10, UP, 5.0), pin("4", 0, 10, DOWN, 5.0),
     pin("5", 7.5, -10, UP, 5.0), pin("6", 7.5, 10, DOWN, 5.0)],
    keywords=["thermique", "surcharge", "protection moteur"],
    fields={"value": "4 - 6,3 A"}, desig=(-14.0, -2.0), value=(13.0, -2.0))

sym("motor-3ph", "Moteur triphasé", "Puissance", "M", "motor",
    [circle(0, 0, 8.0), text(0, 1.0, "M", 5.0), text(0, 6.0, "3~", 3.0)],
    [pin("U", -5, -13, UP, 5.0), pin("V", 0, -13, UP, 5.0), pin("W", 5, -13, UP, 5.0),
     pin("PE", 0, 13, DOWN, 5.0, ptype="ground")],
    keywords=["moteur", "asynchrone", "triphasé"],
    fields={"value": "1,5 kW"}, desig=(-11.0, 0.0), value=(11.0, 0.0))

sym("motor-1ph", "Moteur monophasé", "Puissance", "M", "motor",
    [circle(0, 0, 8.0), text(0, 1.0, "M", 5.0), text(0, 6.0, "1~", 3.0)],
    [pin("U", -4, -13, UP, 5.0), pin("N", 4, -13, UP, 5.0),
     pin("PE", 0, 13, DOWN, 5.0, ptype="ground")],
    keywords=["moteur", "monophasé"], desig=(-11.0, 0.0), value=(11.0, 0.0))

sym("transformer", "Transformateur de commande", "Puissance", "T", "transformer",
    [circle(-3.0, 0, 5.0), circle(3.0, 0, 5.0), line(0, -7, 0, 7, 0.15)],
    [pin("1", -3, -13, UP, 5.0), pin("2", -3, 13, DOWN, 5.0),
     pin("3", 3, -13, UP, 5.0), pin("4", 3, 13, DOWN, 5.0)],
    keywords=["transformateur", "230/24"],
    fields={"value": "230 / 24 V"}, desig=(-11.0, 0.0), value=(11.0, 0.0))

sym("heater", "Résistance chauffante", "Puissance", "E", "heater",
    [rect(-4, -6, 8, 12), line(-4, -2, 4, -2), line(-4, 2, 4, 2)],
    [pin("1", 0, -10, UP, 4.0), pin("2", 0, 10, DOWN, 4.0)],
    keywords=["chauffage", "résistance"])

sym("socket-outlet", "Prise de courant", "Puissance", "X", "socket",
    [arc(0, 0, 5.0, 0, 180), line(-5, 0, 5, 0), line(0, 0, 0, -5)],
    [pin("L", 0, -10, UP, 5.0), pin("N", 5, 5, RIGHT, 3.0),
     pin("PE", -5, 5, LEFT, 3.0, ptype="ground")],
    keywords=["prise", "socle"])

# ==========================================================================
# Bornes et raccordements
# ==========================================================================

sym("terminal", "Borne", "Bornes", "X", "terminal",
    [circle(0, 0, 1.6)],
    [pin("1", 0, -7, UP, 5.4, ptype="terminal"),
     pin("2", 0, 7, DOWN, 5.4, ptype="terminal")],
    keywords=["borne", "bornier"], desig=(-4.0, 0.0), value=(4.0, 0.0))

sym("earth", "Prise de terre", "Bornes", "PE", "earth",
    [line(-5, 0, 5, 0, 0.4), line(-3.2, 2.2, 3.2, 2.2), line(-1.4, 4.4, 1.4, 4.4)],
    [pin("PE", 0, -6, UP, 6.0, ptype="ground")],
    keywords=["terre", "PE", "masse"], desig=(-8.0, 3.0), value=(8.0, 3.0))

sym("chassis", "Masse châssis", "Bornes", "PE", "earth",
    [line(-4, 0, 4, 0, 0.35), line(-4, 0, -6, 3), line(0, 0, -2, 3), line(4, 0, 2, 3)],
    [pin("MA", 0, -6, UP, 6.0, ptype="ground")],
    keywords=["masse", "châssis"], desig=(-8.0, 3.0), value=(8.0, 3.0))

sym("connector-plug", "Connecteur mâle", "Bornes", "X", "connector",
    [poly([(-2, -3), (2, 0), (-2, 3)])],
    [pin("1", -5, 0, LEFT, 3.0, ptype="terminal")],
    keywords=["connecteur", "fiche"])

sym("connector-socket", "Connecteur femelle", "Bornes", "X", "connector",
    [arc(0, 0, 3.0, 90, 180)],
    [pin("1", 5, 0, RIGHT, 3.0, ptype="terminal")],
    keywords=["connecteur", "embase"])

# ==========================================================================
# Mesure
# ==========================================================================

for logical, letter, label, kws in [
        ("ammeter", "A", "Ampèremètre", ["ampèremètre", "courant"]),
        ("voltmeter", "V", "Voltmètre", ["voltmètre", "tension"]),
        ("wattmeter", "W", "Wattmètre", ["wattmètre", "puissance"]),
        ("frequency-meter", "Hz", "Fréquencemètre", ["fréquence"])]:
    sym("meter-" + logical, label, "Mesure", "P", "meter",
        [circle(0, 0, 5.0), text(0, 1.8, letter, 4.0)],
        [pin("1", 0, -10, UP, 5.0), pin("2", 0, 10, DOWN, 5.0)],
        keywords=kws)

sym("current-transformer", "Transformateur de courant", "Mesure", "T", "ct",
    [circle(0, 0, 5.0), line(-9, 0, 9, 0, 0.5)],
    [pin("P1", -12, 0, LEFT, 3.0), pin("P2", 12, 0, RIGHT, 3.0),
     pin("S1", -2.5, 10, DOWN, 5.0), pin("S2", 2.5, 10, DOWN, 5.0)],
    keywords=["TC", "courant", "mesure"], desig=(0.0, -8.0), value=(0.0, 14.0))

sym("energy-meter", "Compteur d'énergie", "Mesure", "P", "meter",
    [rect(-7, -5, 14, 10), text(0, 1.8, "kWh", 3.0)],
    [pin("1", -3.5, -10, UP, 5.0), pin("2", -3.5, 10, DOWN, 5.0),
     pin("3", 3.5, -10, UP, 5.0), pin("4", 3.5, 10, DOWN, 5.0)],
    keywords=["compteur", "énergie"], desig=(-10.0, 0.0), value=(10.0, 0.0))

# ==========================================================================
# Distribution — representation unifilaire
# ==========================================================================

sym("busbar", "Jeu de barres", "Distribution", "W", "busbar",
    [line(-30, 0, 30, 0, 1.2)],
    [pin("1", -30, 0, LEFT, 0.0), pin("2", 30, 0, RIGHT, 0.0),
     pin("T1", -20, 0, DOWN, 0.0), pin("T2", 0, 0, DOWN, 0.0),
     pin("T3", 20, 0, DOWN, 0.0)],
    keywords=["jeu de barres", "unifilaire", "distribution"],
    desig=(-33.0, -3.0), value=(33.0, -3.0))

sym("transformer-power", "Transformateur de puissance", "Distribution", "T", "transformer",
    [circle(0, -4.0, 6.0), circle(0, 4.0, 6.0)],
    [pin("HT", 0, -14, UP, 4.0), pin("BT", 0, 14, DOWN, 4.0)],
    keywords=["transformateur", "HTA", "poste"],
    fields={"value": "630 kVA"}, desig=(-10.0, 0.0), value=(10.0, 0.0))

sym("surge-arrester", "Parafoudre", "Distribution", "F", "arrester",
    [rect(-3.5, -6, 7, 12), poly([(-2, 3), (0, -1), (-1, -1), (1, -5)])],
    [pin("1", 0, -10, UP, 4.0), pin("2", 0, 10, DOWN, 4.0, ptype="ground")],
    keywords=["parafoudre", "surtension"])

sym("generator", "Groupe électrogène", "Distribution", "G", "generator",
    [circle(0, 0, 8.0), text(0, 1.0, "G", 5.0), text(0, 6.0, "3~", 3.0)],
    [pin("L", 0, -13, UP, 5.0), pin("N", 6, 11, DOWN, 3.0)],
    keywords=["groupe", "générateur", "secours"], desig=(-11.0, 0.0), value=(11.0, 0.0))

sym("battery", "Batterie", "Distribution", "G", "battery",
    [line(-4, -2, 4, -2, 0.5), line(-2, 1, 2, 1), line(-4, 4, 4, 4, 0.5),
     line(-2, 7, 2, 7)],
    [pin("+", 0, -8, UP, 6.0, ptype="power"), pin("-", 0, 13, DOWN, 6.0, ptype="power")],
    keywords=["batterie", "accumulateur"], desig=(-8.0, 2.0), value=(8.0, 2.0))

# ==========================================================================
# Automatisme
# ==========================================================================

sym("plc-input", "Entrée automate", "Automatisme", "A", "plc-io",
    [rect(-10, -4, 20, 8), text(-6, 1.5, "I", 3.0), line(-2, -4, -2, 4)],
    [pin("I", -14, 0, LEFT, 4.0, ptype="input"),
     pin("COM", 14, 0, RIGHT, 4.0)],
    keywords=["automate", "API", "entrée"], desig=(0.0, -7.0), value=(0.0, 9.0))

sym("plc-output", "Sortie automate", "Automatisme", "A", "plc-io",
    [rect(-10, -4, 20, 8), text(-6, 1.5, "Q", 3.0), line(-2, -4, -2, 4)],
    [pin("Q", -14, 0, LEFT, 4.0, ptype="output"),
     pin("COM", 14, 0, RIGHT, 4.0)],
    keywords=["automate", "API", "sortie"], desig=(0.0, -7.0), value=(0.0, 9.0))

# ==========================================================================
# Electronique — trace horizontal
# ==========================================================================

sym("resistor", "Résistance", "Électronique", "R", "resistor",
    [rect(-5, -2, 10, 4)],
    [pin("1", -10, 0, LEFT, 5.0), pin("2", 10, 0, RIGHT, 5.0)],
    keywords=["résistance", "resistor"], fields={"value": "10 kΩ"},
    desig=(0.0, -4.5), value=(0.0, 7.0))

sym("capacitor", "Condensateur", "Électronique", "C", "capacitor",
    [line(-1.2, -4, -1.2, 4, 0.4), line(1.2, -4, 1.2, 4, 0.4)],
    [pin("1", -8, 0, LEFT, 6.8), pin("2", 8, 0, RIGHT, 6.8)],
    keywords=["condensateur"], fields={"value": "100 nF"},
    desig=(0.0, -6.0), value=(0.0, 8.5))

sym("capacitor-polarized", "Condensateur polarisé", "Électronique", "C", "capacitor",
    [line(-1.2, -4, -1.2, 4, 0.4), arc(-6.0, 0, 5.6, -45, 90, 0.4),
     text(-4.0, -5.0, "+", 2.5)],
    [pin("+", -8, 0, LEFT, 6.8), pin("-", 8, 0, RIGHT, 6.8)],
    keywords=["condensateur", "chimique"], fields={"value": "470 µF"},
    desig=(0.0, -7.5), value=(0.0, 8.5))

sym("inductor", "Bobine", "Électronique", "L", "inductor",
    [arc(-4.5, 0, 1.5, 0, 180), arc(-1.5, 0, 1.5, 0, 180),
     arc(1.5, 0, 1.5, 0, 180), arc(4.5, 0, 1.5, 0, 180)],
    [pin("1", -10, 0, LEFT, 4.0), pin("2", 10, 0, RIGHT, 4.0)],
    keywords=["inductance", "self"], fields={"value": "10 mH"},
    desig=(0.0, -4.5), value=(0.0, 6.0))

sym("diode", "Diode", "Électronique", "D", "diode",
    [poly([(-2.5, -3), (2.5, 0), (-2.5, 3), (-2.5, -3)]), line(2.5, -3, 2.5, 3, 0.4)],
    [pin("A", -8, 0, LEFT, 5.5), pin("K", 8, 0, RIGHT, 5.5)],
    keywords=["diode"], fields={"value": "1N4007"},
    desig=(0.0, -5.0), value=(0.0, 7.5))

sym("led", "Diode électroluminescente", "Électronique", "D", "diode",
    [poly([(-2.5, -3), (2.5, 0), (-2.5, 3), (-2.5, -3)]), line(2.5, -3, 2.5, 3, 0.4),
     poly([(1.0, -5.0), (3.5, -7.0)]), poly([(2.5, -4.0), (5.0, -6.0)])],
    [pin("A", -8, 0, LEFT, 5.5), pin("K", 8, 0, RIGHT, 5.5)],
    keywords=["led", "diode", "voyant"], desig=(0.0, -8.5), value=(0.0, 7.5))

sym("zener", "Diode Zener", "Électronique", "D", "diode",
    [poly([(-2.5, -3), (2.5, 0), (-2.5, 3), (-2.5, -3)]),
     poly([(1.2, -4.0), (2.5, -3.0), (2.5, 3.0), (3.8, 4.0)], 0.4)],
    [pin("A", -8, 0, LEFT, 5.5), pin("K", 8, 0, RIGHT, 5.5)],
    keywords=["zener", "régulation"], fields={"value": "5V1"},
    desig=(0.0, -6.0), value=(0.0, 7.5))

sym("transistor-npn", "Transistor NPN", "Électronique", "Q", "transistor",
    [circle(0, 0, 6.0), line(-2.0, -4, -2.0, 4, 0.4),
     line(-2.0, -2, 3.0, -5), line(-2.0, 2, 3.0, 5),
     poly([(3.0, 5.0), (0.8, 4.2), (1.6, 2.6)], 0.25)],
    [pin("B", -11, 0, LEFT, 5.0), pin("C", 3, -11, UP, 6.0), pin("E", 3, 11, DOWN, 6.0)],
    keywords=["transistor", "npn"], fields={"value": "BC547"},
    desig=(-9.0, -7.0), value=(9.0, -7.0))

sym("transistor-pnp", "Transistor PNP", "Électronique", "Q", "transistor",
    [circle(0, 0, 6.0), line(-2.0, -4, -2.0, 4, 0.4),
     line(-2.0, -2, 3.0, -5), line(-2.0, 2, 3.0, 5),
     poly([(-2.0, 2.0), (0.2, 2.8), (-0.6, 4.4)], 0.25)],
    [pin("B", -11, 0, LEFT, 5.0), pin("C", 3, -11, UP, 6.0), pin("E", 3, 11, DOWN, 6.0)],
    keywords=["transistor", "pnp"], fields={"value": "BC557"},
    desig=(-9.0, -7.0), value=(9.0, -7.0))

sym("opamp", "Amplificateur opérationnel", "Électronique", "U", "opamp",
    [poly([(-6, -8), (8, 0), (-6, 8), (-6, -8)]),
     text(-4.0, -3.0, "−", 3.0, "left"), text(-4.0, 6.0, "+", 3.0, "left")],
    [pin("IN-", -11, -4, LEFT, 5.0, ptype="input"),
     pin("IN+", -11, 4, LEFT, 5.0, ptype="input"),
     pin("OUT", 13, 0, RIGHT, 5.0, ptype="output"),
     pin("V+", 1, -11, UP, 6.0, ptype="power"),
     pin("V-", 1, 11, DOWN, 6.0, ptype="power")],
    keywords=["ampli", "aop", "opamp"], fields={"value": "LM358"},
    desig=(0.0, -13.0), value=(0.0, 15.0))

sym("voltage-source", "Source de tension", "Électronique", "V", "source",
    [circle(0, 0, 5.0), text(0, -1.0, "+", 3.0), text(0, 4.5, "−", 3.0)],
    [pin("+", 0, -10, UP, 5.0, ptype="power"), pin("-", 0, 10, DOWN, 5.0, ptype="power")],
    keywords=["source", "alimentation"], fields={"value": "24 V"},
    desig=(-8.0, 0.0), value=(8.0, 0.0))

sym("gnd", "Masse électronique", "Électronique", "GND", "earth",
    [line(-4, 0, 4, 0, 0.4), line(-2.5, 2, 2.5, 2), line(-1, 4, 1, 4)],
    [pin("GND", 0, -5, UP, 5.0, ptype="ground")],
    keywords=["masse", "gnd", "0V"], desig=(-7.0, 2.0), value=(7.0, 2.0))

# ==========================================================================
# Variantes ANSI
#
# Meme identifiant logique, autre norme : un projet bascule d'un jeu a l'autre
# sans toucher a ses instances.
# ==========================================================================

def ansi(logical_id, name, category, prefix, kind, graphics, pins, keywords=(),
         fields=None, desig=(-8.0, -2.0), value=(8.0, -2.0)):
    sym(logical_id, name, category, prefix, kind, graphics, pins, norm="ANSI",
        keywords=keywords, fields=fields, desig=desig, value=value)

# En Amerique du Nord les schemas se lisent en echelle horizontale : les
# contacts sont traces avec leurs bornes a gauche et a droite.
ansi("contact-no", "Normally open contact", "Control", "K", "contact",
     [line(-2.0, -4, -2.0, 4, 0.35), line(2.0, -4, 2.0, 4, 0.35)],
     [pin("13", -10, 0, LEFT, 8.0), pin("14", 10, 0, RIGHT, 8.0)],
     keywords=["contact", "NO", "normally open"])

ansi("contact-nc", "Normally closed contact", "Control", "K", "contact",
     [line(-2.0, -4, -2.0, 4, 0.35), line(2.0, -4, 2.0, 4, 0.35),
      line(-4.0, 4.5, 4.0, -4.5, 0.35)],
     [pin("11", -10, 0, LEFT, 8.0), pin("12", 10, 0, RIGHT, 8.0)],
     keywords=["contact", "NC", "normally closed"])

ansi("coil", "Relay coil", "Control", "K", "coil",
     [circle(0, 0, 5.0)],
     [pin("A1", -10, 0, LEFT, 5.0), pin("A2", 10, 0, RIGHT, 5.0)],
     keywords=["coil", "relay"])

ansi("pushbutton-no", "Pushbutton, normally open", "Control", "S", "pushbutton",
     [line(-2.0, -4, -2.0, 4, 0.35), line(2.0, -4, 2.0, 4, 0.35),
      line(0, -4, 0, -8), line(-3, -8, 3, -8)],
     [pin("13", -10, 0, LEFT, 8.0), pin("14", 10, 0, RIGHT, 8.0)],
     keywords=["pushbutton", "start"])

ansi("pushbutton-nc", "Pushbutton, normally closed", "Control", "S", "pushbutton",
     [line(-2.0, -4, -2.0, 4, 0.35), line(2.0, -4, 2.0, 4, 0.35),
      line(-4.0, 4.5, 4.0, -4.5, 0.35), line(0, -6, 0, -9), line(-3, -9, 3, -9)],
     [pin("11", -10, 0, LEFT, 8.0), pin("12", 10, 0, RIGHT, 8.0)],
     keywords=["pushbutton", "stop"])

ansi("indicator-lamp", "Indicating light", "Control", "H", "lamp",
     [circle(0, 0, 4.0), line(-2.8, -2.8, 2.8, 2.8), line(-2.8, 2.8, 2.8, -2.8)],
     [pin("X1", -9, 0, LEFT, 5.0), pin("X2", 9, 0, RIGHT, 5.0)],
     keywords=["pilot light", "lamp"])

# Le fusible NFPA est une bosse sur un trait continu : l'arc part de zero,
# pas de quatre-vingt-dix, sans quoi il dessine un « C » couche.
ansi("fuse", "Fuse", "Power", "FU", "fuse",
     [arc(0, 0, 4.0, 0, 180, 0.3), line(-6, 0, 6, 0, 0.25)],
     [pin("1", -10, 0, LEFT, 4.0), pin("2", 10, 0, RIGHT, 4.0)],
     keywords=["fuse", "protection"])

ansi("circuit-breaker-1p", "Circuit breaker, 1-pole", "Power", "CB", "breaker",
     [line(-5, 0, -1.5, 0), arc(1.0, 0, 2.6, 0, 180, 0.3), line(3.6, 0, 5, 0)],
     [pin("1", -10, 0, LEFT, 5.0), pin("2", 10, 0, RIGHT, 5.0)],
     keywords=["breaker", "protection"])

ansi("circuit-breaker-3p", "Circuit breaker, 3-pole", "Power", "CB", "breaker",
     [line(-5, -7.5, -1.5, -7.5), arc(1.0, -7.5, 2.6, 0, 180, 0.3), line(3.6, -7.5, 5, -7.5),
      line(-5, 0, -1.5, 0), arc(1.0, 0, 2.6, 0, 180, 0.3), line(3.6, 0, 5, 0),
      line(-5, 7.5, -1.5, 7.5), arc(1.0, 7.5, 2.6, 0, 180, 0.3), line(3.6, 7.5, 5, 7.5),
      line(0, -9.5, 0, 9.5, 0.15)],
     [pin("1", -10, -7.5, LEFT, 5.0), pin("2", 10, -7.5, RIGHT, 5.0),
      pin("3", -10, 0, LEFT, 5.0), pin("4", 10, 0, RIGHT, 5.0),
      pin("5", -10, 7.5, LEFT, 5.0), pin("6", 10, 7.5, RIGHT, 5.0)],
     keywords=["breaker", "three pole"], desig=(0.0, -12.0), value=(0.0, 14.0))

ansi("motor-3ph", "Motor, three phase", "Power", "M", "motor",
     [circle(0, 0, 8.0), text(0, 2.0, "M", 6.0)],
     [pin("T1", -5, -13, UP, 5.0), pin("T2", 0, -13, UP, 5.0), pin("T3", 5, -13, UP, 5.0),
      pin("PE", 0, 13, DOWN, 5.0, ptype="ground")],
     keywords=["motor", "induction"], desig=(-11.0, 0.0), value=(11.0, 0.0))

ansi("thermal-relay-3p", "Overload relay", "Power", "OL", "thermal",
     [rect(-8, -9, 16, 18),
      poly([(-4, -9), (-4, -2), (-1, -2), (-1, 2), (-4, 2), (-4, 9)]),
      poly([(1, -9), (1, -2), (4, -2), (4, 2), (1, 2), (1, 9)])],
     [pin("1", -12, -4.5, LEFT, 4.0), pin("2", 12, -4.5, RIGHT, 4.0),
      pin("3", -12, 4.5, LEFT, 4.0), pin("4", 12, 4.5, RIGHT, 4.0)],
     keywords=["overload", "thermal"], desig=(0.0, -12.0), value=(0.0, 14.0))

ansi("terminal", "Terminal", "Terminals", "TB", "terminal",
     [circle(0, 0, 1.6)],
     [pin("1", -7, 0, LEFT, 5.4, ptype="terminal"),
      pin("2", 7, 0, RIGHT, 5.4, ptype="terminal")],
     keywords=["terminal", "terminal block"], desig=(0.0, -4.0), value=(0.0, 6.0))

ansi("earth", "Ground", "Terminals", "GND", "earth",
     [line(-5, 0, 5, 0, 0.4), line(-3.2, 2.2, 3.2, 2.2), line(-1.4, 4.4, 1.4, 4.4)],
     [pin("PE", 0, -6, UP, 6.0, ptype="ground")],
     keywords=["ground", "earth"], desig=(-8.0, 3.0), value=(8.0, 3.0))

ansi("disconnector", "Disconnect switch", "Power", "DS", "disconnector",
     [line(-5, 0, -1.5, 0), line(-1.5, 0, 4.0, -4.0), line(5, 0, 3.0, 0)],
     [pin("1", -10, 0, LEFT, 5.0), pin("2", 10, 0, RIGHT, 5.0)],
     keywords=["disconnect", "isolator"])

ansi("contactor-power-3p", "Contactor, 3-pole", "Power", "K", "contactor",
     [line(-5, -7.5, -1.5, -7.5), line(-1.5, -7.5, 4.0, -10.5), line(5, -7.5, 3.4, -7.5),
      line(-5, 0, -1.5, 0), line(-1.5, 0, 4.0, -3.0), line(5, 0, 3.4, 0),
      line(-5, 7.5, -1.5, 7.5), line(-1.5, 7.5, 4.0, 4.5), line(5, 7.5, 3.4, 7.5),
      line(0, -9.5, 0, 9.5, 0.15)],
     [pin("1", -10, -7.5, LEFT, 5.0), pin("2", 10, -7.5, RIGHT, 5.0),
      pin("3", -10, 0, LEFT, 5.0), pin("4", 10, 0, RIGHT, 5.0),
      pin("5", -10, 7.5, LEFT, 5.0), pin("6", 10, 7.5, RIGHT, 5.0)],
     keywords=["contactor"], desig=(0.0, -13.0), value=(0.0, 14.0))

# -- commande, complement ------------------------------------------------

ansi("emergency-stop", "Emergency stop, mushroom head", "Control", "S", "pushbutton",
     [line(-2.0, -4, -2.0, 4, 0.35), line(2.0, -4, 2.0, 4, 0.35),
      line(-4.0, 4.5, 4.0, -4.5, 0.35),
      line(0, -4.5, 0, -8.0), arc(0, -8.0, 3.5, 0, 180, 0.3)],
     [pin("11", -10, 0, LEFT, 8.0), pin("12", 10, 0, RIGHT, 8.0)],
     keywords=["emergency", "stop", "mushroom"])

ansi("selector-switch", "Selector switch, 2-position", "Control", "SS", "switch",
     [line(-2.0, -4, -2.0, 4, 0.35), line(2.0, -4, 2.0, 4, 0.35),
      line(0, -4.5, -3.5, -8.0), line(-5.5, -8.0, -2.0, -8.0)],
     [pin("1", -10, 0, LEFT, 8.0), pin("2", 10, 0, RIGHT, 8.0)],
     keywords=["selector", "switch"])

ansi("limit-switch-no", "Limit switch, normally open", "Control", "LS", "switch",
     [line(-2.0, -4, -2.0, 4, 0.35), line(2.0, -4, 2.0, 4, 0.35),
      poly([(-6.5, 6.0), (-2.5, 6.0), (-4.5, 8.5), (-6.5, 6.0)])],
     [pin("13", -10, 0, LEFT, 8.0), pin("14", 10, 0, RIGHT, 8.0)],
     keywords=["limit switch"])

ansi("limit-switch-nc", "Limit switch, normally closed", "Control", "LS", "switch",
     [line(-2.0, -4, -2.0, 4, 0.35), line(2.0, -4, 2.0, 4, 0.35),
      line(-4.0, 4.5, 4.0, -4.5, 0.35),
      poly([(-6.5, 6.0), (-2.5, 6.0), (-4.5, 8.5), (-6.5, 6.0)])],
     [pin("11", -10, 0, LEFT, 8.0), pin("12", 10, 0, RIGHT, 8.0)],
     keywords=["limit switch"])

# Contact de relais de surcharge : les deux crochets decales de la convention
# NEMA, reconnaissables au premier coup d'oeil sur une echelle de commande.
ansi("thermal-contact", "Overload relay contact", "Control", "OL", "contact",
     [poly([(-3.0, 0.0), (-3.0, -2.6), (-0.8, -2.6)], 0.3),
      poly([(3.0, 0.0), (3.0, 2.6), (0.8, 2.6)], 0.3)],
     [pin("95", -10, 0, LEFT, 7.0), pin("96", 10, 0, RIGHT, 7.0)],
     keywords=["overload", "OL", "thermal"])

ansi("relay-coil", "Control relay coil", "Control", "CR", "coil",
     [circle(0, 0, 5.0), line(-3.5, -3.5, 3.5, 3.5)],
     [pin("A1", -10, 0, LEFT, 5.0), pin("A2", 10, 0, RIGHT, 5.0)],
     keywords=["relay", "CR"])

ansi("coil-timer-on", "On-delay timer coil", "Control", "TR", "coil",
     [circle(0, 0, 5.0), text(0, 1.6, "TR", 3.0)],
     [pin("A1", -10, 0, LEFT, 5.0), pin("A2", 10, 0, RIGHT, 5.0)],
     keywords=["timer", "on-delay"])

ansi("coil-timer-off", "Off-delay timer coil", "Control", "TR", "coil",
     [circle(0, 0, 5.0), text(0, 1.6, "TO", 3.0)],
     [pin("A1", -10, 0, LEFT, 5.0), pin("A2", 10, 0, RIGHT, 5.0)],
     keywords=["timer", "off-delay"])

ansi("horn", "Horn, audible alarm", "Control", "H", "horn",
     [arc(0, 0, 4.0, -90, 180, 0.3), line(0, -4, 0, 4)],
     [pin("1", -9, 0, LEFT, 5.0), pin("2", 9, 0, RIGHT, 5.0)],
     keywords=["horn", "alarm", "siren"])

# -- puissance, complement -----------------------------------------------

ansi("switch-disconnector", "Load-break switch", "Power", "DS", "disconnector",
     [line(-5, 0, -1.5, 0), line(-1.5, 0, 4.0, -4.0), line(5, 0, 3.0, 0),
      circle(-1.5, 0, 0.8, 0.3, True)],
     [pin("1", -10, 0, LEFT, 5.0), pin("2", 10, 0, RIGHT, 5.0)],
     keywords=["load break", "switch", "disconnect"])

ansi("fuse-disconnector", "Fused disconnect", "Power", "DS", "disconnector",
     [line(-7, 0, -4.5, 0), line(-4.5, 0, 0, -3.8),
      line(1.5, 0, 5.5, 0, 0.2), arc(3.5, 0, 2.0, 0, 180, 0.3), line(5.5, 0, 7, 0)],
     [pin("1", -10, 0, LEFT, 3.0), pin("2", 10, 0, RIGHT, 3.0)],
     keywords=["fused", "disconnect"])

ansi("motor-1ph", "Motor, single phase", "Power", "M", "motor",
     [circle(0, 0, 8.0), text(0, 2.0, "M", 6.0), text(0, 6.5, "1~", 2.6)],
     [pin("T1", -4, -13, UP, 5.0), pin("T2", 4, -13, UP, 5.0)],
     keywords=["motor", "single phase"], desig=(-11.0, 0.0), value=(11.0, 0.0))

ansi("transformer", "Control transformer", "Power", "T", "transformer",
     [arc(-4.5, -3, 1.5, 0, 180, 0.3), arc(-1.5, -3, 1.5, 0, 180, 0.3),
      arc(1.5, -3, 1.5, 0, 180, 0.3), arc(4.5, -3, 1.5, 0, 180, 0.3),
      line(-6, -0.7, 6, -0.7, 0.2), line(-6, 0.7, 6, 0.7, 0.2),
      arc(-4.5, 3, 1.5, 180, 180, 0.3), arc(-1.5, 3, 1.5, 180, 180, 0.3),
      arc(1.5, 3, 1.5, 180, 180, 0.3), arc(4.5, 3, 1.5, 180, 180, 0.3)],
     [pin("H1", -6, -10, UP, 7.0), pin("H2", 6, -10, UP, 7.0),
      pin("X1", -6, 10, DOWN, 7.0), pin("X2", 6, 10, DOWN, 7.0)],
     keywords=["transformer", "control"], fields={"value": "480 / 120 V"},
     desig=(-10.0, 0.0), value=(10.0, 0.0))

ansi("heater", "Heating element", "Power", "HTR", "heater",
     [poly([(-6, 0), (-4.5, -3), (-1.5, 3), (1.5, -3), (4.5, 3), (6, 0)])],
     [pin("1", -10, 0, LEFT, 4.0), pin("2", 10, 0, RIGHT, 4.0)],
     keywords=["heater", "element"])

ansi("socket-outlet", "Receptacle", "Power", "REC", "socket",
     [arc(0, 0, 5.0, 0, 180, 0.3), line(-5, 0, 5, 0), line(0, 0, 0, -5)],
     [pin("L", 0, -10, UP, 5.0), pin("N", 5, 5, RIGHT, 3.0),
      pin("G", -5, 5, LEFT, 3.0, ptype="ground")],
     keywords=["receptacle", "outlet"])

# -- mesure ----------------------------------------------------------------

ansi("meter-ammeter", "Ammeter", "Metering", "AM", "meter",
     [circle(0, 0, 5.0), text(0, 1.8, "A", 4.0)],
     [pin("1", -10, 0, LEFT, 5.0), pin("2", 10, 0, RIGHT, 5.0)],
     keywords=["ammeter", "current"])

ansi("meter-voltmeter", "Voltmeter", "Metering", "VM", "meter",
     [circle(0, 0, 5.0), text(0, 1.8, "V", 4.0)],
     [pin("1", -10, 0, LEFT, 5.0), pin("2", 10, 0, RIGHT, 5.0)],
     keywords=["voltmeter", "voltage"])

ansi("current-transformer", "Current transformer", "Metering", "CT", "ct",
     [circle(0, 0, 5.0), line(-9, 0, 9, 0, 0.5)],
     [pin("H1", -12, 0, LEFT, 3.0), pin("H2", 12, 0, RIGHT, 3.0),
      pin("X1", -2.5, 10, DOWN, 5.0), pin("X2", 2.5, 10, DOWN, 5.0)],
     keywords=["CT", "current"], desig=(0.0, -8.0), value=(0.0, 14.0))

# -- distribution et automatisme -------------------------------------------

ansi("battery", "Battery", "Distribution", "BAT", "battery",
     [line(-4, -2, 4, -2, 0.5), line(-2, 1, 2, 1), line(-4, 4, 4, 4, 0.5),
      line(-2, 7, 2, 7)],
     [pin("+", 0, -8, UP, 6.0, ptype="power"), pin("-", 0, 13, DOWN, 6.0, ptype="power")],
     keywords=["battery"], desig=(-8.0, 2.0), value=(8.0, 2.0))

ansi("generator", "Generator", "Distribution", "G", "generator",
     [circle(0, 0, 8.0), text(0, 1.0, "G", 5.0), text(0, 6.0, "3~", 3.0)],
     [pin("L", 0, -13, UP, 5.0), pin("N", 6, 11, DOWN, 3.0)],
     keywords=["generator", "genset"], desig=(-11.0, 0.0), value=(11.0, 0.0))

ansi("busbar", "Busbar", "Distribution", "BUS", "busbar",
     [line(-30, 0, 30, 0, 1.2)],
     [pin("1", -30, 0, LEFT, 0.0), pin("2", 30, 0, RIGHT, 0.0),
      pin("T1", -20, 0, DOWN, 0.0), pin("T2", 0, 0, DOWN, 0.0),
      pin("T3", 20, 0, DOWN, 0.0)],
     keywords=["busbar", "bus"], desig=(-33.0, -3.0), value=(33.0, -3.0))

ansi("surge-arrester", "Surge arrester", "Distribution", "SA", "arrester",
     [rect(-3.5, -6, 7, 12), poly([(-2, 3), (0, -1), (-1, -1), (1, -5)])],
     [pin("1", 0, -10, UP, 4.0), pin("2", 0, 10, DOWN, 4.0, ptype="ground")],
     keywords=["surge", "arrester", "SPD"])

ansi("plc-input", "PLC input", "Automation", "PLC", "plc-io",
     [rect(-10, -4, 20, 8), text(-6, 1.5, "I", 3.0), line(-2, -4, -2, 4)],
     [pin("I", -14, 0, LEFT, 4.0, ptype="input"), pin("COM", 14, 0, RIGHT, 4.0)],
     keywords=["PLC", "input"], desig=(0.0, -7.0), value=(0.0, 9.0))

ansi("plc-output", "PLC output", "Automation", "PLC", "plc-io",
     [rect(-10, -4, 20, 8), text(-6, 1.5, "Q", 3.0), line(-2, -4, -2, 4)],
     [pin("Q", -14, 0, LEFT, 4.0, ptype="output"), pin("COM", 14, 0, RIGHT, 4.0)],
     keywords=["PLC", "output"], desig=(0.0, -7.0), value=(0.0, 9.0))

ansi("chassis", "Chassis ground", "Terminals", "GND", "earth",
     [line(-4, 0, 4, 0, 0.35), line(-4, 0, -6, 3), line(0, 0, -2, 3), line(4, 0, 2, 3)],
     [pin("MA", 0, -6, UP, 6.0, ptype="ground")],
     keywords=["chassis", "ground"], desig=(-8.0, 3.0), value=(8.0, 3.0))

ansi("resistor", "Resistor", "Electronics", "R", "resistor",
     [poly([(-6, 0), (-5, -3), (-3, 3), (-1, -3), (1, 3), (3, -3), (5, 3), (6, 0)])],
     [pin("1", -10, 0, LEFT, 4.0), pin("2", 10, 0, RIGHT, 4.0)],
     keywords=["resistor"], fields={"value": "10k"},
     desig=(0.0, -5.0), value=(0.0, 7.0))

# ==========================================================================
# Ecriture
# ==========================================================================

def write_library():
    by_dir = {}
    for s in SYMBOLS:
        folder = ROOT / "libraries" / s["norm"].lower()
        slug = s["category"].lower()
        for a, b in [("é", "e"), ("è", "e"), ("ê", "e"), ("à", "a"), ("ç", "c"), (" ", "-")]:
            slug = slug.replace(a, b)
        by_dir.setdefault((folder, slug), []).append(s)

    total = 0
    for (folder, slug), symbols in sorted(by_dir.items(), key=lambda kv: str(kv[0])):
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / ("%s.json" % slug)
        payload = {
            "format": "dessins-symbol-library",
            "version": 1,
            "symbols": sorted(symbols, key=lambda s: s["logicalId"]),
        }
        path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8")
        total += len(symbols)
        print("%-46s %3d symboles" % (os.path.relpath(path, ROOT), len(symbols)))

    ids = [s["id"] for s in SYMBOLS]
    assert len(ids) == len(set(ids)), "identifiants en double : %s" % (
        sorted({i for i in ids if ids.count(i) > 1}),)
    print("---")
    print("%d symboles au total (%d CEI, %d ANSI)"
          % (total,
             sum(1 for s in SYMBOLS if s["norm"] == "IEC"),
             sum(1 for s in SYMBOLS if s["norm"] == "ANSI")))

if __name__ == "__main__":
    write_library()
