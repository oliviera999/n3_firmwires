# Rapport de vérification — cartes `hardware/`

Instantané du **2026-08-27** (branche `claude/firmware-pcb-generation-wo7wwr`,
carte commune en **rev 0.2** — rail capteurs commuté par GPIO13 — et carte
ffp5cs 12/24 V passée en **rev 0.6 bi-module** WROOM/ESP32-S3). Chaque contrôle est
reproductible avec la commande indiquée — refaire la passe complète avant toute
commande de PCB (checklist : [TUTO_PCB.md §9](TUTO_PCB.md)).

## Résultats

| Contrôle | n3pp-msp-commun | ffp5cs-wroom-prod | ffp5cs-wroom-prod-230v |
|---|---|---|---|
| **ERC** schéma (erreurs) | **0** | **0** | **0** |
| **DRC** violations | **0** | **0** | **0** |
| **DRC** pastilles non connectées | **0** | **0** | **0** |
| **DRC** erreurs d'empreinte | **0** | **0** | **0** |
| **Garde anti-dérive** code ↔ plan | OK (21 signaux, 2 firmwares, + topologie power-gate) | OK (16+16 signaux, sites A1 WROOM + A2 S3) | OK (16 signaux) |
| **Contrôle 3 mm** secteur ↔ logique | n/a | n/a | **0 écart** |
| **Sérigraphie** : conflits repères/étiquettes | 0 | 0 | 0 |
| **Sérigraphie** : hauteur / trait des textes | ≥ 1 mm / ≥ 0,15 mm | idem | idem |
| **Gerbers** : contenu du zip | 10 fichiers (7 couches + PTH/NPTH + job) | idem | idem |
| **Perçages** PTH + NPTH | 404 + 4 (M3) | 367 + 4 | 378 + 4 |
| Marqueur n° de commande (`JLCJLCJLCJLC`, dos) | présent | présent | présent |
| Vue Fritzing (`.fzz`) | présent | présent | présent |

Détails dimensionnels (mesurés sur les `.kicad_pcb`) :

| | n3pp-msp-commun | ffp5cs-wroom-prod | ffp5cs-wroom-prod-230v |
|---|---|---|---|
| Dimensions | 210 × 105 mm (rev 0.2) | 190 × 100 mm (rev 0.6 bi-module) | 234 × 110 mm |
| Piste la plus fine / la plus large | 0,4 / 2,0 mm | 0,4 / 2,0 mm | 0,4 / **2,5** mm |
| Vias (perçage / pastille) | 0,35 / 0,7 mm | 0,35 / 0,7 mm | 0,3 / 0,5 mm |
| Perçage composant minimum | 0,75 mm | 0,75 mm | 0,75 mm |
| Fentes fraisées (Edge.Cuts internes) | — | — | **18** |

Tout est au-dessus des minima JLCPCB (piste/isolement 0,127 mm, via 0,3/0,4 mm,
perçage 0,2 mm, sérigraphie 0,15 mm) — voir [COMMANDE_JLCPCB.md](COMMANDE_JLCPCB.md).

## Reproduire chaque contrôle

```bash
cd hardware/<carte>

# ERC (schéma) et DRC (PCB)
kicad-cli sch erc --severity-error -o /tmp/erc.rpt kicad/<carte>.kicad_sch
kicad-cli pcb drc --severity-error -o /tmp/drc.rpt kicad/<carte>.kicad_pcb

# Garde anti-dérive firmware(s) <-> plan
python3 tools/check_pinmap_vs_firmware.py

# Sérigraphie (audit sans modification)
python3 generator/tidy_silkscreen.py --dry-run

# Contrôle 3 mm indépendant (carte 230 V uniquement)
cd generator && python3 -c "
import sys; sys.path.insert(0, '.')
import route_230v as R, pcbnew
print('écarts <3mm :', R.check_mains_gap(pcbnew.LoadBoard(str(R.BOARD_PATH))))"

# Regénérer les fichiers de fabrication (zip 10 fichiers exactement)
python3 generator/export_fab.py
```

> Note : les ~94 *warnings* ERC `lib_symbol_issues` sont attendus — les schémas
> embarquent leur bibliothèque de symboles (aucune dépendance externe), et
> KiCad signale simplement qu'elle n'est pas installée sur la machine.
> Zéro **erreur** est le critère.

## Spécifique rev 0.6 (ffp5cs-wroom-prod, bi-module)

- **Site A2 (ESP32-S3-DevKitC-1)** : le checker vérifie les deux cartographies —
  section WROOM de `pins.h` contre le site A1 **et** section `PINMAP_S3_CARRIER`
  contre le site A2 (`pinmap_s3_carrier.json`), plus une liste de broches S3
  interdites (strapping 0/3/45/46, USB 19/20, UART0 43/44, PSRAM 35-37,
  LED RGB 38/48) qui ne doivent porter aucun net.
- **Amorces de routage** (`SEED_TRACKS` de `route_lv.py`) : pads pincés sous les
  zones antenne (A1-15 `SPARE_GPIO23`, A1-30 `EN`), rive gauche encombrée
  (`PWR_LED`) et colonne droite du S3 (`ONEWIRE` pad A2-29) — échecs freerouting
  reproductibles sans elles. Un petit **rectangle d'interdiction de vias**
  (`VIA_BLOCKS`) neutralise un via posé par freerouting à 0,05 mm d'une piste
  Alim (court-circuit US_TANK/VIN_5V reproductible).

## Spécifique rev 0.2 (carte commune)

- **GPIO13 = power-gate** : le checker vérifie désormais la topologie complète
  de l'interrupteur d'alim capteurs (R1→Q1→Q7 P-MOSFET→`+3V3_SW`, pull-ups
  R36/R37, pont batterie commuté Q8/Q9, R34 sur `VBAT_SW`) — architecture
  relevée dans les sources des deux firmwares (RELAIS=1 au réveil, relâché en
  deep sleep, jamais écrit à 0).
- **Amorces de routage** : le net `AUX6_GPIO23` (pad 15 du DevKit, pincé sous
  la zone antenne) est pré-amorcé par `SEED_TRACKS` dans `route_lv.py` —
  échec reproductible de freerouting sans elles, vérifié.

## Ce que la passe a déjà corrigé (historique)

- Sérigraphie sous les minima fabricant (0,8 mm / 0,12 mm → 1 mm / 0,16 mm) ;
- 34 chevauchements repères/étiquettes (dont `ZONE 230V - DANGER` masqué) —
  résolus par `tidy_silkscreen.py` + déplacement du titre 12/24 V ;
- zip de la carte commune pollué par 6 couches de documentation et perçages
  PTH/NPTH fusionnés — remplacé par l'export normalisé `export_fab.py`.
