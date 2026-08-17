#!/usr/bin/env python3
"""Génère un fichier Fritzing (.fzz) du câblage de la carte commune n3pp + msp.

Représente le câblage périphériques <-> ESP32 DevKit V1 (utile pour un montage
au banc ou sur protoboard) avec les pièces de la bibliothèque core Fritzing :
DS18B20, LDR, servos, résistances ; le DevKit, les DHT, le capteur de pluie,
l'OLED, les entrées ADC et les sorties relais sont figurés par des headers
génériques étiquetés (pas de pièce core fidèle). Les couleurs de fils suivent l'usage : rouge=5V, orange=3V3,
noir=GND, autres=signaux.

⚠️ Livrable « best effort » : la structure .fz est calquée sur les sketches
officiels de Fritzing 1.0 mais il n'existe pas de validateur en CLI — ouvrir
dans Fritzing >= 1.0 pour vérifier. Le PCB de référence reste le projet KiCad.

Usage : python3 generate_fritzing.py   (écrit ../fritzing/n3pp-msp-commun-cablage.fzz)
Nécessite les .svg de pièces core (paquet fritzing-parts) UNIQUEMENT pour
recalculer les positions de broches ; le .fzz produit n'embarque rien.
"""

from __future__ import annotations

import json
import re
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT_DIR = ROOT / "fritzing"
PARTS = Path("/usr/share/fritzing/parts")
PINMAP = json.loads((ROOT / "pinmap.json").read_text(encoding="utf-8"))
P = PINMAP["pins"]

IN = 90.0  # unités Fritzing par pouce (breadboard view)
PITCH = 9.0  # 0.1 in


# --- extraction des positions de broches dans les SVG breadboard -------------

def _mat(tr: str):
    M = [1, 0, 0, 1, 0, 0]

    def mul(A, B):
        a, b, c, d, e, f = A
        g, h, i, j, k, l = B
        return [a * g + c * h, b * g + d * h, a * i + c * j, b * i + d * j,
                a * k + c * l + e, b * k + d * l + f]

    for name, args in re.findall(r"(\w+)\(([^)]*)\)", tr or ""):
        v = [float(x) for x in re.split(r"[,\s]+", args.strip()) if x]
        if name == "translate":
            B = [1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0]
        elif name == "matrix":
            B = v
        elif name == "scale":
            B = [v[0], 0, 0, v[-1], 0, 0]
        else:
            B = [1, 0, 0, 1, 0, 0]
        M = mul(M, B)
    return M


def svg_pins(svg_rel: str, ids: set[str]) -> dict[str, tuple[float, float]]:
    path = PARTS / "svg" / svg_rel
    root = ET.parse(path).getroot()
    vbw = float(root.get("viewBox").split()[2])
    w = root.get("width").strip()
    win = (float(w[:-2]) if w.endswith("in")
           else float(w[:-2]) / 25.4 if w.endswith("mm")
           else float(re.sub("[a-z]*$", "", w)) / IN)
    scale = IN * win / vbw
    found: dict[str, tuple[float, float]] = {}

    def mul(A, B):
        a, b, c, d, e, f = A
        g, h, i, j, k, l = B
        return [a * g + c * h, b * g + d * h, a * i + c * j, b * i + d * j,
                a * k + c * l + e, b * k + d * l + f]

    def walk(el, M):
        M2 = mul(M, _mat(el.get("transform"))) if el.get("transform") else M
        eid = el.get("id")
        if eid in ids:
            x = y = None
            if el.get("cx") is not None:
                x, y = float(el.get("cx")), float(el.get("cy"))
            elif el.get("x") is not None:
                x = float(el.get("x")) + float(el.get("width", "0")) / 2
                y = float(el.get("y")) + float(el.get("height", "0")) / 2
            elif el.get("x1") is not None:
                x, y = float(el.get("x1")), float(el.get("y1"))
            elif el.get("d"):
                m = re.match(r"[mM]\s*([-\d.]+)[,\s]+([-\d.]+)", el.get("d"))
                if m:
                    x, y = float(m.group(1)), float(m.group(2))
            if x is not None:
                a, b, c, d, e, f = M2
                found[eid] = ((a * x + c * y + e) * scale,
                              (b * x + d * y + f) * scale)
        for ch in el:
            walk(ch, M2)

    walk(root, [1, 0, 0, 1, 0, 0])
    return found


