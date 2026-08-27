#!/usr/bin/env python3
"""Étude de faisabilité « n3-universal » : une carte porteuse commune msp + n3pp + ffp5cs.

Étape 0 du projet (avant toute génération de PCB) : prouver ou infirmer le budget
GPIO d'une carte bi-module (site A1 = ESP32 DevKit V1 WROOM 30p, site A2 =
ESP32-S3-DevKitC-1 44p) portant l'UNION des connecteurs des trois systèmes,
avec les mécanismes déjà validés ailleurs dans le dépôt :
  - rail capteurs commuté +3V3_SW + pont diviseur commuté (n3pp-msp-commun rev 0.2),
  - double site module + pinmap par flag (ffp5cs-wroom-prod rev 0.6, PINMAP_S3_CARRIER),
  - nets PARTAGÉS entre rôles : un même GPIO alimente deux connecteurs différents,
    utilisés par des firmwares DISJOINTS (déjà pratiqué sur la carte commune).

Ajouts demandés : slot microSD (S3 uniquement), RTC DS3231 et 2-3 INA219/226
(I2C, coût GPIO nul, INA sur +3V3_SW pour la veille).

Usage : python3 etude_pinmap.py   (imprime le verdict, écrit ETUDE_PINMAP.md)
"""

from __future__ import annotations
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ---------------------------------------------------------------------------
# 1. Fonctions à porter, et quel firmware utilise quoi (union des 3 systèmes)
#    Chaque « net » de la carte est utilisé par un sous-ensemble de firmwares ;
#    deux fonctions peuvent PARTAGER un net ssi leurs firmwares sont disjoints.
# ---------------------------------------------------------------------------
# net -> {firmware: fonction}
NETS: dict[str, dict[str, str]] = {
    "I2C_SDA":  {"msp": "OLED/BME280", "n3pp": "OLED/BME280", "ffp5cs": "OLED/DS3231/INA"},
    "I2C_SCL":  {"msp": "OLED/BME280", "n3pp": "OLED/BME280", "ffp5cs": "OLED/DS3231/INA"},
    # bloc analogique (ADC1 exigé : utilisable WiFi actif)
    "ADC_A":    {"msp": "LUMINOSITEa", "n3pp": "Humid1"},
    "ADC_B":    {"msp": "LUMINOSITEb", "n3pp": "Humid2"},
    "ADC_C":    {"msp": "LUMINOSITEc", "n3pp": "Humid3"},
    "ADC_D":    {"msp": "LUMINOSITEd", "n3pp": "Humid4"},
    "ADC_E":    {"msp": "HumiditeSol", "n3pp": "Luminosite", "ffp5cs": "LUMINOSITE (LDR)"},
    "ADC_VBAT": {"msp": "pontdiv", "n3pp": "pontdiv"},
    # relais embarqués (K1..K4) + 2 sorties AUX en breakout (modules externes)
    "K1":       {"n3pp": "pompe arrosage", "ffp5cs": "POMPE_AQUA"},
    "K2":       {"ffp5cs": "POMPE_RESERV"},
    "K3":       {"ffp5cs": "RADIATEURS"},
    "K4":       {"ffp5cs": "LUMIERE"},
    "AUX1":     {"ffp5cs": "AUX1 (breakout)"},
    "AUX2":     {"ffp5cs": "AUX2 (breakout)"},
    # servos (2 headers partagés)
    "SERVO1":   {"msp": "SERVOGD (tracker)", "ffp5cs": "SERVO_GROS"},
    "SERVO2":   {"msp": "SERVOHB (tracker)", "ffp5cs": "SERVO_PETITS"},
    # ultrasons mono-broche (JST) — partagés avec des fonctions msp/n3pp-only
    "US1":      {"ffp5cs": "ULTRASON_AQUA", "msp": "PLUIE (DO)"},
    "US2":      {"ffp5cs": "ULTRASON_TANK", "msp": "DHT_EXT (legacy, préférer BME280 0x77)"},
    "US3":      {"ffp5cs": "ULTRASON_POTA"},
    # capteurs partout
    "DHT_INT":  {"msp": "DHTPININT", "n3pp": "DHT", "ffp5cs": "DHT_PIN"},
    "ONEWIRE":  {"msp": "TempEau (DS18B20)", "ffp5cs": "DS18B20"},
    # power-gate (rail +3V3_SW) — bypass fermé par défaut (jumper) pour ffp5cs
    "GATE":     {"msp": "RELAIS (gate)", "n3pp": "RELAIS (gate)"},
    # microSD — S3 uniquement (le firmware ffp5cs ne la gère qu'en BOARD_S3)
    "SD_CS":    {"ffp5cs": "SD_CS (S3 only)"},
    "SD_MOSI":  {"ffp5cs": "SD_MOSI (S3 only)"},
    "SD_CLK":   {"ffp5cs": "SD_CLK (S3 only)"},
    "SD_MISO":  {"ffp5cs": "SD_MISO (S3 only)"},
}
S3_ONLY_NETS = {"SD_CS", "SD_MOSI", "SD_CLK", "SD_MISO"}
ADC_NETS = {n for n in NETS if n.startswith("ADC_")}

