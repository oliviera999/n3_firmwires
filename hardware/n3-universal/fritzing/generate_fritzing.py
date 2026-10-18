#!/usr/bin/env python3
"""Vues de câblage Fritzing de la carte n3-universal — une par profil (msp1, n3pp,
ffp5cs 230 V, ffp5cs 12 V).

Chaque `.fzz` contient :
  - la carte dessinée À L'ÉCHELLE depuis le PCB routé (`kicad/n3-universal.kicad_pcb`, lu
    sans KiCad par `kicad_pcb_geom.py`) : chaque connecteur à sa vraie place, composants
    posés en couleur / non posés en gris, cavaliers dessinés dans leur position du profil
    (`achats/inventaire.JUMPER_STATE`), zone 230 V hachurée ;
  - les périphériques et sources d'alimentation du profil (boîtes étiquetées, broches
    nommées — d'après `achats/inventaire.OFF`) reliés aux bons borniers par des fils colorés
    (rouge +5 V, orange 3,3 V, noir GND, rouge sombre 12 V, brun L / bleu N en 230 V) ;
  - une légende (cavaliers, non posés, options, règles de sécurité).

Sources de vérité : `generator/generate.py` (composants, nets, descriptions), le PCB routé
(positions), `achats/inventaire.py` (pose par profil, périphériques, cavaliers). Le câblage
externe est décrit ici (`WIRING`) — c'est la seule information propre à ce fichier.

Usage (depuis hardware/n3-universal) :
  python3 fritzing/generate_fritzing.py            # écrit les 4 .fzz + cablage.csv (photographie)
  python3 fritzing/generate_fritzing.py --check    # CI : code 1 si cablage.csv ne suit plus
                                                   #   (BOM / inventaire / câblage changés sans régénérer)
  python3 fritzing/generate_fritzing.py --render   # + export des vues par Fritzing (xvfb) et PNG d'aperçu

Format .fzz : sketch Fritzing 1.0 ; les pièces maison sont embarquées dans le zip
(`part.<id>.fzp` + `svg.<vue>.<id>_<vue>.svg`), comme le fait Fritzing en enregistrant un
sketch qui contient des pièces non « core ». Validé par export headless (`--render`).
"""
from __future__ import annotations

import csv
import io
import math
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from collections import defaultdict
from pathlib import Path
from xml.sax.saxutils import escape

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "generator"))
sys.path.insert(0, str(ROOT / "achats"))

import generate as G  # noqa: E402
import inventaire as I  # noqa: E402
from kicad_pcb_geom import load_footprints  # noqa: E402

PCB = ROOT / "kicad" / f"{G.PROJECT}.kicad_pcb"
SNAP = HERE / "cablage.csv"
PX = 90 / 25.4  # px Fritzing (90 dpi) par mm
FILE = {"msp1": "n3-universal-msp1", "n3pp": "n3-universal-n3pp",
        "ffp_230": "n3-universal-ffp5cs-230v", "ffp_12v": "n3-universal-ffp5cs-12v"}

# ----------------------------------------------------------------------------- couleurs de fils
C5V, C3V3, CGND, C12V, CL, CN = "#d62828", "#f77f00", "#303030", "#8b0000", "#7b4a12", "#1d4ed8"
CSIG = ["#2a9d8f", "#6a4c93", "#e76f51", "#1982c4", "#8ac926", "#ff924c", "#c9184a", "#5e548e",
        "#52b788", "#0077b6", "#b5838d", "#9c6644"]

# ----------------------------------------------------------------------------- périphériques
# kind -> (titre, [broches], couleur de fond, largeur mini px)
KINDS = {
    "DHT11": ("DHT11", ["+", "DATA", "−"], "#cfe8ff", 70),
    "DS18B20": ("DS18B20 étanche", ["VDD rouge", "DATA jaune", "GND noir"], "#e2e2e2", 110),
    "LDR": ("LDR", ["1", "2"], "#fff3bf", 50),
    "PLUIE": ("Module pluie (FC-37)", ["VCC", "GND", "DO"], "#cfe8ff", 100),
    "SOL_CAPA": ("Sol capacitif v1.2", ["VCC", "GND", "AOUT"], "#d8f3dc", 100),
    "HCSR04": ("HC-SR04", ["VCC", "TRIG", "ECHO", "GND"], "#cfe8ff", 90),
    "SG90": ("Servo SG90", ["marron GND", "rouge 5V", "orange SIG"], "#ffe0b2", 120),
    "OLED": ("OLED SSD1306", ["GND", "VCC", "SCL", "SDA"], "#d0d0d0", 100),
    "DS3231": ("RTC DS3231 (ZS-042)", ["GND", "VCC", "SDA", "SCL"], "#cfe8ff", 110),
    "INA226": ("INA226", ["VCC", "GND", "SCL", "SDA", "IN+", "IN−"], "#cfe8ff", 120),
    "TP4056": ("TP4056 (protégé)", ["IN+", "IN−", "B+", "B−", "OUT+", "OUT−"], "#cfe8ff", 130),
    "BOOST": ("Boost MT3608 réglé 5,1 V", ["VIN+", "VIN−", "VOUT+", "VOUT−"], "#cfe8ff", 130),
    "CELL_18650": ("18650 Li-ion", ["+", "−"], "#d8f3dc", 80),
    "PANNEAU_6V": ("Panneau 6 V", ["+", "−"], "#bde0fe", 80),
    "CHARGEUR_5V": ("Chargeur 5 V 2 A (jack)", ["+ centre", "−"], "#ffd6a5", 120),
    "BUCK": ("Buck MP1584 réglé 5,1 V", ["IN+", "IN−", "OUT+", "OUT−"], "#cfe8ff", 130),
    "FUSIBLE_LAME": ("Fusible lame 10 A", ["1", "2"], "#ffadad", 90),
    "AGM_12V": ("Batterie AGM 12 V", ["+", "−"], "#d8f3dc", 100),
    "PWM_SOLAIRE": ("Régulateur PWM", ["PV+", "PV−", "BAT+", "BAT−"], "#bde0fe", 110),
    "PANNEAU_12V": ("Panneau 12 V", ["+", "−"], "#bde0fe", 80),
    "CORDON_SECTEUR": ("Cordon secteur 230 V", ["L", "N"], "#ffadad", 110),
    "1N4007_EXT": ("1N4007 roue libre", ["K", "A"], "#e2e2e2", 90),
    "POMPE_DC": ("Pompe 12 V", ["+", "−"], "#caffbf", 80),
    "POMPE_AC": ("Pompe 230 V", ["~", "~"], "#ffadad", 80),
    "CHARGE_AC": ("Charge 230 V", ["~", "~"], "#ffadad", 90),
    "CHARGE_DC": ("Charge 12 V", ["+", "−"], "#caffbf", 80),
    "ALIM_POMPE": ("Alim. pompe", ["+", "−"], "#ffd6a5", 90),
    "BUS12": ("Bus 12 V (répartiteur)", ["+", "−"], "#ffd6a5", 110),
}
# articles d'inventaire.OFF qui ne se dessinent pas (fournitures, options rangées dans la légende)
NOT_DRAWN = {"BME280", "MICROSD", "BOITIER", "PRESSE_ETOUPE", "FIL_05", "FIL_15", "CR2032", "FUS_T1A",
             "SUPPORT_18650", "CABLE_USB_JACK"}
