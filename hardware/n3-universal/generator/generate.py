#!/usr/bin/env python3
"""Génère le projet KiCad (schéma + PCB + BOM) — VARIANTE 230 V de la carte porteuse
ffp5cs wroom-prod. Zone secteur isolée en haut de carte (fentes, règle DRC 3 mm
mains<->logique via .kicad_dru), breakout de TOUS les GPIO libres, distribution
5V/3V3/GND en borniers + header. Routage : generator/route_230v.py.

Source de vérité : ../pinmap.json (lui-même vérifié contre ffp5cs/include/pins.h
par tools/check_pinmap_vs_firmware.py). Les empreintes proviennent de la
bibliothèque officielle KiCad 8.0.9 (vendorées dans ./footprints, licence
CC-BY-SA 4.0 avec exception d'usage — voir README).

Usage : python3 generate.py   (écrit dans ../kicad/ et ../BOM.csv)

Les fichiers générés sont au format KiCad 8 (s-expressions), ouvrables et
éditables dans KiCad 8/9/10. Régénérer écrase les fichiers : faire les
retouches durables ici (ou dans pinmap.json), pas dans les fichiers générés,
ou cesser de régénérer une fois le routage manuel commencé.
"""

from __future__ import annotations

import csv
import json
import re
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
KICAD_DIR = ROOT / "kicad"
FP_DIR = HERE / "footprints"
PROJECT = "n3-universal"
REV = "0.1"
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
                  refs: dict | None = None):
    """Canal relais n : commande GPIO -> transistor -> relais SRD-05 -> bornier 230V.
    Relais pivoté 90° : contacts vers le bord haut (zone secteur), bobine vers la
    logique. Bornier : 1=NC 2=COM 3=NO (routage secteur rectiligne, fait par
    route_230v.py, PAS par l'autorouteur)."""
    col = k_x - 7
    # Références par défaut (canaux 1-4). Les canaux ajoutés passent un
    # override explicite pour ne pas percuter D5/LED5/R13... (alim, LDR, US).
    refs = refs or dict(rb=f"R{n}", rp=f"R{n + 4}", rl=f"R{n + 8}",
                        q=f"Q{n}", d=f"D{n}", led=f"LED{n}")
    bx, by = 111, 4 + 26 * (n - 1)  # bloc schéma (unités de grille)
    return [
        dict(ref=refs["rb"], sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance base transistor", sch=(bx, by), pcb=(col, 91, 0),
             nets={"1": gpio_net, "2": f"REL{n}_B"}),
        dict(ref=refs["rp"], sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-down base (état sûr au boot)", sch=(bx, by + 4), pcb=(col, 96, 0),
             nets={"1": f"REL{n}_B", "2": "GND"}),
        dict(ref=refs["rl"], sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance LED témoin", sch=(bx, by + 8), pcb=(col, 100, 0),
             nets={"1": "+5V", "2": f"REL{n}_LED"}),
        dict(ref=refs["q"], sym="NPN", value="BC337-40", fp="TO-92_Inline",
             desc="Transistor NPN commande relais (1=C 2=B 3=E)",
             sch=(bx + 9, by + 2), pcb=(k_x + 7, 96, 0),
             nets={"1": f"REL{n}_SW", "2": f"REL{n}_B", "3": "GND"}),
        dict(ref=refs["d"], sym="D", value="1N4007",
             fp="D_DO-41_SOD81_P10.16mm_Horizontal",
             desc="Diode de roue libre bobine", sch=(bx + 9, by + 8), pcb=(col, 86, 0),
             nets={"1": "+5V", "2": f"REL{n}_SW"}),
        dict(ref=refs["led"], sym="LED", value="rouge", fp="LED_D5.0mm",
             desc="LED témoin relais ON", sch=(bx + 9, by + 12), pcb=(k_x + 9, 90, 90),
             nets={"1": f"REL{n}_SW", "2": f"REL{n}_LED"}),
        dict(ref=f"K{n}", sym="RELAY_SRD", value="SRD-05VDC-SL-C",
             fp="Relay_SPDT_SANYOU_SRD_Series_Form_C",
             desc="Relais 5V SPDT 10A/250VAC (Songle/Sanyou SRD)",
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
               jref: str, x: float):
    """HC-SR04 mono-broche : TRIG piloté direct, ECHO 5V ramené via pont 1k/2k."""
    bx, by = 92, 12 * (idx - 1) + 14
    return [
        dict(ref=rref1, sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc=f"Série écho HC-SR04 {name}", sch=(bx, by), pcb=(x, 129, 0),
             nets={"1": f"US_{name}_ECHO", "2": gpio_net}),
        dict(ref=rref2, sym="R", value="2k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc=f"Pont diviseur écho {name} (5V->3V3)", sch=(bx, by + 4),
             pcb=(x, 133, 0),
             nets={"1": gpio_net, "2": "GND"}),
        dict(ref=jref, sym="CONN_04", value="JST-XH",
             fp="JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
             desc=f"HC-SR04 {name} (1=5V 2=TRIG 3=ECHO 4=GND)",
             sch=(bx + 12, by), pcb=(x, 140, 0),
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
        "19": "SD_MISO",                                          # GPIO12 (SANS pull-up)
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
                      sch=(60, 35), pcb=(100, 110, 0), nets=a1_nets))
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
        "20": "SD_MISO",                               # IO14 (net partagé avec A1-GPIO12)
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
                      sch=(152, 104), pcb=(250, 84, 0), nets=a2_nets))
    # --- Alimentation 5 V ----------------------------------------------------
    comps += [
        dict(ref="J2", sym="BARREL", value="Jack 5.5/2.1", fp="BarrelJack_Horizontal",
             desc="Entrée 5V 3A (jack, centre = +)", sch=(24, 18), pcb=(48, 116, 270),
             nets={"1": "+5V", "2": "GND", "3": "GND"}),
        dict(ref="J1", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Entrée 5V alternative (bornier, 1=+5V 2=GND)",
             sch=(24, 26), pcb=(50, 131, 90),
             nets={"1": "+5V", "2": "GND"}),
        dict(ref="D5", sym="D", value="1N5822", fp="D_DO-201AD_P15.24mm_Horizontal",
             desc="Schottky 3A vers VIN DevKit (anti-retour si USB branché)",
             sch=(24, 34), pcb=(58, 122, 0),
             nets={"1": "VIN_5V", "2": "+5V"}),
        dict(ref="C1", sym="CP", value="1000u/16V", fp="CP_Radial_D10.0mm_P5.00mm",
             desc="Réservoir rail 5V (relais + servos + HC-SR04)",
             sch=(24, 40), pcb=(58, 131, 0),
             nets={"1": "+5V", "2": "GND"}),
        dict(ref="R13", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance LED présence 5V", sch=(24, 46), pcb=(58, 138, 0),
             nets={"1": "+5V", "2": "PWR_LED"}),
        dict(ref="LED5", sym="LED", value="verte", fp="LED_D5.0mm",
             desc="LED présence 5V", sch=(24, 52), pcb=(72, 131, 90),
             nets={"1": "GND", "2": "PWR_LED"}),
    ]
    # --- 4 canaux relais (mapping GPIO = gpio_mapping.h / pins.h) ------------
    comps += relay_channel(1, NET["POMPE_AQUA"], "J3", 58)
    comps += relay_channel(2, NET["POMPE_RESERV"], "J4", 92)
    comps += relay_channel(3, NET["RADIATEURS"], "J5", 126)
    comps += relay_channel(4, NET["LUMIERE"], "J6", 160)
    comps += relay_channel(5, NET["AUX1"], "J23", 194,
                           refs=dict(rb="R28", rp="R29", rl="R30",
                                     q="Q5", d="D6", led="LED6"))
    comps += relay_channel(6, NET["AUX2"], "J24", 228,
                           refs=dict(rb="R31", rp="R32", rl="R33",
                                     q="Q6", d="D7", led="LED7"))
    # --- Servomoteurs nourrisseurs -------------------------------------------
    comps += [
        dict(ref="R20", sym="R", value="220",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Série signal servo gros", sch=(48, 55), pcb=(284, 106, 0),
             nets={"1": NET["SERVO_GROS"], "2": "SERVO_GROS_SIG"}),
        dict(ref="J15", sym="CONN_03", value="Header servo",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Servo GROS (1=SIG 2=+5V 3=GND)", sch=(60, 55), pcb=(302, 110, 0),
             nets={"1": "SERVO_GROS_SIG", "2": "+5V", "3": "GND"}),
        dict(ref="R21", sym="R", value="220",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Série signal servo petits", sch=(48, 65), pcb=(284, 114, 0),
             nets={"1": NET["SERVO_PETITS"], "2": "SERVO_PETITS_SIG"}),
        dict(ref="J16", sym="CONN_03", value="Header servo",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Servo PETITS (1=SIG 2=+5V 3=GND)", sch=(60, 65), pcb=(302, 132, 0),
             nets={"1": "SERVO_PETITS_SIG", "2": "+5V", "3": "GND"}),
        dict(ref="C2", sym="CP", value="470u/16V", fp="CP_Radial_D8.0mm_P3.50mm",
             desc="Découplage rail 5V servos", sch=(48, 71), pcb=(300, 124, 0),
             nets={"1": "+5V", "2": "GND"}),
    ]
    # --- Capteurs ultrason HC-SR04 (mono-broche trig/écho) --------------------
    comps += us_channel(1, "AQUA", NET["ULTRASON_AQUA"], "R14", "R17", "J7", 131)
    comps += us_channel(2, "TANK", NET["ULTRASON_TANK"], "R15", "R18", "J8", 145)
    comps += us_channel(3, "POTA", NET["ULTRASON_POTA"], "R16", "R19", "J9", 159)
    # --- DHT11 / DS18B20 / LDR ------------------------------------------------
    comps += [
        dict(ref="R23", sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up data DHT11", sch=(92, 52), pcb=(170, 120, 0),
             nets={"1": "+3V3_SW", "2": NET["DHT_PIN"]}),
        dict(ref="J10", sym="CONN_03", value="JST-XH",
             fp="JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
             desc="DHT11/DHT22 (1=3V3 2=DATA 3=GND)", sch=(104, 52), pcb=(175, 127, 0),
             nets={"1": "+3V3_SW", "2": NET["DHT_PIN"], "3": "GND"}),
        dict(ref="C3", sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
             desc="Découplage 3V3 capteurs", sch=(92, 56), pcb=(164, 114, 0),
             nets={"1": "+3V3_SW", "2": "GND"}),
        dict(ref="R24", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up bus 1-Wire DS18B20", sch=(92, 64), pcb=(78, 132, 0),
             nets={"1": "+3V3_SW", "2": NET["ONE_WIRE_BUS"]}),
        dict(ref="J11", sym="CONN_03", value="Bornier_5.08",
             fp="TerminalBlock_bornier-3_P5.08mm",
             desc="Sonde DS18B20 étanche (1=3V3 2=DATA 3=GND)",
             sch=(104, 64), pcb=(78, 140, 0),
             nets={"1": "+3V3_SW", "2": NET["ONE_WIRE_BUS"], "3": "GND"}),
        dict(ref="R27", sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Bas du pont diviseur LDR (GPIO34 = entrée seule ADC)",
             sch=(92, 74), pcb=(84, 116, 0),
             nets={"1": NET["LUMINOSITE"], "2": "GND"}),
        dict(ref="J12", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="LDR déportée entre 3V3 et l'ADC (1=3V3 2=ADC)",
             sch=(104, 74), pcb=(78, 124, 0),
             nets={"1": "+3V3_SW", "2": NET["LUMINOSITE"]}),
    ]
    # --- I2C : OLED SSD1306 + extension (DS3231…) -----------------------------
    comps += [
        dict(ref="R25", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up I2C SDA", sch=(92, 84), pcb=(130, 120, 0),
             nets={"1": "+3V3_SW", "2": NET["I2C_SDA"]}),
        dict(ref="R26", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up I2C SCL", sch=(92, 88), pcb=(130, 124, 0),
             nets={"1": "+3V3_SW", "2": NET["I2C_SCL"]}),
        dict(ref="J13", sym="CONN_04", value="Support OLED",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="OLED SSD1306 128x64 I2C 0x3C (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 84), pcb=(94, 129, 0),
             nets={"1": "GND", "2": "+3V3_SW", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
        dict(ref="J14", sym="CONN_04", value="Support I2C ext",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="Extension I2C — ex. module DS3231 (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 92), pcb=(43, 56, 0),
             nets={"1": "GND", "2": "+3V3_SW", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
        dict(ref="C4", sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
             desc="Découplage 3V3 I2C", sch=(92, 96), pcb=(43, 70, 270),
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
        sch=(24, 70), pcb=(312, 120, 0),
        nets={"1": "+3V3", "2": "GND", "3": "EN", "4": "SPARE_RX0",
              "5": "SPARE_TX0", "6": "+5V"}))
    # Distribution d'alimentation supplémentaire : borniers ET header
    comps += [
        dict(ref="J18", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Distribution 5V (1=+5V 2=GND)", sch=(24, 84), pcb=(56, 144, 0),
             nets={"1": "+5V", "2": "GND"}),
        dict(ref="J19", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Distribution 3V3 capteurs — rail COMMUTE (1=+3V3_SW 2=GND)", sch=(24, 90), pcb=(286, 151, 0),
             nets={"1": "+3V3_SW", "2": "GND"}),
        dict(ref="J20", sym="CONN_06", value="Header alim",
             fp="PinHeader_1x06_P2.54mm_Vertical",
             desc="Rail Dupont (1-2=+5V 3-4=GND 5-6=+3V3_SW)", sch=(24, 98), pcb=(298, 142, 0),
             nets={"1": "+5V", "2": "+5V", "3": "GND", "4": "GND",
                   "5": "+3V3_SW", "6": "+3V3_SW"}),
    ]
    # --- Power-gate rail capteurs +3V3_SW (topologie n3pp-msp-commun rev 0.2) --
    comps += [
        dict(ref="R34", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Base commande gate (GPIO13)", sch=(146, 8), pcb=(186, 106, 0),
             nets={"1": "GATE", "2": "GATE_B"}),
        dict(ref="R35", sym="R", value="100k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up grille P-MOSFET (rail OFF par défaut)", sch=(146, 12), pcb=(202, 106, 0),
             nets={"1": "+3V3", "2": "GATE_G"}),
        dict(ref="Q8", sym="NPN", value="BC337-40", fp="TO-92_Inline",
             desc="Driver gate (1=C 2=B 3=E)", sch=(155, 10), pcb=(218, 106, 0),
             nets={"1": "GATE_G", "2": "GATE_B", "3": "GND"}),
        dict(ref="Q7", sym="PMOS_GDS", value="NDP6020P", fp="TO-220-3_Vertical",
             desc="P-MOSFET rail capteurs (1=G 2=D 3=S)", sch=(155, 16), pcb=(232, 106, 0),
             nets={"1": "GATE_G", "2": "+3V3_SW", "3": "+3V3"}),
        dict(ref="JP1", sym="CONN_02", value="Jumper BYPASS",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="BYPASS gate : cavalier 1-2 FERME par défaut (rail permanent, ffp5cs) ; "
                  "l'OTER pour les profils batterie (msp/n3pp, rail commuté par GPIO13)",
             sch=(146, 18), pcb=(242, 106, 0),
             nets={"1": "+3V3", "2": "+3V3_SW"}),
    ]
    # --- Pont diviseur batterie COMMUTE (mesure VBAT, ratio selon profil) -----
    comps += [
        dict(ref="J25", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Sonde batterie (1=VBAT+ 2=GND) — 1S ou bus 12V selon profil",
             sch=(146, 26), pcb=(272, 151, 0),
             nets={"1": "VBAT_SENSE", "2": "GND"}),
        dict(ref="R36", sym="R", value="100k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up grille BS250 (diviseur OFF par défaut)", sch=(146, 30), pcb=(186, 112, 0),
             nets={"1": "VBAT_SENSE", "2": "PDIV_G"}),
        dict(ref="R37", sym="R", value="100k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Base driver diviseur (actif quand +3V3_SW présent)", sch=(146, 34), pcb=(202, 112, 0),
             nets={"1": "+3V3_SW", "2": "PDIV_B"}),
        dict(ref="Q9", sym="PMOS_DGS", value="BS250", fp="TO-92_Inline",
             desc="P-MOSFET coupure haute du diviseur (1=D 2=G 3=S)", sch=(155, 28), pcb=(218, 113, 0),
             nets={"1": "VBAT_SW", "2": "PDIV_G", "3": "VBAT_SENSE"}),
        dict(ref="Q10", sym="NPN", value="BC337-40", fp="TO-92_Inline",
             desc="Driver diviseur (1=C 2=B 3=E)", sch=(155, 34), pcb=(228, 113, 0),
             nets={"1": "PDIV_G", "2": "PDIV_B", "3": "GND"}),
        dict(ref="R38", sym="R", value="100k*",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Haut du pont (VALEUR SELON PROFIL : 1S=100k ; 12V=100k)", sch=(146, 38), pcb=(186, 118, 0),
             nets={"1": "VBAT_SW", "2": "ADC_VBAT"}),
        dict(ref="R39", sym="R", value="27k*",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Bas du pont (VALEUR SELON PROFIL : 1S=100k ; 12V=27k)", sch=(146, 42), pcb=(202, 118, 0),
             nets={"1": "ADC_VBAT", "2": "GND"}),
    ]
    # --- Profil bus 12V : protection + buck externe (fusible lame EN AMONT) ---
    comps += [
        dict(ref="J26", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Entrée bus 12V (1=+12V APRES fusible lame externe 7,5-10A 2=GND)",
             sch=(146, 50), pcb=(216, 151, 0),
             nets={"1": "VBAT12_IN", "2": "GND"}),
        dict(ref="Q11", sym="PMOS_DGS", value="NDP6020P", fp="TO-220-3_Vertical",
             desc="Anti-inversion P-MOSFET (1=D=entrée 2=G 3=S=sortie)", sch=(155, 52), pcb=(232, 146, 0),
             nets={"1": "VBAT12_IN", "2": "QP_G", "3": "VBAT12_PROT"}),
        dict(ref="R40", sym="R", value="100k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Grille anti-inversion vers GND", sch=(146, 54), pcb=(234, 128, 90),
             nets={"1": "QP_G", "2": "GND"}),
        dict(ref="D8", sym="D", value="P6KE18A", fp="D_DO-201AD_P15.24mm_Horizontal",
             desc="TVS 18V transitoires bus batterie", sch=(146, 58), pcb=(212, 140, 0),
             nets={"1": "VBAT12_PROT", "2": "GND"}),
        dict(ref="C5", sym="CP", value="470u/25V", fp="CP_Radial_D10.0mm_P5.00mm",
             desc="Réservoir bus 12V protégé", sch=(146, 62), pcb=(234, 136, 0),
             nets={"1": "VBAT12_PROT", "2": "GND"}),
        dict(ref="J36", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Vers buck IN (module MP1584/XL4015 sur entretoises : 1=+12V 2=GND)",
             sch=(146, 66), pcb=(244, 151, 0),
             nets={"1": "VBAT12_PROT", "2": "GND"}),
        dict(ref="J37", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Depuis buck OUT 5V (1=+5V 2=GND)", sch=(146, 70), pcb=(258, 151, 0),
             nets={"1": "+5V", "2": "GND"}),
    ]
    # --- Profil secteur : Hi-Link 20M05 embarqué (fusible + varistance carte) --
    comps += [
        dict(ref="J27", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="ENTREE SECTEUR 230V (1=L 2=N) — ZONE DANGER", sch=(146, 78), pcb=(250, 46, 0),
             nets={"1": "MAINS_L", "2": "MAINS_N"}),
        dict(ref="F1", sym="FUSE", value="T1A 5x20",
             fp="Fuse_5x20_Horizontal",
             desc="Fusible entrée secteur du module alim (temporisé 1A)", sch=(155, 78), pcb=(266, 44, 270),
             nets={"1": "MAINS_L", "2": "MAINS_LF"}),
        dict(ref="RV1", sym="VARISTOR", value="10D471K",
             fp="CP_Radial_D10.0mm_P5.00mm",
             desc="Varistance 300VAC transitoires secteur", sch=(155, 82), pcb=(253, 62, 0),
             nets={"1": "MAINS_N", "2": "MAINS_LF"}),
        dict(ref="PS1", sym="HLK20M", value="HLK-20M05",
             fp="Converter_ACDC_Hi-Link_HLK-20Mxx",
             desc="Module AC-DC 230V->5V 3,6A (Hi-Link 20M05) — le corps du module "
                  "enjambe la frontière secteur/logique (fente fraisée dessous)",
             sch=(146, 86), pcb=(303, 46, 270),
             nets={"1": "MAINS_LF", "2": "MAINS_N", "3": "GND", "4": "+5V"}),
    ]
    # --- microSD : support module SPI 3V3 + sélection de source par cavaliers --
    comps += [
        dict(ref="J35", sym="CONN_06S", value="Support module microSD",
             fp="PinSocket_1x06_P2.54mm_Vertical",
             desc="Module microSD SPI 3V3 (1=GND 2=VCC 3=MISO 4=MOSI 5=SCK 6=CS)",
             sch=(146, 94), pcb=(204, 124, 0),
             nets={"1": "GND", "2": "+3V3_SW", "3": "SD_MISO",
                   "4": "SD_MOSI", "5": "SD_CLK", "6": "SD_CS"}),
        dict(ref="JP2", sym="CONN_03", value="Jumper SD CS",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Source CS : 1-2 = S3 (IO10, défaut) ; 2-3 = WROOM (US3, env wroom-sd)",
             sch=(155, 92), pcb=(186, 124, 0),
             nets={"1": "SD_CS_S3", "2": "SD_CS", "3": NET["ULTRASON_POTA"]}),
        dict(ref="JP3", sym="CONN_03", value="Jumper SD SCK",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Source SCK : 1-2 = S3 (IO13, défaut) ; 2-3 = WROOM (K5, env wroom-sd)",
             sch=(155, 96), pcb=(191, 124, 0),
             nets={"1": "SD_CLK_S3", "2": "SD_CLK", "3": NET["AUX1"]}),
        dict(ref="JP4", sym="CONN_03", value="Jumper SD MOSI",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Source MOSI : 1-2 = S3 (IO12, défaut) ; 2-3 = WROOM (K6, env wroom-sd)",
             sch=(155, 100), pcb=(196, 124, 0),
             nets={"1": "SD_MOSI_S3", "2": "SD_MOSI", "3": NET["AUX2"]}),
    ]
    # --- Bloc analogique partagé : 4 entrées LDR (msp) / sondes sol (n3pp) ----
    for i, (jref, rref, net, x) in enumerate([
            ("J31", "R43", "ADC_A", 140), ("J32", "R44", "ADC_B", 159),
            ("J33", "R45", "ADC_C", 178), ("J34", "R46", "ADC_D", 197)]):
        comps += [
            dict(ref=jref, sym="CONN_03", value="Bornier_5.08",
                 fp="TerminalBlock_bornier-3_P5.08mm",
                 desc=f"Entrée analogique {net} (1=3V3_SW 2=SIG 3=GND) — LDR msp / sonde sol n3pp",
                 sch=(146, 108 + 4 * i), pcb=(x, 151, 0),
                 nets={"1": "+3V3_SW", "2": net, "3": "GND"}),
            dict(ref=rref, sym="R", value="10k*",
                 fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
                 desc=f"Bas de pont {net} (POSER pour LDR msp ; ABSENT pour sonde sol n3pp)",
                 sch=(155, 108 + 4 * i), pcb=(218, 120 + 4.5 * i, 0),
                 nets={"1": net, "2": "GND"}),
        ]
    # --- Pluie (msp, net US1) et DHT externe (msp, net US2) --------------------
    comps += [
        dict(ref="J29", sym="CONN_03", value="JST-XH",
             fp="JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
             desc="Module pluie DO msp (1=3V3_SW 2=DO 3=GND) — net US1, rôles disjoints",
             sch=(126, 118), pcb=(110, 153, 0),
             nets={"1": "+3V3_SW", "2": NET["ULTRASON_AQUA"], "3": "GND"}),
        dict(ref="J30", sym="CONN_03", value="JST-XH",
             fp="JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
             desc="DHT externe msp (1=3V3_SW 2=DATA 3=GND) — net US2, rôles disjoints",
             sch=(126, 124), pcb=(123, 153, 0),
             nets={"1": "+3V3_SW", "2": NET["ULTRASON_TANK"], "3": "GND"}),
        dict(ref="R47", sym="R", value="10k*",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up DHT ext (POSER sur unités msp uniquement)", sch=(126, 130), pcb=(150, 120, 90),
             nets={"1": "+3V3_SW", "2": NET["ULTRASON_TANK"]}),
    ]
    # --- 4e port I2C (2-3 INA219/226 + DS3231 : J14/J21/J22/J28) --------------
    comps.append(dict(ref="J28", sym="CONN_04", value="Support I2C libre",
                      fp="PinSocket_1x04_P2.54mm_Vertical",
                      desc="Port I2C libre 4 — INA219/226 (1=GND 2=VCC 3=SCL 4=SDA)",
                      sch=(126, 134), pcb=(66, 106, 0),
                      nets={"1": "GND", "2": "+3V3_SW", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}))
    # --- Trous de fixation M3 --------------------------------------------------
    for i, (hx, hy) in enumerate([(45, 45), (313, 108), (45, 155), (310, 146)], 1):
        comps.append(dict(ref=f"H{i}", sym=None, value="M3",
                          fp="MountingHole_3.2mm_M3", desc="Trou de fixation M3",
                          sch=None, pcb=(hx, hy, 0), nets={}))
    return comps


COMPONENTS = build_components()

# Textes explicatifs du schéma : (x_gu, y_gu, texte)
SCH_TEXTS = [
    (18, 12, "ALIMENTATION 5V (3A recommandé) — jack OU bornier.\\nD5 protège le rail si l'USB du DevKit est branché en même temps."),
    (52, 24, "ESP32 DevKit V1 (30 broches, socketé).\\nFlash/monitor via l'USB du module (pio run -e wroom-prod -t upload)."),
    (44, 50, "SERVOS NOURRISSEURS 5V\\nGPIO12 (strapping MTDI) : pull-down R22, signal série R20."),
    (86, 10, "3x HC-SR04 (5V), mode mono-broche TRIG=ECHO (sensor_ultrasonic.cpp).\\nEcho 5V ramené à 3V3 par pont 1k/2k sur chaque canal."),
    (86, 48, "AIR : DHT11 (option DHT22, -DUSE_DHT22) — pull-up 10k."),
    (86, 61, "EAU : DS18B20 (1-Wire, pull-up 4.7k)."),
    (86, 71, "LUMIERE : LDR déportée + 10k (GPIO34 = entrée seule)."),
    (86, 81, "I2C : OLED SSD1306 0x3C + header extension (DS3231 si besoin)."),
    (107, 1, "4 RELAIS 230V 10A — commande ACTIVE HAUT (actuators.h : on() = HIGH).\\nREL1=Pompe aquarium GPIO16, REL2=Pompe réservoir GPIO18,\\nREL3=Chauffage GPIO2, REL4=Lumière GPIO15. Borniers : 1=NC 2=COM 3=NO.\\nZONE SECTEUR ISOLEE sur le PCB (fentes + 3mm mini) — circuit 230V\\nA PROTEGER EN AMONT (fusible/disjoncteur) ; charges inductives : snubber cote charge."),
    (18, 64, "TOUS les GPIO libres sur header (Dupont),\\ny compris RX0/TX0 (les laisser libres pendant le flash USB)\\net GPIO36/39 (entrees seules)."),
    (18, 81, "Distribution 5V / 3V3-capteurs / GND :\\nborniers a vis + rail header."),
    (140, 4, "POWER-GATE +3V3_SW (GPIO13, topologie n3pp-msp rev 0.2) :\\nJP1 FERME par defaut (rail permanent, ffp5cs) ; OTER JP1 sur les\\nprofils batterie -> tout le rail capteurs est coupe en veille."),
    (140, 24, "PONT DIVISEUR VBAT COMMUTE : actif seulement quand +3V3_SW est present.\\nR38/R39 SELON PROFIL : 1S Li-ion = 100k/100k ; bus 12V = 100k/27k."),
    (140, 48, "PROFIL BUS 12V SOLAIRE : fusible lame 7,5-10A EN AMONT (hors carte),\\nanti-inversion P-MOSFET, TVS 18V, reservoir ; buck 12->5V EXTERNE\\n(module MP1584/XL4015 faible Iq sur entretoises, via J36/J37)."),
    (140, 76, "PROFIL SECTEUR : Hi-Link HLK-20M05 EMBARQUE (5V/3,6A) + fusible T1A\\n+ varistance 300VAC. Le corps du module enjambe la frontiere\\nsecteur/logique (fente fraisee dessous). ZONE 230V = DANGER."),
    (140, 92, "microSD : module SPI 3V3 sockete. JP2/3/4 : source des lignes\\nCS/SCK/MOSI = S3 natif (1-2, defaut) ou WROOM env wroom-sd (2-3).\\nMISO = net partage direct (A1-GPIO12 / A2-IO14)."),
    (140, 104, "SITE A2 : ESP32-S3-DevKitC-1 44 broches. UN SEUL module peuple\\n(A1 OU A2). Cartographies PINMAP_UNIVERSAL des 3 firmwares."),
    (120, 114, "ENTREES ANALOGIQUES PARTAGEES ADC_A..D : LDR msp (poser R43-46)\\nou sondes sol n3pp (sans R). ADC_E = HumidSol msp / Luminosite n3pp /\\nLDR ffp5cs. Pluie msp = net US1 ; DHT ext msp = net US2 (roles disjoints)."),
]

# Sérigraphies PCB : (x, y, texte, taille)
PCB_TEXTS = [
    (58, 43, "K1 POMPE AQUA/ARROSAGE", 0.9),
    (92, 43, "K2 POMPE RESERV", 1.0),
    (126, 43, "K3 CHAUFFAGE", 1.0),
    (160, 43, "K4 LUMIERE", 1.0),
    (194, 43, "K5 AUX1", 1.0),
    (228, 43, "K6 AUX2", 1.0),
    (194, 57.5, "NC COM NO", 0.8),
    (228, 57.5, "NC COM NO", 0.8),
    (58, 57.5, "NC COM NO", 0.8),
    (92, 57.5, "NC COM NO", 0.8),
    (126, 57.5, "NC COM NO", 0.8),
    (160, 57.5, "NC COM NO", 0.8),
    (144, 71, "!! ZONE 230V - DANGER - COUPER LE SECTEUR AVANT INTERVENTION !!", 1.3),
    (110, 86.5, f"n3-universal v{REV} — msp / n3pp / ffp5cs", 1.4),
    (134, 146.5, "US AQUA", 1.0),
    (148, 146.5, "US RESERV", 1.0),
    (162, 146.5, "US POTAGER", 1.0),
    (179, 133, "DHT11", 1.0),
    (82, 146.5, "DS18B20", 1.0),
    (82, 119.5, "LDR", 1.0),
    (94, 125.5, "OLED", 1.0),
    (43, 52, "I2C EXT x3", 1.0),
    (266, 55, "SRV GROS", 0.8),
    (266, 67, "SRV PETITS", 0.8),
    (254, 81.5, "GPIO", 0.8),
    (254, 120.5, "ALIM", 0.8),
    (56, 139, "5V", 0.9),
    (268, 121.5, "3V3", 0.9),
    (52, 104, "5V 3A", 1.2),
    (112.7, 101.5, "ANTENNE WIFI : zone degagee (pas de cuivre dessous)", 0.8),
    # Emplacement imposé du numéro de commande JLCPCB (au dos) :
    # sans ce marqueur, le fabricant le place où il veut, parfois
    # sur une étiquette de câblage. Option de commande : "Specify a location".
    (204, 100, "GATE +3V3_SW (GPIO13)", 0.9),
    (238, 111, "JP1 BYPASS: FERME=rail permanent (ffp5cs)", 0.8),
    (238, 114, "OTER pour profils batterie (msp/n3pp)", 0.8),
    (204, 121.5, "PONT DIV VBAT (R38/R39 selon profil)", 0.8),
    (191, 133.5, "JP SD: 1-2=S3 / 2-3=WROOM (wroom-sd)", 0.8),
    (209, 139.5, "SD", 1.0),
    (222, 158, "BUS 12V: FUSIBLE LAME 7,5-10A EN AMONT OBLIGATOIRE", 0.8, "B.SilkS"),
    (272, 147.5, "VBAT SENSE", 0.8),
    (216, 147.5, "12V IN", 0.8),
    (244, 147.5, "BUCK IN", 0.8),
    (258, 147.5, "BUCK OUT 5V", 0.8),
    (145, 147.5, "ADC A", 0.8), (164, 147.5, "ADC B", 0.8),
    (183, 147.5, "ADC C", 0.8), (202, 147.5, "ADC D", 0.8),
    (170, 155.5, "LDR msp (R pose) / SONDE SOL n3pp (sans R)", 0.7, "B.SilkS"),
    (110, 149.5, "PLUIE msp", 0.8),
    (123, 149.5, "DHT EXT msp", 0.8),
    (66, 102, "I2C INA/DS3231", 0.8),
    (261.4, 78, "ANTENNE S3: zone degagee", 0.8),
    (253, 52, "SECTEUR 230V", 1.0),
    (258, 58, "!! DANGER 230V !!", 1.1),
    (294, 102.5, "SERVOS", 0.9),
    (298, 158, "J20 5V/GND/3V3SW", 0.7),
    (250, 156.5, "UN SEUL MODULE : A1 (WROOM) OU A2 (S3)", 0.9, "B.SilkS"),
    (80, 155.5, "JLCJLCJLCJLC", 1.0, "B.SilkS"),
]

BOARD = dict(x0=40, y0=40, x1=318, y1=160)

# Fentes d'isolement (fraisages internes, Edge.Cuts) : entre canaux 230V,
# frontière droite de la zone secteur, et mini-fentes COM<->bobine par canal.
SLOTS = ([(74, 42, 76, 80), (108, 42, 110, 80), (142, 42, 144, 80),
          (176, 42, 178, 80), (210, 42, 212, 80), (245, 42, 247, 82)]
         + [(x + dx - 0.5, 73, x + dx + 0.5, 81)
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


def gen_schematic() -> str:
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
    (property "Description" "{c['desc']}" (at {cx:.2f} {cy:.2f} 0) {hidden})
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
            just = "right" if side == "L" else "left"
            items.append(
                f'  (label "{net}" (at {ex:.2f} {py:.2f} 0)\n'
                f'    (effects (font (size 1.27 1.27)) (justify {just} bottom))\n'
                f'    (uuid "{uid("label", c["ref"], num)}"))')
    for i, (tx, ty, txt) in enumerate(SCH_TEXTS):
        items.append(
            f'  (text "{txt}" (exclude_from_sim no) (at {tx * G:.2f} {ty * G:.2f} 0)\n'
            f'    (effects (font (size 1.7 1.7)) (justify left bottom)) (uuid "{uid("text", i)}"))')
    return f'''(kicad_sch
  (version 20231120)
  (generator "eeschema")
  (generator_version "8.0")
  (uuid "{ROOT_UUID}")
  (paper "A3")
  (title_block
    (title "n3-universal - carte porteuse UNIVERSELLE msp / n3pp / ffp5cs (bi-module WROOM / ESP32-S3)")
    (date "2026-07-07")
    (rev "{REV}")
    (company "salle aeree n3")
    (comment 1 "Genere par hardware/ffp5cs-wroom-prod/generator/generate.py")
    (comment 2 "Source de verite : ffp5cs/include/pins.h (BOARD_WROOM) via pinmap.json")
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


def load_footprint(name: str):
    path = FP_DIR / f"{name}.kicad_mod"
    tree = sx_parse(path.read_text(encoding="utf-8"))
    assert tree[0] == "footprint", name
    # retire version/generator (interdits dans un footprint embarqué en carte)
    tree[:] = [n for n in tree
               if not (isinstance(n, list) and n[0] in ("version", "generator"))]
    return tree


def gen_devkit_footprint() -> str:
    """Empreinte 2x15 supports femelles, entraxe rangées 25.4 mm (DevKit V1 30p)."""
    pads = []
    for n in range(1, 16):   # rangée A (colonne droite, pad 15 en haut)
        pads.append((n, 25.4, (15 - n) * 2.54))
    for n in range(16, 31):  # rangée B (colonne gauche, pad 30 en haut)
        pads.append((n, 0.0, (30 - n) * 2.54))
    pad_s = "\n".join(
        f'  (pad "{n}" thru_hole circle (at {x} {y}) (size 1.7 1.7) (drill 1.0) '
        f'(layers "*.Cu" "*.Mask"))' for n, x, y in pads)
    return f'''(footprint "ESP32_DevKit_V1_30pin"
  (version 20240108)
  (generator "generate.py")
  (layer "F.Cu")
  (descr "ESP32 DevKit V1 30 broches sur 2 supports 1x15 2.54mm, entraxe rangees 25.4mm - VERIFIER sur l'exemplaire reel (variante 36p = autre brochage)")
  (tags "ESP32 DevKit V1")
  (property "Reference" "REF**" (at 12.7 -8.5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (property "Value" "ESP32_DevKit_V1_30pin" (at 12.7 40 0) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -1.9 -7) (end 27.3 39.5) (stroke (width 0.15) (type default)) (layer "F.SilkS"))
  (fp_text user "ANTENNE" (at 12.7 -4.5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_text user "USB" (at 12.7 37.5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -2.4 -7.5) (end 27.8 40) (stroke (width 0.05) (type default)) (layer "F.CrtYd"))
{pad_s}
)
'''


def gen_s3_footprint() -> str:
    """Empreinte 2x22 supports femelles pour ESP32-S3-DevKitC-1 (site A2).
    Entraxe rangées 22.86 mm — VERIFIER sur l'exemplaire réel avant soudure."""
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
  (descr "ESP32-S3-DevKitC-1 44 broches sur 2 supports 1x22 2.54mm, entraxe rangees 22.86mm - VERIFIER sur l'exemplaire reel")
  (tags "ESP32-S3 DevKitC-1")
  (property "Reference" "REF**" (at 11.43 -11.5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (property "Value" "ESP32_S3_DevKitC_1_44pin" (at 11.43 61 0) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -1.4 -10) (end 24.3 59.3) (stroke (width 0.15) (type default)) (layer "F.SilkS"))
  (fp_text user "ANTENNE" (at 11.43 -7 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_text user "USB" (at 11.43 57 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -1.9 -10.5) (end 24.8 59.8) (stroke (width 0.05) (type default)) (layer "F.CrtYd"))
{pad_s}
)
'''


def gen_fuse_footprint() -> str:
    """Porte-fusible 5x20 à souder (2 clips, entraxe 22.5 mm)."""
    return '''(footprint "Fuse_5x20_Horizontal"
  (version 20240108)
  (generator "generate.py")
  (layer "F.Cu")
  (descr "Porte-fusible a souder pour cartouche 5x20mm, entraxe clips 22.5mm")
  (tags "fuse 5x20")
  (property "Reference" "REF**" (at 11.25 -5 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (property "Value" "Fuse_5x20_Horizontal" (at 11.25 5.5 0) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -2.5 -3.4) (end 25 3.4) (stroke (width 0.15) (type default)) (layer "F.SilkS"))
  (fp_rect (start -3 -3.9) (end 25.5 3.9) (stroke (width 0.05) (type default)) (layer "F.CrtYd"))
  (pad "1" thru_hole oval (at 0 0) (size 3 4) (drill oval 1.3 2.6) (layers "*.Cu" "*.Mask"))
  (pad "2" thru_hole oval (at 22.5 0) (size 3 4) (drill oval 1.3 2.6) (layers "*.Cu" "*.Mask"))
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
    """Texte de sérigraphie. entry = (x, y, texte, hauteur[, couche])."""
    x, y, t, s = entry[:4]
    layer = entry[4] if len(entry) > 4 else "F.SilkS"
    h = max(float(s), SILK_MIN_H)
    mirror = " (justify mirror)" if layer.startswith("B.") else ""
    return (f'  (gr_text "{t}" (at {x} {y} 0) (layer "{layer}") '
            f'(uuid "{uid("gtxt", i)}")\n'
            f'    (effects (font (size {h} {h}) '
            f'(thickness {h * SILK_RATIO:.2f})){mirror}))')


def gen_pcb() -> str:
    nets = collect_nets()
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
        for prop in sx_find_all(tree, Sym("property")):
            if prop[1] == "Reference":
                prop[2] = c["ref"]
            elif prop[1] == "Value":
                prop[2] = c["value"]
        for pad_idx, pad in enumerate(sx_find_all(tree, Sym("pad"))):
            net = c["nets"].get(str(pad[1]))
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
        '\n    (polygon (pts (xy 97.6 102.5) (xy 127.8 102.5)'
        ' (xy 127.8 108.5) (xy 97.6 108.5)))'
        '\n  )')
    # Idem pour l'antenne du site A2 (ESP32-S3-DevKitC-1, pads à partir de y=88).
    edge += (
        '\n  (zone (net 0) (net_name "") (layers "F.Cu" "B.Cu")'
        f' (uuid "{uid("antkeepout_s3")}") (hatch edge 0.5)'
        '\n    (keepout (tracks not_allowed) (vias not_allowed) (pads allowed)'
        ' (copperpour not_allowed) (footprints allowed))'
        '\n    (fill (thermal_gap 0.5) (thermal_bridge_width 0.5))'
        '\n    (polygon (pts (xy 247.2 75.5) (xy 275.7 75.5)'
        ' (xy 275.7 83.5) (xy 247.2 83.5)))'
        '\n  )')
    for i, (sx0, sy0, sx1, sy1) in enumerate(SLOTS):
        edge += (f'\n  (gr_rect (start {sx0} {sy0}) (end {sx1} {sy1})\n'
                 f'    (stroke (width 0.1) (type default)) (layer "Edge.Cuts") (uuid "{uid("slot", i)}"))')
    texts = "\n".join(silk_text(i, e) for i, e in enumerate(PCB_TEXTS))
    # zones GND en U : tout le pourtour SAUF le rectangle secteur (44..178, 40..84)
    zx0, zx1, zy = 45.2, 246, 84
    poly = ('(polygon (pts '
            f'(xy {b["x0"]} {b["y0"]}) (xy {zx0} {b["y0"]}) (xy {zx0} {zy}) '
            f'(xy {zx1} {zy}) (xy {zx1} {b["y0"]}) (xy {b["x1"]} {b["y0"]}) '
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
    (date "2026-07-07")
    (rev "{REV}")
    (company "salle aeree n3")
  )
{layers}
  (setup
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


DRU_RULES = """(version 1)
(rule "mains_vs_logic"
  (condition "A.NetClass == 'Mains' && B.NetClass != 'Mains'")
  (constraint clearance (min 3.0mm)))
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
                   for n in ("+5V", "VIN_5V", "GND", "+3V3_SW",
                             "VBAT12_IN", "VBAT12_PROT")])},
        "pcbnew": {"page_layout_descr_file": ""},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [[ROOT_UUID, "Racine"]],
    }, indent=2)


def gen_bom():
    rows = {}
    for c in COMPONENTS:
        key = (c["value"], c["fp"], c["desc"])
        rows.setdefault(key, []).append(c["ref"])
    out = [["Refs", "Qte", "Valeur", "Empreinte", "Description"]]
    for (value, fp, desc), refs in sorted(rows.items(), key=lambda kv: kv[1][0]):
        out.append([" ".join(sorted(refs)), str(len(refs)), value, fp, desc])
    # Pièces sans empreinte propre (montées sur une empreinte existante) :
    # les supports du DevKit doivent apparaître pour être commandés.
    out.append(["A1 (supports)", "2", "Support femelle 1x15 P2.54",
                "monte sur l'empreinte ESP32_DevKit_V1_30pin",
                "Barrettes femelles 15 pts : le DevKit s'enfiche, jamais soudé"])
    return out


def check_pcb_overlaps():
    """Garde-fou grossier : bounding-box des pads + marge, avertit si recouvrement."""
    import math
    boxes = []
    for c in COMPONENTS:
        tree = load_footprint(c["fp"])
        x0, y0, rot = c["pcb"]
        rad = math.radians(rot)
        xs, ys = [], []
        for pad in sx_find_all(tree, Sym("pad")):
            at = sx_find_all(pad, Sym("at"))[0]
            px, py = float(at[1]), float(at[2])
            # convention KiCad (axe y vers le bas) : +90 deg => (px,py)->(py,-px)
            rx = px * math.cos(rad) + py * math.sin(rad)
            ry = -px * math.sin(rad) + py * math.cos(rad)
            xs.append(x0 + rx)
            ys.append(y0 + ry)
        if xs:
            boxes.append((c["ref"], min(xs) - 2, min(ys) - 2, max(xs) + 2, max(ys) + 2))
    warned = []
    for i, (r1, a0, b0, a1, b1) in enumerate(boxes):
        for r2, c0, d0, c1, d1 in boxes[i + 1:]:
            if a0 < c1 and c0 < a1 and b0 < d1 and d0 < b1:
                warned.append((r1, r2))
    return warned


def main():
    KICAD_DIR.mkdir(parents=True, exist_ok=True)
    devkit_fp = FP_DIR / "ESP32_DevKit_V1_30pin.kicad_mod"
    devkit_fp.write_text(gen_devkit_footprint(), encoding="utf-8")
    (FP_DIR / "ESP32_S3_DevKitC_1_44pin.kicad_mod").write_text(
        gen_s3_footprint(), encoding="utf-8")
    (FP_DIR / "Fuse_5x20_Horizontal.kicad_mod").write_text(
        gen_fuse_footprint(), encoding="utf-8")

    sch = gen_schematic()
    pcb = gen_pcb()
    sx_parse(sch)  # auto-validation syntaxique
    sx_parse(pcb)
    (KICAD_DIR / f"{PROJECT}.kicad_sch").write_text(sch, encoding="utf-8")
    (KICAD_DIR / f"{PROJECT}.kicad_pcb").write_text(pcb, encoding="utf-8")
    (KICAD_DIR / f"{PROJECT}.kicad_pro").write_text(gen_project(), encoding="utf-8")
    (KICAD_DIR / f"{PROJECT}.kicad_dru").write_text(DRU_RULES, encoding="utf-8")
    with open(ROOT / "BOM.csv", "w", newline="", encoding="utf-8") as f:
        csv.writer(f, delimiter=";").writerows(gen_bom())

    nets = collect_nets()
    print(f"OK: {len(COMPONENTS)} composants, {len(nets)} nets")
    over = check_pcb_overlaps()
    for r1, r2 in over:
        print(f"  ATTENTION recouvrement possible (pads+2mm) : {r1} / {r2}")


if __name__ == "__main__":
    main()