HDR = "generic_female_pin_header_{n}_100mil"


def header_pins(n: int) -> dict[str, tuple[float, float]]:
    # header générique horizontal : broche i à ~(0.05in + i*0.1in, 0.05in)
    return {f"connector{i}": (4.5 + PITCH * i, 4.5) for i in range(n)}


# --- catalogue des pièces utilisées ------------------------------------------

R_PINS = svg_pins("core/breadboard/resistor_220.svg",
                  {"connector0pin", "connector1pin"})
DS_PINS = svg_pins("core/breadboard/DS18B20_breadboard.svg",
                   {f"connector{i}pin" for i in (1, 2, 3)})
SRV_PINS = svg_pins("core/breadboard/Dagu_DGServo_9g_End_view_breadboard.svg",
                    {f"connector{i}pin" for i in range(3)})
LDR_PINS = svg_pins("core/breadboard/ldr.svg", {"connector0pin", "connector1pin"})


def strippin(d):  # connectorNpin -> connectorN
    return {k.replace("pin", ""): v for k, v in d.items()}


CATALOG = {
    "res": dict(moduleId="ResistorModuleID", pins=strippin(R_PINS)),
    "ds18b20": dict(moduleId="DS18B20_fixed", pins=strippin(DS_PINS)),
    "servo": dict(moduleId="Dagu_DGServo_9g_End_view", pins=strippin(SRV_PINS)),
    "ldr": dict(moduleId="2010BBCD20113", pins=strippin(LDR_PINS)),
    "hdr15": dict(moduleId=HDR.format(n=15), pins=header_pins(15)),
    "hdr4": dict(moduleId=HDR.format(n=4), pins=header_pins(4)),
    "hdr3": dict(moduleId=HDR.format(n=3), pins=header_pins(3)),
    "hdr2": dict(moduleId=HDR.format(n=2), pins=header_pins(2)),
    "hdr6": dict(moduleId=HDR.format(n=6), pins=header_pins(6)),
    "hdr7": dict(moduleId=HDR.format(n=7), pins=header_pins(7)),
}

# --- instances : (nom, type, titre, x, y, propriétés) ------------------------
# Le DevKit est figuré par 2 headers 15 broches : rangée A (3V3..D23) en bas,
# rangée B (VIN..EN) en haut — mêmes ordres que l'empreinte KiCad.
DEVKIT_A = ["3V3", "GND", "D15", "D2", "D4", "D16", "D17", "D5", "D18", "D19",
            "D21", "RX0", "TX0", "D22", "D23"]
DEVKIT_B = ["VIN", "GND", "D13", "D12", "D14", "D27", "D26", "D25", "D33",
            "D32", "D35", "D34", "VN", "VP", "EN"]

