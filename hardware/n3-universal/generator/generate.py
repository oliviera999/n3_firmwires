#!/usr/bin/env python3
"""Génère le projet KiCad (schéma + PCB + BOM) de la carte porteuse UNIVERSELLE
n3-universal (msp / n3pp / ffp5cs, bi-module WROOM ou ESP32-S3). Zone secteur
isolée en haut de carte (fentes, règle DRC 3 mm mains<->logique via .kicad_dru),
header de service, distribution 5V/3V3/GND en borniers + header.
Routage : generator/route_universal.py.

Source de vérité : ../pinmap_universel_propose.json et les sections
PINMAP_UNIVERSAL des trois firmwares (l'ensemble étant vérifié contre les pads
réels du PCB par tools/check_pinmap_vs_firmware.py ; la géométrie des corps 3D et
des couloirs d'insertion par tools/check_pcb_clearance.py). Les empreintes proviennent de la
bibliothèque officielle KiCad 8.0.9 (vendorées dans ./footprints, licence
CC-BY-SA 4.0 avec exception d'usage — voir README).

Usage : python generate.py [--regen-pcb]   (écrit dans ../kicad/ et ../BOM.csv)

Les fichiers générés sont au format KiCad 8 (s-expressions), ouvrables et
éditables dans KiCad 8/9/10 (le PCB routé est enregistré au format KiCad 10).
Méthode hybride : un PCB déjà routé n'est PAS réécrit (pistes et zones
perdues) sauf `--regen-pcb` ; les libellés `PCB_TEXTS` y sont reportés par
finalize_board.py. Schéma, BOM, règles et empreintes sont toujours régénérés.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
KICAD_DIR = ROOT / "kicad"
FP_DIR = HERE / "footprints"
PROJECT = "n3-universal"
REV = "0.2"
REV_DATE = "2026-10-07"
NS = uuid.UUID("6ba7b810-9dad-11d1-80b4-00c04fd430c8")
ROOT_UUID = str(uuid.uuid5(NS, PROJECT + "/root"))

# Nets UNIVERSELS (rôle-neutres, cf. ../pinmap_universel_propose.json) : chaque
# net touche le site A1 (WROOM) ET le site A2 (S3) ; plusieurs firmwares peuvent
# l'utiliser pour des fonctions différentes (rôles disjoints). Les clés du dict
# gardent les noms historiques ffp5cs pour minimiser la dérive avec la base 230V.
PINMAP = json.loads((ROOT / "pinmap_universel_propose.json").read_text(encoding="utf-8"))
NET = {
    "POMPE_AQUA": "K1", "POMPE_RESERV": "K2", "RADIATEURS": "K3", "LUMIERE": "K4",
    "AUX1": "K5", "AUX2": "K6",
    "ULTRASON_AQUA": "US1", "ULTRASON_TANK": "US2", "ULTRASON_POTA": "US3",
    "LUMINOSITE": "ADC_E", "DHT_PIN": "DHT_INT", "ONE_WIRE_BUS": "ONEWIRE",
    "I2C_SDA": "I2C_SDA", "I2C_SCL": "I2C_SCL",
    "SERVO_GROS": "SERVO1", "SERVO_PETITS": "SERVO2",
}

G = 2.54  # pas de grille schéma (mm)


def uid(*parts: object) -> str:
    """UUID déterministe (régénération stable → diffs git lisibles)."""
    return str(uuid.uuid5(NS, PROJECT + "/" + "/".join(str(p) for p in parts)))


# ---------------------------------------------------------------------------
# Mini bibliothèque S-expression (parse + dump) pour manipuler les .kicad_mod
# ---------------------------------------------------------------------------

class Sym(str):
    """Atome non quoté."""


def sx_parse(text: str):
    tokens = re.findall(r'"(?:[^"\\]|\\.)*"|[()]|[^\s()"]+', text)
    pos = 0

    def parse():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok == "(":
            lst = []
            while tokens[pos] != ")":
                lst.append(parse())
            pos += 1
            return lst
        if tok == ")":
            raise ValueError("unbalanced )")
        if tok.startswith('"'):
            return tok[1:-1].replace('\\"', '"').replace("\\\\", "\\")
        return Sym(tok)

    out = parse()
    if pos != len(tokens):
        raise ValueError("trailing tokens in s-expression")
    return out


def sx_dump(node, indent: int = 0) -> str:
    if isinstance(node, list):
        head = "  " * indent + "("
        parts, complex_child = [], any(isinstance(c, list) for c in node)
        if not complex_child:
            inner = " ".join(sx_atom(c) for c in node)
            return head + inner + ")"
        # atomes de tête sur la première ligne, listes enfants indentées
        i = 0
        first = []
        while i < len(node) and not isinstance(node[i], list):
            first.append(sx_atom(node[i]))
            i += 1
        lines = [head + " ".join(first)]
        for child in node[i:]:
            if isinstance(child, list):
                lines.append(sx_dump(child, indent + 1))
            else:
                lines.append("  " * (indent + 1) + sx_atom(child))
        lines.append("  " * indent + ")")
        return "\n".join(lines)
    return "  " * indent + sx_atom(node)


def sx_atom(a) -> str:
    if isinstance(a, Sym):
        return str(a)
    if isinstance(a, str):
        return '"' + a.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return str(a)


def sx_find_all(node, name):
    return [c for c in node if isinstance(c, list) and c and c[0] == name]


# ---------------------------------------------------------------------------
# Définition des symboles schématiques (bibliothèque embarquée "ffp5cs")
# left/right : listes (numéro, nom, py_en_unités_de_grille)
# ---------------------------------------------------------------------------

SYMBOLS: dict[str, dict] = {
    "R": dict(ref="R", w=2, left=[("1", "~", 0)], right=[("2", "~", 0)]),
    "C": dict(ref="C", w=2, left=[("1", "~", 0)], right=[("2", "~", 0)]),
    "CP": dict(ref="C", w=2, left=[("1", "+", 0)], right=[("2", "-", 0)]),
    "LED": dict(ref="D", w=2, left=[("1", "K", 0)], right=[("2", "A", 0)]),
    "D": dict(ref="D", w=2, left=[("1", "K", 0)], right=[("2", "A", 0)]),
    "NPN": dict(ref="Q", w=3, left=[("2", "B", 0)],
                right=[("1", "C", 1), ("3", "E", -1)]),
    "RELAY_SRD": dict(ref="K", w=5,
                      left=[("5", "COIL+", 1), ("2", "COIL-", -1)],
                      right=[("1", "COM", 2), ("3", "NO", 0), ("4", "NC", -2)]),
    "BARREL": dict(ref="J", w=4,
                   right=[("1", "TIP", 1), ("3", "SW", 0), ("2", "SLEEVE", -1)]),
    "CONN_02": dict(ref="J", w=3, left=[("1", "1", 1), ("2", "2", 0)]),
    "CONN_03": dict(ref="J", w=3,
                    left=[("1", "1", 1), ("2", "2", 0), ("3", "3", -1)]),
    "CONN_04": dict(ref="J", w=3,
                    left=[("1", "1", 2), ("2", "2", 1), ("3", "3", 0), ("4", "4", -1)]),
    "CONN_06": dict(ref="J", w=3,
                    left=[(str(i), str(i), 3 - i) for i in range(1, 7)]),
    "CONN_10": dict(ref="J", w=3,
                    left=[(str(i), str(i), 5 - i) for i in range(1, 11)]),
    "CONN_12": dict(ref="J", w=3,
                    left=[(str(i), str(i), 6 - i) for i in range(1, 13)]),
    "CONN_14": dict(ref="J", w=3,
                    left=[(str(i), str(i), 7 - i) for i in range(1, 15)]),
}

# ESP32 DevKit V1 30 broches — côté A (droite du module, USB en bas) = pads 1..15,
# côté B (gauche) = pads 16..30. Ordre physique bas→haut côté A : 3V3..D23.
DEVKIT_A = ["3V3", "GND", "GPIO15", "GPIO2", "GPIO4", "GPIO16", "GPIO17",
            "GPIO5", "GPIO18", "GPIO19", "GPIO21", "RX0", "TX0", "GPIO22", "GPIO23"]
DEVKIT_B = ["VIN", "GND", "GPIO13", "GPIO12", "GPIO14", "GPIO27", "GPIO26",
            "GPIO25", "GPIO33", "GPIO32", "GPIO35", "GPIO34", "GPIO39_VN",
            "GPIO36_VP", "EN"]
SYMBOLS["ESP32_DEVKIT_V1_30"] = dict(
    ref="A", w=8,
    left=[(str(i + 1), DEVKIT_A[i], 7 - i) for i in range(15)],
    right=[(str(i + 16), DEVKIT_B[i], 7 - i) for i in range(15)],
)

# ESP32-S3-DevKitC-1 44 broches (site A2, un seul module A1 OU A2 peuplé).
# Ordre physique haut->bas, antenne en haut : J1 (gauche) = pads 1..22,
# J3 (droite) = pads 23..44 — VERIFIER sur l'exemplaire réel avant soudure.
S3_LEFT = ["3V3", "3V3", "RST", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO15",
           "GPIO16", "GPIO17", "GPIO18", "GPIO8", "GPIO3", "GPIO46", "GPIO9",
           "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "5V", "GND"]
S3_RIGHT = ["GND", "TX0_43", "RX0_44", "GPIO1", "GPIO2", "GPIO42", "GPIO41",
            "GPIO40", "GPIO39", "GPIO38", "GPIO37", "GPIO36", "GPIO35",
            "GPIO0", "GPIO45", "GPIO48", "GPIO47", "GPIO21", "GPIO20",
            "GPIO19", "GND", "GND"]
SYMBOLS["ESP32_S3_DEVKITC_44"] = dict(
    ref="A", w=8,
    left=[(str(i + 1), S3_LEFT[i], 11 - i) for i in range(22)],
    right=[(str(i + 23), S3_RIGHT[i], 11 - i) for i in range(22)],
)
# P-MOSFET (power-gate rail capteurs et pont diviseur commuté — topologie
# reprise de la carte n3pp-msp-commun rev 0.2)
SYMBOLS["PMOS_GDS"] = dict(ref="Q", w=3, left=[("1", "G", 0)],
                           right=[("2", "D", 1), ("3", "S", -1)])
SYMBOLS["PMOS_DGS"] = dict(ref="Q", w=3, left=[("2", "G", 0)],
                           right=[("1", "D", 1), ("3", "S", -1)])
SYMBOLS["FUSE"] = dict(ref="F", w=2, left=[("1", "~", 0)], right=[("2", "~", 0)])
SYMBOLS["VARISTOR"] = dict(ref="RV", w=2, left=[("1", "~", 0)], right=[("2", "~", 0)])
SYMBOLS["HLK20M"] = dict(ref="PS", w=6,
                         left=[("1", "AC-L", 1), ("2", "AC-N", -1)],
                         right=[("4", "+5V", 1), ("3", "GND", -1)])
SYMBOLS["CONN_06S"] = dict(ref="J", w=3,
                           left=[(str(i), str(i), 3 - i) for i in range(1, 7)])
# Régulateur LD1117V33 / LM1117T (TO-220) : 1=GND 2=OUT 3=IN — brochage DIFFERENT
# d'un 78xx (IN/GND/OUT), d'où les libellés GND/OUT/IN sérigraphiés sous les pads.
SYMBOLS["LDO"] = dict(ref="U", w=4, left=[("3", "IN", 1), ("1", "GND", -1)],
                      right=[("2", "OUT", 1)])
SYMBOLS["TP"] = dict(ref="TP", w=1, left=[("1", "~", 0)])
SYMBOLS["CONN_08"] = dict(ref="J", w=3,
                          left=[(str(i), str(i), 4 - i) for i in range(1, 9)])


def sym_def(name: str, meta: dict) -> str:
    w_mm = meta["w"] * G
    half = w_mm / 2
    pins_y = [p[2] for p in meta.get("left", [])] + [p[2] for p in meta.get("right", [])]
    top = (max(pins_y) + 1) * G
    bot = (min(pins_y) - 1) * G
    font = "(effects (font (size 1.27 1.27)))"
    hidden = "(effects (font (size 1.27 1.27)) (hide yes))"
    pins = []
    for num, pname, py in meta.get("left", []):
        pins.append(
            f'      (pin passive line (at {-(half + G):.2f} {py * G:.2f} 0) (length {G})\n'
            f'        (name "{pname}" {font})\n        (number "{num}" {font})\n      )')
    for num, pname, py in meta.get("right", []):
        pins.append(
            f'      (pin passive line (at {half + G:.2f} {py * G:.2f} 180) (length {G})\n'
            f'        (name "{pname}" {font})\n        (number "{num}" {font})\n      )')
    return f'''    (symbol "n3u:{name}"
      (exclude_from_sim no) (in_bom yes) (on_board yes)
      (property "Reference" "{meta['ref']}" (at 0 {top + 1.27:.2f} 0) {font})
      (property "Value" "{name}" (at 0 {bot - 1.27:.2f} 0) {font})
      (property "Footprint" "" (at 0 0 0) {hidden})
      (property "Datasheet" "~" (at 0 0 0) {hidden})
      (symbol "{name}_0_1"
        (rectangle (start {-half:.2f} {top:.2f}) (end {half:.2f} {bot:.2f})
          (stroke (width 0.254) (type default)) (fill (type background)))
      )
      (symbol "{name}_1_1"
{chr(10).join(pins)}
      )
    )'''


# ---------------------------------------------------------------------------
# Nomenclature des composants — LA table qui décrit toute la carte.
# sch=(x,y) en unités de grille ; pcb=(x,y,rot) en mm ; nets: broche -> net.
# ---------------------------------------------------------------------------

def relay_channel(n: int, gpio_net: str, jref: str, k_x: float,
                  refs: dict | None = None, force_on: bool = True):
    """Canal relais n : commande GPIO -> transistor -> relais SRD-05 -> bornier 230V.
    Relais pivoté 90° : contacts vers le bord haut (zone secteur), bobine vers la
    logique. Bornier : 1=NC 2=COM 3=NO (routage secteur rectiligne, fait par
    route_universal.py, PAS par l'autorouteur).

    Rev 0.2 — sélecteur de forçage JPf (1x3) sur la base du transistor, SANS
    cavalier dans le chemin GPIO -> base (le mode AUTO, celui de la production,
    ne dépend d'aucun contact) : 1-2 = ON forcé (+3V3 permanent du module à
    travers Rf 1k, le GPIO bas n'absorbe que 0,7 mA), 2-3 = OFF forcé (base à
    GND, le GPIO haut débite 3,3 mA dans Rb), retiré = AUTO. `force_on=False`
    (chauffage K3) ne câble pas la broche 1 : ON forcé impossible.
    """
    col = k_x - 7
    # Références par défaut (canaux 1-4). Les canaux ajoutés passent un
    # override explicite pour ne pas percuter D5/LED5/R13... (alim, LDR, US).
    refs = refs or dict(rb=f"R{n}", rp=f"R{n + 4}", rl=f"R{n + 8}",
                        q=f"Q{n}", d=f"D{n}", led=f"LED{n}")
    rf, jpf = f"R{60 + n}", f"JP{4 + n}"
    bx, by = 111, 4 + 26 * (n - 1)  # bloc schéma (unités de grille)
    jp_nets = {"2": f"REL{n}_B", "3": "GND"}
    if force_on:
        jp_nets["1"] = f"REL{n}_ON"
    return [
        dict(ref=refs["rb"], sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance base transistor", sch=(bx, by), pcb=(col, 90.5, 0),
             nets={"1": gpio_net, "2": f"REL{n}_B"}),
        dict(ref=refs["rp"], sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-down base (état sûr au boot)", sch=(bx, by + 4), pcb=(col, 95, 0),
             nets={"1": f"REL{n}_B", "2": "GND"}),
        dict(ref=refs["rl"], sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance LED témoin", sch=(bx, by + 8), pcb=(col, 99.5, 0),
             nets={"1": "+5V", "2": f"REL{n}_LED"}),
        dict(ref=refs["q"], sym="NPN", value="BC337-40", fp="TO-92_Inline_Wide_CBE",
             desc="Transistor NPN commande relais (1=C 2=B 3=E, brochage C-B-E "
                  "sérigraphié : un 2N2222/S8050 E-B-C se monte retourné)",
             sch=(bx + 9, by + 2), pcb=(k_x + 7, 95, 0),
             nets={"1": f"REL{n}_SW", "2": f"REL{n}_B", "3": "GND"}),
        dict(ref=refs["d"], sym="D", value="1N4007",
             fp="D_DO-41_SOD81_P10.16mm_Horizontal",
             desc="Diode de roue libre bobine", sch=(bx + 9, by + 8), pcb=(col, 87, 0),
             nets={"1": "+5V", "2": f"REL{n}_SW"}),
        dict(ref=refs["led"], sym="LED", value="rouge", fp="LED_D5.0mm",
             desc="LED témoin relais ON", sch=(bx + 9, by + 12), pcb=(k_x + 9, 90, 90),
             nets={"1": f"REL{n}_SW", "2": f"REL{n}_LED"}),
        *([dict(ref=rf, sym="R", value="1k",
                fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
                desc=f"Forçage ON du canal K{n} : +3V3 -> 1k -> cavalier {jpf} 1-2 -> base",
                sch=(bx + 19, by + 10), pcb=(k_x + 16, 89.3, 270),
                silk=dict(ref=(5.08, 0, 90), value=(12.3, 0, 0)),
                nets={"1": "+3V3", "2": f"REL{n}_ON"})] if force_on else []),
        dict(ref=jpf, sym="CONN_03", value="Jumper ON/OFF", silk=dict(ref=(0, 7.6, 0)),
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc=f"Sélecteur K{n} : SANS cavalier = AUTO (firmware) ; 1-2 = ON forcé"
                  + (" ; 2-3 = OFF forcé" if force_on else
                     " INTERDIT (broche 1 non câblée, chauffage) ; 2-3 = OFF forcé"),
             sch=(bx + 19, by + 14), pcb=(k_x + 20.5, 90, 0),
             nets=jp_nets),
        dict(ref=f"K{n}", sym="RELAY_SRD", value="SRD-05VDC-SL-C",
             fp="Relay_SPDT_SANYOU_SRD_Series_Form_C",
             desc="Relais 5V SPDT Songle/Sanyou SRD Form C — 7A/240VAC et 3A "
                  "inductif REELS par contact (le 10A ne vaut qu'en 125VAC ou en Form A)",
             sch=(bx + 19, by + 2), pcb=(k_x, 78, 90),
             nets={"5": "+5V", "2": f"REL{n}_SW", "1": f"REL{n}_COM",
                   "3": f"REL{n}_NO", "4": f"REL{n}_NC"}),
        dict(ref=jref, sym="CONN_03", value="Bornier_5.08",
             fp="TerminalBlock_bornier-3_P5.08mm",
             desc="Bornier charge 230V (1=NC 2=COM 3=NO)",
             sch=(bx + 30, by + 2), pcb=(k_x - 5.08, 50, 0),
             nets={"1": f"REL{n}_NC", "2": f"REL{n}_COM", "3": f"REL{n}_NO"}),
    ]


def us_channel(idx: int, name: str, gpio_net: str, rref1: str, rref2: str,
               jref: str, x: float, jp: str):
    """HC-SR04 mono-broche : TRIG piloté direct, ECHO 5V ramené via pont 1k/2k.
    Rev 0.2 : bornier à vis 5,08 (fils nus, embouts) à la place du JST-XH ;
    le 2k vers GND passe par le cavalier de profil `jp` (FERMÉ = ffp5cs)."""
    bx, by = 92, 12 * (idx - 1) + 14
    rx = 132 + 21 * (idx - 1)
    return [
        dict(ref=rref1, sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc=f"Série écho HC-SR04 {name}", sch=(bx, by), pcb=(rx, 106, 0),
             nets={"1": f"US_{name}_ECHO", "2": gpio_net}),
        # Pont 2k vers GND requis pour ffp5cs (écho HC-SR04 5V) mais les 2k
        # permanents écrasaient les rôles msp des nets partagés (PLUIE/US1,
        # DHT_EXT/US2 : niveau haut plafonné à ~0,55 V) — audit rev 0.1. En 0.2
        # la résistance est TOUJOURS posée, c'est le cavalier qui la met en
        # circuit (profil ffp5cs) : une seule BOM pour les 5 cartes.
        dict(ref=rref2, sym="R", value="2k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc=f"Pont diviseur écho {name} (5V->3V3) — en circuit si {jp} FERMÉ (ffp5cs)",
             sch=(bx, by + 4), pcb=(rx, 111, 0),
             nets={"1": gpio_net, "2": f"US_{name}_DIV"}),
        dict(ref=jp, sym="CONN_02", value="Jumper profil",
             fp="PinHeader_1x02_P2.54mm_Vertical",
             desc=f"Profil : FERMÉ = pont écho {name} actif (ffp5cs) ; OUVERT = msp/n3pp"
                  + (" et option wroom-sd" if idx == 3 else ""),
             sch=(bx + 8, by + 4), pcb=(rx + 15, 111, 90),
             nets={"1": f"US_{name}_DIV", "2": "GND"}),
        dict(ref=jref, sym="CONN_04", value="Bornier_5.08",
             fp="TerminalBlock_bornier-4_P5.08mm",
             desc=f"HC-SR04 {name} (1=5V 2=TRIG 3=ECHO 4=GND) — fils nus + embouts",
             sch=(bx + 16, by), pcb=(x, 166, 0),
             nets={"1": "+5V", "2": gpio_net, "3": f"US_{name}_ECHO", "4": "GND"}),
    ]


def build_components():
    comps = []
    # --- Module ESP32 DevKit V1 (sur supports 2×15) --------------------------
    # Cartographie WROOM universelle (pinmap_universel_propose.json / pins.h
    # PINMAP_UNIVERSAL des 3 firmwares) : DEVKIT_A/B -> pads 1..30.
    a1_nets = {
        "1": "+3V3", "2": "GND", "3": NET["DHT_PIN"],            # GPIO15
        "4": NET["ONE_WIRE_BUS"],                                 # GPIO2
        "5": NET["ULTRASON_AQUA"],                                # GPIO4 (US1, ∥ Pluie msp)
        "6": NET["POMPE_AQUA"], "7": NET["POMPE_RESERV"],         # GPIO16/17 (K1/K2)
        "8": NET["ULTRASON_TANK"],                                # GPIO5 (US2, ∥ DHT_EXT msp)
        "9": NET["RADIATEURS"], "10": NET["LUMIERE"],             # GPIO18/19 (K3/K4)
        "11": NET["I2C_SDA"], "12": "SPARE_RX0",
        "13": "SPARE_TX0", "14": NET["I2C_SCL"],
        "15": NET["AUX1"],                                        # GPIO23 (K5, = SD_CLK en wroom-sd)
        "16": "VIN_5V", "17": "GND",
        "18": "GATE",                                             # GPIO13 (rail +3V3_SW)
        "19": "SD_MISO_W",                                        # GPIO12/MTDI (-> JP11 2-3, SANS pull-up)
        "20": NET["ULTRASON_POTA"],                               # GPIO14 (US3, = SD_CS en wroom-sd)
        "21": NET["SERVO_PETITS"], "22": NET["SERVO_GROS"],       # GPIO27/26
        "23": NET["AUX2"],                                        # GPIO25 (K6, = SD_MOSI en wroom-sd)
        "24": "ADC_B", "25": "ADC_A",                             # GPIO33/32
        "26": "ADC_D", "27": "ADC_C",                             # GPIO35/34 (entrées seules)
        "28": "ADC_VBAT", "29": NET["LUMINOSITE"],                # GPIO39/36
        "30": "EN",
    }
    comps.append(dict(ref="A1", sym="ESP32_DEVKIT_V1_30", value="ESP32 DevKit V1",
                      fp="ESP32_DevKit_V1_30pin",
                      desc="Module ESP32-WROOM-32 DevKit V1 30 broches, sur 2 supports 1x15",
                      sch=(60, 35), pcb=(100, 111, 0), nets=a1_nets))
    # --- Site A2 : ESP32-S3-DevKitC-1 (un seul module A1 OU A2 peuplé) -------
    # Cartographie S3 universelle (pins.h PINMAP_UNIVERSAL, section BOARD_S3).
    a2_nets = {
        "1": "+3V3", "2": "+3V3", "3": "EN",
        "4": "ADC_C", "5": "ADC_D",                    # IO4/IO5
        "6": NET["LUMINOSITE"], "7": "ADC_VBAT",       # IO6/IO7
        "8": NET["POMPE_AQUA"], "9": NET["POMPE_RESERV"],   # IO15/IO16 (K1/K2)
        "10": NET["RADIATEURS"], "11": NET["LUMIERE"],      # IO17/IO18 (K3/K4)
        "12": NET["I2C_SDA"],                          # IO8
        "13": "GATE",                                  # IO3 (strapping JTAG-sel : OK)
        "14": None,                                    # IO46 (strapping, jamais câblé)
        "15": NET["I2C_SCL"],                          # IO9
        "16": "SD_CS_S3",                              # IO10 (-> JP_SD1)
        "17": None,                                    # IO11
        "18": "SD_MOSI_S3",                            # IO12 (-> JP_SD3)
        "19": "SD_CLK_S3",                             # IO13 (-> JP_SD2)
        "20": "SD_MISO_S3",                            # IO14 (-> JP11 1-2)
        "21": "VIN_5V", "22": "GND",
        "23": "GND", "24": "SPARE_TX0", "25": "SPARE_RX0",  # UART0 vers J17
        "26": "ADC_A", "27": "ADC_B",                  # IO1/IO2
        "28": NET["ONE_WIRE_BUS"],                     # IO42
        "29": NET["ULTRASON_POTA"],                    # IO41 (US3)
        "30": NET["ULTRASON_TANK"],                    # IO40 (US2)
        "31": NET["ULTRASON_AQUA"],                    # IO39 (US1)
        "32": NET["DHT_PIN"],                          # IO38 (LED RGB v1.1 : cosmétique)
        "33": None, "34": None, "35": None,            # IO37/36/35 (PSRAM octale)
        "36": None, "37": NET["AUX2"],                 # IO0 jamais ; IO45 (K6, base 1k+pulldown = sûr au boot)
        "38": NET["AUX1"],                             # IO48 (K5, LED RGB v1.0 : recopie d'état)
        "39": NET["SERVO_PETITS"], "40": NET["SERVO_GROS"],  # IO47/IO21
        "41": None, "42": None,                        # IO20/IO19 (USB)
        "43": "GND", "44": "GND",
    }
    comps.append(dict(ref="A2", sym="ESP32_S3_DEVKITC_44", value="ESP32-S3-DevKitC-1",
                      fp="ESP32_S3_DevKitC_1_44pin",
                      desc="Site optionnel ESP32-S3-DevKitC-1 44 broches, sur 2 supports 1x22 (un seul module A1 OU A2)",
                      sch=(152, 104), pcb=(257, 128, 90), nets=a2_nets))
    # --- Alimentation 5 V (rev 0.2 : anti-inversion P-MOSFET + TVS) ----------
    # J1 / J2 / J37 arrivent sur VIN_RAW ; Q12 (IRF4905, VGS = -5 V) ne laisse
    # passer qu'une polarité : un fil inversé par un élève ne détruit plus la
    # carte (audit NET-06, levé en 0.2). PS1 (Hi-Link) sort directement sur
    # +5V : sa polarité est fixée par l'empreinte.
    comps += [
        # Ouverture vers le bord gauche (x=40) : rotation 0° = montage
        # classique (lèvre ~6 mm hors carte). 270° pointait l'ouverture vers
        # J1 — enfichage bloqué (audit GEN-02).
        dict(ref="J2", sym="BARREL", value="Jack 5.5/2.1", fp="BarrelJack_Horizontal",
             desc="Entrée 5V (jack DC-005 ~2,5 A, centre = +, ouverture bord gauche)", sch=(24, 18), pcb=(48, 119, 0),
             nets={"1": "VIN_RAW", "2": "GND", "3": "GND"}),
        dict(ref="J1", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Entrée 5V alternative (bornier, 1=+5V 2=GND) — protégée contre l'inversion par Q12",
             sch=(24, 26), pcb=(46, 105, 270),
             nets={"1": "VIN_RAW", "2": "GND"}),
        dict(ref="Q12", sym="PMOS_GDS", value="IRF4905", fp="TO-220-3_Vertical_GDS",
             desc="Anti-inversion entrée 5V (P-MOSFET 55V, VGS = -5V ; 1=G 2=D=entrée 3=S=+5V)",
             sch=(34, 22), pcb=(58, 106, 0),
             nets={"1": "Q12_G", "2": "VIN_RAW", "3": "+5V"}),
        dict(ref="R57", sym="R", value="100k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Grille de Q12 vers GND", sch=(34, 28), pcb=(68, 108, 0),
             nets={"1": "Q12_G", "2": "GND"}),
        dict(ref="D12", sym="D", value="1.5KE6.8A", fp="D_DO-201AD_P15.24mm_Horizontal",
             desc="TVS 6,8V (Vrwm 5,8V) sur le rail +5V : surtension / inversion résiduelle",
             sch=(34, 34), pcb=(56, 114, 0),
             nets={"1": "+5V", "2": "GND"}),
        dict(ref="D5", sym="D", value="1N5822", fp="D_DO-201AD_P15.24mm_Horizontal",
             desc="Schottky 3A vers VIN DevKit (anti-retour si USB branché)",
             sch=(24, 34), pcb=(76, 114, 0),
             nets={"1": "VIN_5V", "2": "+5V"}),
        dict(ref="C1", sym="CP", value="1000u/16V", fp="CP_Radial_D10.0mm_P5.00mm",
             desc="Réservoir rail 5V (relais + servos + HC-SR04)",
             sch=(24, 40), pcb=(58, 124, 0),
             nets={"1": "+5V", "2": "GND"}),
        dict(ref="R13", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance LED présence 5V", sch=(24, 46), pcb=(70, 121, 0),
             nets={"1": "+5V", "2": "PWR_LED"}),
        dict(ref="LED5", sym="LED", value="verte", fp="LED_D5.0mm",
             desc="LED présence 5V", sch=(24, 52), pcb=(86, 123, 90),
             nets={"1": "GND", "2": "PWR_LED"}),
    ]
    # --- 6 canaux relais (mapping GPIO = gpio_mapping.h / pins.h) ------------
    comps += relay_channel(1, NET["POMPE_AQUA"], "J3", 58)
    comps += relay_channel(2, NET["POMPE_RESERV"], "J4", 92)
    # K3 = chauffage ffp5cs : jamais de ON forcé (court-circuiterait l'hystérésis
    # de HeaterOrchestrator) — décision A3-b (EVOLUTIONS_PROPOSEES.md).
    comps += relay_channel(3, NET["RADIATEURS"], "J5", 126, force_on=False)
    comps += relay_channel(4, NET["LUMIERE"], "J6", 160)
    comps += relay_channel(5, NET["AUX1"], "J23", 194,
                           refs=dict(rb="R28", rp="R29", rl="R30",
                                     q="Q5", d="D6", led="LED6"))
    comps += relay_channel(6, NET["AUX2"], "J24", 228,
                           refs=dict(rb="R31", rp="R32", rl="R33",
                                     q="Q6", d="D7", led="LED7"))
    # --- Retour d'état des relais (collecteurs 0/5 V) : header 1x8 ----------
    # Évolution A2 (EVOLUTIONS_PROPOSEES.md) : pas d'expandeur sur la carte, un
    # header expose l'état RÉEL de chaque relais (collecteur : ~0 V = relais
    # collé, 5 V = au repos), lisible par un PCF8574 déporté via un diviseur.
    comps.append(dict(
        ref="J38", sym="CONN_08", value="Header CMD SENSE",
        fp="PinHeader_1x08_P2.54mm_Vertical",
        desc="Etat réel des relais K1..K6 (1-6 = collecteurs 0V=ON/5V=OFF, 7=GND, 8=+5V)",
        sch=(188, 4), pcb=(133, 154, 90),
        nets={"1": "REL1_SW", "2": "REL2_SW", "3": "REL3_SW", "4": "REL4_SW",
              "5": "REL5_SW", "6": "REL6_SW", "7": "GND", "8": "+5V"}))
    # --- Servomoteurs nourrisseurs -------------------------------------------
    comps += [
        dict(ref="R20", sym="R", value="220",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Série signal servo gros", sch=(48, 55), pcb=(282, 140, 0),
             nets={"1": NET["SERVO_GROS"], "2": "SERVO_GROS_SIG"}),
        dict(ref="J15", sym="CONN_03", value="Header servo",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Servo GROS (1=SIG 2=+5V 3=GND)", sch=(60, 55), pcb=(298, 138, 0),
             nets={"1": "SERVO_GROS_SIG", "2": "+5V", "3": "GND"}),
        dict(ref="R21", sym="R", value="220",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Série signal servo petits", sch=(48, 65), pcb=(282, 145, 0),
             nets={"1": NET["SERVO_PETITS"], "2": "SERVO_PETITS_SIG"}),
        dict(ref="J16", sym="CONN_03", value="Header servo",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Servo PETITS (1=SIG 2=+5V 3=GND)", sch=(60, 65), pcb=(304, 138, 0),
             nets={"1": "SERVO_PETITS_SIG", "2": "+5V", "3": "GND"}),
        dict(ref="C2", sym="CP", value="470u/16V", fp="CP_Radial_D8.0mm_P3.50mm",
             desc="Découplage rail 5V servos", sch=(48, 71), pcb=(296, 152, 0),
             nets={"1": "+5V", "2": "GND"}),
    ]
    # --- Capteurs ultrason HC-SR04 (mono-broche trig/écho) --------------------
    comps += us_channel(1, "AQUA", NET["ULTRASON_AQUA"], "R14", "R17", "J7", 125, "JP13")
    comps += us_channel(2, "TANK", NET["ULTRASON_TANK"], "R15", "R18", "J8", 146.5, "JP14")
    comps += us_channel(3, "POTA", NET["ULTRASON_POTA"], "R16", "R19", "J9", 168, "JP15")
    # --- DHT11 / DS18B20 / LDR ------------------------------------------------
    comps += [
        dict(ref="R23", sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up data DHT11", sch=(92, 52), pcb=(224, 106, 0),
             nets={"1": "+3V3_SW", "2": NET["DHT_PIN"]}),
        dict(ref="J10", sym="CONN_03", value="Bornier_5.08",
             fp="TerminalBlock_bornier-3_P5.08mm",
             desc="DHT11/DHT22 (1=3V3 2=DATA 3=GND) — fils nus + embouts", sch=(104, 52), pcb=(86, 166, 0),
             nets={"1": "+3V3_SW", "2": NET["DHT_PIN"], "3": "GND"}),
        dict(ref="C3", sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
             desc="Découplage 3V3 capteurs", sch=(92, 56), pcb=(238, 106, 0),
             nets={"1": "+3V3_SW", "2": "GND"}),
        dict(ref="R24", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up bus 1-Wire DS18B20", sch=(92, 64), pcb=(56, 146, 0),
             nets={"1": "+3V3_SW", "2": NET["ONE_WIRE_BUS"]}),
        dict(ref="J11", sym="CONN_03", value="Bornier_5.08",
             fp="TerminalBlock_bornier-3_P5.08mm",
             desc="Sonde DS18B20 étanche (1=3V3 2=DATA 3=GND)",
             sch=(104, 64), pcb=(46, 154, 270),
             nets={"1": "+3V3_SW", "2": NET["ONE_WIRE_BUS"], "3": "GND"}),
        # Bas de pont LDR TOUJOURS posé ; en circuit par le cavalier JP20 (LDR
        # n3pp/ffp5cs) ; ouvert pour un module à sortie AO (msp HumiditeSol :
        # les 10k // sortie du module divisaient la mesure par ~2).
        dict(ref="R27", sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Bas de pont LDR sur ADC_E (GPIO36 WROOM / IO6 S3) — en circuit si JP20 FERMÉ",
             sch=(92, 74), pcb=(217, 139, 0),
             nets={"1": "ADC_E_IN", "2": "ADC_E_DIV"}),
        dict(ref="JP20", sym="CONN_02", value="Jumper profil",
             fp="PinHeader_1x02_P2.54mm_Vertical",
             desc="Profil : FERMÉ = LDR sur J12 (n3pp, ffp5cs) ; OUVERT = module AO (msp HumiditeSol)",
             sch=(98, 78), pcb=(232, 139, 90),
             nets={"1": "ADC_E_DIV", "2": "GND"}),
        dict(ref="J12", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="LDR déportée entre 3V3 et l'ADC (1=3V3 2=ADC)",
             sch=(104, 74), pcb=(46, 143, 270),
             nets={"1": "+3V3_SW", "2": "ADC_E_IN"}),
    ]
    # --- I2C : OLED SSD1306 + extension (DS3231…) -----------------------------
    comps += [
        dict(ref="R25", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up I2C SDA", sch=(92, 84), pcb=(196, 106, 0),
             nets={"1": "+3V3_SW", "2": NET["I2C_SDA"]}),
        dict(ref="R26", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up I2C SCL", sch=(92, 88), pcb=(210, 106, 0),
             nets={"1": "+3V3_SW", "2": NET["I2C_SCL"]}),
        dict(ref="J13", sym="CONN_04", value="Support OLED",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="OLED SSD1306 128x64 I2C 0x3C (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 84), pcb=(60, 133, 0),
             nets={"1": "GND", "2": "+3V3_SW", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
        dict(ref="J14", sym="CONN_04", value="Support I2C ext",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="Extension I2C — ex. module DS3231 (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 92), pcb=(68, 133, 0),
             nets={"1": "GND", "2": "+3V3_SW", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
        dict(ref="C4", sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
             desc="Découplage 3V3 I2C", sch=(92, 96), pcb=(84, 133, 0),
             nets={"1": "+3V3_SW", "2": "GND"}),
        dict(ref="J21", sym="CONN_04", value="Support I2C libre",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="Port I2C libre 2 (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 100), pcb=(43, 80, 0),
             nets={"1": "GND", "2": "+3V3_SW", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
        dict(ref="J22", sym="CONN_04", value="Support I2C libre",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="Port I2C libre 3 (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 108), pcb=(47, 90, 0),
             nets={"1": "GND", "2": "+3V3_SW", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
    ]
    # --- GPIO libres + EN -----------------------------------------------------
    comps.append(dict(
        ref="J17", sym="CONN_06", value="Header service",
        fp="PinHeader_1x06_P2.54mm_Vertical",
        desc="Header service (1=3V3 2=GND 3=EN 4=RX0 5=TX0 6=+5V) — tous les autres "
             "GPIO sont consommés par la carte universelle",
        sch=(24, 70), pcb=(312, 133, 0),
        nets={"1": "+3V3", "2": "GND", "3": "EN", "4": "SPARE_RX0",
              "5": "SPARE_TX0", "6": "+5V"}))
    # Distribution d'alimentation supplémentaire : borniers ET header
    comps += [
        dict(ref="J18", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Distribution 5V (1=+5V 2=GND)", sch=(24, 84), pcb=(46, 131, 270),
             nets={"1": "+5V", "2": "GND"}),
        dict(ref="J19", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Distribution 3V3 capteurs — rail COMMUTE (1=+3V3_SW 2=GND)", sch=(24, 90), pcb=(302, 166, 0),
             nets={"1": "+3V3_SW", "2": "GND"}),
        dict(ref="J20", sym="CONN_06", value="Header alim",
             fp="PinHeader_1x06_P2.54mm_Vertical",
             desc="Rail Dupont (1-2=+5V 3-4=GND 5-6=+3V3_SW)", sch=(24, 98), pcb=(312, 150, 0),
             nets={"1": "+5V", "2": "+5V", "3": "GND", "4": "GND",
                   "5": "+3V3_SW", "6": "+3V3_SW"}),
    ]
    # --- Power-gate rail capteurs +3V3_SW (rev 0.2 : LDO dédié) ---------------
    # Le rail capteurs n'est plus tiré sur l'AMS1117 du DevKit : Q7 (IRF4905,
    # VGS = -5 V, dispo au Maroc — plus de P-FET logic-level introuvable) commute
    # le 5 V vers un LD1117V33 dédié. R35 tire la grille à +5V : rail OFF par
    # défaut ; Q8 (commandé par GATE) la met à GND. JP1 ponte Q7 (rail permanent,
    # ffp5cs). R48 : pull-down de la base de Q8 (audit NET-02, symétrie relais).
    comps += [
        dict(ref="R34", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Base commande gate (GPIO13)", sch=(146, 8), pcb=(224, 111, 0),
             nets={"1": "GATE", "2": "GATE_B"}),
        dict(ref="R48", sym="R", silk=dict(ref=(5.08, 0, 0), value=(5.08, -2.35, 0)), value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-down base Q8 (rail OFF tant que le GPIO flotte)", sch=(146, 12), pcb=(210, 116, 0),
             nets={"1": "GATE_B", "2": "GND"}),
        dict(ref="R35", sym="R", value="100k",
             silk=dict(ref=(5.08, 0, 0), value=(-2.6, 0, 0)),  # valeur à gauche : R47 au-dessus, Q7 dessous
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up grille P-MOSFET vers +5V (rail OFF par défaut)", sch=(146, 16), pcb=(196, 116, 0),
             nets={"1": "+5V", "2": "GATE_G"}),
        dict(ref="Q8", sym="NPN", value="BC337-40", fp="TO-92_Inline_Wide_CBE",
             desc="Driver gate (1=C 2=B 3=E)", sch=(155, 12), pcb=(238, 116, 0),
             nets={"1": "GATE_G", "2": "GATE_B", "3": "GND"}),
        dict(ref="Q7", sym="PMOS_GDS", value="IRF4905", fp="TO-220-3_Vertical_GDS",
             silk=dict(ref=(0.0, 4.4, 0)),  # R35 juste au-dessus, JP1 à droite
             desc="P-MOSFET commutation 5V du LDO capteurs (1=G 2=D=+5V 3=S=entrée LDO)",
             sch=(155, 18), pcb=(196, 122, 0),
             nets={"1": "GATE_G", "2": "+5V", "3": "LDO_IN"}),
        dict(ref="JP1", sym="CONN_02", value="Jumper BYPASS",
             fp="PinHeader_1x03_P2.54mm_Vertical", silk=dict(ref=(-2.4, 2.54, 90)),  # valeur de U1 à droite
             desc="BYPASS gate : cavalier 1-2 FERME par défaut (rail permanent, ffp5cs) ; "
                  "l'OTER pour les profils batterie (msp/n3pp, rail commuté par GPIO13)",
             sch=(146, 22), pcb=(208, 120, 0),
             nets={"1": "+5V", "2": "LDO_IN"}),
        dict(ref="U1", sym="LDO", value="LD1117V33", fp="TO-220-3_Vertical_LDO",
             desc="Régulateur 3,3V 800mA du rail capteurs (1=GND 2=OUT 3=IN — PAS un 78xx)",
             sch=(166, 18), pcb=(214, 122, 0),
             nets={"3": "LDO_IN", "2": "+3V3_SW", "1": "GND"}),
        dict(ref="C11", sym="CP", value="10u/25V", fp="CP_Radial_D5.0mm_P2.50mm",
             desc="Entrée LDO", sch=(166, 26), pcb=(226, 122, 0),
             nets={"1": "LDO_IN", "2": "GND"}),
        dict(ref="C12", sym="CP", value="100u/16V", fp="CP_Radial_D6.3mm_P2.50mm",
             desc="Sortie LDO (stabilité LD1117 : >= 10 µF, électrolytique)", sch=(174, 26), pcb=(235, 122, 0),
             nets={"1": "+3V3_SW", "2": "GND"}),
    ]
    # --- Pont diviseur batterie PERMANENT (rev 0.2) ----------------------------
    # La coupure haute (BS250 + driver, rev 0.1) est supprimée : 21 µA en 1S,
    # négligeable devant l'AMS1117 du DevKit, et le BS250 était marginal sous
    # 3,5 V (audit NET-04). Ratio par cavalier JP12 : 1-2 = 22k (bus 12V),
    # 2-3 = 100k (1S Li-ion). R49/C13 : filtre ADC ; D11 : clamp sur +3V3.
    comps += [
        dict(ref="J25", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Sonde batterie (1=VBAT+ 2=GND) — 1S ou bus 12V selon profil",
             sch=(146, 30), pcb=(290.5, 166, 0),
             nets={"1": "VBAT_SENSE", "2": "GND"}),
        dict(ref="R38", sym="R", value="100k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Haut du pont VBAT (commun aux deux profils)", sch=(146, 36), pcb=(132, 116, 0),
             nets={"1": "VBAT_SENSE", "2": "VBAT_DIV"}),
        dict(ref="R39", sym="R", value="22k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Bas du pont VBAT profil bus 12V (JP12 en 1-2) : 100k/22k", sch=(146, 40), pcb=(153, 116, 0),
             nets={"1": "VBAT_22K", "2": "GND"}),
        dict(ref="R58", sym="R", value="100k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Bas du pont VBAT profil 1S (JP12 en 2-3) : 100k/100k", sch=(146, 44), pcb=(174, 116, 0),
             nets={"1": "VBAT_100K", "2": "GND"}),
        dict(ref="JP12", sym="CONN_03", value="Jumper profil", silk=dict(ref=(0, 7.9, 90)),  # tourné 90° : repère vertical à droite (A1 à gauche, R38 dessus, 12V/1S dessous)
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Ratio VBAT : 1-2 = 22k (bus 12V, ffp5cs) ; 2-3 = 100k (1S, msp/n3pp)",
             sch=(155, 38), pcb=(133, 122, 90),
             nets={"1": "VBAT_22K", "2": "VBAT_DIV", "3": "VBAT_100K"}),
        dict(ref="R49", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Série ADC_VBAT", sch=(146, 48), pcb=(147, 122, 0),
             nets={"1": "VBAT_DIV", "2": "ADC_VBAT"}),
        dict(ref="C13", sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
             desc="Filtre ADC_VBAT", sch=(155, 48), pcb=(161, 122, 0),
             nets={"1": "ADC_VBAT", "2": "GND"}),
        dict(ref="D11", sym="D", value="BAT85", fp="D_DO-35_SOD-123_Dual_P7.62mm",
             desc="Clamp ADC_VBAT sur +3V3 (panneau branché par erreur sur J25) — "
                  "empreinte double : BAT85/BAT42/BAT43 DO-35 OU BAT43W/BAT54 SOD-123 "
                  "(LCSC C19167), un seul des deux posé",
             sch=(164, 48), pcb=(171, 122, 0),
             nets={"1": "+3V3", "2": "ADC_VBAT"}),
    ]
    # --- Profil bus 12V : protection + buck externe (fusible lame EN AMONT) ---
    comps += [
        dict(ref="J26", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Entrée bus 12V (1=+12V APRES fusible lame externe 7,5-10A 2=GND)",
             sch=(146, 54), pcb=(256, 166, 0),
             nets={"1": "VBAT12_IN", "2": "GND"}),
        # Brochage TO-220 = 1=G 2=D(+tab) 3=S. R40 tire la grille à GND :
        # VGS = -VBAT ; IRF4905 (±20 V / 55 V) + zener D9 grille-source (rev 0.1.2).
        dict(ref="Q11", sym="PMOS_GDS", value="IRF4905", fp="TO-220-3_Vertical_GDS",
             silk=dict(ref=(2.54, 4.4, 0)),  # D8 juste au-dessus
             desc="Anti-inversion P-MOSFET 55V ±20V VGS (1=G 2=D=entrée 3=S=sortie)",
             sch=(155, 56), pcb=(248, 142, 0),
             nets={"1": "QP_G", "2": "VBAT12_IN", "3": "VBAT12_PROT"}),
        dict(ref="R40", sym="R", value="100k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Grille anti-inversion vers GND (limite le courant de D9)", sch=(146, 58), pcb=(268, 134, 0),
             nets={"1": "QP_G", "2": "GND"}),
        dict(ref="D9", sym="D", value="1N4744A",
             fp="D_DO-41_SOD81_P5.08mm_Vertical_AnodeUp",
             desc="Zener 15V grille-source de Q11 (K=source, A=grille) : VGS borné à -15 V",
             sch=(172, 60), pcb=(260, 142, 0),
             nets={"1": "VBAT12_PROT", "2": "QP_G"}),
        dict(ref="D8", sym="D", value="1.5KE18A", fp="D_DO-201AD_P15.24mm_Horizontal",
             desc="TVS 18V transitoires bus batterie (DO-201 : le P6KE est en DO-15, trop fin)", sch=(146, 62), pcb=(248, 134, 0),
             nets={"1": "VBAT12_PROT", "2": "GND"}),
        dict(ref="C5", sym="CP", value="470u/25V", fp="CP_Radial_D10.0mm_P5.00mm",
             desc="Réservoir bus 12V protégé", sch=(146, 66), pcb=(272, 143, 0),
             nets={"1": "VBAT12_PROT", "2": "GND"}),
        dict(ref="J36", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Vers buck IN (module MP1584/XL4015 sur entretoises : 1=+12V 2=GND)",
             sch=(146, 70), pcb=(267.5, 166, 0),
             nets={"1": "VBAT12_PROT", "2": "GND"}),
        dict(ref="J37", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Depuis buck OUT 5V (1=+5V 2=GND) — protégé contre l'inversion par Q12", sch=(146, 74), pcb=(279, 166, 0),
             nets={"1": "VIN_RAW", "2": "GND"}),
    ]
    # --- Profil secteur : Hi-Link 20M05 embarqué (fusible + varistance carte) --
    comps += [
        # 1=N / 2=L (le pad 2 est côté F1 : la phase part directement au fusible,
        # sans passer près du pad neutre — géométrie revue à l'audit final rev 0.1).
        dict(ref="J27", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="ENTREE SECTEUR 230V (1=N 2=L) — ZONE DANGER", sch=(146, 78), pcb=(250, 46, 0),
             nets={"1": "MAINS_N", "2": "MAINS_L"}),
        # Empreinte universelle : fentes au pas 22,5-22,6 + ergot (Schurter OG,
        # Stelvio PTF78, Multicomp MC000830 à capot, Würth 6961070) et paire
        # Pad 1 à (268.5,44) et pad 2 à (268.5,66.55) : F1 décalé de 2,5 mm vers
        # la droite par rapport à la 0.1.2 pour loger une varistance 14 mm.
        dict(ref="F1", sym="FUSE", value="T1A 5x20",
             fp="Fuse_5x20_Universal",
             desc="Fusible entrée secteur du module alim (temporisé 1A) — porte-fusible à capot",
             sch=(155, 78), pcb=(268.5, 44, 270),
             nets={"1": "MAINS_L", "2": "MAINS_LF"}),
        # Disque 10 ou 14 mm (14D471K = seule référence locale), pas 7,5 mm,
        # perçage 1,0 mm pour les pattes 0,8 mm.
        dict(ref="RV1", sym="VARISTOR", value="14D471K",
             fp="RV_Disc_D15.5mm_W8mm_P7.5mm",
             desc="Varistance 300VAC transitoires secteur (disque 10 ou 14 mm, pas 7,5 mm)",
             sch=(155, 82), pcb=(252, 60, 0),
             nets={"1": "MAINS_N", "2": "MAINS_LF"}),
        dict(ref="PS1", sym="HLK20M", value="HLK-20M05",
             fp="Converter_ACDC_Hi-Link_HLK-20Mxx",
             desc="Module AC-DC 230V->5V 3,6A (Hi-Link 20M05) — le corps du module "
                  "enjambe la frontière secteur/logique (fente fraisée dessous)",
             sch=(146, 86), pcb=(303, 46, 270),
             nets={"1": "MAINS_LF", "2": "MAINS_N", "3": "GND", "4": "+5V"}),
    ]
    # --- microSD : support module SPI 3V3 + sélection de source par cavaliers --
    # Rev 0.2 : MISO passe aussi par un cavalier (JP11). Câblé en direct sur
    # GPIO12/MTDI (strap tension flash), une carte SD insérée pouvait empêcher
    # une unité WROOM de démarrer ; en 1-2 (S3) le site A1 n'y est plus relié.
    comps += [
        dict(ref="J35", sym="CONN_06S", value="Support module microSD",
             fp="PinSocket_1x06_P2.54mm_Vertical",
             desc="Module microSD SPI 3V3 (1=GND 2=VCC 3=MISO 4=MOSI 5=SCK 6=CS)",
             sch=(146, 94), pcb=(240, 130, 0),
             nets={"1": "GND", "2": "+3V3_SW", "3": "SD_MISO",
                   "4": "SD_MOSI", "5": "SD_CLK", "6": "SD_CS"}),
        dict(ref="JP2", sym="CONN_03", value="Jumper SD CS",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Source CS : 1-2 = S3 (IO10, défaut) ; 2-3 = WROOM (US3, env wroom-sd)",
             sch=(155, 92), pcb=(194, 130, 0),
             nets={"1": "SD_CS_S3", "2": "SD_CS", "3": NET["ULTRASON_POTA"]}),
        dict(ref="JP3", sym="CONN_03", value="Jumper SD SCK",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Source SCK : 1-2 = S3 (IO13, défaut) ; 2-3 = WROOM (K5, env wroom-sd)",
             sch=(155, 96), pcb=(199, 130, 0),
             nets={"1": "SD_CLK_S3", "2": "SD_CLK", "3": NET["AUX1"]}),
        dict(ref="JP4", sym="CONN_03", value="Jumper SD MOSI",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Source MOSI : 1-2 = S3 (IO12, défaut) ; 2-3 = WROOM (K6, env wroom-sd)",
             sch=(155, 100), pcb=(204, 130, 0),
             nets={"1": "SD_MOSI_S3", "2": "SD_MOSI", "3": NET["AUX2"]}),
        dict(ref="JP11", sym="CONN_03", value="Jumper SD MISO",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Source MISO : 1-2 = S3 (IO14, défaut) ; 2-3 = WROOM (GPIO12/MTDI : "
                  "efuse VDD_SDIO=3V3 obligatoire avant)",
             sch=(155, 104), pcb=(209, 130, 0),
             nets={"1": "SD_MISO_S3", "2": "SD_MISO", "3": "SD_MISO_W"}),
    ]
    # --- Points de test ------------------------------------------------------
    for i, (net, x) in enumerate([("GND", 224), ("+5V", 228), ("+3V3_SW", 232), ("+3V3", 236)], 1):
        comps.append(dict(ref=f"TP{i}", sym="TP", value=net,
                          fp="TestPoint_THTPad_D2.0mm_Drill1.0mm",
                          desc=f"Point de test {net} (boucle de fil ou picot)",
                          sch=(188, 12 + 4 * i), pcb=(x, 131, 0), nets={"1": net}))
    # --- Bloc analogique partagé : 4 entrées LDR (msp) / sondes sol (n3pp) ----
    # Rev 0.2 : bas de pont toujours posé + cavalier (JP16-19), et filtre série
    # 1k / 100 nF sur chaque entrée ADC (fils longs vers l'extérieur).
    for i, (jref, rref, net, x, jp, rs, cf) in enumerate([
            ("J31", "R43", "ADC_A", 190, "JP16", "R50", "C6"),
            ("J32", "R44", "ADC_B", 206.5, "JP17", "R51", "C7"),
            ("J33", "R45", "ADC_C", 223, "JP18", "R52", "C8"),
            ("J34", "R46", "ADC_D", 239.5, "JP19", "R53", "C9")]):
        cx = 132 + 21 * i
        comps += [
            dict(ref=jref, sym="CONN_03", value="Bornier_5.08",
                 fp="TerminalBlock_bornier-3_P5.08mm",
                 desc=f"Entrée analogique {net} (1=3V3_SW 2=SIG 3=GND) — LDR msp / sonde sol n3pp",
                 sch=(156, 124 + 4 * i), pcb=(x, 166, 0),
                 nets={"1": "+3V3_SW", "2": f"{net}_IN", "3": "GND"}),
            dict(ref=rref, sym="R", value="10k",
                 fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
                 desc=f"Bas de pont {net} (en circuit si {jp} FERMÉ : LDR msp ; OUVERT : sonde sol n3pp)",
                 sch=(166, 124 + 4 * i), pcb=(cx, 139, 0),
                 nets={"1": f"{net}_IN", "2": f"{net}_DIV"}),
            dict(ref=jp, sym="CONN_02", value="Jumper profil",
                 fp="PinHeader_1x02_P2.54mm_Vertical",
                 desc=f"Profil : FERMÉ = LDR tracker msp sur {jref} ; OUVERT = sonde sol n3pp",
                 sch=(176, 124 + 4 * i), pcb=(cx + 15, 139, 90),
                 nets={"1": f"{net}_DIV", "2": "GND"}),
            dict(ref=rs, sym="R", value="1k",
                 fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
                 desc=f"Série {net} (protection GPIO, fils longs)",
                 sch=(186, 124 + 4 * i), pcb=(cx, 144, 0),
                 nets={"1": f"{net}_IN", "2": net}),
            dict(ref=cf, sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
                 desc=f"Filtre ADC {net}", sch=(196, 124 + 4 * i), pcb=(cx, 149, 0),
                 nets={"1": net, "2": "GND"}),
        ]
    comps += [
        dict(ref="R54", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Série ADC_E (protection GPIO, fils longs)", sch=(186, 140), pcb=(217, 144, 0),
             nets={"1": "ADC_E_IN", "2": NET["LUMINOSITE"]}),
        dict(ref="C10", sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
             desc="Filtre ADC_E", sch=(196, 140), pcb=(217, 149, 0),
             nets={"1": NET["LUMINOSITE"], "2": "GND"}),
    ]
    # --- Pluie (msp, net US1) et DHT externe (msp, net US2) --------------------
    comps += [
        dict(ref="J29", sym="CONN_03", value="Bornier_5.08",
             fp="TerminalBlock_bornier-3_P5.08mm",
             desc="Module pluie DO msp (1=3V3_SW 2=DO 3=GND) — net US1, rôles disjoints",
             sch=(126, 118), pcb=(53, 166, 0),
             nets={"1": "+3V3_SW", "2": NET["ULTRASON_AQUA"], "3": "GND"}),
        dict(ref="J30", sym="CONN_03", value="Bornier_5.08",
             fp="TerminalBlock_bornier-3_P5.08mm",
             desc="DHT externe msp (1=3V3_SW 2=DATA 3=GND) — net US2, rôles disjoints",
             sch=(126, 124), pcb=(69.5, 166, 0),
             nets={"1": "+3V3_SW", "2": NET["ULTRASON_TANK"], "3": "GND"}),
        dict(ref="R47", sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up DHT ext (en circuit si JP21 FERMÉ : unités msp)", sch=(126, 130), pcb=(196, 111, 0),
             nets={"1": "+3V3_SW", "2": "DHT_EXT_PU"}),
        dict(ref="JP21", sym="CONN_02", value="Jumper profil",
             fp="PinHeader_1x02_P2.54mm_Vertical",
             desc="Profil : FERMÉ = pull-up DHT externe sur J30 (msp) ; OUVERT = HC-SR04 sur J8 (ffp5cs)",
             sch=(132, 134), pcb=(211, 111, 90),
             nets={"1": "DHT_EXT_PU", "2": NET["ULTRASON_TANK"]}),
    ]
    # --- 4e port I2C (3 INA226 + DS3231 : J14/J21/J22/J28) --------------------
    comps.append(dict(ref="J28", sym="CONN_04", value="Support I2C libre",
                      fp="PinSocket_1x04_P2.54mm_Vertical",
                      desc="Port I2C libre 4 — INA226 (1=GND 2=VCC 3=SCL 4=SDA)",
                      sch=(156, 142), pcb=(76, 133, 0),
                      nets={"1": "GND", "2": "+3V3_SW", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}))
    # --- Trous de fixation M3 --------------------------------------------------
    # H1 (coin relais) : enclavé par le corps de K1, une tête de vis métal serait
    # à ~4,3 mm des contacts 230 V — vis NYLON obligatoire, plan GND écarté sous
    # la tête (keepout), marquage sérigraphié « H1=NYLON » (audit rev 0.1).
    # H5 (coin PSU, entre N et L) : point d'appui du coin secteur (GEN-05).
    # Rev 0.2 : 7 trous (carte 278 x 135 mm) — H6/H7 au milieu de la zone logique.
    nylon = {1: "tête métal à <5 mm du 230V", 5: "entre les pistes N et L du coin secteur"}
    holes = [(45, 45), (275, 97), (45, 171), (314, 171), (276.8, 59),
             (165, 153), (235, 153)]
    for i, (hx, hy) in enumerate(holes, 1):
        desc = (f"Trou de fixation M3 — H{i} : VIS NYLON OBLIGATOIRE ({nylon[i]})"
                if i in nylon else "Trou de fixation M3")
        comps.append(dict(ref=f"H{i}", sym=None, value="M3",
                          fp="MountingHole_3.2mm_M3", desc=desc,
                          sch=None, pcb=(hx, hy, 0), nets={}))
    return comps


COMPONENTS = build_components()


# ---------------------------------------------------------------------------
# RÔLE de chaque composant, FIRMWARE PAR FIRMWARE — « à quoi sert ce composant
# sur MA carte ? ». Alimente le champ Description des symboles (eeschema) ET
# des empreintes (pcbnew) : un clic dans KiCad doit répondre à la question,
# sans ouvrir un CSV à côté.
#
# Sources de vérité (ne pas inventer ici — recopier) :
#   - ffp5cs : ../COMPOSANTS_FFP5CS.csv, colonne « Rôle ffp5cs »
#   - msp    : msp/include/msp_config.h, bloc PINMAP_UNIVERSAL
#   - n3pp   : n3pp/include/n3pp_config.h, bloc PINMAP_UNIVERSAL
# `None` = le firmware n'utilise pas ce composant (à ne pas peupler, ou net
# porté par un autre connecteur — les « rôles disjoints » de la carte).
# ---------------------------------------------------------------------------

ROLES = {
    # --- Sites MCU --------------------------------------------------------
    "A1": dict(ffp5cs="cerveau de l'unité (site WROOM)",
               msp="cerveau de l'unité (site WROOM)",
               n3pp="cerveau de l'unité (site WROOM)"),
    "A2": dict(ffp5cs="cerveau S3 — SD native, K5/K6 embarqués",
               msp="cerveau S3", n3pp="cerveau S3"),
    # --- Canaux relais (nets K1..K4 / AUX1 / AUX2) ------------------------
    "K1": dict(ffp5cs="pompe aquarium (support de vie)",
               msp=None, n3pp="pompe d'arrosage (POMPE)"),
    "J3": dict(ffp5cs="charge pompe aquarium",
               msp=None, n3pp="charge pompe d'arrosage"),
    "K2": dict(ffp5cs="pompe réservoir (remplissage aquarium)",
               msp=None, n3pp=None),
    "J4": dict(ffp5cs="charge pompe réservoir", msp=None, n3pp=None),
    "K3": dict(ffp5cs="chauffage / radiateurs", msp=None, n3pp=None),
    "J5": dict(ffp5cs="charge chauffage", msp=None, n3pp=None),
    "K4": dict(ffp5cs="lumière / UV", msp=None, n3pp=None),
    "J6": dict(ffp5cs="charge lumière / UV", msp=None, n3pp=None),
    "K5": dict(ffp5cs="relais auxiliaire AUX1 (net SD_CLK en env wroom-sd)",
               msp=None, n3pp=None),
    "J23": dict(ffp5cs="charge AUX1", msp=None, n3pp=None),
    "K6": dict(ffp5cs="relais auxiliaire AUX2 (net SD_MOSI en env wroom-sd)",
               msp=None, n3pp=None),
    "J24": dict(ffp5cs="charge AUX2", msp=None, n3pp=None),
    # --- Servos (nets SERVO1 / SERVO2) ------------------------------------
    "J15": dict(ffp5cs="servo distributeur GROS poissons",
                msp="servo du tracker solaire, axe GD (LDR ADC_A/ADC_B)",
                n3pp=None),
    "J16": dict(ffp5cs="servo distributeur PETITS poissons",
                msp="servo du tracker solaire, axe HB (LDR ADC_C/ADC_D)",
                n3pp=None),
    # --- Capteurs sur nets partagés (rôles DISJOINTS) ---------------------
    "J7": dict(ffp5cs="HC-SR04 niveau aquarium (ULTRASON_AQUA)",
               msp="net US1 = PLUIE côté msp — poser J29, pas J7", n3pp=None),
    "J29": dict(ffp5cs="même net que J7 — ne pas poser",
                msp="module pluie sortie DO (PLUIE)", n3pp=None),
    "J8": dict(ffp5cs="HC-SR04 niveau réservoir (ULTRASON_TANK)",
               msp="net US2 = DHT externe côté msp — poser J30, pas J8",
               n3pp=None),
    "J30": dict(ffp5cs="même net que J8 — ne pas poser",
                msp="DHT externe (DHTPINEXT, legacy : préférer BME280 0x77)",
                n3pp=None),
    "J9": dict(ffp5cs="HC-SR04 niveau potager (ULTRASON_POTA)",
               msp=None, n3pp=None),
    "J10": dict(ffp5cs="DHT11 air : température + humidité (DHT_PIN)",
                msp="DHT11 interne (DHTPININT)", n3pp="DHT11 (DHTPIN)"),
    "J11": dict(ffp5cs="DS18B20 : température de l'eau (seuil chauffage)",
                msp="DS18B20 : température du sol", n3pp=None),
    "J12": dict(ffp5cs="LDR déportée : luminosité (net ADC_E)",
                msp="humidité du sol, module à sortie AO (HumiditeSol) — "
                    "R27 à NE PAS poser",
                n3pp="LDR luminosité (LUMINOSITE)"),
    "J31": dict(ffp5cs=None, msp="LDR tracker LUMINOSITEa (axe GD)",
                n3pp="sonde humidité du sol 1 (humidite1)"),
    "J32": dict(ffp5cs=None, msp="LDR tracker LUMINOSITEb (axe GD)",
                n3pp="sonde humidité du sol 2 (humidite2)"),
    "J33": dict(ffp5cs=None, msp="LDR tracker LUMINOSITEc (axe HB)",
                n3pp="sonde humidité du sol 3 (humidite3)"),
    "J34": dict(ffp5cs=None, msp="LDR tracker LUMINOSITEd (axe HB)",
                n3pp="sonde humidité du sol 4 (humidite4)"),
    # --- I2C ---------------------------------------------------------------
    "J13": dict(ffp5cs="OLED SSD1306 0x3C : état et mesures",
                msp="OLED SSD1306 0x3C", n3pp="OLED SSD1306 0x3C"),
    "J14": dict(ffp5cs="DS3231 : horloge hors ligne (resynchro après veille)",
                msp="extension I2C (BME280, DS3231...)",
                n3pp="extension I2C (BME280, DS3231...)"),
    "J21": dict(ffp5cs="INA226 : courant panneau / batterie / charge",
                msp="port I2C libre", n3pp="port I2C libre"),
    "J22": dict(ffp5cs="2e INA226 ou BME280",
                msp="port I2C libre", n3pp="port I2C libre"),
    "J28": dict(ffp5cs="3e INA226 (charges, shunt ext. 5-10 mΩ)",
                msp="port I2C libre", n3pp="port I2C libre"),
    # --- Stockage (ffp5cs seul) -------------------------------------------
    "J35": dict(ffp5cs="module microSD SPI 3,3 V : journal / logs",
                msp=None, n3pp=None),
    "JP2": dict(ffp5cs="source SD_CS : 1-2 = S3 (IO10) / 2-3 = WROOM (US3)",
                msp=None, n3pp=None),
    "JP3": dict(ffp5cs="source SD_CLK : 1-2 = S3 (IO13) / 2-3 = WROOM (K5)",
                msp=None, n3pp=None),
    "JP4": dict(ffp5cs="source SD_MOSI : 1-2 = S3 (IO12) / 2-3 = WROOM (K6)",
                msp=None, n3pp=None),
    # --- Rail capteurs commuté (net GATE) ---------------------------------
    "JP1": dict(ffp5cs="cavalier 1-2 FERME : rail +3V3_SW permanent",
                msp="cavalier OTE : rail coupé en veille par RELAIS (GPIO13)",
                n3pp="cavalier OTE : rail coupé en veille par RELAIS (GPIO13)"),
    # --- Batterie ----------------------------------------------------------
    "J25": dict(ffp5cs="sonde tension du bus 12 V (pont 100k/22k, JP12 en 1-2)",
                msp="sonde tension batterie 1S (pontdiv, pont 100k/100k, JP12 en 2-3)",
                n3pp="sonde tension batterie 1S (pontdiv, pont 100k/100k, JP12 en 2-3)"),
    "JP12": dict(ffp5cs="1-2 : bas de pont 22k (bus 12 V)",
                 msp="2-3 : bas de pont 100k (1S)", n3pp="2-3 : bas de pont 100k (1S)"),
    # --- Cavaliers de profil (rev 0.2) ------------------------------------
    "JP13": dict(ffp5cs="FERMÉ : pont écho HC-SR04 AQUA", msp="OUVERT (PLUIE sur US1)", n3pp="OUVERT"),
    "JP14": dict(ffp5cs="FERMÉ : pont écho HC-SR04 TANK", msp="OUVERT (DHT ext sur US2)", n3pp="OUVERT"),
    "JP15": dict(ffp5cs="FERMÉ : pont écho HC-SR04 POTA (OUVERT en option wroom-sd)", msp="OUVERT", n3pp="OUVERT"),
    "JP16": dict(ffp5cs="OUVERT", msp="FERMÉ : bas de pont LDR tracker a", n3pp="OUVERT (sonde sol 1)"),
    "JP17": dict(ffp5cs="OUVERT", msp="FERMÉ : bas de pont LDR tracker b", n3pp="OUVERT (sonde sol 2)"),
    "JP18": dict(ffp5cs="OUVERT", msp="FERMÉ : bas de pont LDR tracker c", n3pp="OUVERT (sonde sol 3)"),
    "JP19": dict(ffp5cs="OUVERT", msp="FERMÉ : bas de pont LDR tracker d", n3pp="OUVERT (sonde sol 4)"),
    "JP20": dict(ffp5cs="FERMÉ : bas de pont LDR luminosité", msp="OUVERT (module AO HumiditeSol)",
                 n3pp="FERMÉ : bas de pont LDR luminosité"),
    "JP21": dict(ffp5cs="OUVERT", msp="FERMÉ : pull-up DHT externe (J30)", n3pp="OUVERT"),
    "JP11": dict(ffp5cs="source SD_MISO : 1-2 = S3 (IO14) / 2-3 = WROOM (GPIO12, efuse 3V3 requis)",
                 msp=None, n3pp=None),
    # --- Forçage des relais (rev 0.2) ----------------------------------------
    "JP5": dict(ffp5cs="forçage pompe aquarium : sans cavalier = AUTO", msp=None,
                n3pp="forçage pompe d'arrosage : sans cavalier = AUTO"),
    "JP6": dict(ffp5cs="forçage pompe réservoir", msp=None, n3pp=None),
    "JP7": dict(ffp5cs="forçage chauffage : OFF seulement (ON interdit)", msp=None, n3pp=None),
    "JP8": dict(ffp5cs="forçage lumière / UV", msp=None, n3pp=None),
    "JP9": dict(ffp5cs="forçage AUX1", msp=None, n3pp=None),
    "JP10": dict(ffp5cs="forçage AUX2", msp=None, n3pp=None),
    "J38": dict(ffp5cs="état réel des 6 relais (collecteurs) pour un expandeur déporté",
                msp="état réel des relais", n3pp="état réel des relais"),
    # --- Alimentation (rev 0.2) ----------------------------------------------
    "U1": dict(ffp5cs="LDO 3,3 V du rail capteurs (permanent via JP1)",
               msp="LDO 3,3 V du rail capteurs (coupé en veille)",
               n3pp="LDO 3,3 V du rail capteurs (coupé en veille)"),
    "Q12": dict(ffp5cs="anti-inversion de l'entrée 5 V", msp="anti-inversion de l'entrée 5 V",
                n3pp="anti-inversion de l'entrée 5 V"),
    "D12": dict(ffp5cs="TVS 6,8 V du rail 5 V", msp="TVS 6,8 V du rail 5 V", n3pp="TVS 6,8 V du rail 5 V"),
}


def described(ref: str, desc: str) -> str:
    """Description KiCad d'un composant : rôle générique + rôle par firmware.

    Le rôle ffp5cs est donné en clair, msp et n3pp entre parenthèses — c'est
    ce que le champ Description montre au clic dans eeschema comme dans
    pcbnew. Un composant absent de ROLES (passif, visserie, distribution)
    garde sa seule description générique.
    """
    role = ROLES.get(ref)
    if not role:
        return desc
    autres = " ; ".join(f"{fw} : {role.get(fw) or 'non utilisé'}"
                        for fw in ("msp", "n3pp"))
    ffp = role.get("ffp5cs") or "non utilisé"
    return f"{desc} — ROLE ffp5cs : {ffp} ({autres})"

# Textes explicatifs du schéma : (x_gu, y_gu, texte)
SCH_TEXTS = [
    (18, 12, "ALIMENTATION 5V (3A recommandé) — jack OU bornier J1 OU sortie buck J37, tous sur VIN_RAW.\\nQ12 (IRF4905, VGS=-5V) = anti-inversion : un fil inversé ne détruit plus la carte. D12 = TVS 6,8V.\\nD5 protège le rail si l'USB du DevKit est branché en même temps."),
    (52, 24, "SITE A1 : ESP32 DevKit V1 (30 broches, socketé) — UN SEUL module peuplé (A1 OU A2).\\nEmpreinte à DEUX entraxes (25,4 = rangée A, 27,94 = rangée A') : souder le support droit là où tombent les broches du module.\\nEnvs carte universelle : msp/n3pp esp32dev_universal_test, ffp5cs wroom-universal-test.\\nSur une unité ffp5cs, ÔTER JP1 avant un flash UART (le pull-up R24 sur GPIO2 bloque le bootloader)."),
    (44, 50, "SERVOS 5V (nets SERVO1/SERVO2) — WROOM : GPIO26/27, S3 : IO21/IO47.\\nSignal en série R20/R21 (220R). Alimentation prise directement sur le rail +5V."),
    (86, 10, "3x HC-SR04 (5V) sur borniers 5,08, mode mono-broche TRIG=ECHO (sensor_ultrasonic.cpp).\\nEcho 5V ramené à 3V3 par pont 1k/2k ; le 2k est en circuit si JP13/14/15 FERMÉ (profil ffp5cs).\\nOUVERT sur msp (PLUIE sur US1, DHT ext sur US2) et pour l'option wroom-sd (JP15)."),
    (86, 48, "AIR : DHT11 (option DHT22, -DUSE_DHT22) — pull-up 10k."),
    (86, 61, "EAU : DS18B20 (1-Wire, pull-up 4.7k)."),
    (86, 71, "LUMIERE : LDR déportée sur J12, net ADC_E (GPIO36 WROOM / IO6 S3).\\nBas de pont R27 10k en circuit si JP20 FERMÉ (LDR n3pp/ffp5cs) ; OUVERT pour un module AO (msp HumiditeSol).\\nFiltre série 1k + 100n sur chaque entrée ADC (fils longs)."),
    (86, 81, "I2C : OLED SSD1306 0x3C + 3 supports extension (DS3231, INA226...)."),
    (107, 1, "6 RELAIS K1..K6 — commande ACTIVE HAUT (base 1k + pull-down 10k : etat sur au boot).\\nWROOM : GPIO16/17/18/19/23/25 — S3 : IO15/16/17/18/48/45. Le role de chaque canal\\ndepend du firmware (voir les sections PINMAP_UNIVERSAL). Borniers : 1=NC 2=COM 3=NO.\\nFORCAGE JP5..JP10 (1x3 sur la base) : SANS cavalier = AUTO (firmware) ; 1-2 = ON force (+3V3 via 1k) ;\\n2-3 = OFF force (base a GND). K3 (chauffage) : broche 1 NON cablee, ON force impossible.\\nSRD Form C = 7A/240VAC et 3A inductif REELS par contact (le 10A ne vaut qu'en 125VAC ou en Form A).\\nZONE SECTEUR ISOLEE sur le PCB (fentes + 3mm mini, ligne de fuite >= 6,4 mm) — circuit 230V\\nA PROTEGER EN AMONT (fusible/disjoncteur) ; charges inductives : snubber cote charge."),
    (18, 64, "HEADER SERVICE J17 : 3V3 / GND / EN / RX0 / TX0 / +5V uniquement.\\nTous les autres GPIO sont consommes par la carte universelle\\n(GPIO36 = ADC_E, GPIO39 = ADC_VBAT). Laisser RX0/TX0 libres pendant le flash USB."),
    (18, 81, "Distribution 5V / 3V3-capteurs / GND :\\nborniers a vis + rail header. Points de test TP1..TP4."),
    (140, 4, "POWER-GATE +3V3_SW (GPIO13) — rev 0.2 : LDO DEDIE U1 (LD1117V33) alimente par le 5V\\ncommute par Q7 (IRF4905, VGS=-5V). R35 : rail OFF par defaut ; R48 : pull-down base Q8.\\nJP1 FERME par defaut (rail permanent, ffp5cs) ; OTER JP1 sur les profils batterie\\n-> tout le rail capteurs (LDO compris) est coupe en veille."),
    (140, 28, "PONT DIVISEUR VBAT PERMANENT (rev 0.2, plus de coupure BS250) : R38 100k + JP12 :\\n1-2 = 22k (bus 12V, ffp5cs) ; 2-3 = 100k (1S Li-ion, msp/n3pp). R49/C13 filtre, D11 clamp 3V3."),
    (140, 52, "PROFIL BUS 12V SOLAIRE : fusible lame 7,5-10A EN AMONT (hors carte),\\nanti-inversion P-MOSFET Q11, TVS 18V D8 (1.5KE18A, DO-201), reservoir ; buck 12->5V EXTERNE\\n(module MP1584/XL4015 faible Iq sur entretoises, via J36/J37 ; J37 passe par Q12)."),
    (140, 76, "PROFIL SECTEUR : Hi-Link HLK-20M05 EMBARQUE (5V/3,6A) + fusible T1A (porte-fusible\\na capot, empreinte universelle pas 22,5-22,6) + varistance 10 ou 14 mm. Le corps du\\nmodule enjambe la frontiere secteur/logique (fente fraisee dessous). ZONE 230V = DANGER."),
    (140, 92, "microSD : module SPI 3V3 sockete. JP2/3/4/11 : source des lignes CS/SCK/MOSI/MISO\\n= S3 natif (1-2, defaut) ou WROOM env wroom-sd (2-3). MISO WROOM = GPIO12/MTDI :\\nefuse VDD_SDIO=3V3 OBLIGATOIRE avant (strap tension flash)."),
    (140, 108, "SITE A2 : ESP32-S3-DevKitC-1 44 broches, couche, USB vers le bord droit.\\nUN SEUL module peuple (A1 OU A2). Cartographies PINMAP_UNIVERSAL des 3 firmwares."),
    (120, 114, "ENTREES ANALOGIQUES PARTAGEES ADC_A..D : LDR msp (JP16-19 FERMES) ou sondes sol\\nn3pp (JP OUVERTS). ADC_E = HumidSol msp / Luminosite n3pp / LDR ffp5cs (JP20).\\nPluie msp = net US1 ; DHT ext msp = net US2 (pull-up R47 si JP21 FERME) — roles disjoints."),
    (184, 2, "J38 CMD SENSE : collecteurs des 6 transistors relais (0V = relais colle,\\n5V = repos) + GND + 5V — retour d'etat reel pour un expandeur deporte (PCF8574 via diviseur)."),
]

# Sérigraphies PCB : (x, y, texte, taille[, couche[, angle]])
_KX = (58, 92, 126, 160, 194, 228)
PCB_TEXTS = [
    (58, 43, "K1 POMPE AQUA/ARROSAGE", 0.9),
    (92, 43, "K2 POMPE RESERV", 1.0),
    (126, 43, "K3 CHAUFFAGE", 1.0),
    (160, 43, "K4 LUMIERE", 1.0),
    (194, 43, "K5 AUX1", 1.0),
    (228, 43, "K6 AUX2", 1.0),
    *[(x, 55.8, "NC COM NO", 0.8) for x in _KX],
    # COM reçoit la PHASE : un COM sur le neutre laisse la charge sous tension
    # relais ouvert (audit SEC-COM-01).
    *[(x, 57.9, "COM = PHASE", 0.8) for x in _KX],
    # Rappel 230 V entre les paires de mini-fentes de chaque canal (les fentes
    # descendent à y83 en 0.2 : plus de bandeau continu possible sous les relais)
    *[(x + 17, 84.2, "!! 230V !!", 1.0) for x in _KX[:-1]],
    (150, 84.2, "ZONE 230V - DANGER - COUPER LE SECTEUR AVANT INTERVENTION", 1.1, "B.SilkS"),
    # Sélecteurs de forçage JP5..JP10 (K3 : pas de position ON)
    *[(x + 20.5, 87.0, f"K{i} FORCE", 0.9) for i, x in enumerate(_KX, 1)],
    *[(x + 24.3, 90.0, "ON" if i != 3 else "--", 0.9) for i, x in enumerate(_KX, 1)],
    *[(x + 24.6, 95.1, "OFF", 0.9) for x in _KX],
    # légende verticale entre JP7 et la colonne K4 (seule bande libre de la zone relais)
    (150.2, 98.6, "JP5-10 : rien = AUTO\\n1-2 = ON   2-3 = OFF", 0.8, "F.SilkS", 90),
    (212, 103, "US ECHO : JP13-15 = ffp5cs", 1.0),
    (185, 158.2, f"n3-universal v{REV} — msp / n3pp / ffp5cs", 1.4, "B.SilkS"),
    # Colonne gauche (borniers tournés : fils vers le bord gauche)
    (51.6, 105, "+", 1.2, "F.SilkS", 90), (51.6, 110.1, "GND", 0.9, "F.SilkS", 90),
    (52.9, 107.5, "5V IN", 0.9, "F.SilkS", 90),
    (51.6, 119.5, "JACK 5V C+", 0.9, "F.SilkS", 90),
    (51.6, 131, "+5V", 0.9, "F.SilkS", 90), (51.6, 136.1, "GND", 0.9, "F.SilkS", 90),
    (52.9, 133.5, "5V OUT", 0.9, "F.SilkS", 90),
    (51.6, 143, "3V3", 0.9, "F.SilkS", 90), (51.6, 148.1, "ADC", 0.9, "F.SilkS", 90),
    (52.9, 145.5, "LDR", 0.9, "F.SilkS", 90),
    (51.6, 154, "3V3", 0.9, "F.SilkS", 90), (51.6, 159.1, "DAT", 0.9, "F.SilkS", 90),
    (51.6, 164.2, "GND", 0.9, "F.SilkS", 90), (52.9, 159, "DS18B20", 0.9, "F.SilkS", 90),
    (46.5, 82.5, "I2C J21/J22", 0.8, "F.SilkS", 90),
    (60, 143.6, "OLED", 0.9), (68, 143.6, "DS3231", 0.9), (76, 143.6, "INA226", 0.9),
    # Rangée basse : nom au-dessus du bornier (y160,8), broches dessous (y172,5)
    (58.1, 160.8, "PLUIE msp", 1.0), (53, 172.5, "3V3", 0.9), (58.08, 172.5, "DO", 0.9), (63.16, 172.5, "GND", 0.9),
    (74.6, 160.8, "DHT EXT msp", 1.0), (69.5, 172.5, "3V3", 0.9), (74.58, 172.5, "DATA", 0.9), (79.66, 172.5, "GND", 0.9),
    (91.1, 160.8, "DHT11", 1.0), (86, 172.5, "3V3", 0.9), (91.08, 172.5, "DATA", 0.9), (96.16, 172.5, "GND", 0.9),
    (104, 161.0, "UN SEUL MODULE : A1 OU A2", 0.9),
    (280, 131.0, "UN SEUL MODULE : A1 OU A2", 0.9),
    (132.6, 160.8, "US AQUA", 1.0), (154.1, 160.8, "US RESERV", 1.0), (175.6, 160.8, "US POTAGER", 1.0),
    *[(x, 172.5, lbl, 0.9) for x0 in (125, 146.5, 168)
      for x, lbl in ((x0, "5V"), (x0 + 5.08, "TRIG"), (x0 + 10.16, "ECHO"), (x0 + 15.24, "GND"))],
    (195.1, 160.8, "ADC A", 1.0), (211.6, 160.8, "ADC B", 1.0), (228.1, 160.8, "ADC C", 1.0), (244.6, 160.8, "ADC D", 1.0),
    *[(x, 172.5, lbl, 0.9) for x0 in (190, 206.5, 223, 239.5)
      for x, lbl in ((x0, "3V3"), (x0 + 5.08, "SIG"), (x0 + 10.16, "GND"))],
    (258.5, 160.8, "12V IN", 1.0), (270, 160.8, "BUCK IN", 1.0), (281.5, 160.8, "BUCK OUT 5V", 1.0),
    (293, 160.8, "VBAT SENSE", 1.0), (304.5, 160.8, "3V3 SW", 1.0),
    *[(x, 172.5, lbl, 0.9) for x0 in (256, 267.5, 279, 290.5, 302)
      for x, lbl in ((x0, "+"), (x0 + 5.08, "GND"))],
    # Zone centrale : cavaliers de profil et blocs
    (149.5, 108.4, "ffp5cs", 0.9), (170.5, 108.4, "ffp5cs", 0.9), (191.5, 108.4, "ffp5cs", 0.9),
    (214.5, 113.9, "msp", 0.9),
    (133, 124.8, "12V", 0.9), (138.1, 124.8, "1S", 0.9), (143, 124.8, "VBAT", 0.9),
    (185.5, 129.5, "SD 1-2=S3", 0.9), (185.5, 132.5, "2-3=WROOM", 0.9),
    # pas de place pour CS/SCK/MOSI/MISO (Q7, JP1, U1, R43-R46 devant, vias derrière) :
    # JP2/JP3/JP4/JP11 = CS/SCK/MOSI/MISO, voir README (tableau des cavaliers) et BOM
    (224, 128.6, "GND", 0.9), (228, 128.6, "5V", 0.9), (232, 128.6, "3V3SW", 0.9), (236, 128.6, "3V3", 0.9),
    (235.5, 136.4, "LDR", 0.9),
    (195, 157.5, "JP16-20 FERME = LDR | OUVERT = sonde sol n3pp, module AO msp", 0.9),
    (157.5, 154, "CMD SENSE", 0.9),
    # Zone droite
    (301, 133.2, "SERVOS", 0.9), (298, 146.3, "G", 0.9), (304, 146.3, "P", 0.9),
    (315.6, 140.5, "SERVICE", 0.9, "F.SilkS", 90), (315.6, 156, "5V GND 3V3SW", 0.9, "F.SilkS", 90),
    # Coin secteur
    (250, 41.6, "N", 0.9), (256.8, 41.6, "L", 0.9),
    (44.3, 55, "H1=NYLON", 0.9), (276.8, 65.2, "NYLON", 0.9),
    (253, 52, "SECTEUR 230V", 1.0), (255, 55, "!! DANGER 230V !!", 1.0),
    # Dos : notes longues sous les modules (zones sans pastille)
    (112.7, 112, "PROFIL ffp5cs :", 0.9, "B.SilkS"),
    (112.7, 115, "JP1 ferme, JP13-15 fermes", 0.9, "B.SilkS"),
    (112.7, 118, "JP20 ferme, JP12 1-2 (12V)", 0.9, "B.SilkS"),
    (112.7, 125, "PROFIL msp : JP1 ote,", 0.9, "B.SilkS"),
    (112.7, 128, "JP16-19 + JP21 fermes", 0.9, "B.SilkS"),
    (112.7, 131, "JP12 2-3 (1S)", 0.9, "B.SilkS"),
    (112.7, 135, "PROFIL n3pp : JP1 ote,", 0.9, "B.SilkS"),
    (112.7, 138, "JP20 ferme, JP12 2-3 (1S)", 0.9, "B.SilkS"),
    (112.7, 142, "autres JP : OUVERTS", 0.9, "B.SilkS"),
    (284, 110, "JP1 FERME = rail 3V3 permanent (ffp5cs)", 0.9, "B.SilkS"),
    (284, 113, "JP1 OUVERT = commute par GPIO13 (msp, n3pp)", 0.9, "B.SilkS"),
    (284, 117, "BUS 12V : FUSIBLE LAME 7,5-10A EN AMONT", 0.9, "B.SilkS"),
    (255.5, 116.6, "ANTENNE S3 : pas de cuivre", 0.8, "B.SilkS", 90),
    (80, 155.5, "JLCJLCJLCJLC", 1.0, "B.SilkS"),
]

BOARD = dict(x0=40, y0=40, x1=318, y1=175)

# Fentes d'isolement (fraisages internes, Edge.Cuts) : entre canaux 230V,
# frontière droite de la zone secteur, et mini-fentes COM<->bobine par canal.
SLOTS = ([(74, 42, 76, 80), (108, 42, 110, 80), (142, 42, 144, 80),
          (176, 42, 178, 80), (210, 42, 212, 80), (245, 42, 247, 82)]
         # Mini-fentes COM <-> bobine : y66..83 (rev 0.2, étaient 73..81) — le
         # chemin de fuite pad bobine +5V -> piste COM contournait l'extrémité
         # haute à 4,9 mm (moteur creepage KiCad 10, audit SEC-CRP-02) ; à 66
         # et 83 il dépasse 9 mm dans les deux sens. Extrémité basse à 1,9 mm
         # des pads des diodes de roue libre (y86), haute sous le corps du relais.
         + [(x + dx - 0.5, 66, x + dx + 0.5, 83)
            for x in (58, 92, 126, 160, 194, 228) for dx in (-3, 3)]
         # Coin PSU secteur (J27/F1/RV1 + entrée du Hi-Link) : frontière gauche,
         # et fente SOUS le corps du module (entre ses broches AC y~46 et DC y~97)
         # — le transformateur du module est la barrière d'isolement, la fente
         # allonge la ligne de fuite sous le boîtier.
         + [(248, 71, 316, 73)])


# ---------------------------------------------------------------------------
# Génération du schéma (.kicad_sch, format KiCad 8)
# ---------------------------------------------------------------------------

def pin_endpoints(comp):
    """Renvoie [(numéro, net|None, x_mm, y_mm, side)] pour chaque broche."""
    meta = SYMBOLS[comp["sym"]]
    cx, cy = comp["sch"][0] * G, comp["sch"][1] * G
    out = []
    for num, _n, py in meta.get("left", []):
        out.append((num, comp["nets"].get(num), cx - (meta["w"] / 2 + 1) * G,
                    cy - py * G, "L"))
    for num, _n, py in meta.get("right", []):
        out.append((num, comp["nets"].get(num), cx + (meta["w"] / 2 + 1) * G,
                    cy - py * G, "R"))
    return out


def check_schematic_overlaps() -> list[str]:
    """Deux étiquettes de nets différents au même point (symboles trop serrés)
    fusionneraient les nets dans eeschema — et le DRC de parité le révélerait
    seulement sur le PCB (103 conflits GND/ADC_A_DIV lors de la 0.2)."""
    seen: dict[tuple[float, float], tuple[str, str]] = {}
    errors = []
    for c in COMPONENTS:
        if not c["sym"]:
            continue
        for num, net, px, py, side in pin_endpoints(c):
            ex = px - G if side == "L" else px + G
            for key in ((round(px, 2), round(py, 2)), (round(ex, 2), round(py, 2))):
                prev = seen.get(key)
                if prev and prev[1] != (net or f"nc:{c['ref']}:{num}"):
                    errors.append(f"schéma : {c['ref']}.{num} ({net}) touche {prev[0]} ({prev[1]}) en {key}")
                seen[key] = (f"{c['ref']}.{num}", net or f"nc:{c['ref']}:{num}")
    return errors


def gen_schematic() -> str:
    for e in check_schematic_overlaps():
        raise SystemExit("ERREUR " + e)
    used_syms = sorted({c["sym"] for c in COMPONENTS if c["sym"]})
    lib = "\n".join(sym_def(s, SYMBOLS[s]) for s in used_syms)
    font = "(effects (font (size 1.27 1.27)))"
    items = []
    for c in COMPONENTS:
        if not c["sym"]:
            continue
        meta = SYMBOLS[c["sym"]]
        cx, cy = c["sch"][0] * G, c["sch"][1] * G
        u = uid("sym", c["ref"])
        pins_y = [p[2] for p in meta.get("left", [])] + [p[2] for p in meta.get("right", [])]
        top_mm = (max(pins_y) + 1) * G
        hidden = "(effects (font (size 1.27 1.27)) (hide yes))"
        pin_uuids = "\n".join(
            f'    (pin "{num}" (uuid "{uid("pin", c["ref"], num)}"))'
            for num, _n, _y in meta.get("left", []) + meta.get("right", []))
        items.append(f'''  (symbol (lib_id "n3u:{c['sym']}") (at {cx:.2f} {cy:.2f} 0) (unit 1)
    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)
    (uuid "{u}")
    (property "Reference" "{c['ref']}" (at {cx:.2f} {cy - top_mm - 2.54:.2f} 0) {font})
    (property "Value" "{c['value']}" (at {cx:.2f} {cy - top_mm - 0.4:.2f} 0) {font})
    (property "Footprint" "n3u:{c['fp']}" (at {cx:.2f} {cy:.2f} 0) {hidden})
    (property "Datasheet" "~" (at {cx:.2f} {cy:.2f} 0) {hidden})
    (property "Description" "{described(c['ref'], c['desc'])}" (at {cx:.2f} {cy:.2f} 0) {hidden})
{pin_uuids}
    (instances (project "{PROJECT}" (path "/{ROOT_UUID}" (reference "{c['ref']}") (unit 1))))
  )''')
        # fils + étiquettes (ou croix de non-connexion)
        for num, net, px, py, side in pin_endpoints(c):
            if net is None:
                items.append(f'  (no_connect (at {px:.2f} {py:.2f}) (uuid "{uid("nc", c["ref"], num)}"))')
                continue
            ex = px - G if side == "L" else px + G
            items.append(
                f'  (wire (pts (xy {px:.2f} {py:.2f}) (xy {ex:.2f} {py:.2f}))\n'
                f'    (stroke (width 0) (type default)) (uuid "{uid("wire", c["ref"], num)}"))')
            # Étiquette GLOBALE : un label local nomme le net « /GND » alors que
            # le PCB (et .kicad_dru, check_mains_gap, le checker de brochage)
            # le nomme « GND » — le DRC de parité signalait chaque pad.
            just, angle = ("right", 180) if side == "L" else ("left", 0)
            items.append(
                f'  (global_label "{net}" (shape passive) (at {ex:.2f} {py:.2f} {angle})\n'
                f'    (effects (font (size 1.27 1.27)) (justify {just}))\n'
                f'    (uuid "{uid("label", c["ref"], num)}")\n'
                f'    (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {ex:.2f} {py:.2f} 0)\n'
                f'      (effects (font (size 1.27 1.27)) (hide yes))))')
    for i, (tx, ty, txt) in enumerate(SCH_TEXTS):
        items.append(
            f'  (text "{txt}" (exclude_from_sim no) (at {tx * G:.2f} {ty * G:.2f} 0)\n'
            f'    (effects (font (size 1.7 1.7)) (justify left bottom)) (uuid "{uid("text", i)}"))')
    return f'''(kicad_sch
  (version 20231120)
  (generator "eeschema")
  (generator_version "8.0")
  (uuid "{ROOT_UUID}")
  (paper "A2")
  (title_block
    (title "n3-universal - carte porteuse UNIVERSELLE msp / n3pp / ffp5cs (bi-module WROOM / ESP32-S3)")
    (date "{REV_DATE}")
    (rev "{REV}")
    (company "salle aeree n3")
    (comment 1 "Genere par hardware/n3-universal/generator/generate.py")
    (comment 2 "Source de verite : pinmap_universel_propose.json + sections PINMAP_UNIVERSAL (msp / n3pp / ffp5cs)")
  )
  (lib_symbols
{lib}
  )
{chr(10).join(items)}
  (sheet_instances
    (path "/" (page "1"))
  )
)
'''


# ---------------------------------------------------------------------------
# Génération du PCB (.kicad_pcb, format KiCad 8)
# ---------------------------------------------------------------------------

def collect_nets():
    nets = set()
    for c in COMPONENTS:
        nets.update(n for n in c["nets"].values() if n)
    return sorted(nets)


def unconnected_pins(c):
    """{numéro: net} des broches de symbole non câblées, au nom que KiCad leur
    donne (« unconnected-(A2-GPIO46-Pad14) ») : le DRC de parité exige ce net
    sur le pad, comme après « Mettre à jour le PCB depuis le schéma »."""
    if not c["sym"]:
        return {}
    meta = SYMBOLS[c["sym"]]
    # KiCad omet le nom de broche quand il est vide ou égal au numéro
    # (connecteurs : « unconnected-(JP7-Pad1) »).
    return {num: (f"unconnected-({c['ref']}-Pad{num})" if name in ("", "~", num)
                  else f"unconnected-({c['ref']}-{name}-Pad{num})")
            for num, name, _y in meta.get("left", []) + meta.get("right", [])
            if not c["nets"].get(num)}


def load_footprint(name: str):
    path = FP_DIR / f"{name}.kicad_mod"
    tree = sx_parse(path.read_text(encoding="utf-8"))
    assert tree[0] == "footprint", name
    # retire version/generator (interdits dans un footprint embarqué en carte)
    tree[:] = [n for n in tree
               if not (isinstance(n, list) and n[0] in ("version", "generator"))]
    return tree


def gen_devkit_footprint() -> str:
    """Empreinte 2x15 supports femelles pour DevKit V1 30 broches.

    Rev 0.2 — DEUX entraxes de rangées sur la même empreinte : rangée A (pads
    1..15) à 25,4 mm (DOIT DevKit V1 et clones Type-C) ET rangée A' (mêmes
    numéros, même net) à 27,94 mm pour un clone plus large. On soude le support
    de droite là où tombent les broches du module acheté — sans mesure. Les
    deux rangées A/A' sont reliées par de courtes pistes (route_universal.py).
    Courtyard aux cotes réelles : débord 8 mm côté antenne, 11 mm côté USB
    (empreinte DOIT MIT syauqibilfaqih/ESP32-DevKit-V1-DOIT, mesure forum
    Fritzing 10,3 mm) — audit MECA-01.
    """
    pads = []
    for n in range(1, 16):   # rangée A (colonne droite, pad 15 en haut)
        pads.append((n, 25.4, (15 - n) * 2.54))
    for n in range(1, 16):   # rangée A' (entraxe 27,94) — mêmes numéros
        pads.append((n, 27.94, (15 - n) * 2.54))
    for n in range(16, 31):  # rangée B (colonne gauche, pad 30 en haut)
        pads.append((n, 0.0, (30 - n) * 2.54))
    pad_s = "\n".join(
        f'  (pad "{n}" thru_hole circle (at {x} {y}) (size 1.7 1.7) (drill 1.0) '
        f'(layers "*.Cu" "*.Mask"))' for n, x, y in pads)
    return f'''(footprint "ESP32_DevKit_V1_30pin"
  (version 20240108)
  (generator "generate.py")
  (layer "F.Cu")
  (descr "ESP32 DevKit V1 30 broches sur 2 supports 1x15 2.54mm, entraxe rangees 25.4 (A) OU 27.94 mm (A') : souder le support droit sur la rangee qui correspond au module. Comparer l'ORDRE des 30 etiquettes du module a la carte avant de souder.")
  (tags "ESP32 DevKit V1")
  (property "Reference" "REF**" (at 12.7 -9.8 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (property "Value" "ESP32_DevKit_V1_30pin" (at 12.7 44 0) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -1.9 -8) (end 29.9 46.5) (stroke (width 0.15) (type default)) (layer "F.SilkS"))
  (fp_text user "ANTENNE" (at 12.7 -5.5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_text user "USB" (at 12.7 44.5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_text user "A" (at 25.4 -1.9 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_text user "A'" (at 27.94 38 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -2.4 -8.5) (end 30.4 47.5) (stroke (width 0.05) (type default)) (layer "F.CrtYd"))
{pad_s}
)
'''


def gen_s3_footprint() -> str:
    """Empreinte 2x22 supports femelles pour ESP32-S3-DevKitC-1 (site A2).
    Entraxe rangées 22,86 mm (dessin Espressif v1.1). Courtyard aux cotes
    réelles : 1,57 mm + antenne 6,3 mm en débord côté antenne, 7,96 mm + USB
    0,9 mm côté connecteurs — audit MECA-02. Rotation 90° sur la carte :
    l'USB sort par le bord droit."""
    pads = []
    for n in range(1, 23):
        pads.append((n, 0.0, (n - 1) * 2.54))
    for n in range(23, 45):
        pads.append((n, 22.86, (n - 23) * 2.54))
    pad_s = "\n".join(
        f'  (pad "{n}" thru_hole circle (at {x} {y}) (size 1.7 1.7) (drill 1.0) '
        f'(layers "*.Cu" "*.Mask"))' for n, x, y in pads)
    return f'''(footprint "ESP32_S3_DevKitC_1_44pin"
  (version 20240108)
  (generator "generate.py")
  (layer "F.Cu")
  (descr "ESP32-S3-DevKitC-1 44 broches sur 2 supports 1x22 2.54mm, entraxe rangees 22.86mm (Espressif v1.1) - comparer l'ORDRE des etiquettes du module a la carte avant de souder")
  (tags "ESP32-S3 DevKitC-1")
  (property "Reference" "REF**" (at 11.43 -9.8 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (property "Value" "ESP32_S3_DevKitC_1_44pin" (at 11.43 64 0) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -1.4 -1.6) (end 24.3 61.3) (stroke (width 0.15) (type default)) (layer "F.SilkS"))
  (fp_text user "ANTENNE" (at 11.43 -4.5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_text user "USB" (at 11.43 58.5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -1.9 -8.5) (end 24.8 62.5) (stroke (width 0.05) (type default)) (layer "F.CrtYd"))
{pad_s}
)
'''