# kinds dessinés qui n'ont pas d'article d'achat (charges du client)
FREE_KINDS = {"POMPE_DC", "POMPE_AC", "CHARGE_AC", "CHARGE_DC", "ALIM_POMPE", "BUS12"}


def D(key, kind, label, pins, side=None, art=None):
    """Périphérique : key unique, kind (KINDS), libellé, broches -> (ref, pad, couleur) ou autre périph
    (ou liste de cibles pour une broche reliée à plusieurs endroits)."""
    pins = {k: (v if isinstance(v, list) else [v]) for k, v in pins.items()}
    return dict(key=key, kind=kind, label=label, pins=pins, side=side, art=art or kind)


def targets(d):
    """[(pin, ref, pad, couleur)] d'un périphérique."""
    return [(pin, r, p, c) for pin, lst in d["pins"].items() for (r, p, c) in lst]


I2C = lambda ref: {"GND": (ref, "1", CGND), "VCC": (ref, "2", C3V3), "SCL": (ref, "3", CSIG[3]), "SDA": (ref, "4", CSIG[0])}  # noqa: E731


def sensors_common(us=True, ds=True, ldr=True, servos=True, rtc=False):
    d = [
        D("dht_int", "DHT11", "DHT11 intérieur", {"+": ("J10", "1", C3V3), "DATA": ("J10", "2", CSIG[1]), "−": ("J10", "3", CGND)}),
        D("oled", "OLED", "OLED 0,96\" (vérifier l'ordre des broches)", I2C("J13")),
    ]
    if ds:
        d.append(D("ds18", "DS18B20", "DS18B20 eau/sol", {"VDD rouge": ("J11", "1", C3V3), "DATA jaune": ("J11", "2", CSIG[2]), "GND noir": ("J11", "3", CGND)}))
    if ldr:
        d.append(D("ldr_e", "LDR", "LDR luminosité", {"1": ("J12", "1", C3V3), "2": ("J12", "2", CSIG[4])}))
    if us:
        for k, (ref, nom) in enumerate((("J7", "AQUARIUM"), ("J8", "RÉSERVOIR"), ("J9", "POTAGER"))):
            d.append(D(f"us_{k}", "HCSR04", f"HC-SR04 {nom}", {"VCC": (ref, "1", C5V), "TRIG": (ref, "2", CSIG[5 + k]), "ECHO": (ref, "3", CSIG[8 + k]), "GND": (ref, "4", CGND)}))
    if servos:
        for k, (ref, nom) in enumerate((("J15", "GROS"), ("J16", "PETITS"))):
            d.append(D(f"srv_{k}", "SG90", f"Servo {nom}", {"marron GND": (ref, "3", CGND), "rouge 5V": (ref, "2", C5V), "orange SIG": (ref, "1", CSIG[6 + k])}))
    if rtc:
        d.append(D("rtc", "DS3231", "RTC DS3231 — SDA/SCL croisés vs J14", {"GND": ("J14", "1", CGND), "VCC": ("J14", "2", C3V3), "SDA": ("J14", "4", CSIG[0]), "SCL": ("J14", "3", CSIG[3])}))
    return d


def relay_loads_ac(chan):
    """Charges 230 V : L du cordon -> COM ; NO -> charge ; charge -> N du cordon."""
    out = []
    for k, (ref, kind, nom) in enumerate(chan):
        out.append(D(f"ld_{ref}", kind, nom, {"~": (ref, "3", CL), "~ ": ("cordon", "N", CN)}, side="top"))
    return out


