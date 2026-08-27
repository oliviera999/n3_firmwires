#!/usr/bin/env python3
"""Vérifie la cohérence firmware <-> plan PCB (garde anti-dérive).

Trois contrôles :
 1. pinmap.json == section ESP32-WROOM de ffp5cs/include/pins.h (parsée) ;
 2. chaque net GPIO de pinmap.json (netByPin) est présent dans le schéma généré ;
 3. chaque net GPIO est présent dans le PCB généré ET raccordé à la bonne
    broche du module DevKit (pad = position physique du GPIO sur le DevKit V1).

Usage :  python3 check_pinmap_vs_firmware.py   (code retour != 0 si dérive)
À relancer après toute modification de pins.h (WROOM) ou du générateur.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HW = HERE.parent
REPO = HW.parent.parent
PINS_H = REPO / "ffp5cs" / "include" / "pins.h"
SCH = HW / "kicad" / "ffp5cs-wroom-prod.kicad_sch"
PCB = HW / "kicad" / "ffp5cs-wroom-prod.kicad_pcb"

# Brochage physique du DevKit V1 30 broches, tel que modélisé dans l'empreinte
# ESP32_DevKit_V1_30pin (pad -> nom). Doit rester aligné avec generate.py.
DEVKIT_PADS = {
    "1": "3V3", "2": "GND", "3": "GPIO15", "4": "GPIO2", "5": "GPIO4",
    "6": "GPIO16", "7": "GPIO17", "8": "GPIO5", "9": "GPIO18", "10": "GPIO19",
    "11": "GPIO21", "12": "RX0", "13": "TX0", "14": "GPIO22", "15": "GPIO23",
    "16": "VIN", "17": "GND", "18": "GPIO13", "19": "GPIO12", "20": "GPIO14",
    "21": "GPIO27", "22": "GPIO26", "23": "GPIO25", "24": "GPIO33",
    "25": "GPIO32", "26": "GPIO35", "27": "GPIO34", "28": "GPIO39_VN",
    "29": "GPIO36_VP", "30": "EN",
}

# Brochage physique de l'ESP32-S3-DevKitC-1 44 broches, tel que modélisé dans
# l'empreinte ESP32_S3_DevKitC_1_44pin (pad -> nom). Doit rester aligné avec
# S3_LEFT/S3_RIGHT de generate.py. Site A2 (option bi-module, rev >= 0.6).
S3_LEFT = ["3V3", "3V3", "RST", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO15",
           "GPIO16", "GPIO17", "GPIO18", "GPIO8", "GPIO3", "GPIO46", "GPIO9",
           "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "5V", "GND"]
S3_RIGHT = ["GND", "TX0_43", "RX0_44", "GPIO1", "GPIO2", "GPIO42", "GPIO41",
            "GPIO40", "GPIO39", "GPIO38", "GPIO37", "GPIO36", "GPIO35",
            "GPIO0", "GPIO45", "GPIO48", "GPIO47", "GPIO21", "GPIO20",
            "GPIO19", "GND", "GND"]
S3_PADS = {str(i + 1): S3_LEFT[i] for i in range(22)}
S3_PADS.update({str(i + 23): S3_RIGHT[i] for i in range(22)})


def parse_pins_h_s3_carrier() -> dict[str, int]:
    """Extrait la section carrier `#if defined(BOARD_S3) && defined(PINMAP_S3_CARRIER)`."""
    text = PINS_H.read_text(encoding="utf-8")
    m = re.search(
        r"#if defined\(BOARD_S3\) && defined\(PINMAP_S3_CARRIER\)(.*?)#elif defined\(BOARD_S3\)",
        text, re.S)
    if not m:
        sys.exit(f"ERREUR: section carrier PINMAP_S3_CARRIER introuvable dans {PINS_H}")
    section = m.group(1)
    pins: dict[str, int] = {}
    for name, val in re.findall(r"constexpr\s+int\s+(\w+)\s*=\s*(\w+)\s*;", section):
        if val.isdigit():
            pins[name] = int(val)
        elif val in pins:
            pins[name] = pins[val]
    return pins


def extract_fp_pad_nets(pcb: str, fp_name: str) -> dict[str, str] | None:
    """Nets par pad d'une empreinte du PCB (équilibrage de parenthèses)."""
    start = pcb.find(f'(footprint "ffp5cs:{fp_name}"')
    if start < 0:
        return None
    depth, end = 0, start
    for i, ch in enumerate(pcb[start:], start):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    block = pcb[start:end]
    pad_nets: dict[str, str] = {}
    for pm in re.finditer(r'\(pad "(\d+)"', block):
        nxt = block.find('(pad "', pm.end())
        sub = block[pm.end():nxt if nxt > 0 else len(block)]
        nm = re.search(r'\(net \d+ "([^"]+)"\)', sub)
        if nm:
            pad_nets[pm.group(1)] = nm.group(1)
    return pad_nets


