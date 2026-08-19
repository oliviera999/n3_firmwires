# Carte commune n3pp + msp (KiCad)

**Un seul PCB pour deux stations** : la serre/élevage (**n3pp**) et la station météo
(**msp**) partagent cette carte porteuse pour **ESP32 DevKit V1 30 broches**. Les deux
brochages firmware forment une **union disjointe vérifiée** (aucun GPIO utilisé pour deux
usages différents) : les firmwares actuels tournent **sans aucune modification**, c'est le
**profil d'assemblage** (quels composants on soude) qui spécialise la carte.

```
pinmap.json                  Source de vérité (union n3pp_config.h + msp_config.h, profils par broche)
kicad/n3pp-msp-commun.*      Projet KiCad 8 (schéma, PCB ROUTÉ) — ouvrable KiCad 8/9/10
BOM.csv                      Nomenclature (séparateur ;) avec colonne Profil
ACHATS.md                    Liste d'achat complète par profil (BOM + périphériques + boîtier + outillage)
ASSEMBLAGE-N3PP.md           Feuille d'assemblage générée : quoi souder pour une station n3pp
ASSEMBLAGE-MSP.md            Feuille d'assemblage générée : quoi souder pour une station msp
exports/gerbers-*.zip        Fabrication (Gerbers + perçages) — 210 x 105 mm, 2 couches
exports/schema.pdf, pcb-*.svg   Rendus pour revue sans KiCad
fritzing/*.fzz               Vue breadboard du câblage (Fritzing >= 1.0)
generator/generate.py        Régénère schéma + PCB (non routé) + BOM + feuilles d'assemblage
generator/route_lv.py        Pipeline de routage (freerouting + couture GND + reliefs)
generator/tidy_silkscreen.py  Range la sérigraphie (repères vs étiquettes)
generator/export_fab.py      Export Gerbers/perçages prêts fabricant + rendus
generator/generate_fritzing.py  Régénère le .fzz de câblage
tools/check_pinmap_vs_firmware.py   Garde anti-dérive DOUBLE : n3pp_config.h ET msp_config.h ↔ plan
```

## Le principe : une base, deux profils

| Profil | Composants à souder | Le firmware tourne tel quel ? |
|---|---|---|
| **commun** | DevKit + alim + REL1 + 5 entrées ADC + pont batterie + I2C/OLED + breakout + distribution | — (socle des deux profils) |
| **n3pp** | + REL2 (pompe GPIO12) + DHT GPIO18 | ✅ `pio run -e esp32dev` (n3pp) |
| **msp** | + servos tracker + 2 DHT + DS18B20 + pluie + 10k LDR | ✅ `pio run -e esp32dev` (msp) |
| **extension** | REL3..REL6 (GPIO 16/17/19/23), à activer côté firmware plus tard | provision matérielle |

Aucun strap, aucun interrupteur : les GPIO des deux firmwares ne se marchent jamais
dessus (voir la table ci-dessous), on peuple ou pas. Les feuilles
[`ASSEMBLAGE-N3PP.md`](ASSEMBLAGE-N3PP.md) / [`ASSEMBLAGE-MSP.md`](ASSEMBLAGE-MSP.md)
sont **générées depuis le même code que le PCB** : elles ne peuvent pas dériver du plan.

## Table des broches (union disjointe)

| GPIO | Bloc carte | n3pp (`n3pp_config.h`) | msp (`msp_config.h`) |
|---|---|---|---|
| 13 | **REL1** (bornier NO/COM/NC) | `RELAIS` | `RELAIS` |
| 12 | **REL2** (strapping MTDI → pull-down base 10k) | `POMPE` | — |
| 16/17/19/23 | **REL3..REL6** extensions AUX | — | — |
| 18 | DHT n3pp (JST, pull-up 10k) | `DHTPIN` | — |
| 26 / 15 | DHT INT / EXT (JST, pull-ups 10k) | — | `DHTPININT` / `DHTPINEXT` |
| 2 | 1-Wire DS18B20 (bornier, pull-up 4,7k) ⚠️ strapping | — | `oneWireBus` |
| 27 | Pluie — **sortie DO numérique** (ADC2 ≠ WiFi) | — | `PLUIE` |
| 33/32/35/34/39 | **ADC A..E** (borniers 3V3/AIN/GND) | `humidite1..4`, `LUMINOSITE` | `LUMINOSITEa..d`, `HumiditeSol` |
| 36 (VP) | Pont batterie 2,2k/2,2k (bornier VBAT/GND) | `pontdiv` | `pontdiv` |
| 25 / 14 | Servos tracker (headers, série 220R) | — | `SERVOGD` / `SERVOHB` |
| 21 / 22 | I2C : OLED 0x3C + **3 ports libres** (pull-ups 4,7k) | ✅ | ✅ |
| 4, 5, EN, RX0, TX0 | **Breakout J18** (tous les GPIO restants) | libres | libres |

