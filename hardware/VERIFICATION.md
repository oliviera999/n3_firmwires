# Rapport de vérification — cartes `hardware/`

Instantané du **2026-08-17** (branche `claude/firmware-pcb-generation-wo7wwr`,
après mise en conformité sérigraphie/fabrication). Chaque contrôle est
reproductible avec la commande indiquée — refaire la passe complète avant toute
commande de PCB (checklist : [TUTO_PCB.md §9](TUTO_PCB.md)).

## Résultats

| Contrôle | n3pp-msp-commun | ffp5cs-wroom-prod | ffp5cs-wroom-prod-230v |
|---|---|---|---|
| **ERC** schéma (erreurs) | **0** | **0** | **0** |
| **DRC** violations | **0** | **0** | **0** |
| **DRC** pastilles non connectées | **0** | **0** | **0** |
| **DRC** erreurs d'empreinte | **0** | **0** | **0** |
| **Garde anti-dérive** code ↔ plan | OK (21 signaux, 2 firmwares) | OK (16 signaux) | OK (16 signaux) |
| **Contrôle 3 mm** secteur ↔ logique | n/a | n/a | **0 écart** |
| **Sérigraphie** : conflits repères/étiquettes | 0 | 0 | 0 |
| **Sérigraphie** : hauteur / trait des textes | ≥ 1 mm / ≥ 0,15 mm | idem | idem |
| **Gerbers** : contenu du zip | 10 fichiers (7 couches + PTH/NPTH + job) | idem | idem |
| **Perçages** PTH + NPTH | 374 + 4 (M3) | 276 + 4 | 378 + 4 |
| Marqueur n° de commande (`JLCJLCJLCJLC`, dos) | présent | présent | présent |
| Vue Fritzing (`.fzz`) | présent | présent | présent |

Détails dimensionnels (mesurés sur les `.kicad_pcb`) :

| | n3pp-msp-commun | ffp5cs-wroom-prod | ffp5cs-wroom-prod-230v |
|---|---|---|---|
| Dimensions | 210 × 105 mm | 150 × 100 mm | 234 × 110 mm |
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

## Ce que la passe a déjà corrigé (historique)

- Sérigraphie sous les minima fabricant (0,8 mm / 0,12 mm → 1 mm / 0,16 mm) ;
- 34 chevauchements repères/étiquettes (dont `ZONE 230V - DANGER` masqué) —
  résolus par `tidy_silkscreen.py` + déplacement du titre 12/24 V ;
- zip de la carte commune pollué par 6 couches de documentation et perçages
  PTH/NPTH fusionnés — remplacé par l'export normalisé `export_fab.py`.