def strip_uuids(node):
    """Retire récursivement les (uuid ...) : les empreintes clonées N fois
    doivent recevoir des UUID uniques, sinon le DRC apparie des objets fantômes."""
    if isinstance(node, list):
        node[:] = [c for c in node
                   if not (isinstance(c, list) and c and c[0] == "uuid")]
        for c in node:
            strip_uuids(c)


# Minima sérigraphie du fabricant (JLCPCB : hauteur >= 1 mm, trait >= 0,15 mm ;
# en deçà les caractères sont floutés, voire supprimés par leur DFM).
# Réf. capacités JLCPCB : https://jlcpcb.com/capabilities/pcb-capabilities
SILK_MIN_H = 1.0
SILK_RATIO = 0.16   # trait / hauteur -> 0,16 mm à hauteur 1 mm


def silk_text(i: int, entry) -> str:
    """Texte de sérigraphie. entry = (x, y, texte, hauteur[, couche[, angle]])."""
    x, y, t, s = entry[:4]
    layer = entry[4] if len(entry) > 4 else "F.SilkS"
    angle = entry[5] if len(entry) > 5 else 0
    h = max(float(s), SILK_MIN_H)
    mirror = " (justify mirror)" if layer.startswith("B.") else ""
    return (f'  (gr_text "{t}" (at {x} {y} {angle}) (layer "{layer}") '
            f'(uuid "{uid("gtxt", i)}")\n'
            f'    (effects (font (size {h} {h}) '
            f'(thickness {h * SILK_RATIO:.2f})){mirror}))')