WIRING = {
    "msp1": sensors_common(us=False, ds=True, ldr=False, servos=True) + [
        D("dht_ext", "DHT11", "DHT11 extérieur (JP21 fermé)", {"+": ("J30", "1", C3V3), "DATA": ("J30", "2", CSIG[7]), "−": ("J30", "3", CGND)}),
        D("pluie", "PLUIE", "Pluie (sortie DO)", {"VCC": ("J29", "1", C3V3), "GND": ("J29", "3", CGND), "DO": ("J29", "2", CSIG[9])}),
        D("sol", "SOL_CAPA", "Humidité du sol (AO) — GND sur J11", {"VCC": ("J12", "1", C3V3), "AOUT": ("J12", "2", CSIG[4]), "GND": ("J11", "3", CGND)}),
        *[D(f"ldr_{k}", "LDR", f"LDR tracker {'ABCD'[k]}", {"1": (ref, "1", C3V3), "2": (ref, "2", CSIG[k])})
          for k, ref in enumerate(("J31", "J32", "J33", "J34"))],
        D("panneau", "PANNEAU_6V", "Panneau 6 V 3,8–6 W", {"+": ("tp4056", "IN+", C5V), "−": ("tp4056", "IN−", CGND)}, side="left"),
        D("tp4056", "TP4056", "TP4056 avec protection", {"B+": ("cell", "+", C5V), "B−": ("cell", "−", CGND), "OUT+": ("boost", "VIN+", C5V), "OUT−": ("boost", "VIN−", CGND)}, side="left"),
        D("cell", "CELL_18650", "18650 (à l'ombre)", {"+": ("J25", "1", CSIG[10]), "−": ("J25", "2", CGND)}, side="left"),
        D("boost", "BOOST", "Boost 5,1 V → J1", {"VOUT+": ("J1", "1", C5V), "VOUT−": ("J1", "2", CGND)}, side="left"),
    ],
    "n3pp": sensors_common(us=False, ds=False, ldr=True, servos=False) + [
        *[D(f"sol_{k}", "SOL_CAPA", f"Sol capacitif {k + 1}", {"VCC": (ref, "1", C3V3), "GND": (ref, "3", CGND), "AOUT": (ref, "2", CSIG[k])})
          for k, ref in enumerate(("J31", "J32", "J33", "J34"))],
        D("chargeur", "CHARGEUR_5V", "Chargeur 5 V 2 A, cordon court → jack J2", {"+ centre": ("J2", "1", C5V), "−": ("J2", "2", CGND)}, side="left"),
        D("alim_pompe", "ALIM_POMPE", "Alim. de la pompe (12 V ou 230 V)", {"+": ("J3", "2", C12V), "−": ("pompe", "−", CGND)}, side="top"),
        D("pompe", "POMPE_DC", "Pompe d'arrosage (K1)", {"+": ("J3", "3", C12V)}, side="top"),
        D("rl_pompe", "1N4007_EXT", "Roue libre si pompe DC (K côté +)", {"K": ("pompe", "+", C12V), "A": ("pompe", "−", CGND)}, side="top"),
    ],
    "ffp_230": sensors_common(rtc=True) + [
        D("cordon", "CORDON_SECTEUR", "Cordon 230 V → J27 (1=N 2=L) — via répartiteur Wago", {"L": [("J27", "2", CL), ("J3", "2", CL)], "N": ("J27", "1", CN)}, side="top"),
        *relay_loads_ac((("J3", "POMPE_AC", "Pompe aquarium (K1)"), ("J4", "POMPE_AC", "Pompe réservoir (K2)"),
                         ("J5", "CHARGE_AC", "Chauffage (K3)"), ("J6", "CHARGE_AC", "Lumière / UV (K4)"))),
    ],
    "ffp_12v": sensors_common(rtc=True) + [
        D("bus", "BUS12", "Bus 12 V après fusible (Wago) → COM des relais", {"+": ("J3", "2", C12V), "−": ("J26", "2", CGND)}, side="top"),
        D("p_aq", "POMPE_DC", "Pompe aquarium 12 V (K1)", {"+": ("J3", "3", C12V), "−": ("bus", "−", CGND)}, side="top"),
        D("p_res", "POMPE_DC", "Pompe réservoir 12 V (K2)", {"+": ("J4", "3", C12V), "−": ("bus", "−", CGND)}, side="top"),
        D("lum", "CHARGE_DC", "Lumière 12 V (K4)", {"+": ("J6", "3", C12V), "−": ("bus", "−", CGND)}, side="top"),
        D("fus", "FUSIBLE_LAME", "Fusible lame 7,5–10 A au plus près de la batterie", {"2": ("bus", "+", C12V)}, side="top"),
        D("agm", "AGM_12V", "AGM 12 V", {"+": ("fus", "1", C12V), "−": ("bus", "−", CGND)}, side="top"),
        D("pwm", "PWM_SOLAIRE", "Régulateur PWM — carte sur BATTERIE, pas sur LOAD", {"BAT+": ("agm", "+", C12V), "BAT−": ("agm", "−", CGND)}, side="top"),
        D("panneau12", "PANNEAU_12V", "Panneau 12 V", {"+": ("pwm", "PV+", C12V), "−": ("pwm", "PV−", CGND)}, side="top"),
        D("ina", "INA226", "INA226 batterie (0x40) — shunt du module en série sur le +", {**I2C("J21"), "IN+": ("bus", "+", C12V), "IN−": ("J26", "1", C12V)}, side="left"),
        D("buck", "BUCK", "Buck J36 → J37", {"IN+": ("J36", "1", C12V), "IN−": ("J36", "2", CGND), "OUT+": ("J37", "1", C5V), "OUT−": ("J37", "2", CGND)}, side="right"),
        D("rl_aq", "1N4007_EXT", "Roue libre pompe aquarium", {"K": ("p_aq", "+", C12V), "A": ("p_aq", "−", CGND)}, side="top"),
        D("rl_res", "1N4007_EXT", "Roue libre pompe réservoir", {"K": ("p_res", "+", C12V), "A": ("p_res", "−", CGND)}, side="top"),
    ],
}
# fils supplémentaires entre deux broches de la carte elle-même
BOARD_WIRES = {
    "n3pp": [(("J25", "1"), ("J18", "1"), CSIG[10], "J25-1 → +5V : sinon « batterie vide » et veille d'urgence")],
    "ffp_12v": [(("J4", "2"), ("J3", "2"), C12V, ""), (("J6", "2"), ("J4", "2"), C12V, "")],
    "ffp_230": [(("J4", "2"), ("J3", "2"), CL, ""), (("J5", "2"), ("J4", "2"), CL, ""), (("J6", "2"), ("J5", "2"), CL, "")],
}
LEGENDE = {
    "msp1": ["Alimentation : panneau 6 V → TP4056 → 18650 → boost 5,1 V → J1.",
             "Servos, OLED et capteurs coupés en sommeil (JP1 parqué).",
             "Options non dessinées : BME280 ×2 (0x76 int., 0x77 ext.) sur J21 / J14."],
    "n3pp": ["Jamais d'alimentation par le micro-USB du DevKit : jack J2.",
             "Relier J25-1 au +5V (J18-1) : sinon le firmware lit une batterie vide.",
             "Option non dessinée : BME280 (0x76) sur J21."],
    "ffp_230": ["ZONE 230 V : boîtier isolant fermé, différentiel 30 mA, supervision adulte.",
                "H1 et H5 : vis NYLON. J1 seulement sur table, jamais avec le secteur.",
                "K5 / K6 (J23 / J24) libres. Options : microSD (S3 seulement), INA226.",
                "COM des relais (J3-2 … J6-2) pontés entre eux par un fil 1,5 mm² : la phase L arrive sur J3-2."],
    "ffp_12v": ["Carte sur les bornes BATTERIE du régulateur, jamais sur LOAD.",
                "Pompes 12 V sur les CONTACTS des relais ; diode de roue libre sur chaque pompe.",
                "K3 chauffage : interdit sur batterie (firmware). Options : INA226 ×2 (0x40 / 0x44), microSD.",
                "COM des relais (J3-2, J4-2, J6-2) pontés entre eux par un fil 1,5 mm² depuis le bus +12 V."],
}

# ----------------------------------------------------------------------------- carte (SVG)
FAMILY_STYLE = {  # couleur de corps par famille d'empreinte
    "TerminalBlock": ("#1b7f3b", "#0d4d22"), "PinHeader": ("#222222", "#000000"), "PinSocket": ("#333333", "#000000"),
    "Relay": ("#1d3557", "#0b1a2e"), "Converter_ACDC": ("#111111", "#000000"), "ESP32_DevKit": ("#2b2d42", "#000000"),
    "ESP32_S3": ("#2b2d42", "#000000"), "TO-220": ("#2f2f2f", "#000000"), "TO-92": ("#2f2f2f", "#000000"),
    "CP_Radial": ("#2a4d9b", "#1b2f66"), "C_Disc": ("#d9a441", "#8a6a20"), "R_Axial": ("#d2b48c", "#8a6a20"),
    "D_DO": ("#1a1a1a", "#000000"), "LED": ("#c1121f", "#7a0a14"), "Fuse": ("#8d99ae", "#4a5568"),
    "RV_Disc": ("#1f6f8b", "#0e3a4a"), "BarrelJack": ("#111111", "#000000"), "TestPoint": ("#b8860b", "#7a5a08"),
    "MountingHole": (None, "#777777"),
}


def family(fp):
    for k in FAMILY_STYLE:
        if fp.startswith(k) or k in fp:
            return k
    return None