## Les demandes spécifiques de cette carte

- **6 canaux relais** embarqués (transistor BC337 + diode + LED témoin + bornier
  NO/COM/NC) : REL1/REL2 pilotés par les firmwares actuels, REL3..REL6 en réserve sur des
  GPIO **non-strapping** — le jour voulu, on les active côté firmware comme AUX1/AUX2 de
  ffp5cs, sans retoucher la carte.
- **Connexions I2C supplémentaires** : OLED + 3 ports libres (J15/J16/J17, brochage
  GND/VCC/SCL/SDA), 3-4 modules simultanés avec les pull-ups 4,7 k.
- **Toutes les broches restantes utilisables** : breakout J18 (`3V3 GND EN IO4 IO5 RX0
  TX0` — RX0/TX0 à laisser libres pendant un flash USB), rail d'alim Dupont J19
  (2×5V 2×GND 2×3V3), borniers de distribution 5 V (J28) et 3V3 (J29).
- **Zéro soudure à l'usage** : DevKit socketé, charges sur borniers à vis, capteurs sur
  JST-XH/borniers, servos/OLED/GPIO en headers.

## Pièges connus (hérités des firmwares, choix assumés)

- **GPIO2 (1-Wire msp)** est une broche de strapping : le pull-up 4,7 k peut gêner le
  passage en mode flash. Si le flash USB échoue, débrancher la sonde DS18B20 le temps du
  flash. (Conservé pour rester compatible avec le firmware msp actuel.)
- **GPIO27 (pluie msp)** est sur ADC2, inutilisable en analogique quand le WiFi est actif :
  brancher la sortie **DO** (numérique) du module pluie, pas AO — lue en `digitalRead`
  par le firmware msp ≥ 2.72 (projection 4095 sec / 100 mouillé, contrat serveur inchangé).
- **GPIO12 (pompe n3pp)** est strapping MTDI : le pull-down de base du canal REL2 garantit
  l'état sûr au boot.
- Profils **sur batterie (18650 + TP4056 solaire)** : ne pas peupler les LED témoin,
  et **ne jamais brancher la batterie brute sur J1/jack** (brownouts + relais 5 V) —
  boost 5 V → J1 si relais/servos, ou LDO 3,3 V → J29 sinon ; mesure VBAT → J27.
  Câblages, achats et autonomies chiffrées : [`ACHATS.md` §5 bis](ACHATS.md).

## Régénérer / rerouter / vérifier

```bash
cd hardware/n3pp-msp-commun
python3 generator/generate.py          # schéma + PCB placé (NON routé) + BOM + feuilles
python3 generator/route_lv.py          # autoroute, coud les masses, corrige les reliefs
python3 generator/tidy_silkscreen.py   # écarte les repères qui masquent une étiquette
python3 generator/export_fab.py        # zip Gerbers prêt JLCPCB + rendus de revue
python3 tools/check_pinmap_vs_firmware.py   # garde anti-dérive (les DEUX firmwares)
kicad-cli pcb drc --severity-error kicad/n3pp-msp-commun.kicad_pcb -o /tmp/drc.rpt
```

⚠️ `generate.py` écrase le PCB routé — enchaîner systématiquement avec `route_lv.py`.
Le routage livré dans `kicad/` est déjà fait et validé (DRC 0 violation).

Empreintes : bibliothèque officielle KiCad 8.0.9 (CC-BY-SA 4.0 + exception d'usage),
vendorées dans `generator/footprints/`.

Commander les cartes : voir **[../COMMANDE_JLCPCB.md](../COMMANDE_JLCPCB.md)**
(réglages du formulaire, options utiles, pourquoi 1 oz ici et 2 oz sur la 230 V).