SILK_MIN_STROKE = 0.15   # minimum JLCPCB ; les libs KiCad dessinent à 0,12 (GBR-02)


def widen_silk_strokes(tree) -> None:
    for item in tree:
        if not (isinstance(item, list) and item and str(item[0]).startswith("fp_")):
            continue
        layer = sx_find_all(item, Sym("layer"))
        if not layer or "SilkS" not in str(layer[0][1]):
            continue
        for stroke in sx_find_all(item, Sym("stroke")):
            for w in sx_find_all(stroke, Sym("width")):
                if float(w[1]) < SILK_MIN_STROKE:
                    w[1] = Sym(f"{SILK_MIN_STROKE}")


# Rev 0.2 — repère DANS le corps de son composant et valeur sérigraphiée à
# côté (audit SILK-REF-01 : en 0.1.x, 21 repères s'étaient retrouvés dans le
# corps du composant voisin). Positions locales (x, y, angle) ; `None` = garder
# la position de la bibliothèque / pas de valeur.
SILK_LAYOUT = {
    "R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal": dict(ref=(5.08, 0, 0), value=(5.08, 2.35, 0)),
    "D_DO-41_SOD81_P10.16mm_Horizontal": dict(ref=(5.08, 0, 0), value=(5.08, -2.6, 0)),
    "D_DO-201AD_P15.24mm_Horizontal": dict(ref=(7.62, 0, 0), value=(7.62, 3.9, 0)),
    "D_DO-35_SOD-123_Dual_P7.62mm": dict(ref=(3.81, 0, 0), value=(3.81, 2.6, 0)),
    "D_DO-41_SOD81_P5.08mm_Vertical_AnodeUp": dict(ref=(2.54, -2.9, 0), value=(2.54, 2.9, 0)),
    "C_Disc_D5.0mm_W2.5mm_P5.00mm": dict(ref=(2.5, 0, 0), value=(2.5, 2.55, 0)),
    "CP_Radial_D10.0mm_P5.00mm": dict(ref=(2.5, -3, 0), value=(2.5, 6.3, 0)),
    "CP_Radial_D8.0mm_P3.50mm": dict(ref=(1.75, -2.4, 0), value=(1.75, 5.3, 0)),
    "CP_Radial_D6.3mm_P2.50mm": dict(ref=(1.25, -1.9, 0), value=(1.25, 4.4, 0)),
    "CP_Radial_D5.0mm_P2.50mm": dict(ref=(1.25, -1.6, 0), value=(1.25, 3.7, 0)),
    # TO-92 : repère sous le boîtier (au-dessus = LED témoin du canal relais)
    "TO-92_Inline_Wide_CBE": dict(ref=(2.54, 4.9, 0), value=None),  # sous les libellés C B E (y 3,0)
    # headers : le marqueur broche 1 de la sérigraphie KiCad monte à -1,76
    "PinHeader_1x03_P2.54mm_Vertical": dict(ref=(0, -2.8, 0), value=None),
    "PinHeader_1x02_P2.54mm_Vertical": dict(ref=(0, -2.8, 0), value=None),
    # TO-220 : valeur verticale à gauche du boîtier (dessus = repère, dessous = brochage)
    "TO-220-3_Vertical_GDS": dict(ref=(2.54, -4.5, 0), value=(-3.6, -1.0, 90)),
    "TestPoint_THTPad_D2.0mm_Drill1.0mm": dict(ref=(0, 2.3, 0), value=None),  # libellé au-dessus, repère dessous
    "TerminalBlock_bornier-4_P5.08mm": dict(ref=(7.62, -2.65, 0), value=None),  # comme les borniers 2/3
    # LDO U1 : R48 juste au-dessus -> repère sous le boîtier
    "TO-220-3_Vertical_LDO": dict(ref=(2.54, 4.4, 0), value=(-3.6, -1.0, 90)),  # sous GND OUT IN (0,7 mm, y 2,8)
}


