#!/usr/bin/env python3
"""Génère le projet KiCad (schéma + PCB + BOM) de la carte porteuse ffp5cs wroom-prod.

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
PROJECT = "ffp5cs-wroom-prod"
REV = "0.5"
NS = uuid.UUID("6ba7b810-9dad-11d1-80b4-00c04fd430c8")
ROOT_UUID = str(uuid.uuid5(NS, PROJECT + "/root"))

PINMAP = json.loads((ROOT / "pinmap.json").read_text(encoding="utf-8"))
NET = PINMAP["netByPin"]

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
    "CONN_10": dict(ref="J", w=3,
                    left=[(str(i), str(i), 5 - i) for i in range(1, 11)]),
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
    return f'''    (symbol "ffp5cs:{name}"
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

def relay_channel(n: int, gpio_net: str, jref: str, k_x: float):
    """Canal relais n : commande GPIO -> transistor -> relais SRD-05 -> bornier."""
    col = 56 + 32 * (n - 1)
    bx, by = 111, 4 + 26 * (n - 1)  # bloc schéma (unités de grille)
    return [
        dict(ref=f"R{n}", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance base transistor", sch=(bx, by), pcb=(col, 85, 0),
             nets={"1": gpio_net, "2": f"REL{n}_B"}),
        dict(ref=f"R{n + 4}", sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-down base (état sûr au boot)", sch=(bx, by + 4), pcb=(col, 90, 0),
             nets={"1": f"REL{n}_B", "2": "GND"}),
        dict(ref=f"R{n + 8}", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance LED témoin", sch=(bx, by + 8), pcb=(col, 95, 0),
             nets={"1": "+5V", "2": f"REL{n}_LED"}),
        dict(ref=f"Q{n}", sym="NPN", value="BC337-40", fp="TO-92_Inline",
             desc="Transistor NPN commande relais (1=C 2=B 3=E)",
             sch=(bx + 9, by + 2), pcb=(col + 14, 92, 0),
             nets={"1": f"REL{n}_SW", "2": f"REL{n}_B", "3": "GND"}),
        dict(ref=f"D{n}", sym="D", value="1N4007",
             fp="D_DO-41_SOD81_P10.16mm_Horizontal",
             desc="Diode de roue libre bobine", sch=(bx + 9, by + 8), pcb=(col, 80, 0),
             nets={"1": "+5V", "2": f"REL{n}_SW"}),
        dict(ref=f"LED{n}", sym="LED", value="rouge", fp="LED_D5.0mm",
             desc="LED témoin relais ON", sch=(bx + 9, by + 12), pcb=(col + 16, 84, 90),
             nets={"1": f"REL{n}_SW", "2": f"REL{n}_LED"}),
        dict(ref=f"K{n}", sym="RELAY_SRD", value="SRD-05VDC-SL-C",
             fp="Relay_SPDT_SANYOU_SRD_Series_Form_C",
             desc="Relais 5V SPDT 10A (Songle/Sanyou SRD)",
             sch=(bx + 19, by + 2), pcb=(k_x, 67, 0),
             nets={"5": "+5V", "2": f"REL{n}_SW", "1": f"REL{n}_COM",
                   "3": f"REL{n}_NO", "4": f"REL{n}_NC"}),
        dict(ref=jref, sym="CONN_03", value="Bornier_5.08",
             fp="TerminalBlock_bornier-3_P5.08mm",
             desc="Bornier charge (1=NO 2=COM 3=NC)",
             sch=(bx + 30, by + 2), pcb=(k_x + 2, 53, 0),
             nets={"1": f"REL{n}_NO", "2": f"REL{n}_COM", "3": f"REL{n}_NC"}),
    ]


def us_channel(idx: int, name: str, gpio_net: str, rref1: str, rref2: str,
               jref: str, x: float):
    """HC-SR04 mono-broche : TRIG piloté direct, ECHO 5V ramené via pont 1k/2k."""
    bx, by = 92, 12 * (idx - 1) + 14
    return [
        dict(ref=rref1, sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc=f"Série écho HC-SR04 {name}", sch=(bx, by), pcb=(x, 124, 0),
             nets={"1": f"US_{name}_ECHO", "2": gpio_net}),
        dict(ref=rref2, sym="R", value="2k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc=f"Pont diviseur écho {name} (5V->3V3)", sch=(bx, by + 4),
             pcb=(x, 128, 0),
             nets={"1": gpio_net, "2": "GND"}),
        dict(ref=jref, sym="CONN_04", value="JST-XH",
             fp="JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
             desc=f"HC-SR04 {name} (1=5V 2=TRIG 3=ECHO 4=GND)",
             sch=(bx + 12, by), pcb=(x, 135, 0),
             nets={"1": "+5V", "2": gpio_net, "3": f"US_{name}_ECHO", "4": "GND"}),
    ]


def build_components():
    comps = []
    # --- Module ESP32 DevKit V1 (sur supports 2×15) --------------------------
    a1_nets = {
        "1": "+3V3", "2": "GND", "3": NET["LUMIERE"], "4": NET["RADIATEURS"],
        "5": NET["ULTRASON_AQUA"], "6": NET["POMPE_AQUA"], "7": "SPARE_GPIO17",
        "8": "SPARE_GPIO5", "9": NET["POMPE_RESERV"], "10": NET["ULTRASON_TANK"],
        "11": NET["I2C_SDA"], "12": None, "13": None, "14": NET["I2C_SCL"],
        "15": "SPARE_GPIO23", "16": "VIN_5V", "17": "GND",
        "18": NET["SERVO_PETITS"], "19": NET["SERVO_GROS"], "20": "SPARE_GPIO14",
        "21": NET["DHT_PIN"], "22": NET["ONE_WIRE_BUS"], "23": "SPARE_GPIO25",
        "24": NET["ULTRASON_POTA"], "25": "SPARE_GPIO32", "26": "SPARE_GPIO35",
        "27": NET["LUMINOSITE"], "28": None, "29": None, "30": "EN",
    }
    comps.append(dict(ref="A1", sym="ESP32_DEVKIT_V1_30", value="ESP32 DevKit V1",
                      fp="ESP32_DevKit_V1_30pin",
                      desc="Module ESP32-WROOM-32 DevKit V1 30 broches, sur 2 supports 1x15",
                      sch=(60, 35), pcb=(100, 104, 0), nets=a1_nets))
    # --- Alimentation 5 V ----------------------------------------------------
    comps += [
        dict(ref="J2", sym="BARREL", value="Jack 5.5/2.1", fp="BarrelJack_Horizontal",
             desc="Entrée 5V 3A (jack, centre = +)", sch=(24, 18), pcb=(48, 108, 270),
             nets={"1": "+5V", "2": "GND", "3": "GND"}),
        dict(ref="J1", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="Entrée 5V alternative (bornier, 1=+5V 2=GND)",
             sch=(24, 26), pcb=(50, 127, 90),
             nets={"1": "+5V", "2": "GND"}),
        dict(ref="D5", sym="D", value="1N5822", fp="D_DO-201AD_P15.24mm_Horizontal",
             desc="Schottky 3A vers VIN DevKit (anti-retour si USB branché)",
             sch=(24, 34), pcb=(58, 118, 0),
             nets={"1": "VIN_5V", "2": "+5V"}),
        dict(ref="C1", sym="CP", value="1000u/16V", fp="CP_Radial_D10.0mm_P5.00mm",
             desc="Réservoir rail 5V (relais + servos + HC-SR04)",
             sch=(24, 40), pcb=(58, 127, 0),
             nets={"1": "+5V", "2": "GND"}),
        dict(ref="R13", sym="R", value="1k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Résistance LED présence 5V", sch=(24, 46), pcb=(58, 134, 0),
             nets={"1": "+5V", "2": "PWR_LED"}),
        dict(ref="LED5", sym="LED", value="verte", fp="LED_D5.0mm",
             desc="LED présence 5V", sch=(24, 52), pcb=(44, 92, 90),
             nets={"1": "GND", "2": "PWR_LED"}),
    ]
    # --- 4 canaux relais (mapping GPIO = gpio_mapping.h / pins.h) ------------
    comps += relay_channel(1, NET["POMPE_AQUA"], "J3", 56)
    comps += relay_channel(2, NET["POMPE_RESERV"], "J4", 88)
    comps += relay_channel(3, NET["RADIATEURS"], "J5", 120)
    comps += relay_channel(4, NET["LUMIERE"], "J6", 152)
    # --- Servomoteurs nourrisseurs -------------------------------------------
    comps += [
        dict(ref="R20", sym="R", value="220",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Série signal servo gros", sch=(48, 55), pcb=(130, 101, 0),
             nets={"1": NET["SERVO_GROS"], "2": "SERVO_GROS_SIG"}),
        dict(ref="R22", sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-down GPIO12 (strapping MTDI : doit être bas au boot)",
             sch=(48, 59), pcb=(130, 109, 0),
             nets={"1": NET["SERVO_GROS"], "2": "GND"}),
        dict(ref="J15", sym="CONN_03", value="Header servo",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Servo GROS (1=SIG 2=+5V 3=GND)", sch=(60, 55), pcb=(178, 58, 0),
             nets={"1": "SERVO_GROS_SIG", "2": "+5V", "3": "GND"}),
        dict(ref="R21", sym="R", value="220",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Série signal servo petits", sch=(48, 65), pcb=(130, 105, 0),
             nets={"1": NET["SERVO_PETITS"], "2": "SERVO_PETITS_SIG"}),
        dict(ref="J16", sym="CONN_03", value="Header servo",
             fp="PinHeader_1x03_P2.54mm_Vertical",
             desc="Servo PETITS (1=SIG 2=+5V 3=GND)", sch=(60, 65), pcb=(178, 70, 0),
             nets={"1": "SERVO_PETITS_SIG", "2": "+5V", "3": "GND"}),
        dict(ref="C2", sym="CP", value="470u/16V", fp="CP_Radial_D8.0mm_P3.50mm",
             desc="Découplage rail 5V servos", sch=(48, 71), pcb=(166, 116, 90),
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
             desc="Pull-up data DHT11", sch=(92, 52), pcb=(172, 112, 0),
             nets={"1": "+3V3", "2": NET["DHT_PIN"]}),
        dict(ref="J10", sym="CONN_03", value="JST-XH",
             fp="JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
             desc="DHT11/DHT22 (1=3V3 2=DATA 3=GND)", sch=(104, 52), pcb=(174, 122, 0),
             nets={"1": "+3V3", "2": NET["DHT_PIN"], "3": "GND"}),
        dict(ref="C3", sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
             desc="Découplage 3V3 capteurs", sch=(92, 56), pcb=(74, 106, 0),
             nets={"1": "+3V3", "2": "GND"}),
        dict(ref="R24", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up bus 1-Wire DS18B20", sch=(92, 64), pcb=(78, 116, 0),
             nets={"1": "+3V3", "2": NET["ONE_WIRE_BUS"]}),
        dict(ref="J11", sym="CONN_03", value="Bornier_5.08",
             fp="TerminalBlock_bornier-3_P5.08mm",
             desc="Sonde DS18B20 étanche (1=3V3 2=DATA 3=GND)",
             sch=(104, 64), pcb=(78, 135, 0),
             nets={"1": "+3V3", "2": NET["ONE_WIRE_BUS"], "3": "GND"}),
        dict(ref="R27", sym="R", value="10k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Bas du pont diviseur LDR (GPIO34 = entrée seule ADC)",
             sch=(92, 74), pcb=(84, 112, 0),
             nets={"1": NET["LUMINOSITE"], "2": "GND"}),
        dict(ref="J12", sym="CONN_02", value="Bornier_5.08",
             fp="TerminalBlock_bornier-2_P5.08mm",
             desc="LDR déportée entre 3V3 et l'ADC (1=3V3 2=ADC)",
             sch=(104, 74), pcb=(78, 124, 0),
             nets={"1": "+3V3", "2": NET["LUMINOSITE"]}),
    ]
    # --- I2C : OLED SSD1306 + extension (DS3231…) -----------------------------
    comps += [
        dict(ref="R25", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up I2C SDA", sch=(92, 84), pcb=(48, 72, 270),
             nets={"1": "+3V3", "2": NET["I2C_SDA"]}),
        dict(ref="R26", sym="R", value="4.7k",
             fp="R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
             desc="Pull-up I2C SCL", sch=(92, 88), pcb=(44, 72, 270),
             nets={"1": "+3V3", "2": NET["I2C_SCL"]}),
        dict(ref="J13", sym="CONN_04", value="Support OLED",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="OLED SSD1306 128x64 I2C 0x3C (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 84), pcb=(94, 124, 0),
             nets={"1": "GND", "2": "+3V3", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
        dict(ref="J14", sym="CONN_04", value="Support I2C ext",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="Extension I2C — ex. module DS3231 (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 92), pcb=(48, 60, 0),
             nets={"1": "GND", "2": "+3V3", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
        dict(ref="C4", sym="C", value="100n", fp="C_Disc_D5.0mm_W2.5mm_P5.00mm",
             desc="Découplage 3V3 I2C", sch=(92, 96), pcb=(44, 60, 90),
             nets={"1": "+3V3", "2": "GND"}),
        dict(ref="J18", sym="CONN_04", value="Support I2C libre",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="Port I2C libre 2 (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 100), pcb=(171, 100, 0),
             nets={"1": "GND", "2": "+3V3", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
        dict(ref="J19", sym="CONN_04", value="Support I2C libre",
             fp="PinSocket_1x04_P2.54mm_Vertical",
             desc="Port I2C libre 3 (1=GND 2=VCC 3=SCL 4=SDA)",
             sch=(104, 108), pcb=(158, 100, 0),
             nets={"1": "GND", "2": "+3V3", "3": NET["I2C_SCL"], "4": NET["I2C_SDA"]}),
    ]
    # --- GPIO libres + EN -----------------------------------------------------
    comps.append(dict(
        ref="J17", sym="CONN_10", value="Header GPIO libres",
        fp="PinHeader_1x10_P2.54mm_Vertical",
        desc="GPIO libres (1=3V3 2=GND 3=EN 4=IO5 5=IO14 6=IO17 7=IO23 8=IO25 9=IO32 10=IO35)",
        sch=(24, 68), pcb=(178, 82, 0),
        nets={"1": "+3V3", "2": "GND", "3": "EN", "4": "SPARE_GPIO5",
              "5": "SPARE_GPIO14", "6": "SPARE_GPIO17", "7": "SPARE_GPIO23",
              "8": "SPARE_GPIO25", "9": "SPARE_GPIO32", "10": "SPARE_GPIO35"}))
    # --- Trous de fixation M3 --------------------------------------------------
    for i, (hx, hy) in enumerate([(45, 50), (185, 50), (45, 140), (185, 140)], 1):
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
    (107, 1, "4 RELAIS 5V — commande ACTIVE HAUT (actuators.h : on() = digitalWrite HIGH).\\nREL1=Pompe aquarium GPIO16, REL2=Pompe réservoir GPIO18,\\nREL3=Chauffage GPIO2, REL4=Lumière GPIO15.\\nGPIO2/GPIO15 = broches de strapping : pull-down base 10k = état sûr au boot."),
    (18, 62, "GPIO libres pour évolutions."),
]

# Sérigraphies PCB : (x, y, texte, taille)
PCB_TEXTS = [
    (63, 48.5, "REL1 POMPE AQUA", 1.0),
    (95, 48.5, "REL2 POMPE RESERV", 1.0),
    (127, 48.5, "REL3 CHAUFFAGE", 1.0),
    (159, 48.5, "REL4 LUMIERE", 1.0),
    (63, 61.5, "NO COM NC", 0.8),
    (95, 61.5, "NO COM NC", 0.8),
    (127, 61.5, "NO COM NC", 0.8),
    (159, 61.5, "NO COM NC", 0.8),
    (134, 141.5, "US AQUA", 1.0),
    (148, 141.5, "US RESERV", 1.0),
    (162, 141.5, "US POTAGER", 1.0),
    (174, 117.5, "DHT11", 1.0),
    (82, 141.5, "DS18B20", 1.0),
    (82, 119.5, "LDR", 1.0),
    (94, 120, "OLED", 1.0),
    (48, 56.5, "I2C EXT", 1.0), (164, 97, "I2C EXT x2", 1.0),
    (183, 58, "SRV GROS", 0.8),
    (183, 70, "SRV PETITS", 0.8),
    (184, 93, "GPIO", 0.8),
    (52, 100, "5V 3A", 1.2),
    (115, 47, f"ffp5cs wroom-prod carrier v{REV}", 1.5),
    (112.7, 95.5, "ANTENNE WIFI : zone degagee (pas de cuivre dessous)", 0.8),
]

BOARD = dict(x0=40, y0=45, x1=190, y1=145)


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
        items.append(f'''  (symbol (lib_id "ffp5cs:{c['sym']}") (at {cx:.2f} {cy:.2f} 0) (unit 1)
    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)
    (uuid "{u}")
    (property "Reference" "{c['ref']}" (at {cx:.2f} {cy - top_mm - 2.54:.2f} 0) {font})
    (property "Value" "{c['value']}" (at {cx:.2f} {cy - top_mm - 0.4:.2f} 0) {font})
    (property "Footprint" "ffp5cs:{c['fp']}" (at {cx:.2f} {cy:.2f} 0) {hidden})
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
    (title "ffp5cs wroom-prod - carte porteuse ESP32 DevKit V1")
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


def strip_uuids(node):
    """Retire récursivement les (uuid ...) : les empreintes clonées N fois
    doivent recevoir des UUID uniques, sinon le DRC apparie des objets fantômes."""
    if isinstance(node, list):
        node[:] = [c for c in node
                   if not (isinstance(c, list) and c and c[0] == "uuid")]
        for c in node:
            strip_uuids(c)


def gen_pcb() -> str:
    nets = collect_nets()
    net_no = {n: i + 1 for i, n in enumerate(nets)}
    net_decl = "\n".join(f'  (net {i + 1} "{n}")' for i, n in enumerate(nets))
    fp_blocks = []
    for c in COMPONENTS:
        tree = load_footprint(c["fp"])
        strip_uuids(tree)
        tree[1] = f'ffp5cs:{tree[1]}'
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
        '\n    (polygon (pts (xy 97.6 96.5) (xy 127.8 96.5)'
        ' (xy 127.8 102.5) (xy 97.6 102.5)))'
        '\n  )')
    texts = "\n".join(
        f'  (gr_text "{t}" (at {x} {y} 0) (layer "F.SilkS") (uuid "{uid("gtxt", i)}")\n'
        f'    (effects (font (size {s} {s}) (thickness {s * 0.15:.2f}))))'
        for i, (x, y, t, s) in enumerate(PCB_TEXTS))
    poly = (f'(polygon (pts (xy {b["x0"]} {b["y0"]}) (xy {b["x1"]} {b["y0"]}) '
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
    (title "ffp5cs wroom-prod - carte porteuse ESP32 DevKit V1")
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
            dict(_NC, name="Relais", track_width=2.0, clearance=0.5,
                 via_diameter=1.6, via_drill=0.8),
            dict(_NC, name="Alim", track_width=1.2, clearance=0.2,
                 via_diameter=1.0, via_drill=0.5),
        ], "meta": {"version": 3},
            "netclass_patterns": (
                [{"netclass": "Relais", "pattern": f"REL{i}_{c}"}
                 for i in range(1, 5) for c in ("COM", "NO", "NC")]
                + [{"netclass": "Alim", "pattern": n}
                   for n in ("+5V", "VIN_5V", "GND")])},
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

    sch = gen_schematic()
    pcb = gen_pcb()
    sx_parse(sch)  # auto-validation syntaxique
    sx_parse(pcb)
    (KICAD_DIR / f"{PROJECT}.kicad_sch").write_text(sch, encoding="utf-8")
    (KICAD_DIR / f"{PROJECT}.kicad_pcb").write_text(pcb, encoding="utf-8")
    (KICAD_DIR / f"{PROJECT}.kicad_pro").write_text(gen_project(), encoding="utf-8")
    with open(ROOT / "BOM.csv", "w", newline="", encoding="utf-8") as f:
        csv.writer(f, delimiter=";").writerows(gen_bom())

    nets = collect_nets()
    print(f"OK: {len(COMPONENTS)} composants, {len(nets)} nets")
    over = check_pcb_overlaps()
    for r1, r2 in over:
        print(f"  ATTENTION recouvrement possible (pads+2mm) : {r1} / {r2}")


if __name__ == "__main__":
    main()
