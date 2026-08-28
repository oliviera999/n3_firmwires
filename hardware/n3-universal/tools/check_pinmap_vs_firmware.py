#!/usr/bin/env python3
"""Garde anti-dérive de la carte UNIVERSELLE n3-universal.

Vérifie que les cartographies `PINMAP_UNIVERSAL` des TROIS firmwares (msp,
n3pp, ffp5cs WROOM + ffp5cs S3) et le PCB généré racontent la même histoire :
 1. chaque fonction firmware est sur le GPIO attendu (net universel) ;
 2. chaque net universel est raccordé au BON pad du site A1 (WROOM) ET du
    site A2 (S3-DevKitC-1) dans le PCB généré ;
 3. les broches S3 interdites (strapping durs, USB, UART0, flash/PSRAM) ne
    portent aucun net ;
 4. topologies critiques : canaux HC-SR04 (pont 1k/2k), power-gate +3V3_SW
    (R34->Q8->Q7 + R35 + JP1), diviseur VBAT commuté, cavaliers SD.

Usage : python3 check_pinmap_vs_firmware.py  (code retour != 0 si dérive)
"""
from __future__ import annotations
import json, re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HW = HERE.parent
REPO = HW.parent.parent
PCB = HW / "kicad" / "n3-universal.kicad_pcb"
SCH = HW / "kicad" / "n3-universal.kicad_sch"
PINMAP = json.loads((HW / "pinmap_universel_propose.json").read_text(encoding="utf-8"))