def apply_silk_layout(tree, fp_name: str, override: dict | None = None) -> None:
    lay = override or SILK_LAYOUT.get(fp_name)
    if not lay:
        return
    if lay.get("ref"):
        x, y, rot = lay["ref"]
        for prop in sx_find_all(tree, Sym("property")):
            if prop[1] == "Reference":
                for atn in sx_find_all(prop, Sym("at")):
                    atn[1:] = [Sym(f"{x}"), Sym(f"{y}"), Sym(f"{rot}")]
    if lay.get("value"):
        x, y, rot = lay["value"]
        txt = [Sym("fp_text"), Sym("user"), "${VALUE}",
               [Sym("at"), Sym(f"{x}"), Sym(f"{y}"), Sym(f"{rot}")],
               [Sym("layer"), "F.SilkS"],
               [Sym("effects"), [Sym("font"), [Sym("size"), Sym("1"), Sym("1")],
                                 [Sym("thickness"), Sym("0.16")]]]]
        # avant le premier pad (ordre usuel des fichiers KiCad)
        idx = next((i for i, n in enumerate(tree) if isinstance(n, list) and n and n[0] == "pad"), len(tree))
        tree.insert(idx, txt)


def gen_pcb() -> str:
    nets = collect_nets() + sorted(n for c in COMPONENTS
                                   for n in unconnected_pins(c).values())
    net_no = {n: i + 1 for i, n in enumerate(nets)}
    net_decl = "\n".join(f'  (net {i + 1} "{n}")' for i, n in enumerate(nets))
    fp_blocks = []
    for c in COMPONENTS:
        tree = load_footprint(c["fp"])
        strip_uuids(tree)
        tree[1] = f'n3u:{tree[1]}'
        x, y, rot = c["pcb"]
        at = [Sym("at"), Sym(f"{x}"), Sym(f"{y}")] + ([Sym(f"{rot}")] if rot else [])
        insert = [
            [Sym("uuid"), uid("fp", c["ref"])],
            at,
            [Sym("path"), f"/{uid('sym', c['ref'])}"],
        ]
        tree[2:2] = insert
        widen_silk_strokes(tree)
        apply_silk_layout(tree, c["fp"], c.get("silk"))
        if not c["sym"]:
            # Empreinte sans symbole (visserie) : « board only », sinon le DRC
            # de parité schéma/PCB la signale comme empreinte orpheline.
            for attr in sx_find_all(tree, Sym("attr")):
                if Sym("board_only") not in attr:
                    attr.insert(1, Sym("board_only"))
        has_desc = False
        for prop in sx_find_all(tree, Sym("property")):
            if prop[1] == "Reference":
                prop[2] = c["ref"]
            elif prop[1] == "Value":
                prop[2] = c["value"]
            elif prop[1] == "Description":
                prop[2] = described(c["ref"], c["desc"])
                has_desc = True
        if not has_desc and c["sym"]:
            # empreintes générées (modules, porte-fusible) : le DRC de parité
            # KiCad 10 compare ce champ au symbole
            idx = next(i for i, n in enumerate(tree) if isinstance(n, list) and n and n[0] == "property")
            tree.insert(idx, [Sym("property"), "Description", described(c["ref"], c["desc"]),
                              [Sym("at"), Sym("0"), Sym("0"), Sym("0")], [Sym("layer"), "F.Fab"],
                              [Sym("hide"), Sym("yes")],
                              [Sym("effects"), [Sym("font"), [Sym("size"), Sym("1"), Sym("1")],
                                                [Sym("thickness"), Sym("0.15")]]]])
        for pad_idx, pad in enumerate(sx_find_all(tree, Sym("pad"))):
            net = c["nets"].get(str(pad[1])) or unconnected_pins(c).get(str(pad[1]))
            if rot:
                for atn in sx_find_all(pad, Sym("at")):
                    while len(atn) < 4:
                        atn.append(Sym("0"))
                    atn[3] = Sym(f"{(float(atn[3]) + rot) % 360:g}")
            if net:
                # insère (net N "nom") avant d'éventuels sous-blocs finaux
                pad.append([Sym("net"), Sym(str(net_no[net])), net])
            pad.append([Sym("uuid"), uid("pad", c["ref"], pad_idx)])
        fp_blocks.append(sx_dump(tree, 1))
    b = BOARD
    edge = (f'  (gr_rect (start {b["x0"]} {b["y0"]}) (end {b["x1"]} {b["y1"]})\n'
            f'    (stroke (width 0.1) (type default)) (layer "Edge.Cuts") (uuid "{uid("edge")}"))')
    # Zone interdite sous l'antenne WiFi du DevKit (recommandation Espressif :
    # pas de cuivre sous l'antenne PCB). Couvre le débord antenne du module,
    # au-dessus de la première rangée de pads.
    edge += (
        '\n  (zone (net 0) (net_name "") (layers "F.Cu" "B.Cu")'
        f' (uuid "{uid("antkeepout")}") (hatch edge 0.5)'
        '\n    (keepout (tracks not_allowed) (vias not_allowed) (pads allowed)'
        ' (copperpour not_allowed) (footprints allowed))'
        '\n    (fill (thermal_gap 0.5) (thermal_bridge_width 0.5))'
        '\n    (polygon (pts (xy 97.6 103.5) (xy 127.8 103.5)'
        ' (xy 127.8 109.5) (xy 97.6 109.5)))'
        '\n  )')
    # Idem pour l'antenne du site A2 (S3-DevKitC-1 couché, antenne vers la
    # gauche : module centré y=116,6, antenne 18 mm de large, débord 6,3 mm).
    edge += (
        '\n  (zone (net 0) (net_name "") (layers "F.Cu" "B.Cu")'
        f' (uuid "{uid("antkeepout_s3")}") (hatch edge 0.5)'
        '\n    (keepout (tracks not_allowed) (vias not_allowed) (pads allowed)'
        ' (copperpour not_allowed) (footprints allowed))'
        '\n    (fill (thermal_gap 0.5) (thermal_bridge_width 0.5))'
        '\n    (polygon (pts (xy 248.5 107.5) (xy 263 107.5)'
        ' (xy 263 125.5) (xy 248.5 125.5)))'
        '\n  )')
    # Interdiction de coulée sous la tête de la vis H1 (seul trou de fixation
    # proche de la bande secteur : tête à ~4,3 mm des contacts K1 → vis NYLON
    # prescrite, et pas de cuivre GND sous la tête — audit rev 0.1).
    edge += (
        '\n  (zone (net 0) (net_name "") (layers "F.Cu" "B.Cu")'
        f' (uuid "{uid("h1keepout")}") (hatch edge 0.5)'
        '\n    (keepout (tracks allowed) (vias allowed) (pads allowed)'
        ' (copperpour not_allowed) (footprints allowed))'
        '\n    (fill (thermal_gap 0.5) (thermal_bridge_width 0.5))'
        '\n    (polygon (pts (xy 41 41) (xy 49 41)'
        ' (xy 49 49) (xy 41 49)))'
        '\n  )')
    for i, (sx0, sy0, sx1, sy1) in enumerate(SLOTS):
        edge += (f'\n  (gr_rect (start {sx0} {sy0}) (end {sx1} {sy1})\n'
                 f'    (stroke (width 0.1) (type default)) (layer "Edge.Cuts") (uuid "{uid("slot", i)}"))')
    texts = "\n".join(silk_text(i, e) for i, e in enumerate(PCB_TEXTS))
    # Zones GND en escalier : tout le pourtour SAUF la bande relais
    # (45.2..246 x 40..84) ET le coin PSU secteur (246..318 x 40..73) — l'audit
    # rev 0.1 a montré que le coin PSU, ajouté avec le profil Hi-Link, n'était
    # pas exclu : le plan GND coulait sous les pistes/pads 230 V.
    # Bord bas de la bande relais à y86 (et non y84) : les pads COM descendent
    # à y79,5, la ligne de fuite pad COM -> plan GND passe de 4,5 à 6,5 mm, au-dessus
    # de l'exigence d'isolation renforcée (~5,0 mm) — audit SEC-CRP-01.
    zx0, zx1, zy, pzy = 45.2, 246, GND_PLANE_RELAY_Y, 73
    poly = ('(polygon (pts '
            f'(xy {b["x0"]} {b["y0"]}) (xy {zx0} {b["y0"]}) (xy {zx0} {zy}) '
            f'(xy {zx1} {zy}) (xy {zx1} {pzy}) (xy {b["x1"]} {pzy}) '
            f'(xy {b["x1"]} {b["y1"]}) (xy {b["x0"]} {b["y1"]})))')
    zones = "\n".join(
        f'  (zone (net {net_no["GND"]}) (net_name "GND") (layer "{layer}") (uuid "{uid("zone", layer)}")\n'
        f'    (hatch edge 0.5)\n'
        f'    (connect_pads (clearance 0.5))\n'
        f'    (min_thickness 0.25) (filled_areas_thickness no)\n'
        f'    (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5))\n'
        f'    {poly})'
        for layer in ("F.Cu", "B.Cu"))
    layers = '''  (layers
    (0 "F.Cu" signal)
    (31 "B.Cu" signal)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user)
    (39 "F.Mask" user)
    (40 "Dwgs.User" user "User.Drawings")
    (41 "Cmts.User" user "User.Comments")
    (44 "Edge.Cuts" user)
    (45 "Margin" user)
    (46 "B.CrtYd" user "B.Courtyard")
    (47 "F.CrtYd" user "F.Courtyard")
    (48 "B.Fab" user)
    (49 "F.Fab" user)
  )'''
    return f'''(kicad_pcb
  (version 20240108)
  (generator "pcbnew")
  (generator_version "8.0")
  (general
    (thickness 1.6)
    (legacy_teardrops no)
  )
  (paper "A3")
  (title_block
    (title "n3-universal - carte porteuse UNIVERSELLE msp / n3pp / ffp5cs (bi-module WROOM / ESP32-S3)")
    (date "{REV_DATE}")
    (rev "{REV}")
    (company "salle aeree n3")
  )
{layers}
  (setup
    (stackup
      (layer "F.SilkS" (type "Top Silk Screen"))
      (layer "F.Paste" (type "Top Solder Paste"))
      (layer "F.Mask" (type "Top Solder Mask") (thickness 0.01))
      (layer "F.Cu" (type "copper") (thickness 0.07))
      (layer "dielectric 1" (type "core") (thickness 1.44) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
      (layer "B.Cu" (type "copper") (thickness 0.07))
      (layer "B.Mask" (type "Bottom Solder Mask") (thickness 0.01))
      (layer "B.Paste" (type "Bottom Solder Paste"))
      (layer "B.SilkS" (type "Bottom Silk Screen"))
      (copper_finish "HAL lead-free")
      (dielectric_constraints no)
    )
    (pad_to_mask_clearance 0)
    (allow_soldermask_bridges_in_footprints no)
  )
  (net 0 "")
{net_decl}
{chr(10).join(fp_blocks)}
{edge}
{texts}
{zones}
)
'''


