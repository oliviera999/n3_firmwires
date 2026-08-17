#!/usr/bin/env python3
"""Vérifie la cohérence firmwares <-> plan PCB de la carte COMMUNE n3pp + msp.

La carte implémente l'union disjointe des brochages de DEUX firmwares
inchangés. Contrôles :
 1. pinmap.json == n3pp/include/n3pp_config.h ET msp/include/msp_config.h
    (chaque nom firmware déclaré dans `firmwareNames` doit valoir le même GPIO,
    indirection N3_PONTDIV_PIN de n3_defaults.h résolue) ;
 2. l'union est bien DISJOINTE : aucun GPIO revendiqué par les deux firmwares
    pour des usages différents (hors RELAIS/pontdiv partagés à l'identique) ;
 3. chaque net GPIO de pinmap.json est présent dans le schéma généré ;
 4. chaque net GPIO est raccordé à la bonne broche physique du DevKit V1 dans
    le PCB généré, et la topologie des blocs (relais, DHT, ADC, batterie,
    breakout) est celle attendue par les firmwares.

Usage :  python3 check_pinmap_vs_firmware.py   (code retour != 0 si dérive)
À relancer après toute modification des *_config.h ou du générateur.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HW = HERE.parent
REPO = HW.parent.parent
N3PP_H = REPO / "n3pp" / "include" / "n3pp_config.h"
MSP_H = REPO / "msp" / "include" / "msp_config.h"
DEFAULTS_H = REPO / "shared" / "n3_common" / "src" / "n3_defaults.h"
SCH = HW / "kicad" / "n3pp-msp-commun.kicad_sch"
PCB = HW / "kicad" / "n3pp-msp-commun.kicad_pcb"

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


def parse_defines(path: Path) -> dict[str, int]:
    """#define NAME <int> et `const ... name = <int>;`, lignes commentées exclues.
    Résout une indirection vers les macros N3_* de n3_defaults.h."""
    text = path.read_text(encoding="utf-8")
    defaults = {}
    for name, val in re.findall(r"^\s*#define\s+(N3_\w+)\s+(\d+)\b",
                                DEFAULTS_H.read_text(encoding="utf-8"), re.M):
        defaults[name] = int(val)
    pins: dict[str, int] = {}
    for name, val in re.findall(r"^\s*#define\s+(\w+)\s+(\w+)\b", text, re.M):
        if val.isdigit():
            pins[name] = int(val)
        elif val in defaults:
            pins[name] = defaults[val]
    for name, val in re.findall(
            r"^\s*const\s+[\w ]+\s(\w+)\s*=\s*(\d+)\s*;", text, re.M):
        pins[name] = int(val)
    return pins


def pads_of(pcb_text: str, ref: str) -> dict[str, str]:
    """pad -> net d'une empreinte du PCB généré (par référence)."""
    blocks = re.split(r"\n[\t ]*\(footprint ", pcb_text)
    for blk in blocks[1:]:
        if not re.search(r'\(property "Reference" "' + ref + '"', blk):
            continue
        pads = {}
        for p in blk.split('(pad "')[1:]:
            num = p.split('"')[0]
            n = re.search(r'\(net \d+ "([^"]+)"', p)
            pads[num] = n.group(1) if n else ""
        return pads
    return {}


def expect_pads(pcb: str, ref: str, expected: dict[str, str],
                what: str, errors: list[str]) -> None:
    got = pads_of(pcb, ref)
    if not got:
        errors.append(f"{what}: {ref} introuvable dans le PCB")
        return
    for pad, net in expected.items():
        if got.get(pad) != net:
            errors.append(f"{what}: {ref} pad {pad} = {got.get(pad)!r}, "
                          f"attendu {net!r}")


# Topologie des 6 canaux relais : n -> (bornier, transistor, pull-down base)
RELAY_CHANNELS = {
    1: ("J3", "Q1", "R5"), 2: ("J4", "Q2", "R6"), 3: ("J5", "Q3", "R7"),
    4: ("J6", "Q4", "R8"), 5: ("J7", "Q5", "R29"), 6: ("J8", "Q6", "R32"),
}
RELAY_GPIO_KEY = {1: "RELAIS", 2: "POMPE", 3: "AUX3", 4: "AUX4",
                  5: "AUX5", 6: "AUX6"}