def board_svg(prof, fps, conn_index):
    """SVG breadboard de la carte pour le profil : (svg, {(ref,pad): (x_px, y_px)})."""
    B = G.BOARD
    x0, y0, W, H = B["x0"], B["y0"], B["x1"] - B["x0"], B["y1"] - B["y0"]
    S = 10  # unités SVG par mm
    pop = I.POP[prof]
    jp = I.JUMPER_STATE[prof]
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="{W}mm" height="{H}mm" viewBox="0 0 {W * S} {H * S}">',
         '<g id="breadboard">',
         f'<rect x="0" y="0" width="{W * S}" height="{H * S}" rx="30" ry="30" fill="#2e7d32" stroke="#1b5e20" stroke-width="6"/>',
         '<defs><pattern id="hz" width="40" height="40" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
         '<rect width="40" height="40" fill="#ffcccc" fill-opacity="0.25"/><line x1="0" y1="0" x2="0" y2="40" stroke="#c1121f" stroke-width="6" stroke-opacity="0.5"/></pattern></defs>']
    # zone 230 V (au-dessus de la fente d'isolement horizontale)
    mz = [s for s in G.SLOTS if s[2] - s[0] > 30]
    if mz:
        sx0, sy0, sx1, sy1 = mz[0]
        o.append(f'<rect x="{(sx0 - 2 - x0) * S}" y="0" width="{(sx1 + 2 - sx0 + 2) * S}" height="{(sy1 - y0) * S}" fill="url(#hz)"/>')
        o.append(f'<text x="{(sx0 + 4 - x0) * S}" y="{(sy1 - 18 - y0) * S}" font-family="DejaVu Sans, sans-serif" font-size="60" font-weight="bold" fill="#c1121f" fill-opacity="0.6">ZONE 230 V</text>')
    for sx0, sy0, sx1, sy1 in G.SLOTS:
        o.append(f'<rect x="{(sx0 - x0) * S}" y="{(sy0 - y0) * S}" width="{(sx1 - sx0) * S}" height="{(sy1 - sy0) * S}" fill="#ffffff"/>')
    pins = {}
    texts = []
    for ref, f in sorted(fps.items(), key=lambda kv: I.natkey(kv[0])):
        fam = family(f["fpid"])
        cx0, cy0, cx1, cy1 = f["crt"] or f["bbox"]
        posed = ref in pop
        fill, stroke = FAMILY_STYLE.get(fam, ("#888888", "#444444"))
        if ref == "A2":
            o.append(f'<rect x="{(cx0 - x0) * S}" y="{(cy0 - y0) * S}" width="{(cx1 - cx0) * S}" height="{(cy1 - cy0) * S}" fill="none" stroke="#bbbbbb" stroke-width="4" stroke-dasharray="20,12"/>')
            texts.append(((cx0 + cx1) / 2, (cy0 + cy1) / 2, "site S3 (A2)\nnon posé", "#dddddd", 34))
            continue
        if fam == "MountingHole":
            o.append(f'<circle cx="{(f["pos"][0] - x0) * S}" cy="{(f["pos"][1] - y0) * S}" r="16" fill="#ffffff" stroke="#999999" stroke-width="3"/>')
            if ref in ("H1", "H5"):
                texts.append((f["pos"][0], f["pos"][1] + 5.5, f"{ref} NYLON", "#ffe066", 26))
            continue
        if not posed:
            o.append(f'<rect x="{(cx0 - x0) * S}" y="{(cy0 - y0) * S}" width="{(cx1 - cx0) * S}" height="{(cy1 - cy0) * S}" rx="6" fill="#9aa5a0" fill-opacity="0.35" stroke="#cfd8d3" stroke-width="3" stroke-dasharray="10,8"/>')
        else:
            o.append(f'<rect x="{(cx0 - x0) * S}" y="{(cy0 - y0) * S}" width="{(cx1 - cx0) * S}" height="{(cy1 - cy0) * S}" rx="6" fill="{fill}" stroke="{stroke}" stroke-width="3"/>')
        # pastilles
        for num, (px, py) in f["pads"].items():
            r = 7 if fam in ("TerminalBlock",) else 5
            colr = "#c9b037" if posed else "#777777"
            if ref.startswith("J") and not ref.startswith("JP"):
                idx = conn_index[(ref, num)]
                o.append(f'<circle id="connector{idx}pin" cx="{(px - x0) * S}" cy="{(py - y0) * S}" r="{r + 4}" fill="{colr}" stroke="#333333" stroke-width="2"/>')
                o.append(f'<circle id="connector{idx}terminal" cx="{(px - x0) * S}" cy="{(py - y0) * S}" r="1" fill="none"/>')
                pins[(ref, num)] = ((px - x0) * PX, (py - y0) * PX)
            else:
                o.append(f'<circle cx="{(px - x0) * S}" cy="{(py - y0) * S}" r="{r}" fill="{colr}" stroke="#333333" stroke-width="1.5"/>')
        # cavaliers
        if ref in jp:
            a, b = jp[ref].split("-")
            (ax, ay), (bx, by) = f["pads"][a], f["pads"][b]
            o.append(f'<rect x="{(min(ax, bx) - x0 - 1.5) * S}" y="{(min(ay, by) - y0 - 1.5) * S}" width="{(abs(bx - ax) + 3) * S}" height="{(abs(by - ay) + 3) * S}" rx="8" fill="#1e3a8a" stroke="#93c5fd" stroke-width="3"/>')
        # libellés
        label = None
        if ref.startswith("JP"):
            n = int(ref[2:])
            label = f"{ref} {jp[ref]}" if ref in jp else (ref if not posed else (f"{ref} AUTO" if 5 <= n <= 10 else f"{ref} ouvert"))
        elif ref.startswith("J") or ref.startswith("K") or ref in ("PS1", "A1", "U1", "F1", "RV1", "LED5") or ref.startswith("Q"):
            label = ref if ref != "A1" else "A1 — ESP32 DevKit V1\n(enfiché, jamais soudé)"
            if ref.startswith("K"):
                label = f"{ref} relais"
        elif ref.startswith("LED"):
            label = ref
        if label:
            col = "#ffffff" if posed else "#eeeeee"
            size = 30 if not ref.startswith("A") else 34
            ty = cy1 + 2.6 if (cy1 - cy0) < 12 else (cy0 + cy1) / 2
            texts.append(((cx0 + cx1) / 2, ty, label + ("" if posed else " ×"), col, size))
    for tx, ty, txt, col, size in texts:
        for k, line in enumerate(txt.split("\n")):
            o.append(f'<text x="{(tx - x0) * S:.1f}" y="{(ty - y0) * S + k * size * 1.1:.1f}" text-anchor="middle" font-family="DejaVu Sans, sans-serif" font-size="{size}" fill="{col}" stroke="#000000" stroke-width="1.2" paint-order="stroke">{escape(line)}</text>')
    o.append("</g></svg>")
    return "\n".join(o), pins


def simple_view_svg(view, wmm, hmm, conns):
    """Vue schematic/pcb minimale : même ids de connecteurs, rectangle."""
    S = 10
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="{wmm}mm" height="{hmm}mm" viewBox="0 0 {wmm * S} {hmm * S}">',
         f'<g id="{view}"><rect x="2" y="2" width="{wmm * S - 4}" height="{hmm * S - 4}" fill="none" stroke="#000000" stroke-width="3"/>']
    for idx, (x, y) in conns.items():
        o.append(f'<circle id="connector{idx}pin" cx="{x * S}" cy="{y * S}" r="5" fill="#000000"/>')
        o.append(f'<circle id="connector{idx}terminal" cx="{x * S}" cy="{y * S}" r="1" fill="none"/>')
    o.append("</g></svg>")
    return "\n".join(o)