INSTANCES = [
    ("mcuB", "hdr15", "ESP32 DevKit V1 - rangée B : " + " ".join(DEVKIT_B), 260, 330, {}),
    ("mcuA", "hdr15", "ESP32 DevKit V1 - rangée A : " + " ".join(DEVKIT_A), 260, 430, {}),
    # 5 entrées analogiques ADC1 (borniers 3V3/AIN/GND sur la carte)
    ("adcA", "hdr3", "ADC A : 3V3 AIN GND (D33 - n3pp humidite1 / msp LUMINOSITEa)", 30, 30, {}),
    ("adcB", "hdr3", "ADC B : 3V3 AIN GND (D32 - n3pp humidite2 / msp HumiditeSol)", 200, 30, {}),
    ("adcC", "hdr3", "ADC C : 3V3 AIN GND (D35 - n3pp humidite3 / msp LUMINOSITEc)", 370, 30, {}),
    ("adcD", "hdr3", "ADC D : 3V3 AIN GND (D34 - n3pp humidite4 / msp LUMINOSITEb)", 540, 30, {}),
    ("ldrE", "ldr", "ADC E : LDR -> VN/D39 (n3pp LUMINOSITE / msp LUMINOSITEd)", 690, 40, {}),
    ("r_ldrE", "res", "10k LDR E vers GND", 690, 110, {"resistance": "10k"}),
    # pont batterie -> VP/D36
    ("batt", "hdr2", "Batterie : VBAT GND (pont 2.2k/2.2k -> VP/D36)", 30, 140, {}),
    ("r_b1", "res", "2.2k pont haut (N3_BATTERY_R1)", 30, 190, {"resistance": "2.2k"}),
    ("r_b2", "res", "2.2k pont bas (N3_BATTERY_R2)", 30, 230, {"resistance": "2.2k"}),
    # 3 DHT + DS18B20 + pluie
    ("dht1", "hdr3", "DHT n3pp : 3V3 DATA GND (D18)", 40, 560, {}),
    ("r_dht1", "res", "10k pull-up DHT n3pp", 40, 530, {"resistance": "10k"}),
    ("dht2", "hdr3", "DHT INT msp : 3V3 DATA GND (D26)", 170, 560, {}),
    ("r_dht2", "res", "10k pull-up DHT INT", 170, 530, {"resistance": "10k"}),
    ("dht3", "hdr3", "DHT EXT msp : 3V3 DATA GND (D15)", 300, 560, {}),
    ("r_dht3", "res", "10k pull-up DHT EXT", 300, 530, {"resistance": "10k"}),
    ("ds", "ds18b20", "DS18B20 msp (1-Wire D2)", 430, 560, {}),
    ("r_ds", "res", "4.7k pull-up 1-Wire", 430, 530, {"resistance": "4.7k"}),
    ("rain", "hdr3", "Pluie msp : 3V3 DO GND (D27, sortie NUMERIQUE)", 550, 560, {}),
    ("oled", "hdr4", "OLED SSD1306 : GND VCC SCL SDA", 690, 560, {}),
    # servos tracker msp
    ("srv1", "servo", "Servo G/D msp (D25)", 640, 240, {}),
    ("r_srv1", "res", "220 signal G/D", 560, 240, {"resistance": "220"}),
    ("srv2", "servo", "Servo H/B msp (D14)", 640, 310, {}),
    ("r_srv2", "res", "220 signal H/B", 560, 310, {"resistance": "220"}),
    # commandes relais + breakout GPIO libres
    ("rel", "hdr6", "Vers 6 relais actif-HAUT : D13 D12 D16 D17 D19 D23", 620, 470, {}),
    ("brk", "hdr7", "Breakout J18 : 3V3 GND EN D4 D5 RX0 TX0", 620, 500, {}),
]

AIDX = {n: i for i, n in enumerate(DEVKIT_A)}
BIDX = {n: i for i, n in enumerate(DEVKIT_B)}


def A(pin):  # connecteur de la rangée A
    return ("mcuA", f"connector{AIDX[pin]}")


def B(pin):
    return ("mcuB", f"connector{BIDX[pin]}")