# Capteurs : clé pinmap -> (connecteur, pull-up/série éventuel)
DHT_CHANNELS = {"DHT_N3PP": ("J9", "R21"), "DHT_INT": ("J10", "R22"),
                "DHT_EXT": ("J11", "R23")}
ADC_CHANNELS = {"ADC_A": ("J22", "R14"), "ADC_B": ("J23", "R15"),
                "ADC_C": ("J24", "R16"), "ADC_D": ("J25", "R17"),
                "ADC_E": ("J26", "R18")}


def check_topology(pcb: str, net: dict[str, str], errors: list[str]) -> None:
    """Fige le câblage attendu par les firmwares (actuators/sensors)."""
    for n, (jref, qref, rp) in RELAY_CHANNELS.items():
        g = net[RELAY_GPIO_KEY[n]]
        expect_pads(pcb, jref, {"1": f"REL{n}_NO", "2": f"REL{n}_COM",
                                "3": f"REL{n}_NC"}, f"REL{n} bornier", errors)
        expect_pads(pcb, qref, {"1": f"REL{n}_SW", "2": f"REL{n}_B",
                                "3": "GND"}, f"REL{n} transistor", errors)
        expect_pads(pcb, f"K{n}", {"2": f"REL{n}_SW", "5": "+5V"},
                    f"REL{n} bobine", errors)
        got = pads_of(pcb, rp)
        if set(got.values()) != {f"REL{n}_B", "GND"}:
            errors.append(f"REL{n}: {rp} (pull-down base, état sûr au boot) "
                          f"doit relier REL{n}_B et GND, trouvé {sorted(got.values())}")
    for key, (jref, pull) in DHT_CHANNELS.items():
        expect_pads(pcb, jref, {"1": "+3V3", "2": net[key], "3": "GND"},
                    f"DHT {key}", errors)
        got = pads_of(pcb, pull)
        if set(got.values()) != {"+3V3", net[key]}:
            errors.append(f"DHT {key}: {pull} (pull-up 10k) doit relier "
                          f"+3V3 et {net[key]}, trouvé {sorted(got.values())}")
    for key, (jref, rlow) in ADC_CHANNELS.items():
        expect_pads(pcb, jref, {"1": "+3V3", "2": net[key], "3": "GND"},
                    f"ADC {key}", errors)
        got = pads_of(pcb, rlow)
        if set(got.values()) != {net[key], "GND"}:
            errors.append(f"ADC {key}: {rlow} (bas de pont 10k) doit relier "
                          f"{net[key]} et GND, trouvé {sorted(got.values())}")
    # 1-Wire (bornier + pull-up 4.7k) et pluie (DO numérique)
    expect_pads(pcb, "J12", {"1": "+3V3", "2": net["ONEWIRE"], "3": "GND"},
                "DS18B20", errors)
    got = pads_of(pcb, "R24")
    if set(got.values()) != {"+3V3", net["ONEWIRE"]}:
        errors.append(f"1-Wire: R24 (pull-up 4.7k) doit relier +3V3 et "
                      f"{net['ONEWIRE']}, trouvé {sorted(got.values())}")
    expect_pads(pcb, "J13", {"1": "+3V3", "2": net["PLUIE"], "3": "GND"},
                "PLUIE (DO)", errors)
    # pont batterie 2.2k/2.2k -> GPIO36
    expect_pads(pcb, "J27", {"1": "VBAT", "2": "GND"}, "batterie", errors)
    got = pads_of(pcb, "R34")
    if set(got.values()) != {"VBAT", net["PONTDIV"]}:
        errors.append("batterie: R34 doit relier VBAT et "
                      f"{net['PONTDIV']}, trouvé {sorted(got.values())}")
    got = pads_of(pcb, "R35")
    if set(got.values()) != {net["PONTDIV"], "GND"}:
        errors.append(f"batterie: R35 doit relier {net['PONTDIV']} et GND, "
                      f"trouvé {sorted(got.values())}")
    # breakout GPIO restants + OLED
    expect_pads(pcb, "J18", {"1": "+3V3", "2": "GND", "3": "EN",
                             "4": "SPARE_GPIO4", "5": "SPARE_GPIO5",
                             "6": "RX0", "7": "TX0"}, "breakout J18", errors)
    expect_pads(pcb, "J14", {"1": "GND", "2": "+3V3", "3": net["I2C_SCL"],
                             "4": net["I2C_SDA"]}, "OLED", errors)