def icon_svg(color="#2e7d32"):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32"><rect x="2" y="6" width="28" height="20" rx="3" fill="{color}"/></svg>'


def fzp(module_id, title, desc, connectors, props=None):
    """connectors : liste de (idx, name, description)."""
    props = props or {}
    o = [f'<?xml version="1.0" encoding="UTF-8"?>\n<module fritzingVersion="1.0.2" moduleId="{module_id}">',
         f'<version>1</version><author>n3_firmwires generate_fritzing.py</author><title>{escape(title)}</title>',
         f'<label>{escape(props.get("label", "n3u"))}</label><date>{G.REV_DATE}</date><tags><tag>n3</tag></tags>',
         '<properties><property name="family">' + escape(props.get("family", "n3-universal")) + '</property>'
         + "".join(f'<property name="{k}">{escape(v)}</property>' for k, v in props.items() if k not in ("family", "label")) + '</properties>',
         f'<description>{escape(desc)}</description>',
         '<views>',
         f'<iconView><layers image="icon/{module_id}_icon.svg"><layer layerId="icon"/></layers></iconView>',
         f'<breadboardView><layers image="breadboard/{module_id}_breadboard.svg"><layer layerId="breadboard"/></layers></breadboardView>',
         f'<schematicView><layers image="schematic/{module_id}_schematic.svg"><layer layerId="schematic"/></layers></schematicView>',
         f'<pcbView><layers image="pcb/{module_id}_pcb.svg"><layer layerId="copper0"/><layer layerId="copper1"/></layers></pcbView>',
         '</views><connectors>']
    for idx, name, d in connectors:
        o.append(f'<connector id="connector{idx}" name="{escape(name)}" type="female"><description>{escape(d)}</description><views>'
                 f'<breadboardView><p layer="breadboard" svgId="connector{idx}pin" terminalId="connector{idx}terminal"/></breadboardView>'
                 f'<schematicView><p layer="schematic" svgId="connector{idx}pin" terminalId="connector{idx}terminal"/></schematicView>'
                 f'<pcbView><p layer="copper0" svgId="connector{idx}pin"/><p layer="copper1" svgId="connector{idx}pin"/></pcbView>'
                 '</views></connector>')
    o.append("</connectors></module>")
    return "\n".join(o)


# ----------------------------------------------------------------------------- périphériques (SVG)
PITCH = 12.0  # px entre broches d'un périphérique (0,133 in : lisible)


def device_part(kind, side, module_id, label=None):
    """Boîte étiquetée, broches sur le bord qui regarde la carte. Retourne (fzp, svg, pins_px, w, h)."""
    title, names, fill, wmin = KINDS[kind]
    label = label or title
    n = len(names)
    w = max(wmin, n * PITCH + 16, 6.2 * len(label) + 16)
    h = 46
    S = 4  # unités SVG par px
    pins = {}
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="{w / 90:.4f}in" height="{h / 90:.4f}in" viewBox="0 0 {w * S} {h * S}"><g id="breadboard">',
         f'<rect x="2" y="2" width="{w * S - 4}" height="{h * S - 4}" rx="16" fill="{fill}" stroke="#333333" stroke-width="4"/>']
    pin_y = (h - 6) if side in ("top", None) else 6  # côté bas si le périphérique est au-dessus de la carte
    name_y = (h - 16) if side in ("top", None) else 18
    title_y = 16 if side in ("top", None) else h - 10
    o.append(f'<text x="{w * S / 2}" y="{title_y * S}" text-anchor="middle" font-family="DejaVu Sans, sans-serif" font-size="{8.5 * S}" font-weight="bold" fill="#111111">{escape(label)}</text>')
    xoff = (w - (n - 1) * PITCH) / 2
    for i, nm in enumerate(names):
        x = xoff + i * PITCH
        pins[nm] = (x, pin_y)
        o.append(f'<circle id="connector{i}pin" cx="{x * S}" cy="{pin_y * S}" r="{3.2 * S}" fill="#c9b037" stroke="#333333" stroke-width="3"/>')
        o.append(f'<circle id="connector{i}terminal" cx="{x * S}" cy="{pin_y * S}" r="1" fill="none"/>')
        o.append(f'<text x="{x * S}" y="{name_y * S}" text-anchor="middle" font-family="DejaVu Sans, sans-serif" font-size="{5.2 * S}" fill="#111111" transform="rotate(-35 {x * S} {name_y * S})">{escape(nm)}</text>')
    o.append("</g></svg>")
    conns = [(i, nm, f"{title} — {nm}") for i, nm in enumerate(names)]
    return fzp(module_id, label, title, conns, {"family": "n3 périphérique", "label": kind}), "\n".join(o), pins, w, h


def legend_part(module_id, prof, lines):
    w, h = 420, 24 + 16 * len(lines)
    S = 4
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="{w / 90:.4f}in" height="{h / 90:.4f}in" viewBox="0 0 {w * S} {h * S}"><g id="breadboard">',
         f'<rect x="2" y="2" width="{w * S - 4}" height="{h * S - 4}" rx="12" fill="#fffbe6" stroke="#8a6a20" stroke-width="4"/>',
         f'<text x="{12 * S}" y="{16 * S}" font-family="DejaVu Sans, sans-serif" font-size="{9 * S}" font-weight="bold" fill="#111111">{escape(I.PROFILES[prof])}</text>']
    for k, line in enumerate(lines):
        o.append(f'<text x="{12 * S}" y="{(32 + 16 * k) * S}" font-family="DejaVu Sans, sans-serif" font-size="{7 * S}" fill="#111111">{escape(line)}</text>')
    o.append("</g></svg>")
    return fzp(module_id, "Légende", "Légende du profil", [], {"family": "n3 légende", "label": "légende"}), "\n".join(o), w, h


# ----------------------------------------------------------------------------- sketch
def side_of(x, y, W, H):
    d = {"left": x, "right": W - x, "top": y, "bottom": H - y}
    return min(d, key=d.get)