_NC = {"clearance": 0.2, "track_width": 0.3, "via_diameter": 0.7,
       "via_drill": 0.35, "bus_width": 12, "diff_pair_gap": 0.25,
       "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2, "line_style": 0,
       "microvia_diameter": 0.3, "microvia_drill": 0.1,
       "pcb_color": "rgba(0, 0, 0, 0.000)",
       "schematic_color": "rgba(0, 0, 0, 0.000)", "wire_width": 6}


GND_PLANE_RELAY_Y = 86
MAINS_PLANE_GAP_MM = 6.5
# Ligne de fuite secteur <-> basse tension vérifiée par le moteur creepage de
# KiCad 10 (fentes comprises) : 6,4 mm = 2 x 3,2 mm (isolation renforcée,
# 250 V, degré de pollution 2, groupe IIIa, avec marge) — audit SEC-CRP-02.
MAINS_CREEPAGE_MM = 6.4

# 2e règle : un plan coulé est une surface étendue, la ligne de fuite vers lui
# est le plus court chemin en surface — on exige 6,5 mm (isolation renforcée
# ~5,0 mm, PD2, groupe IIIa) et le remplisseur de zones l'applique de lui-même.
DRU_RULES = f"""(version 1)
(rule "mains_vs_logic"
  (condition "A.NetClass == 'Mains' && B.NetClass != 'Mains'")
  (constraint clearance (min 3.0mm)))
(rule "mains_vs_gnd_plane"
  (condition "A.NetClass == 'Mains' && B.Type == 'Zone' && B.NetClass != 'Mains'")
  (constraint clearance (min {MAINS_PLANE_GAP_MM}mm)))
(rule "mains_creepage"
  (condition "A.NetClass == 'Mains' && B.NetClass != 'Mains'")
  (constraint creepage (min {MAINS_CREEPAGE_MM}mm)))
"""