def main() -> int:
    errors: list[str] = []
    pinmap = json.loads((HW / "pinmap.json").read_text(encoding="utf-8"))
    declared = pinmap["pins"]
    net_by_pin = pinmap["netByPin"]
    fw_names = pinmap["firmwareNames"]

    # 1. *_config.h <-> pinmap.json (les deux firmwares)
    firmware = {"n3pp": parse_defines(N3PP_H), "msp": parse_defines(MSP_H)}
    for key, gpio in declared.items():
        for fw, name in fw_names.get(key, {}).items():
            actual = firmware[fw].get(name)
            if actual is None:
                errors.append(f"pinmap.json: '{name}' absent de {fw}_config.h")
            elif actual != gpio:
                errors.append(f"DERIVE: {name} = GPIO{actual} dans "
                              f"{fw}_config.h mais GPIO{gpio} dans pinmap.json "
                              f"({key})")

    # 2. union disjointe : un GPIO utilisé par les deux firmwares doit l'être
    #    via la MÊME clé pinmap (usage identique, ex. RELAIS 13, pontdiv 36)
    gpio_owner: dict[int, set[str]] = {}
    for key, gpio in declared.items():
        for fw in fw_names.get(key, {}):
            gpio_owner.setdefault(gpio, set()).add(f"{fw}:{key}")
    for gpio, owners in gpio_owner.items():
        keys = {o.split(":", 1)[1] for o in owners}
        if len(keys) > 1:
            errors.append(f"CONFLIT: GPIO{gpio} revendiqué pour des usages "
                          f"différents : {sorted(owners)}")

    # cohérence interne : le net doit porter le bon numéro de GPIO dans son nom
    for key, net in net_by_pin.items():
        gpio = declared.get(key)
        if gpio is not None and not net.endswith(f"GPIO{gpio}"):
            errors.append(f"pinmap.json: net '{net}' ne se termine pas par GPIO{gpio}")

    # 3. présence des nets dans le schéma
    if SCH.exists():
        sch = SCH.read_text(encoding="utf-8")
        for key, net in net_by_pin.items():
            if f'(label "{net}"' not in sch:
                errors.append(f"schéma: net '{net}' ({key}) absent")
    else:
        errors.append(f"schéma introuvable: {SCH} (lancer generator/generate.py)")

    # 4. nets du PCB raccordés à la bonne broche physique du DevKit + topologie
    if PCB.exists():
        pcb = PCB.read_text(encoding="utf-8")
        check_topology(pcb, net_by_pin, errors)
        start = pcb.find('(footprint "n3commun:ESP32_DevKit_V1_30pin"')
        if start < 0:
            errors.append("PCB: empreinte ESP32_DevKit_V1_30pin introuvable")
        else:
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
            for key, net in net_by_pin.items():
                gpio_name = f"GPIO{declared[key]}"
                expected_pad = next(
                    (p for p, n in DEVKIT_PADS.items()
                     if n == gpio_name or n.startswith(gpio_name + "_")), None)
                if expected_pad is None:
                    errors.append(f"PCB: {gpio_name} n'existe pas sur le DevKit V1 30p")
                elif pad_nets.get(expected_pad) != net:
                    errors.append(
                        f"PCB: pad {expected_pad} ({gpio_name}) porte "
                        f"'{pad_nets.get(expected_pad)}' au lieu de '{net}'")
    else:
        errors.append(f"PCB introuvable: {PCB} (lancer generator/generate.py)")

    if errors:
        print("ECHEC — incohérences firmwares <-> plan PCB :")
        for e in errors:
            print("  -", e)
        return 1
    print(f"OK — {len(net_by_pin)} signaux GPIO cohérents entre n3pp_config.h, "
          "msp_config.h, pinmap.json, le schéma et le PCB (union disjointe vérifiée).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