def build_profile(prof):
    """Retourne (fichiers du fzz {nom: contenu}, lignes de câblage pour la photographie)."""
    fps = load_footprints(PCB)
    B = G.BOARD
    Wb, Hb = (B["x1"] - B["x0"]) * PX, (B["y1"] - B["y0"]) * PX
    # connecteurs de la carte = toutes les pastilles des J (pas les JP)
    conn_index, conn_list = {}, []
    desc = {c["ref"]: c for c in G.COMPONENTS}
    for ref in sorted((r for r in fps if r.startswith("J") and not r.startswith("JP")), key=I.natkey):
        for num in sorted(fps[ref]["pads"], key=lambda s: int(s) if s.isdigit() else 99):
            idx = len(conn_list)
            conn_index[(ref, num)] = idx
            net = desc[ref]["nets"].get(num, "")
            conn_list.append((idx, f"{ref}-{num} {net}", desc[ref]["desc"][:120]))
    board_id = f"n3u_{G.REV.replace('.', '')}_{prof}"
    svg, bpins = board_svg(prof, fps, conn_index)
    files = {}
    conns_mm = {conn_index[k]: ((fps[k[0]]["pads"][k[1]][0] - B["x0"]), (fps[k[0]]["pads"][k[1]][1] - B["y0"])) for k in conn_index}
    files[f"part.{board_id}.fzp"] = fzp(board_id, f"n3-universal rev {G.REV} — {I.SHEET[prof]}",
                                        "Carte porteuse universelle msp / n3pp / ffp5cs (hardware/n3-universal)", conn_list,
                                        {"family": "n3-universal", "label": "n3u", "profil": I.SHEET[prof]})
    files[f"svg.breadboard.{board_id}_breadboard.svg"] = svg
    files[f"svg.schematic.{board_id}_schematic.svg"] = simple_view_svg("schematic", B["x1"] - B["x0"], B["y1"] - B["y0"], conns_mm)
    files[f"svg.pcb.{board_id}_pcb.svg"] = simple_view_svg("copper0", B["x1"] - B["x0"], B["y1"] - B["y0"], conns_mm)
    files[f"svg.icon.{board_id}_icon.svg"] = icon_svg()

    devices = WIRING[prof]
    dev_by_key = {d["key"]: d for d in devices}
    # côté de chaque périphérique : explicite, sinon bord le plus proche de ses broches carte
    for d in devices:
        if d["side"]:
            continue
        pts = [bpins[(r, p)] for (_, r, p, _) in targets(d) if (r, p) in bpins]
        if not pts:
            # branché sur un autre périphérique : même côté que lui
            tgt = targets(d)[0][1]
            d["side"] = dev_by_key[tgt]["side"] or "left"
            continue
        mx, my = sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)
        d["side"] = side_of(mx, my, Wb, Hb)
    # ordre et placement le long de chaque bord
    GAP = 14.0
    place = {}  # key -> (x, y, w, h) relatif au coin de la carte
    parts = {}  # key -> (module_id, pins_px, w, h)
    for side in ("top", "bottom", "left", "right"):
        ds = [d for d in devices if d["side"] == side]
        if not ds:
            continue

        def anchor(d):
            pts = [bpins[(r, p)] for (_, r, p, _) in targets(d) if (r, p) in bpins]
            if not pts:
                k = targets(d)[0][1]
                pts = [place.get(k, (0, 0))[:2]] if k in place else [(0, 0)]
            return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))

        for d in ds:
            mid = f"n3u_dev_{prof}_{d['key']}"
            _, _, pins, w, h = device_part(d["kind"], side, mid, d["label"])
            parts[d["key"]] = (mid, pins, w, h)

        def dep_order(lst):
            """périphériques reliés à la carte d'abord, puis ceux reliés à un périphérique déjà placé"""
            done, out, pending = set(), [], list(lst)
            while pending:
                progress = False
                for d in list(pending):
                    if {r for (_, r, p, _) in targets(d) if r in dev_by_key} <= done:
                        out.append(d); done.add(d["key"]); pending.remove(d); progress = True
                if not progress:
                    out += pending
                    break
            return out
        chained = any({r for (_, r, p, _) in targets(d) if r in dev_by_key} for d in ds)
        ds = dep_order(ds)
        nw = sum(len(targets(d)) for d in ds)
        lanes = nw * 5 + 16
        if side in ("top", "bottom"):
            for ordered in ((ds,) if not chained else (ds, sorted(ds, key=lambda d: anchor(d)[0]))) if chained else (sorted(ds, key=lambda d: anchor(d)[0]),):
                cur = -1e9
                for d in ordered:
                    mid, pins, w, h = parts[d["key"]]
                    x = max(anchor(d)[0] - w / 2, cur + GAP)
                    y = -lanes - h if side == "top" else Hb + lanes
                    place[d["key"]] = (x, y, w, h)
                    cur = x + w
            # recentre si la rangée est plus large que la carte
            x0r, x1r = min(v[0] for k, v in place.items() if dev_by_key[k]["side"] == side), max(v[0] + v[2] for k, v in place.items() if dev_by_key[k]["side"] == side)
            if x1r - x0r > Wb:
                sh = (Wb - (x1r - x0r)) / 2 - x0r
                for k in list(place):
                    if dev_by_key[k]["side"] == side:
                        x, y, w, h = place[k]
                        place[k] = (x + sh, y, w, h)
        else:
            for ordered in ((ds, sorted(ds, key=lambda d: anchor(d)[1])) if chained else (sorted(ds, key=lambda d: anchor(d)[1]),)):
                cur = -1e9
                for d in ordered:
                    mid, pins, w, h = parts[d["key"]]
                    y = max(anchor(d)[1] - h / 2, cur + GAP)
                    x = -lanes - w if side == "left" else Wb + lanes
                    place[d["key"]] = (x, y, w, h)
                    cur = y + h
    # légende
    lines = LEGENDE[prof] + [
        "Cavaliers posés : " + ", ".join(f"{k} {v}" for k, v in sorted(I.JUMPER_STATE[prof].items(), key=lambda kv: I.natkey(kv[0]))),
        "Les résistances et diodes sont SUR la carte : aucune à câbler. Embouts sur tout fil vissé.",
        f"Carte n3-universal rev {G.REV} à l'échelle : gris pointillé / × = NON posé sur ce profil ; bleu = cavalier posé.",
    ]
    leg_id = f"n3u_legend_{prof}"
    lf, ls, lw, lh = legend_part(leg_id, prof, lines)
    files[f"part.{leg_id}.fzp"] = lf
    files[f"svg.breadboard.{leg_id}_breadboard.svg"] = ls
    files[f"svg.schematic.{leg_id}_schematic.svg"] = simple_view_svg("schematic", lw / PX, lh / PX, {})
    files[f"svg.pcb.{leg_id}_pcb.svg"] = simple_view_svg("copper0", lw / PX, lh / PX, {})
    files[f"svg.icon.{leg_id}_icon.svg"] = icon_svg("#8a6a20")
    for key, (mid, pins, w, h) in parts.items():
        if f"part.{mid}.fzp" in files:
            continue
        pf, ps, _, _, _ = device_part(dev_by_key[key]["kind"], dev_by_key[key]["side"], mid, dev_by_key[key]["label"])
        files[f"part.{mid}.fzp"] = pf
        files[f"svg.breadboard.{mid}_breadboard.svg"] = ps
        files[f"svg.schematic.{mid}_schematic.svg"] = simple_view_svg("schematic", w / PX, h / PX, {i: (p[0] / PX, p[1] / PX) for i, p in enumerate(pins.values())})
        files[f"svg.pcb.{mid}_pcb.svg"] = simple_view_svg("copper0", w / PX, h / PX, {i: (p[0] / PX, p[1] / PX) for i, p in enumerate(pins.values())})
        files[f"svg.icon.{mid}_icon.svg"] = icon_svg("#cfe8ff")

    # décalage global : tout en coordonnées positives
    left_x = min([0.0] + [v[0] for v in place.values()])
    leg_x, leg_y = left_x - lw - 30, 0.0
    allx = [leg_x] + [v[0] for v in place.values()]
    ally = [0.0] + [v[1] for v in place.values()]
    OX, OY = 30 - min(allx), 30 - min(ally)

    # instances
    inst, wires, rows = [], [], []
    model = {}
    nxt = [1000]

    def new_idx():
        nxt[0] += 1
        return nxt[0]

    def instance(mid, title, x, y, idx, path):
        inst.append([f'<instance moduleIdRef="{mid}" modelIndex="{idx}" path="{path}"><title>{escape(title)}</title><views>'
                     f'<breadboardView layer="breadboard"><geometry z="{1.5 if mid.startswith("n3u_0") else 2.5}" x="{x:.2f}" y="{y:.2f}"/>',
                     '</breadboardView></views></instance>'])

    def pin_abs(key, pin):
        if key == "__board__":
            x, y = bpins[pin]
            return OX + x, OY + y
        x, y, w, h = place[key]
        px, py = parts[key][1][pin]
        return OX + x + px, OY + y + py

    bidx = new_idx()
    model["__board__"] = bidx
    instance(board_id, f"n3-universal rev {G.REV} — {I.SHEET[prof]}", OX, OY, bidx, f"part.{board_id}.fzp")
    lidx = new_idx()
    instance(leg_id, "Légende", OX + leg_x, OY + leg_y, lidx, f"part.{leg_id}.fzp")
    for key, (x, y, w, h) in place.items():
        idx = new_idx()
        model[key] = idx
        instance(parts[key][0], dev_by_key[key]["label"], OX + x, OY + y, idx, f"part.{parts[key][0]}.fzp")

    connects = defaultdict(lambda: defaultdict(list))  # (key)->(connectorId)->[(wire idx, wire connector)]

    def conn_id(key, pin):
        if key == "__board__":
            return f"connector{conn_index[pin]}"
        return f"connector{list(parts[key][1]).index(pin)}"

    lane_n = defaultdict(int)
    # anneau extérieur (contourne tous les périphériques) pour les fils qui changent de bord
    ext_x0 = OX + min([0.0] + [v[0] for v in place.values()]) - 24
    ext_x1 = OX + max([Wb] + [v[0] + v[2] for v in place.values()]) + 24
    ext_y0 = OY + min([0.0] + [v[1] for v in place.values()]) - 24
    ext_y1 = OY + max([Hb] + [v[1] + v[3] for v in place.values()]) + 24
    ring_n = defaultdict(int)

    def lane(side):
        k = lane_n[side]
        lane_n[side] += 1
        return {"top": OY - 8 - k * 5, "bottom": OY + Hb + 8 + k * 5, "left": OX - 8 - k * 5, "right": OX + Wb + 8 + k * 5}[side]

    def pin_side(key, pin):
        if key == "__board__":
            x, y = bpins[pin]
            return side_of(x, y, Wb, Hb)
        return dev_by_key[key]["side"]

    def route(a_key, a_pin, b_key, b_pin, color, note=""):
        """Fil en segments orthogonaux : couloir du bord de départ, couloir du bord d'arrivée,
        anneau extérieur si les deux bords sont opposés."""
        xa, ya = pin_abs(a_key, a_pin)
        xb, yb = pin_abs(b_key, b_pin)
        if a_key == "__board__" and b_key == "__board__":
            pts = [(xa, ya), (xb, yb)]  # fil posé sur la carte elle-même : droit
        else:
            sa, sb = pin_side(a_key, a_pin), pin_side(b_key, b_pin)
            H = ("top", "bottom")
            if sa == sb:
                l = lane(sa)
                pts = [(xa, ya), (xa, l), (xb, l), (xb, yb)] if sa in H else [(xa, ya), (l, ya), (l, yb), (xb, yb)]
            elif (sa in H) != (sb in H):  # bords adjacents : couloir a puis couloir b
                la, lb = lane(sa), lane(sb)
                pts = [(xa, ya), (xa, la), (lb, la), (lb, yb), (xb, yb)] if sa in H else [(xa, ya), (la, ya), (la, lb), (xb, lb), (xb, yb)]
            else:  # bords opposés : par l'anneau extérieur
                la, lb = lane(sa), lane(sb)
                k = ring_n[sa]
                ring_n[sa] += 1
                if sa in H:
                    rx = (ext_x1 + k * 5) if (xa + xb) / 2 > OX + Wb / 2 else (ext_x0 - k * 5)
                    pts = [(xa, ya), (xa, la), (rx, la), (rx, lb), (xb, lb), (xb, yb)]
                else:
                    ry = (ext_y1 + k * 5) if (ya + yb) / 2 > OY + Hb / 2 else (ext_y0 - k * 5)
                    pts = [(xa, ya), (la, ya), (la, ry), (lb, ry), (lb, yb), (xb, yb)]
        # supprime les segments nuls
        pts = [p for i, p in enumerate(pts) if i == 0 or (abs(p[0] - pts[i - 1][0]) > 0.01 or abs(p[1] - pts[i - 1][1]) > 0.01)]
        idxs = [new_idx() for _ in range(len(pts) - 1)]
        for i, (p, q) in enumerate(zip(pts, pts[1:])):
            widx = idxs[i]
            c0 = (f'<connect connectorId="{conn_id(a_key, a_pin)}" modelIndex="{model[a_key]}" layer="breadboard"/>' if i == 0
                  else f'<connect connectorId="connector1" modelIndex="{idxs[i - 1]}" layer="breadboardWire"/>')
            c1 = (f'<connect connectorId="{conn_id(b_key, b_pin)}" modelIndex="{model[b_key]}" layer="breadboard"/>' if i == len(idxs) - 1
                  else f'<connect connectorId="connector0" modelIndex="{idxs[i + 1]}" layer="breadboardWire"/>')
            wires.append(f'''<instance moduleIdRef="WireModuleID" modelIndex="{widx}" path=":/resources/parts/core/wire.fzp"><title>Wire{widx}</title><views>
<breadboardView layer="breadboardWire"><geometry z="3" x="{p[0]:.2f}" y="{p[1]:.2f}" x1="0" y1="0" x2="{q[0] - p[0]:.2f}" y2="{q[1] - p[1]:.2f}" wireFlags="64"/>
<wireExtras mils="22.2222" color="{color}" opacity="1" banded="0"/>
<connectors><connector connectorId="connector0" layer="breadboardWire"><geometry x="0" y="0"/><connects>{c0}</connects></connector>
<connector connectorId="connector1" layer="breadboardWire"><geometry x="0" y="0"/><connects>{c1}</connects></connector></connectors>
</breadboardView></views></instance>''')
        connects[a_key][conn_id(a_key, a_pin)].append((idxs[0], "connector0"))
        connects[b_key][conn_id(b_key, b_pin)].append((idxs[-1], "connector1"))
        a_lab = f"{a_pin[0]}-{a_pin[1]}" if a_key == "__board__" else f"{a_key}:{a_pin}"
        b_lab = f"{b_pin[0]}-{b_pin[1]}" if b_key == "__board__" else f"{b_key}:{b_pin}"
        rows.append((prof, a_lab, b_lab, color, note))

    for d in devices:
        for pin, ref, pad, color in targets(d):
            pin = pin.strip()
            if ref in dev_by_key:
                route(d["key"], pin, ref, pad, color)
            else:
                route(d["key"], pin, "__board__", (ref, pad), color)
    for (a, b, color, note) in BOARD_WIRES.get(prof, []):
        route("__board__", a, "__board__", b, color, note)

    # connecteurs réciproques dans les instances de pièces
    for key, idx in model.items():
        if key not in connects:
            continue
        blocks = []
        for cid, links in connects[key].items():
            c = "".join(f'<connect connectorId="{wc}" modelIndex="{wm}" layer="breadboardWire"/>' for wm, wc in links)
            blocks.append(f'<connector connectorId="{cid}" layer="breadboard"><geometry x="0" y="0"/><connects>{c}</connects></connector>')
        for it in inst:
            if f'modelIndex="{idx}"' in it[0]:
                it[0] += "<connectors>" + "".join(blocks) + "</connectors>"
    fz = f'''<?xml version="1.0" encoding="UTF-8"?>
<module fritzingVersion="1.0.2" icon=".png">
<project_properties/><boards/>
<views>
<view name="breadboardView" backgroundColor="#ffffff" gridSize="0.1in" showGrid="0" alignToGrid="0" viewFromBelow="0" colorWiresByLength="0"/>
<view name="schematicView" backgroundColor="#ffffff" gridSize="0.1in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>
<view name="pcbView" backgroundColor="#333333" gridSize="0.05in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>
</views>
<instances>
{chr(10).join("".join(i) for i in inst)}
{chr(10).join(wires)}
</instances>
</module>
'''
    files[f"{FILE[prof]}.fz"] = fz
    return files, rows