# ---------------------------------------------------------------------------
# 2. Modules : broches disponibles et contraintes
# ---------------------------------------------------------------------------
# WROOM DevKit V1 30p — GPIO exposés utilisables ; 34/35/36/39 = entrée seule
WROOM_PINS = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27,
              32, 33, 34, 35, 36, 39}
WROOM_INPUT_ONLY = {34, 35, 36, 39}
WROOM_ADC1 = {32, 33, 34, 35, 36, 39}
WROOM_CAVEATS = {
    12: "strapping MTDI : PAS de pull-up externe (flash 3V3)",
    2:  "strapping : éviter pull-up fort pendant le flash UART",
    15: "strapping : doit être haut au boot (pull-up OK)",
    0:  "strapping BOOT — non exposé ici",
}

# ESP32-S3-DevKitC-1 44p — politique STRICTE puis PRAGMATIQUE
S3_ALL = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
          38, 39, 40, 41, 42, 45, 47, 48}
S3_ADC1 = set(range(1, 11))            # ADC1 = GPIO1..10 (ADC2 inutilisable WiFi)
S3_STRICT_EXCLUDE = {3, 45, 38, 48, 11}    # strapping (3/45), LED RGB (38/48), 11=ADC2
S3_PRAGMATIC_CAVEATS = {
    3:  "strapping JTAG-sel : OK si JTAG inutilisé ; pull-up admissible",
    45: "strapping VDD_SPI : NE JAMAIS tirer haut au boot (pas de pull-up !)",
    38: "LED RGB DevKitC-1 v1.1 : scintillement cosmétique si utilisé",
    48: "LED RGB DevKitC-1 v1.0 : scintillement cosmétique si utilisé",
}
# exclus d'office (pas dans S3_ALL) : 0/46 strapping durs, 19/20 USB, 26-32 flash,
# 33-37 PSRAM octale (modules R8), 43/44 UART0.

# ---------------------------------------------------------------------------
# 3. Affectations proposées
# ---------------------------------------------------------------------------
WROOM_MAP = {
    "I2C_SDA": 21, "I2C_SCL": 22,
    "ADC_A": 32, "ADC_B": 33, "ADC_C": 34, "ADC_D": 35, "ADC_E": 36, "ADC_VBAT": 39,
    "K1": 16, "K2": 17, "K3": 18, "K4": 19,
    "AUX1": 23, "AUX2": 25,
    "SERVO1": 26, "SERVO2": 27,
    "US1": 4, "US2": 5, "US3": 14,
    "DHT_INT": 15, "ONEWIRE": 2, "GATE": 13,
    # SD : absente du site WROOM (S3 uniquement) ; GPIO12 reste libre (header)
}
S3_MAP = {
    "I2C_SDA": 8, "I2C_SCL": 9,
    "ADC_A": 1, "ADC_B": 2, "ADC_C": 4, "ADC_D": 5, "ADC_E": 6, "ADC_VBAT": 7,
    "SD_CS": 10, "SD_MOSI": 12, "SD_CLK": 13, "SD_MISO": 14,
    "K1": 15, "K2": 16, "K3": 17, "K4": 18,
    "SERVO1": 21, "SERVO2": 47,
    "US1": 39, "US2": 40, "US3": 41,
    "ONEWIRE": 42,
    "GATE": 3,          # pull-up de grille admissible (JTAG inutilisé) + rail ON au boot
    "DHT_INT": 38,      # caveat LED v1.1 (cosmétique)
    "AUX1": 48,         # caveat LED v1.0 (cosmétique, breakout)
    "AUX2": 45,         # caveat strapping : entrée de module relais SANS pull-up
}

