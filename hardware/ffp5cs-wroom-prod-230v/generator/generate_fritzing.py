#!/usr/bin/env python3
"""Génère un fichier Fritzing (.fzz) du câblage ffp5cs wroom-prod-230v (vue breadboard).

Vue du câblage PÉRIPHÉRIQUES <-> DevKit de la carte porteuse 230V. Les charges
230V ne figurent PAS ici : elles se câblent uniquement sur les borniers NC/COM/NO
de la zone secteur isolée de la carte (voir README).

Représente le câblage périphériques <-> ESP32 DevKit V1 (utile pour un montage
au banc ou sur protoboard) avec les pièces de la bibliothèque core Fritzing :
HC-SR04, DS18B20, LDR, servos, résistances ; le DevKit, le DHT11, l'OLED et les
sorties relais sont figurés par des headers génériques étiquetés (pas de pièce
core fidèle). Les couleurs de fils suivent l'usage : rouge=5V, orange=3V3,
noir=GND, autres=signaux.

⚠️ Livrable « best effort » : la structure .fz est calquée sur les sketches
officiels de Fritzing 1.0 mais il n'existe pas de validateur en CLI — ouvrir
dans Fritzing >= 1.0 pour vérifier. Le PCB de référence reste le projet KiCad.

Usage : python3 generate_fritzing.py   (écrit ../fritzing/ffp5cs-wroom-prod-cablage.fzz)
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

US_PINS = svg_pins("core/breadboard/hc-sr04_bf8299a_002.svg",
                   {f"connector{i}pin" for i in range(4)})
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
    "us": dict(moduleId="hc-sr0489a24442a28f4051a3a8757dd4825d87",
               pins=strippin(US_PINS)),
    "res": dict(moduleId="ResistorModuleID", pins=strippin(R_PINS)),
    "ds18b20": dict(moduleId="DS18B20_fixed", pins=strippin(DS_PINS)),
    "servo": dict(moduleId="Dagu_DGServo_9g_End_view", pins=strippin(SRV_PINS)),
    "ldr": dict(moduleId="2010BBCD20113", pins=strippin(LDR_PINS)),
    "hdr15": dict(moduleId=HDR.format(n=15), pins=header_pins(15)),
    "hdr4": dict(moduleId=HDR.format(n=4), pins=header_pins(4)),
    "hdr3": dict(moduleId=HDR.format(n=3), pins=header_pins(3)),
    "hdr2": dict(moduleId=HDR.format(n=2), pins=header_pins(2)),
    "hdr6": dict(moduleId=HDR.format(n=6), pins=header_pins(6)),
    "hdr14": dict(moduleId=HDR.format(n=14), pins=header_pins(14)),
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
    ("us1", "us", "HC-SR04 aquarium (D4)", 30, 30, {}),
    ("us2", "us", "HC-SR04 réservoir (D19)", 230, 30, {}),
    ("us3", "us", "HC-SR04 potager (D33)", 430, 30, {}),
    ("r_us1a", "res", "1k écho aqua", 55, 160, {"resistance": "1k"}),
    ("r_us1b", "res", "2k vers GND aqua", 55, 190, {"resistance": "2k"}),
    ("r_us2a", "res", "1k écho réservoir", 255, 160, {"resistance": "1k"}),
    ("r_us2b", "res", "2k vers GND réservoir", 255, 190, {"resistance": "2k"}),
    ("r_us3a", "res", "1k écho potager", 455, 160, {"resistance": "1k"}),
    ("r_us3b", "res", "2k vers GND potager", 455, 190, {"resistance": "2k"}),
    ("dht", "hdr3", "DHT11 : 3V3 DATA GND", 40, 560, {}),
    ("r_dht", "res", "10k pull-up DHT", 40, 530, {"resistance": "10k"}),
    ("ds", "ds18b20", "DS18B20 (D26)", 170, 560, {}),
    ("r_ds", "res", "4.7k pull-up 1-Wire", 170, 530, {"resistance": "4.7k"}),
    ("ldr", "ldr", "LDR -> D34", 300, 570, {}),
    ("r_ldr", "res", "10k LDR vers GND", 300, 530, {"resistance": "10k"}),
    ("oled", "hdr4", "OLED SSD1306 : GND VCC SCL SDA", 420, 560, {}),
    ("srv1", "servo", "Servo GROS (D12)", 640, 300, {}),
    ("srv2", "servo", "Servo PETITS (D13)", 640, 380, {}),
    ("r_srv1", "res", "220 signal gros", 560, 300, {"resistance": "220"}),
    ("r_srv2", "res", "220 signal petits", 560, 380, {"resistance": "220"}),
    ("r_pd12", "res", "10k pull-down D12 (strapping)", 560, 340, {"resistance": "10k"}),
    ("rel", "hdr4", "Commandes relais INTERNES carte 230V : D16 D18 D2 D15 (charges sur borniers NC/COM/NO)", 640, 470, {}),
    ("gpio", "hdr14", "Breakout J17 tous GPIO libres : 3V3 GND EN RX0 TX0 D5 D14 D17 D23 D25 D32 D35 D36 D39", 40, 640, {}),
    ("alim", "hdr6", "Rail alim J20 : 5V 5V GND GND 3V3 3V3", 640, 540, {}),
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
    # HC-SR04 x3 : VCC(0) TRIG(1) ECHO(2) GND(3), mono-broche via 1k/2k
    (("us1", "connector0"), B("VIN"), C5V),
    (("us1", "connector3"), B("GND"), CGND),
    (("us1", "connector1"), A("D4"), "#3399ff"),
    (("us1", "connector2"), ("r_us1a", "connector0"), "#3399ff"),
    (("r_us1a", "connector1"), A("D4"), "#3399ff"),
    (("r_us1b", "connector0"), A("D4"), "#3399ff"),
    (("r_us1b", "connector1"), A("GND"), CGND),
    (("us2", "connector0"), B("VIN"), C5V),
    (("us2", "connector3"), B("GND"), CGND),
    (("us2", "connector1"), A("D19"), "#33cc33"),
    (("us2", "connector2"), ("r_us2a", "connector0"), "#33cc33"),
    (("r_us2a", "connector1"), A("D19"), "#33cc33"),
    (("r_us2b", "connector0"), A("D19"), "#33cc33"),
    (("r_us2b", "connector1"), A("GND"), CGND),
    (("us3", "connector0"), B("VIN"), C5V),
    (("us3", "connector3"), B("GND"), CGND),
    (("us3", "connector1"), B("D33"), "#9933ff"),
    (("us3", "connector2"), ("r_us3a", "connector0"), "#9933ff"),
    (("r_us3a", "connector1"), B("D33"), "#9933ff"),
    (("r_us3b", "connector0"), B("D33"), "#9933ff"),
    (("r_us3b", "connector1"), A("GND"), CGND),
    # DHT11 (header 3 pts : 3V3 DATA GND)
    (("dht", "connector0"), A("3V3"), C3V3),
    (("dht", "connector1"), B("D27"), "#cc33cc"),
    (("dht", "connector2"), A("GND"), CGND),
    (("r_dht", "connector0"), A("3V3"), C3V3),
    (("r_dht", "connector1"), ("dht", "connector1"), "#cc33cc"),
    # DS18B20 : VDD(3) DQ(2) GND(1)
    (("ds", "connector3"), A("3V3"), C3V3),
    (("ds", "connector2"), B("D26"), "#00cccc"),
    (("ds", "connector1"), A("GND"), CGND),
    (("r_ds", "connector0"), A("3V3"), C3V3),
    (("r_ds", "connector1"), ("ds", "connector2"), "#00cccc"),
    # LDR : 3V3 -- LDR -- D34, 10k vers GND
    (("ldr", "connector0"), A("3V3"), C3V3),
    (("ldr", "connector1"), B("D34"), "#996633"),
    (("r_ldr", "connector0"), B("D34"), "#996633"),
    (("r_ldr", "connector1"), A("GND"), CGND),
    # OLED : GND VCC SCL SDA
    (("oled", "connector0"), A("GND"), CGND),
    (("oled", "connector1"), A("3V3"), C3V3),
    (("oled", "connector2"), A("D22"), "#ffcc00"),
    (("oled", "connector3"), A("D21"), "#0066cc"),
    # Servos : gnd(0) vcc(1) pulse(2)
    (("srv1", "connector0"), B("GND"), CGND),
    (("srv1", "connector1"), B("VIN"), C5V),
    (("srv1", "connector2"), ("r_srv1", "connector1"), "#ff6600"),
    (("r_srv1", "connector0"), B("D12"), "#ff6600"),
    (("r_pd12", "connector0"), B("D12"), "#ff6600"),
    (("r_pd12", "connector1"), B("GND"), CGND),
    (("srv2", "connector0"), B("GND"), CGND),
    (("srv2", "connector1"), B("VIN"), C5V),
    (("srv2", "connector2"), ("r_srv2", "connector1"), "#66cc00"),
    (("r_srv2", "connector0"), B("D13"), "#66cc00"),
    # Sorties relais (commande active HAUT)
    (("rel", "connector0"), A("D16"), "#cc6666"),
    (("rel", "connector1"), A("D18"), "#cc9966"),
    (("rel", "connector2"), A("D2"), "#cc66cc"),
    (("rel", "connector3"), A("D15"), "#6666cc"),
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
    out = OUT_DIR / "ffp5cs-wroom-prod-230v-cablage.fzz"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("ffp5cs-wroom-prod-230v-cablage.fz", fz)
    print(f"OK: {out} ({len(INSTANCES)} pièces, {len(WIRES)} fils)")


if __name__ == "__main__":
    main()