def coverage_errors():
    """Chaque article d'inventaire.OFF (à acheter ou possédé, dessinable) doit être dessiné en bonne quantité."""
    errs = []
    for prof in I.PROFILES:
        drawn = defaultdict(int)
        for d in WIRING[prof]:
            drawn[d["art"]] += 1
        for art, q, st, _, _ in I.OFF[prof]:
            if st == I.O or art in NOT_DRAWN:
                continue
            if drawn.get(art, 0) != q:
                errs.append(f"{prof} : {art} — {q} dans achats/inventaire.OFF, {drawn.get(art, 0)} dessiné(s) dans WIRING")
        for art in drawn:
            if art not in FREE_KINDS and art not in {o[0] for o in I.OFF[prof]}:
                errs.append(f"{prof} : {art} dessiné mais absent de achats/inventaire.OFF")
        for d in WIRING[prof]:
            for pin, ref, pad, _ in targets(d):
                if ref not in {x["key"] for x in WIRING[prof]} and ref not in I.POP[prof]:
                    errs.append(f"{prof} : {d['key']}:{pin} câblé sur {ref}-{pad} qui n'est pas posé sur ce profil")
    return errs


def snapshot(all_rows):
    """Photographie texte : fils, cavaliers et repères posés par profil (toute dérive de la BOM,
    de l'inventaire ou du câblage la change)."""
    buf = io.StringIO()
    w = csv.writer(buf, delimiter=";", lineterminator="\n")
    w.writerow(["profil", "de", "vers", "couleur", "note"])
    for r in all_rows:
        w.writerow(r)
    for prof in I.PROFILES:
        for jp, st in sorted(I.JUMPER_STATE[prof].items(), key=lambda kv: I.natkey(kv[0])):
            w.writerow([prof, "cavalier", jp, st, ""])
        w.writerow([prof, "posés", " ".join(sorted(I.POP[prof], key=I.natkey)), "", ""])
    return buf.getvalue()