C5V, C3V3, CGND = "#cc0000", "#ff9400", "#404040"
WIRES = [
    # (extrémité1, extrémité2, couleur)
    # ADC A-D : 3V3(0) AIN(1) GND(2)
    (("adcA", "connector0"), A("3V3"), C3V3),
    (("adcA", "connector1"), B("D33"), "#3399ff"),
    (("adcA", "connector2"), A("GND"), CGND),
    (("adcB", "connector0"), A("3V3"), C3V3),
    (("adcB", "connector1"), B("D32"), "#33cc33"),
    (("adcB", "connector2"), A("GND"), CGND),
    (("adcC", "connector0"), A("3V3"), C3V3),
    (("adcC", "connector1"), B("D35"), "#9933ff"),
    (("adcC", "connector2"), A("GND"), CGND),
    (("adcD", "connector0"), A("3V3"), C3V3),
    (("adcD", "connector1"), B("D34"), "#996633"),
    (("adcD", "connector2"), A("GND"), CGND),
    # ADC E : 3V3 -- LDR -- VN, 10k vers GND
    (("ldrE", "connector0"), A("3V3"), C3V3),
    (("ldrE", "connector1"), B("VN"), "#cc9900"),
    (("r_ldrE", "connector0"), B("VN"), "#cc9900"),
    (("r_ldrE", "connector1"), A("GND"), CGND),
    # batterie : VBAT -- 2.2k -- VP -- 2.2k -- GND
    (("batt", "connector0"), ("r_b1", "connector0"), "#cc0066"),
    (("r_b1", "connector1"), B("VP"), "#cc0066"),
    (("r_b2", "connector0"), B("VP"), "#cc0066"),
    (("r_b2", "connector1"), A("GND"), CGND),
    (("batt", "connector1"), A("GND"), CGND),
    # DHT x3 (header 3 pts : 3V3 DATA GND) + pull-ups 10k
    (("dht1", "connector0"), A("3V3"), C3V3),
    (("dht1", "connector1"), A("D18"), "#cc33cc"),
    (("dht1", "connector2"), A("GND"), CGND),
    (("r_dht1", "connector0"), A("3V3"), C3V3),
    (("r_dht1", "connector1"), ("dht1", "connector1"), "#cc33cc"),
    (("dht2", "connector0"), A("3V3"), C3V3),
    (("dht2", "connector1"), B("D26"), "#6666cc"),
    (("dht2", "connector2"), A("GND"), CGND),
    (("r_dht2", "connector0"), A("3V3"), C3V3),
    (("r_dht2", "connector1"), ("dht2", "connector1"), "#6666cc"),
    (("dht3", "connector0"), A("3V3"), C3V3),
    (("dht3", "connector1"), A("D15"), "#cc6666"),
    (("dht3", "connector2"), A("GND"), CGND),
    (("r_dht3", "connector0"), A("3V3"), C3V3),
    (("r_dht3", "connector1"), ("dht3", "connector1"), "#cc6666"),
    # DS18B20 : VDD(3) DQ(2) GND(1), 1-Wire sur D2
    (("ds", "connector3"), A("3V3"), C3V3),
    (("ds", "connector2"), A("D2"), "#00cccc"),
    (("ds", "connector1"), A("GND"), CGND),
    (("r_ds", "connector0"), A("3V3"), C3V3),
    (("r_ds", "connector1"), ("ds", "connector2"), "#00cccc"),
    # pluie : 3V3 DO GND (DO numérique seulement, D27=ADC2)
    (("rain", "connector0"), A("3V3"), C3V3),
    (("rain", "connector1"), B("D27"), "#0099cc"),
    (("rain", "connector2"), A("GND"), CGND),
    # OLED : GND VCC SCL SDA
    (("oled", "connector0"), A("GND"), CGND),
    (("oled", "connector1"), A("3V3"), C3V3),
    (("oled", "connector2"), A("D22"), "#ffcc00"),
    (("oled", "connector3"), A("D21"), "#0066cc"),
    # servos : gnd(0) vcc(1) pulse(2), série 220R
    (("srv1", "connector0"), B("GND"), CGND),
    (("srv1", "connector1"), B("VIN"), C5V),
    (("srv1", "connector2"), ("r_srv1", "connector1"), "#ff6600"),
    (("r_srv1", "connector0"), B("D25"), "#ff6600"),
    (("srv2", "connector0"), B("GND"), CGND),
    (("srv2", "connector1"), B("VIN"), C5V),
    (("srv2", "connector2"), ("r_srv2", "connector1"), "#66cc00"),
    (("r_srv2", "connector0"), B("D14"), "#66cc00"),
    # commandes relais (transistors sur la carte, actif HAUT)
    (("rel", "connector0"), B("D13"), "#cc6666"),
    (("rel", "connector1"), B("D12"), "#cc9966"),
    (("rel", "connector2"), A("D16"), "#cc66cc"),
    (("rel", "connector3"), A("D17"), "#66cccc"),
    (("rel", "connector4"), A("D19"), "#9966cc"),
    (("rel", "connector5"), A("D23"), "#cc9933"),
    # breakout GPIO libres
    (("brk", "connector0"), A("3V3"), C3V3),
    (("brk", "connector1"), A("GND"), CGND),
    (("brk", "connector2"), B("EN"), "#999999"),
    (("brk", "connector3"), A("D4"), "#999999"),
    (("brk", "connector4"), A("D5"), "#999999"),
    (("brk", "connector5"), A("RX0"), "#999999"),
    (("brk", "connector6"), A("TX0"), "#999999"),
]