# ---------------------------------------------------------------------------
# 4. Vérifications
# ---------------------------------------------------------------------------
def check(map_, pins_avail, adc1, input_only, name, s3=False):
    errs, warns = [], []
    used = {}
    for net, pin in map_.items():
        if pin in used:
            errs.append(f"{name}: GPIO{pin} affecté à {net} ET {used[pin]}")
        used[pin] = net
        if pin not in pins_avail:
            errs.append(f"{name}: GPIO{pin} ({net}) hors des broches admissibles")
        if net in ADC_NETS and pin not in adc1:
            errs.append(f"{name}: {net} doit être sur ADC1, GPIO{pin} ne l'est pas")
        if net not in ADC_NETS and pin in input_only:
            errs.append(f"{name}: {net} (numérique/sortie) sur broche entrée-seule GPIO{pin}")
    # nets partagés : firmwares disjoints ?
    for net, users in NETS.items():
        if net not in map_ and not (not s3 and net in S3_ONLY_NETS):
            errs.append(f"{name}: net {net} non affecté")
    for net, users in NETS.items():
        if len(users) > 1:
            pass  # le partage par rôle est le principe même ; conflit impossible par construction
    free = sorted(pins_avail - set(map_.values()))
    return errs, warns, free

def main():
    report = ["# Étude pinmap « n3-universal » — verdict machine\n",
              "Générée par `etude_pinmap.py` (source de vérité : ce script).\n"]
    all_ok = True
    for name, (m, avail, adc1, in_only, caveats, s3) in {
        "WROOM (site A1)": (WROOM_MAP, WROOM_PINS, WROOM_ADC1, WROOM_INPUT_ONLY, WROOM_CAVEATS, False),
        "S3 pragmatique (site A2)": (S3_MAP, S3_ALL - {11}, S3_ADC1, set(), S3_PRAGMATIC_CAVEATS, True),
        "S3 stricte (site A2)": (S3_MAP, S3_ALL - S3_STRICT_EXCLUDE, S3_ADC1, set(), {}, True),
    }.items():
        errs, warns, free = check(m, avail, adc1, in_only, name, s3)
        status = "✅ TIENT" if not errs else f"❌ {len(errs)} problème(s)"
        report.append(f"\n## {name} — {status}\n")
        nets_count = len(m)
        report.append(f"- {nets_count} nets affectés, {len(free)} broche(s) libre(s) : {free}")
        for e in errs:
            report.append(f"- ❌ {e}")
            all_ok = False if "stricte" not in name else all_ok  # la stricte peut échouer
        used_caveats = {p: c for p, c in caveats.items() if p in m.values()}
        if used_caveats:
            report.append("- ⚠️ Broches à précaution documentée :")
            for p, c in sorted(used_caveats.items()):
                net = next(n for n, pp in m.items() if pp == p)
                report.append(f"  - GPIO{p} ({net}) : {c}")
    report.append("\n## Nets partagés entre rôles (firmwares disjoints par construction)\n")
    for net, users in NETS.items():
        if len(users) > 1 and len({tuple(users)}) :
            pass
    for net, users in sorted(NETS.items()):
        if len(users) > 1:
            report.append(f"- `{net}` : " + " / ".join(f"**{fw}**={fn}" for fw, fn in users.items()))
    report.append("\n## Ajouts I2C (coût GPIO nul)\n")
    report.append("- RTC **DS3231** (0x68) — support dédié ; déjà géré par ffp5cs (`USE_RTC_DS3231`).")
    report.append("- **2-3 × INA219/226** (0x40/0x41/0x44) — mesure courant panneau/batterie/charge ;")
    report.append("  à alimenter sur **+3V3_SW** (0,7-1 mA chacun sinon en veille).")
    report.append("- BME280 0x76/0x77, OLED 0x3C : inchangés. 7 périphériques I2C = charge de bus OK à 100 kHz.")
    report.append("\n## microSD\n")
    report.append("- Slot embarqué **câblé au site S3 uniquement** (GPIO 10/12/13/14) — cohérent avec le")
    report.append("  firmware ffp5cs (SD = `BOARD_S3` seulement) ; site WROOM : sans SD, GPIO12 reste libre.")
    out = "\n".join(report) + "\n"
    (HERE / "ETUDE_PINMAP.md").write_text(out, encoding="utf-8")
    (HERE / "pinmap_universel_propose.json").write_text(json.dumps(
        {"wroom": WROOM_MAP, "s3": S3_MAP, "nets": NETS,
         "s3_caveats": {str(k): v for k, v in S3_PRAGMATIC_CAVEATS.items()}},
        indent=2, ensure_ascii=False), encoding="utf-8")
    print(out)
    return 0 if all_ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