def render(fzz_paths):
    """Export des vues par Fritzing (headless, xvfb) + PNG d'aperçu de la vue platine."""
    import os
    if not shutil.which("fritzing") or not shutil.which("xvfb-run"):
        print("  (fritzing / xvfb absents : pas d'aperçu)")
        return
    with tempfile.TemporaryDirectory() as td:
        for p in fzz_paths:
            shutil.copy(p, td)
        env = dict(os.environ, XDG_RUNTIME_DIR="/tmp/runtime-fz")
        Path("/tmp/runtime-fz").mkdir(exist_ok=True)
        r = subprocess.run(["xvfb-run", "-a", "fritzing", "-svg", td], capture_output=True, text=True, timeout=900, env=env)
        bad = [l for l in r.stderr.splitlines() + r.stdout.splitlines() if "not found in database" in l and "n3u_" in l]
        if bad:
            raise SystemExit("Fritzing n'a pas chargé une pièce embarquée :\n" + "\n".join(bad[:5]))
        for p in fzz_paths:
            svg = Path(td) / (p.stem + "_breadboard.svg")
            if not svg.exists():
                raise SystemExit(f"export Fritzing absent pour {p.name}")
            png = HERE / (p.stem + "-apercu.png")
            if shutil.which("rsvg-convert"):
                subprocess.run(["rsvg-convert", "-w", "2600", "-b", "white", str(svg), "-o", str(png)], check=True)
                print(f"  aperçu : {png.name}")


def main() -> int:
    errs = coverage_errors()
    if errs:
        print("ECHEC câblage Fritzing :")
        for e in errs:
            print(" -", e)
        return 1
    all_rows, outputs = [], []
    for prof in I.PROFILES:
        files, rows = build_profile(prof)
        all_rows += rows
        outputs.append((prof, files))
    snap = snapshot(all_rows)
    if "--check" in sys.argv:
        if not SNAP.exists() or SNAP.read_text(encoding="utf-8") != snap:
            print("ECHEC : les vues Fritzing ne suivent plus la carte / l'inventaire — régénérer avec "
                  "`python3 fritzing/generate_fritzing.py` et committer.")
            return 1
        print(f"OK : vues Fritzing synchronisées ({len(all_rows)} fils, 4 profils)")
        return 0
    import xml.etree.ElementTree as ET
    paths = []
    for prof, files in outputs:
        for name, content in files.items():
            if name.endswith((".fz", ".fzp", ".svg")):
                ET.fromstring(content.encode("utf-8"))  # XML bien formé
        out = HERE / f"{FILE[prof]}.fzz"
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for name, content in files.items():
                z.writestr(name, content)
        paths.append(out)
        print(f"écrit {out.name} ({sum(1 for n in files if n.startswith('part.'))} pièces, {sum(1 for r in all_rows if r[0] == prof)} fils)")
    SNAP.write_text(snap, encoding="utf-8")
    if "--render" in sys.argv:
        render(paths)
    return 0


if __name__ == "__main__":
    sys.exit(main())