def build() -> str:
    model_index = {}
    next_idx = 1000
    inst_xml = []
    # positions absolues des connecteurs (pour accrocher les fils)
    abspins = {}
    for name, kind, title, x, y, props in INSTANCES:
        next_idx += 1
        model_index[name] = next_idx
        cat = CATALOG[kind]
        for cid, (px, py) in cat["pins"].items():
            abspins[(name, cid)] = (x + px, y + py)
        prop_xml = "".join(
            f'\n            <property name="{k}" value="{v}"/>' for k, v in props.items())
        inst_xml.append(f'''        <instance moduleIdRef="{cat['moduleId']}" modelIndex="{next_idx}" path=":/resources/parts/core/">
            <title>{title}</title>{prop_xml}
            <views>
                <breadboardView layer="breadboard">
                    <geometry z="1.5" x="{x}" y="{y}"/>
                </breadboardView>
                <schematicView layer="schematic">
                    <geometry z="1.5" x="{x}" y="{y}"/>
                </schematicView>
                <pcbView layer="copper0">
                    <geometry z="1.5" x="{x}" y="{y}"/>
                </pcbView>
            </views>
        </instance>''')
    # fils + connexions réciproques
    part_connects: dict[str, dict[str, list]] = {}
    wire_xml = []
    for wi, ((na, ca), (nb, cb), color) in enumerate(WIRES):
        next_idx += 1
        widx = next_idx
        xa, ya = abspins[(na, ca)]
        xb, yb = abspins[(nb, cb)]
        part_connects.setdefault(na, {}).setdefault(ca, []).append((widx, "connector0"))
        part_connects.setdefault(nb, {}).setdefault(cb, []).append((widx, "connector1"))
        wire_xml.append(f'''        <instance moduleIdRef="WireModuleID" modelIndex="{widx}" path=":/resources/parts/core/wire.fzp">
            <title>Wire{wi}</title>
            <views>
                <breadboardView layer="breadboardWire">
                    <geometry z="3" x="{xa:.1f}" y="{ya:.1f}" x1="0" y1="0" x2="{xb - xa:.1f}" y2="{yb - ya:.1f}" wireFlags="0"/>
                    <wireExtras mils="22.2222" color="{color}" opacity="1" banded="0"/>
                    <connectors>
                        <connector connectorId="connector0" layer="breadboardWire">
                            <geometry x="0" y="0"/>
                            <connects>
                                <connect connectorId="{ca}" modelIndex="{model_index[na]}" layer="breadboard"/>
                            </connects>
                        </connector>
                        <connector connectorId="connector1" layer="breadboardWire">
                            <geometry x="0" y="0"/>
                            <connects>
                                <connect connectorId="{cb}" modelIndex="{model_index[nb]}" layer="breadboard"/>
                            </connects>
                        </connector>
                    </connectors>
                </breadboardView>
            </views>
        </instance>''')
    # injecte les <connectors> réciproques dans les instances de pièces
    final_inst = []
    for (name, kind, *_), xml in zip(INSTANCES, inst_xml):
        conns = part_connects.get(name)
        if conns:
            blocks = []
            for cid, links in conns.items():
                connects = "".join(
                    f'\n                                <connect connectorId="{wc}" modelIndex="{wm}" layer="breadboardWire"/>'
                    for wm, wc in links)
                blocks.append(f'''                        <connector connectorId="{cid}" layer="breadboard">
                            <geometry x="0" y="0"/>
                            <connects>{connects}
                            </connects>
                        </connector>''')
            conn_xml = ('\n                    <connectors>\n' + "\n".join(blocks)
                        + '\n                    </connectors>')
            xml = xml.replace(
                '                </breadboardView>',
                conn_xml + '\n                </breadboardView>')
        final_inst.append(xml)
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<module fritzingVersion="1.0.2" icon=".png">
    <project_properties/>
    <boards/>
    <views>
        <view name="breadboardView" backgroundColor="#ffffff" gridSize="0.1in" showGrid="1" alignToGrid="0" viewFromBelow="0" colorWiresByLength="0"/>
        <view name="schematicView" backgroundColor="#ffffff" gridSize="0.1in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>
        <view name="pcbView" backgroundColor="#333333" gridSize="0.05in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>
    </views>
    <instances>
{chr(10).join(final_inst)}
{chr(10).join(wire_xml)}
    </instances>
</module>
'''


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    fz = build()
    ET.fromstring(fz)  # auto-validation XML
    out = OUT_DIR / "n3pp-msp-commun-cablage.fzz"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("n3pp-msp-commun-cablage.fz", fz)
    print(f"OK: {out} ({len(INSTANCES)} pièces, {len(WIRES)} fils)")


if __name__ == "__main__":
    main()