FP_LIB_TABLE = """(fp_lib_table
  (version 7)
  (lib (name "n3u") (type "KiCad") (uri "${KIPRJMOD}/../generator/footprints") (options "") (descr "Empreintes vendorees n3-universal"))
)
"""


def gen_project() -> str:
    return json.dumps({
        "board": {"3dviewports": [], "design_settings": {"defaults": {},
                  "rules": {"min_clearance": 0.2, "min_track_width": 0.25,
                            "min_via_diameter": 0.5, "min_via_hole": 0.3}}},
        "boards": [], "cvpcb": {"equivalence_files": []}, "libraries":
        {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 1},
        "net_settings": {"classes": [
            dict(_NC, name="Default", track_width=0.4, clearance=0.2),
            dict(_NC, name="Mains", track_width=2.5, clearance=0.5,
                 via_diameter=1.6, via_drill=0.8),
            dict(_NC, name="Alim", track_width=1.2, clearance=0.2,
                 via_diameter=1.0, via_drill=0.5),
        ], "meta": {"version": 3},
            "netclass_patterns": (
                [{"netclass": "Mains", "pattern": f"REL{i}_{c}"}
                 for i in range(1, 7) for c in ("COM", "NO", "NC")]
                + [{"netclass": "Mains", "pattern": n}
                   for n in ("MAINS_L", "MAINS_LF", "MAINS_N")]
                + [{"netclass": "Alim", "pattern": n}
                   for n in ("+5V", "VIN_5V", "VIN_RAW", "GND", "+3V3_SW",
                             "LDO_IN", "VBAT12_IN", "VBAT12_PROT")])},
        "pcbnew": {"page_layout_descr_file": ""},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [[ROOT_UUID, "Racine"]],
    }, indent=2)