def parse_pins_h_wroom() -> dict[str, int]:
    """Extrait les `constexpr int NAME = N;` de la section #else (ESP32-WROOM)."""
    text = PINS_H.read_text(encoding="utf-8")
    m = re.search(r"#else\s*//\s*ESP32-WROOM(.*)#endif", text, re.S)
    if not m:
        sys.exit(f"ERREUR: section '#else // ESP32-WROOM' introuvable dans {PINS_H}")
    section = m.group(1)
    pins: dict[str, int] = {}
    for name, val in re.findall(r"constexpr\s+int\s+(\w+)\s*=\s*(\w+)\s*;", section):
        if val.isdigit():
            pins[name] = int(val)
        elif val in pins:  # alias (ex: EAU_POTAGER = ULTRASON_POTA)
            pins[name] = pins[val]
    return pins




US_CHANNELS = {
    # canal: (jst, r_serie_1k, r_pont_2k, net GPIO attendu (suffixe), net écho)
    "AQUA": ("J7", "R14", "R17"),
    "TANK": ("J8", "R15", "R18"),
    "POTA": ("J9", "R16", "R19"),
}


def check_us_topology(pcb_text: str, pinmap: dict, errors: list[str]) -> None:
    """Vérifie le câblage des canaux HC-SR04 mono-broche dans le PCB généré.

    Le firmware (sensor_ultrasonic.cpp) pilote TRIG et lit ECHO sur le MÊME GPIO :
    TRIG direct, ECHO ramené par pont 1k/2k (2k vers GND = pull-down d'état de repos
    ET adaptation 5V->3V3). Le checker principal ne couvre que les pads du DevKit —
    celui-ci fige la topologie côté capteur.
    """
    import re as _re
    # net GPIO de chaque canal depuis pinmap (netByPin)
    gpio_net = {name: pinmap["netByPin"][f"ULTRASON_{name}"] for name in US_CHANNELS}

    def pads_of(ref: str) -> dict[str, str]:
        blocks = _re.split(r"\n[\t ]*\(footprint ", pcb_text)
        for blk in blocks[1:]:
            m = _re.search(r'\(property "Reference" "' + ref + '"', blk)
            if not m:
                continue
            pads = {}
            for p in blk.split('(pad "')[1:]:
                num = p.split('"')[0]
                n = _re.search(r'\(net \d+ "([^"]+)"', p)
                pads[num] = n.group(1) if n else ""
            return pads
        return {}

    for name, (jst, r1k, r2k) in US_CHANNELS.items():
        g = gpio_net[name]
        echo = f"US_{name}_ECHO"
        jp = pads_of(jst)
        if not jp:
            errors.append(f"US {name}: {jst} introuvable dans le PCB")
            continue
        expect = {"1": "+5V", "2": g, "3": echo, "4": "GND"}
        for pad, net in expect.items():
            if jp.get(pad) != net:
                errors.append(f"US {name}: {jst} pad {pad} = {jp.get(pad)!r}, attendu {net!r}")
        rp = pads_of(r1k)
        if set(rp.values()) != {echo, g}:
            errors.append(f"US {name}: {r1k} (1k série écho) doit relier {echo} et {g}, trouvé {sorted(rp.values())}")
        rp = pads_of(r2k)
        if set(rp.values()) != {g, "GND"}:
            errors.append(f"US {name}: {r2k} (2k pull-down) doit relier {g} et GND, trouvé {sorted(rp.values())}")