# --- brochages physiques des deux modules (alignés sur generate.py) ----------
DEVKIT_PADS = {
    "1": "3V3", "2": "GND", "3": "GPIO15", "4": "GPIO2", "5": "GPIO4",
    "6": "GPIO16", "7": "GPIO17", "8": "GPIO5", "9": "GPIO18", "10": "GPIO19",
    "11": "GPIO21", "12": "RX0", "13": "TX0", "14": "GPIO22", "15": "GPIO23",
    "16": "VIN", "17": "GND", "18": "GPIO13", "19": "GPIO12", "20": "GPIO14",
    "21": "GPIO27", "22": "GPIO26", "23": "GPIO25", "24": "GPIO33",
    "25": "GPIO32", "26": "GPIO35", "27": "GPIO34", "28": "GPIO39",
    "29": "GPIO36", "30": "EN",
}
S3_LEFT = ["3V3", "3V3", "RST", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO15",
           "GPIO16", "GPIO17", "GPIO18", "GPIO8", "GPIO3", "GPIO46", "GPIO9",
           "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "5V", "GND"]
S3_RIGHT = ["GND", "TX0_43", "RX0_44", "GPIO1", "GPIO2", "GPIO42", "GPIO41",
            "GPIO40", "GPIO39", "GPIO38", "GPIO37", "GPIO36", "GPIO35",
            "GPIO0", "GPIO45", "GPIO48", "GPIO47", "GPIO21", "GPIO20",
            "GPIO19", "GND", "GND"]
S3_PADS = {str(i + 1): S3_LEFT[i] for i in range(22)}
S3_PADS.update({str(i + 23): S3_RIGHT[i] for i in range(22)})

# --- fonction firmware -> net universel --------------------------------------
MSP_FN = {"RELAIS": "GATE", "LUMINOSITEa": "ADC_A", "LUMINOSITEb": "ADC_B",
          "LUMINOSITEc": "ADC_C", "LUMINOSITEd": "ADC_D", "SERVOGD": "SERVO1",
          "SERVOHB": "SERVO2", "DHTPININT": "DHT_INT", "DHTPINEXT": "US2",
          "HumiditeSol": "ADC_E", "PLUIE": "US1", "pontdiv": "ADC_VBAT"}
N3PP_FN = {"RELAIS": "GATE", "POMPE": "K1", "pontdiv": "ADC_VBAT",
           "humidite1": "ADC_A", "humidite2": "ADC_B", "humidite3": "ADC_C",
           "humidite4": "ADC_D", "LUMINOSITE": "ADC_E", "DHTPIN": "DHT_INT"}
FFP_FN = {"ULTRASON_AQUA": "US1", "ULTRASON_TANK": "US2", "ULTRASON_POTA": "US3",
          "LUMINOSITE": "ADC_E", "POMPE_AQUA": "K1", "POMPE_RESERV": "K2",
          "RADIATEURS": "K3", "LUMIERE": "K4", "AUX1": "K5", "AUX2": "K6",
          "SERVO_GROS": "SERVO1", "SERVO_PETITS": "SERVO2", "DHT_PIN": "DHT_INT",
          "ONE_WIRE_BUS": "ONEWIRE", "I2C_SDA": "I2C_SDA", "I2C_SCL": "I2C_SCL"}
# nets universels -> GPIO attendu par site (source : pinmap_universel_propose.json)
WROOM_NET_GPIO = {net: gpio for net, gpio in PINMAP["wroom"].items()}
S3_NET_GPIO = {net: gpio for net, gpio in PINMAP["s3"].items()}
# ajustements : sur la carte, K5/K6 = nets des AUX, SD par cavaliers
WROOM_NET_GPIO.update({"K5": WROOM_NET_GPIO.pop("AUX1"), "K6": WROOM_NET_GPIO.pop("AUX2"),
                       "GATE": PINMAP["wroom"]["GATE"]})
S3_NET_GPIO.update({"K5": S3_NET_GPIO.pop("AUX1"), "K6": S3_NET_GPIO.pop("AUX2")})

S3_FORBIDDEN = {"GPIO0", "GPIO46", "GPIO19", "GPIO20", "GPIO35", "GPIO36",
                "GPIO37", "GPIO11"}


def parse_defines(text: str) -> dict[str, int]:
    out = {}
    for name, val in re.findall(r"#define\s+(\w+)\s+(\d+)\b", text):
        out.setdefault(name, int(val))
    return out


def section(text: str, start_pat: str, end_pat: str, what: str) -> str:
    m = re.search(start_pat + r"(.*?)" + end_pat, text, re.S)
    if not m:
        sys.exit(f"ERREUR: section {what} introuvable")
    return m.group(1)


def parse_constexpr(text: str) -> dict[str, int]:
    pins = {}
    for name, val in re.findall(r"constexpr\s+int\s+(\w+)\s*=\s*(\w+)\s*;", text):
        if val.isdigit():
            pins[name] = int(val)
        elif val in pins:
            pins[name] = pins[val]
    return pins


def extract_fp_pad_nets(pcb: str, fp_name: str) -> dict[str, str] | None:
    start = pcb.find(f'(footprint "n3u:{fp_name}"')
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
    pad_nets = {}
    for pm in re.finditer(r'\(pad "(\d+)"', block):
        nxt = block.find('(pad "', pm.end())
        sub = block[pm.end():nxt if nxt > 0 else len(block)]
        nm = re.search(r'\(net \d+ "([^"]+)"\)', sub)
        if nm:
            pad_nets[pm.group(1)] = nm.group(1)
    return pad_nets


def pads_of(pcb_text: str, ref: str) -> dict[str, str]:
    blocks = re.split(r"\n[\t ]*\(footprint ", pcb_text)
    for blk in blocks[1:]:
        if re.search(r'\(property "Reference" "' + ref + '"', blk):
            pads = {}
            for pp in blk.split('(pad "')[1:]:
                num = pp.split('"')[0]
                n = re.search(r'\(net \d+ "([^"]+)"', pp)
                pads[num] = n.group(1) if n else ""
            return pads
    return {}


def main() -> int:
    errors: list[str] = []

    # 1. firmwares : fonction -> GPIO attendu du net (par site)
    msp = parse_defines(section((REPO / "msp/include/msp_config.h").read_text(encoding="utf-8"),
                                r"#if defined\(PINMAP_UNIVERSAL\)", r"#else", "msp PINMAP_UNIVERSAL"))
    n3pp = parse_defines(section((REPO / "n3pp/include/n3pp_config.h").read_text(encoding="utf-8"),
                                 r"#if defined\(PINMAP_UNIVERSAL\)", r"#else", "n3pp PINMAP_UNIVERSAL"))
    pins_h = (REPO / "ffp5cs/include/pins.h").read_text(encoding="utf-8")
    ffp_s3 = parse_constexpr(section(pins_h, r"#elif defined\(BOARD_S3\) && defined\(PINMAP_UNIVERSAL\)",
                                     r"#elif", "ffp5cs S3 universel"))
    ffp_wroom = parse_constexpr(section(pins_h, r"#elif !defined\(BOARD_S3\) && defined\(PINMAP_UNIVERSAL\)",
                                        r"#elif", "ffp5cs WROOM universel"))

    for fw, fns, pins, ref in (("msp", MSP_FN, msp, WROOM_NET_GPIO),
                               ("n3pp", N3PP_FN, n3pp, WROOM_NET_GPIO),
                               ("ffp5cs-WROOM", FFP_FN, ffp_wroom, WROOM_NET_GPIO),
                               ("ffp5cs-S3", FFP_FN, ffp_s3, S3_NET_GPIO)):
        for fn, net in fns.items():
            if fn not in pins:
                errors.append(f"{fw}: fonction '{fn}' absente de la section universelle")
                continue
            want = ref.get(net)
            if want is None:
                errors.append(f"{fw}: net '{net}' absent du pinmap universel")
            elif pins[fn] != want:
                errors.append(f"DERIVE {fw}: {fn} = GPIO{pins[fn]}, attendu GPIO{want} (net {net})")
    # OneWire msp : const hors section (oneWireBus = 2)
    msp_full = (REPO / "msp/include/msp_config.h").read_text(encoding="utf-8")
    m = re.search(r"oneWireBus\s*=\s*(\d+)", msp_full)
    if not m or int(m.group(1)) != PINMAP["wroom"]["ONEWIRE"]:
        errors.append(f"msp: oneWireBus != GPIO{PINMAP['wroom']['ONEWIRE']} (net ONEWIRE)")
    # SD ffp5cs : constantes des deux sections
    for name, want in (("SD_CS_PIN", 10), ("SD_MOSI_PIN", 12), ("SD_CLK_PIN", 13), ("SD_MISO_PIN", 14)):
        if ffp_s3.get(name) != want:
            errors.append(f"ffp5cs-S3: {name} = {ffp_s3.get(name)}, attendu {want}")
    for name, want in (("SD_CS_PIN", 14), ("SD_CLK_PIN", 23), ("SD_MOSI_PIN", 25), ("SD_MISO_PIN", 12)):
        if ffp_wroom.get(name) != want:
            errors.append(f"ffp5cs-WROOM: {name} = {ffp_wroom.get(name)}, attendu {want} (option wroom-sd)")

    # 2. PCB : nets des pads des deux sites
    if not PCB.exists():
        errors.append(f"PCB introuvable: {PCB} (lancer generator/generate.py)")
    else:
        pcb = PCB.read_text(encoding="utf-8")
        a1 = extract_fp_pad_nets(pcb, "ESP32_DevKit_V1_30pin")
        a2 = extract_fp_pad_nets(pcb, "ESP32_S3_DevKitC_1_44pin")
        if a1 is None:
            errors.append("PCB: site A1 (DevKit V1) introuvable")
        if a2 is None:
            errors.append("PCB: site A2 (S3-DevKitC-1) introuvable")
        if a1 and a2:
            for net, gpio in WROOM_NET_GPIO.items():
                pad = next((pp for pp, g in DEVKIT_PADS.items() if g == f"GPIO{gpio}"), None)
                if pad is None:
                    errors.append(f"A1: GPIO{gpio} inexistant sur le DevKit V1")
                elif a1.get(pad) != net:
                    errors.append(f"A1: pad {pad} (GPIO{gpio}) porte '{a1.get(pad)}', attendu '{net}'")
            # SD WROOM : le pad GPIO12 porte SD_MISO
            if a1.get("19") != "SD_MISO":
                errors.append(f"A1: pad 19 (GPIO12) porte '{a1.get('19')}', attendu 'SD_MISO'")
            for net, gpio in S3_NET_GPIO.items():
                if net.startswith("SD_") and net != "SD_MISO":
                    net_pcb = net + "_S3"      # côté S3, CS/CLK/MOSI passent par les cavaliers
                else:
                    net_pcb = net
                pad = next((pp for pp, g in S3_PADS.items() if g == f"GPIO{gpio}"), None)
                if pad is None:
                    errors.append(f"A2: GPIO{gpio} inexistant sur le S3-DevKitC-1")
                elif a2.get(pad) != net_pcb:
                    errors.append(f"A2: pad {pad} (GPIO{gpio}) porte '{a2.get(pad)}', attendu '{net_pcb}'")
            for pad, gname in S3_PADS.items():
                if gname in S3_FORBIDDEN and a2.get(pad):
                    errors.append(f"A2: pad {pad} ({gname}) ne doit PAS être câblé, trouvé '{a2[pad]}'")

        # 3. topologies critiques
        for name, jst, r1k, r2k, net in (("AQUA", "J7", "R14", "R17", "US1"),
                                          ("TANK", "J8", "R15", "R18", "US2"),
                                          ("POTA", "J9", "R16", "R19", "US3")):
            jp = pads_of(pcb, jst)
            expect = {"1": "+5V", "2": net, "3": f"US_{name}_ECHO", "4": "GND"}
            for pad, want in expect.items():
                if jp.get(pad) != want:
                    errors.append(f"US {name}: {jst} pad {pad} = {jp.get(pad)!r}, attendu {want!r}")
            if set(pads_of(pcb, r1k).values()) != {f"US_{name}_ECHO", net}:
                errors.append(f"US {name}: {r1k} doit relier US_{name}_ECHO et {net}")
            if set(pads_of(pcb, r2k).values()) != {net, "GND"}:
                errors.append(f"US {name}: {r2k} doit relier {net} et GND")
        # power-gate
        checks = [("R34", {"GATE", "GATE_B"}), ("R35", {"+3V3", "GATE_G"}),
                  ("R36", {"VBAT_SENSE", "PDIV_G"}), ("R37", {"+3V3_SW", "PDIV_B"}),
                  ("R38", {"VBAT_SW", "ADC_VBAT"}), ("R39", {"ADC_VBAT", "GND"})]
        for ref, want in checks:
            got = set(pads_of(pcb, ref).values())
            if got != want:
                errors.append(f"{ref}: nets {sorted(got)}, attendu {sorted(want)}")
        for ref, want in (("Q7", {"1": "GATE_G", "2": "+3V3_SW", "3": "+3V3"}),
                          ("Q9", {"1": "VBAT_SW", "2": "PDIV_G", "3": "VBAT_SENSE"}),
                          ("JP1", {"1": "+3V3", "2": "+3V3_SW"}),
                          ("JP2", {"1": "SD_CS_S3", "2": "SD_CS", "3": "US3"}),
                          ("JP3", {"1": "SD_CLK_S3", "2": "SD_CLK", "3": "K5"}),
                          ("JP4", {"1": "SD_MOSI_S3", "2": "SD_MOSI", "3": "K6"})):
            got = pads_of(pcb, ref)
            for pad, wnet in want.items():
                if got.get(pad) != wnet:
                    errors.append(f"{ref} pad {pad}: '{got.get(pad)}', attendu '{wnet}'")

    if errors:
        print("ECHEC — incohérences firmware(s) <-> plan PCB :")
        for e in errors:
            print("  -", e)
        return 1
    print(f"OK — cartographies universelles cohérentes : msp ({len(MSP_FN)} fn), "
          f"n3pp ({len(N3PP_FN)} fn), ffp5cs WROOM+S3 ({len(FFP_FN)} fn x 2 sites), "
          "sites A1/A2, broches S3 interdites, topologies US/gate/diviseur/SD.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