# Approvisionnement (vérifié en ligne le 2026-10-06 — voir COMMANDE.md §3) :
# clé = valeur normalisée (minuscules, sans espace) ou famille d'empreinte.
# "Maroc" = en stock chez un revendeur marocain ; "import" = colis LCSC/JLCPCB
# unique (ou Mouser/Farnell pour le porte-fusible). Code LCSC vérifié sur la
# page produit, vide quand aucun code sûr n'a été relevé.
SOURCING = {
    "irf4905": ("Maroc / import", "C2564", "A2itronic (10 DH) ; LCSC C2564 IRF4905PBF"),
    "bc337-40": ("Maroc", "C713611", "Moussasoft (1 DH, >1000) ; brochage C-B-E sérigraphié"),
    "1n4007": ("Maroc / import", "C2457", "Moussasoft ; LCSC C2457 (1N4007G MDD) — l'ancien C727 n'existe plus"),
    "1n5822": ("Maroc", "C2476", "Moussasoft (stock faible) ; LCSC C2476"),
    "1n4744a": ("import", "C238928", "zener 15 V DO-41"),
    "1.5ke18a": ("import", "C1666858", "TVS 18 V DO-201 (Littelfuse 1.5KE18A-B) — PAS de 1.5KE15A locale (conduirait à 14,7 V)"),
    "1.5ke6.8a": ("import", "C412500", "TVS 6,8 V DO-201 Littelfuse (Vrwm 5,8 V, stock faible) ; repli P6KE6.8A DO-15 C409413 (pattes 0,8 mm, même trou)"),
    "bat85": ("import", "C19167", "DO-35 épuisé chez LCSC (BAT85 C549292 = 0) → poser la variante SOD-123 BAT43W JSCJ C19167 "
               "sur les pads CMS de l'empreinte double ; ou BAT85/BAT42 DO-35 d'une autre source"),
    "ld1117v33": ("import", "C283467", "ST LD1117V33 TO-220 (1=GND 2=OUT 3=IN, languette = OUT) ; repli LM1117T-3.3 HGSEMI C498321 (même brochage)"),
    "srd-05vdc-sl-c": ("Maroc / import", "C35449", "Moussasoft (18 DH) ; LCSC C35449 Songle"),
    "hlk-20m05": ("Maroc / import", "C465406", "Shop4Makers (90 DH) ; LCSC C465406"),
    "14d471k": ("Maroc / import", "C111188", "MicroPlanet 14D471K (4 DH) ou LCSC C111188 10D471K (même pas 7,5)"),
    "t1a5x20": ("import", "C3131", "porte-fusible 5x20 à capot BLX-A Xucheng C3131 (pas 22,0 ±0,5 mesuré sur sa fiche LCSC, "
                               "fentes 3 mm de l'empreinte) ou Multicomp MC000830 / Schurter 0031.8201 (22,5-22,6) + cartouche T1A"),
    "esp32devkitv1": ("Maroc", "", "Shop4Makers « ESP32 Dev Kit V1 Type-C » (CP2102, 114 DH) — ne pas importer (ANRT)"),
    "esp32-s3-devkitc-1": ("Maroc", "", "Shop4Makers / Moussasoft S3 N16R8 (180 DH) — ne pas importer (ANRT)"),
    "jack5.5/2.1": ("Maroc", "", "jack DC-005 standard"),
}
SOURCING_FAMILY = {
    "bornier": ("Maroc", "", "KF128 / KF301 pas 5,08 (Moussasoft 4 DH, Shop4Makers)"),
    "r_axial": ("Maroc", "", "1/4 W 5 % (Moussasoft)"),
    "c_disc": ("Maroc", "", "céramique 100 nF"),
    "cp_radial": ("Maroc", "", "électrolytique radial"),
    "led_d5": ("Maroc", "", "LED 5 mm"),
    "pinheader": ("Maroc", "", "barrette mâle 2,54 sécable (Moussasoft / Shop4Makers)"),
    "pinsocket": ("Maroc", "", "barrette femelle 2,54 sécable (Moussasoft 6 DH)"),
    "testpoint": ("Maroc", "", "boucle de fil ou picot"),
    "mountinghole": ("Maroc", "", "vis M3 (nylon pour H1/H5)"),
    "barreljack": ("Maroc", "", "jack DC-005"),
}