def main() -> int:
    errors: list[str] = []
    pinmap = json.loads((HW / "pinmap.json").read_text(encoding="utf-8"))
    declared = pinmap["pins"]
    net_by_pin = pinmap["netByPin"]

    # 1. pins.h <-> pinmap.json
    firmware = parse_pins_h_wroom()
    for name, gpio in declared.items():
        if name not in firmware:
            errors.append(f"pinmap.json: '{name}' absent de pins.h (WROOM)")
        elif firmware[name] != gpio:
            errors.append(
                f"DERIVE: {name} = GPIO{firmware[name]} dans pins.h "
                f"mais GPIO{gpio} dans pinmap.json")
    for name, gpio in firmware.items():
        if name not in declared and name != "EAU_POTAGER" and not name.startswith("OLED"):
            errors.append(f"pins.h: '{name}' (GPIO{gpio}) non couvert par pinmap.json")

    # cohérence interne : le net doit porter le bon numéro de GPIO dans son nom
    for name, net in net_by_pin.items():
        gpio = declared.get(name)
        if gpio is not None and not net.endswith(f"GPIO{gpio}"):
            errors.append(f"pinmap.json: net '{net}' ne se termine pas par GPIO{gpio}")

    # 2. présence des nets dans le schéma
    if SCH.exists():
        sch = SCH.read_text(encoding="utf-8")
        for name, net in net_by_pin.items():
            if f'(label "{net}"' not in sch:
                errors.append(f"schéma: net '{net}' ({name}) absent")
    else:
        errors.append(f"schéma introuvable: {SCH} (lancer generator/generate.py)")

    # 3. nets du PCB raccordés à la bonne broche physique du DevKit
    if PCB.exists():
        pcb = PCB.read_text(encoding="utf-8")
        check_us_topology(pcb, pinmap, errors)
        start = pcb.find('(footprint "ffp5cs:ESP32_DevKit_V1_30pin"')
        if start < 0:
            errors.append("PCB: empreinte ESP32_DevKit_V1_30pin introuvable")
        else:
            # extraction du bloc par équilibrage de parenthèses
            depth, end = 0, start
            for i, ch in enumerate(pcb[start:], start):
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        end = i + 1
                        break
            block = pcb[start:end]
            pad_nets = {}
            for pm in re.finditer(r'\(pad "(\d+)"', block):
                sub = block[pm.end():block.find('(pad "', pm.end())
                            if block.find('(pad "', pm.end()) > 0 else len(block)]
                nm = re.search(r'\(net \d+ "([^"]+)"\)', sub)
                if nm:
                    pad_nets[pm.group(1)] = nm.group(1)
            for name, net in net_by_pin.items():
                gpio_name = f"GPIO{declared[name]}"
                expected_pad = next(
                    (p for p, n in DEVKIT_PADS.items() if n == gpio_name), None)
                if expected_pad is None:
                    errors.append(f"PCB: {gpio_name} n'existe pas sur le DevKit V1 30p")
                elif pad_nets.get(expected_pad) != net:
                    errors.append(
                        f"PCB: pad {expected_pad} ({gpio_name}) porte "
                        f"'{pad_nets.get(expected_pad)}' au lieu de '{net}'")
    else:
        errors.append(f"PCB introuvable: {PCB} (lancer generator/generate.py)")

    # 4. Site A2 (ESP32-S3-DevKitC-1, option bi-module) : pins.h (carrier)
    #    <-> pinmap_s3_carrier.json <-> pads de l'empreinte S3 du PCB.
    s3_pinmap_path = HW / "pinmap_s3_carrier.json"
    if s3_pinmap_path.exists():
        s3_pinmap = json.loads(s3_pinmap_path.read_text(encoding="utf-8"))
        s3_declared = s3_pinmap["pins"]
        s3_net_by_pin = s3_pinmap["netByPin"]
        s3_firmware = parse_pins_h_s3_carrier()
        for name, gpio in s3_declared.items():
            if name not in s3_firmware:
                errors.append(f"pinmap_s3_carrier.json: '{name}' absent de pins.h (carrier)")
            elif s3_firmware[name] != gpio:
                errors.append(
                    f"DERIVE S3: {name} = GPIO{s3_firmware[name]} dans pins.h (carrier) "
                    f"mais GPIO{gpio} dans pinmap_s3_carrier.json")
        for name, gpio in s3_firmware.items():
            if (name not in s3_declared and name != "EAU_POTAGER"
                    and not name.startswith(("OLED", "SD_"))):
                errors.append(
                    f"pins.h (carrier): '{name}' (GPIO{gpio}) non couvert par pinmap_s3_carrier.json")
        if PCB.exists():
            pcb = PCB.read_text(encoding="utf-8")
            s3_pads = extract_fp_pad_nets(pcb, "ESP32_S3_DevKitC_1_44pin")
            if s3_pads is None:
                errors.append("PCB: empreinte ESP32_S3_DevKitC_1_44pin (site A2) introuvable")
            else:
                for name, net in s3_net_by_pin.items():
                    gpio_name = f"GPIO{s3_declared[name]}"
                    expected_pad = next(
                        (p for p, n in S3_PADS.items() if n == gpio_name), None)
                    if expected_pad is None:
                        errors.append(f"PCB: {gpio_name} n'existe pas sur le S3-DevKitC-1 44p")
                    elif s3_pads.get(expected_pad) != net:
                        errors.append(
                            f"PCB site A2: pad {expected_pad} ({gpio_name}) porte "
                            f"'{s3_pads.get(expected_pad)}' au lieu de '{net}'")
                # garde-fous : broches interdites jamais câblées côté A2
                forbidden = {"GPIO0", "GPIO3", "GPIO45", "GPIO46", "GPIO19", "GPIO20",
                             "GPIO35", "GPIO36", "GPIO37", "GPIO38", "GPIO48",
                             "TX0_43", "RX0_44"}
                for pad, gname in S3_PADS.items():
                    if gname in forbidden and s3_pads.get(pad):
                        errors.append(
                            f"PCB site A2: pad {pad} ({gname}) ne doit PAS être câblé "
                            f"(strapping/USB/UART0/PSRAM/LED), trouvé '{s3_pads[pad]}'")
    else:
        errors.append(f"pinmap_s3_carrier.json introuvable: {s3_pinmap_path}")

    if errors:
        print("ECHEC — incohérences firmware <-> plan PCB :")
        for e in errors:
            print("  -", e)
        return 1
    print(f"OK — {len(net_by_pin)} signaux WROOM (site A1) + {len(s3_net_by_pin)} signaux "
          "S3 carrier (site A2) cohérents entre pins.h, pinmaps, schéma et PCB.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