def source_of(value: str, fp: str) -> tuple[str, str, str]:
    key = value.lower().replace(" ", "").rstrip("*")
    if key in SOURCING:
        return SOURCING[key]
    f = fp.lower()
    for fam, info in SOURCING_FAMILY.items():
        if fam in f:
            return info
    return ("", "", "")


def gen_bom():
    rows = {}
    for c in COMPONENTS:
        key = (c["value"], c["fp"], c["desc"])
        rows.setdefault(key, []).append(c["ref"])
    out = [["Refs", "Qte", "Valeur", "Empreinte", "Description", "Source", "LCSC", "Note achat"]]
    for (value, fp, desc), refs in sorted(rows.items(), key=lambda kv: kv[1][0]):
        src, lcsc, note = source_of(value, fp)
        out.append([" ".join(sorted(refs)), str(len(refs)), value, fp, desc, src, lcsc, note])
    # Pièces sans empreinte propre (montées sur une empreinte existante) :
    # les supports du DevKit doivent apparaître pour être commandés.
    extra = [
        ["A1 (supports)", "2", "Support femelle 1x15 P2.54",
         "monte sur l'empreinte ESP32_DevKit_V1_30pin",
         "Barrettes femelles 15 pts : le DevKit s'enfiche, jamais soudé. Support droit sur la "
         "rangée A (25,4) OU A' (27,94) selon le module acheté", "Maroc", "", "barrette femelle sécable"],
        ["A2 (supports)", "2", "Support femelle 1x22 P2.54",
         "monte sur l'empreinte ESP32_S3_DevKitC_1_44pin",
         "Barrettes 22 pts (site S3) : à souder si un S3-DevKitC-1 est prévu", "Maroc", "", ""],
        ["J35 (module)", "0-1",
         "Module microSD SPI 3,3V DIRECT (sans régulateur ni tampon 74LVC125)",
         "s'enfiche sur J35",
         "6 broches GND/VCC/MISO/MOSI/SCK/CS. À poser sur unités S3 (site A2, JP2/3/4/11 en 1-2). "
         "Sur WROOM (site A1, JP en 2-3) : UNIQUEMENT après efuse VDD_SDIO=3V3 "
         "(espefuse.py set_flash_voltage 3.3V). PAS de module type Catalex", "Maroc", "", "Moussasoft 18 DH"],
        ["J14/J21/J22/J28 (modules)", "0-4", "DS3231 + INA226",
         "s'enfichent sur les ports I2C",
         "DS3231 : dessouder le circuit de charge, pile CR2032 ; INA sur +3V3_SW", "Maroc", "",
         "Moussasoft / Shop4Makers"],
        ["J36/J37 (module)", "0-1", "Buck 12V->5V faible Iq",
         "MP1584/XL4015 sur entretoises, câblé sur J36 (IN) / J37 (OUT)",
         "Profil bus 12V uniquement ; fusible lame 7,5-10A en amont", "Maroc", "", "Shop4Makers MP1584EN 25 DH"],
        ["F1 (porte-fusible)", "1", "Porte-fusible 5x20 à capot, pas 22,0 à 22,6 mm",
         "se soude sur Fuse_5x20_Universal (fentes 3 mm)",
         "Références compatibles : Xucheng BLX-A LCSC C3131 (pas 22,0 ±0,5, colis d'import), Multicomp MC000830, "
         "Würth 696108003002 / 696107003002 (ergot), Schurter 0031.8201 (ergot), Stelvio PTF78. "
         "Profil secteur uniquement. Cartouche T1A 5x20",
         "import", "", "Farnell/Mouser/LCSC — vérifier le pas 22,5-22,6 et une patte <= 1,5 mm"],
        ["JP1-JP21 (cavaliers)", "18", "Cavalier (shunt) 2,54 mm à languette",
         "se posent sur les headers JP",
         "JP1 + JP2/3/4/11 + JP12 + 9 cavaliers de profil = 15 posés au plus ; JP5-JP10 livrés SANS "
         "cavalier (AUTO) ; 3 en rechange", "Maroc", "", "Shop4Makers lot de cavaliers"],
        ["H1, H5 (visserie)", "2", "Vis + écrou NYLON M3 (+ entretoise nylon)",
         "trous de fixation H1 (coin relais) et H5 (coin secteur)",
         "OBLIGATOIRE : H1 = tête métal à ~4,3 mm du 230V ; H5 = entre les "
         "pistes N et LF. H2-H4, H6, H7 : visserie M3 standard + entretoises", "Maroc", "", ""],
        ["Câblage", "—", "Embouts de câblage (ferrules) + pince ; fil 0,25-0,5 mm² capteurs, 1,5 mm² charges",
         "borniers à vis", "Tout fil souple sur bornier à vis reçoit un embout serti", "Maroc", "",
         "Moussasoft kit pince + 1200 embouts 250 DH"],
        ["Finition", "—", "Vernis de tropicalisation (ex. Plastik 70 / Relife 70)",
         "après montage et test", "Pulvériser hors borniers, supports et cavaliers (masquer)", "Maroc / import", "",
         "Moussasoft Relife 70 (précommande) ou Kontakt Plastik 70"],
    ]
    out += extra
    return out


# ---------------------------------------------------------------------------
# Géométrie des corps 3D (courtyards IPC-7351) — primitives partagées avec
# tools/check_pcb_clearance.py, qui y ajoute les couloirs d'insertion.
# ---------------------------------------------------------------------------

def _xy(node, key):
    """(x, y) du premier sous-bloc `key` de `node`, ou None."""
    found = sx_find_all(node, Sym(key))
    return (float(found[0][1]), float(found[0][2])) if found else None


def courtyard_points(fp_tree):
    """Points du contour courtyard (F/B.CrtYd) d'une empreinte, en local.

    Le courtyard est la projection normalisée du BOÎTIER : contrairement aux
    pads, il tient compte du corps qui déborde (relais, jack, bloc PSU).
    """
    pts = []
    for item in fp_tree:
        if not isinstance(item, list) or not item:
            continue
        layers = sx_find_all(item, Sym("layer"))
        if not layers or "CrtYd" not in str(layers[0][1]):
            continue
        kind = str(item[0])
        if kind in ("fp_line", "fp_arc"):
            pts += [p for p in (_xy(item, k) for k in ("start", "mid", "end")) if p]
        elif kind == "fp_rect":
            (x0, y0), (x1, y1) = _xy(item, "start"), _xy(item, "end")
            pts += [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
        elif kind == "fp_circle":
            (cx, cy), (ex, ey) = _xy(item, "center"), _xy(item, "end")
            r = math.hypot(ex - cx, ey - cy)
            pts += [(cx - r, cy - r), (cx + r, cy - r),
                    (cx + r, cy + r), (cx - r, cy + r)]
        elif kind == "fp_poly":
            for xy in sx_find_all(sx_find_all(item, Sym("pts"))[0], Sym("xy")):
                pts.append((float(xy[1]), float(xy[2])))
    return pts


def place_points(pts, x0: float, y0: float, rot: float):
    """Transporte des points locaux à la position/rotation d'une empreinte.

    Convention KiCad (axe y vers le bas) : +90 deg => (px,py) -> (py,-px).
    """
    rad = math.radians(rot)
    cos, sin = math.cos(rad), math.sin(rad)
    return [(x0 + px * cos + py * sin, y0 - px * sin + py * cos)
            for px, py in pts]


def convex_hull(pts):
    """Enveloppe convexe (monotone chain) — majorant sûr d'un courtyard en L."""
    pts = sorted(set(pts))
    if len(pts) <= 2:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def overlap_depth(poly_a, poly_b) -> float:
    """Profondeur de recouvrement de deux convexes (SAT) ; 0 s'ils sont disjoints."""
    best = float("inf")
    for poly in (poly_a, poly_b):
        n = len(poly)
        for i in range(n):
            (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
            ax, ay = -(y1 - y0), (x1 - x0)
            norm = math.hypot(ax, ay)
            if norm < 1e-12:
                continue
            ax, ay = ax / norm, ay / norm
            pa = [ax * px + ay * py for px, py in poly_a]
            pb = [ax * px + ay * py for px, py in poly_b]
            gap = min(max(pa) - min(pb), max(pb) - min(pa))
            if gap <= 0:
                return 0.0
            best = min(best, gap)
    return 0.0 if best == float("inf") else best


def check_pcb_overlaps():
    """Recouvrement des CORPS (courtyards) des composants placés.

    Remplace l'ancien garde-fou « bounding-box des pads + 2 mm », aveugle au
    boîtier réel (audit `GEN-08`). La marge forfaitaire disparaît : le
    courtyard porte DÉJÀ le jeu d'assemblage normalisé (IPC-7351), donc le
    critère devient le recouvrement franc de deux corps.
    Renvoie (ref1, ref2, profondeur_mm).
    Le contrôle complet — couloirs d'insertion des connecteurs, contour de
    carte, paires à peuplement exclusif — vit dans
    `tools/check_pcb_clearance.py`, qui relit le PCB routé.
    """
    hulls = []
    for c in COMPONENTS:
        tree = load_footprint(c["fp"])
        x0, y0, rot = c["pcb"]
        pts = courtyard_points(tree)
        if not pts:   # empreinte sans courtyard : repli sur les pads + 2 mm
            pads = []
            for pad in sx_find_all(tree, Sym("pad")):
                at = sx_find_all(pad, Sym("at"))[0]
                px, py = float(at[1]), float(at[2])
                pads += [(px - 2, py - 2), (px + 2, py - 2),
                         (px + 2, py + 2), (px - 2, py + 2)]
            pts = pads
        if pts:
            hulls.append((c["ref"], convex_hull(place_points(pts, x0, y0, rot))))
    warned = []
    for i, (r1, h1) in enumerate(hulls):
        for r2, h2 in hulls[i + 1:]:
            depth = overlap_depth(h1, h2)
            if depth > 0.01:
                warned.append((r1, r2, depth))
    return warned


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--regen-pcb", action="store_true",
                    help="réécrire aussi le .kicad_pcb (PERD le routage)")
    args = ap.parse_args()
    KICAD_DIR.mkdir(parents=True, exist_ok=True)
    pcb_path = KICAD_DIR / f"{PROJECT}.kicad_pcb"
    # Méthode hybride : le PCB routé (route_universal + finalize_board) est la
    # référence géométrique ; le régénérer ici effacerait pistes et zones.
    keep_pcb = (not args.regen_pcb and pcb_path.exists()
                and "(segment" in pcb_path.read_text(encoding="utf-8"))
    devkit_fp = FP_DIR / "ESP32_DevKit_V1_30pin.kicad_mod"
    devkit_fp.write_text(gen_devkit_footprint(), encoding="utf-8")
    (FP_DIR / "ESP32_S3_DevKitC_1_44pin.kicad_mod").write_text(
        gen_s3_footprint(), encoding="utf-8")

    sch = gen_schematic()
    pcb = gen_pcb()
    sx_parse(sch)  # auto-validation syntaxique
    sx_parse(pcb)
    (KICAD_DIR / f"{PROJECT}.kicad_sch").write_text(sch, encoding="utf-8")
    if keep_pcb:
        print(f"{pcb_path.name} routé conservé (--regen-pcb pour le réécrire) ;"
              " libellés : finalize_board.py")
    else:
        pcb_path.write_text(pcb, encoding="utf-8")
        (KICAD_DIR / f"{PROJECT}.kicad_pro").write_text(gen_project(), encoding="utf-8")
    (KICAD_DIR / f"{PROJECT}.kicad_dru").write_text(DRU_RULES, encoding="utf-8")
    (KICAD_DIR / "fp-lib-table").write_text(FP_LIB_TABLE, encoding="utf-8")
    with open(ROOT / "BOM.csv", "w", newline="", encoding="utf-8") as f:
        csv.writer(f, delimiter=";").writerows(gen_bom())

    nets = collect_nets()
    print(f"OK: {len(COMPONENTS)} composants, {len(nets)} nets")
    over = check_pcb_overlaps()
    for r1, r2, depth in over:
        print(f"  ATTENTION corps qui se recouvrent : {r1} / {r2} "
              f"({depth:.2f} mm)")


if __name__ == "__main__":
    main()
