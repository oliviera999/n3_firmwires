# `hardware/` — cartes porteuses ESP32 de l'écosystème n³

Trois cartes KiCad **entièrement générées depuis le code des firmwares**
(pipeline Python : génération → routage → sérigraphie → export fabricant),
avec gardes anti-dérive qui relisent les `#define` des firmwares à chaque passe.

## Les cartes

| Dossier | Pour | Charges relais | État |
|---|---|---|---|
| [`n3pp-msp-commun/`](n3pp-msp-commun/) | serre **n3pp** OU météo **msp** — un seul PCB, le profil d'assemblage décide | 12/24 V (6 canaux dont 4 extensions) | routée, DRC 0, gerbers v0.1 |
| [`n3-universal/`](n3-universal/) | **UNIVERSELLE msp + n3pp + ffp5cs** — bi-module WROOM/S3, 6 relais 230 V, 4 profils d'alim (5 V, solaire 1S, bus 12 V, secteur Hi-Link), SD/DS3231/INA | 230 V ≤ 10 A + 12/24 V (6 canaux, zone isolée, **2 oz**) | routée, DRC 0, 3 mm 0, gerbers v0.1 — prochain spin : [`EVOLUTIONS_PROPOSEES.md`](n3-universal/EVOLUTIONS_PROPOSEES.md) (sélecteur AUTO/ON par relais, connectique capteurs) |
| [`ffp5cs-wroom-prod/`](ffp5cs-wroom-prod/) | aquaponie **ffp5cs** — **bi-module** WROOM **ou** ESP32-S3 (site A2, `PINMAP_S3_CARRIER`) | 12/24 V (4 canaux) | routée, DRC 0, gerbers v0.6 |
| [`ffp5cs-wroom-prod-230v/`](ffp5cs-wroom-prod-230v/) | aquaponie, charges **secteur** | **230 V ≤ 10 A** (6 canaux, zone isolée, 2 oz) | routée, DRC 0, gerbers v0.3 |

Chaque dossier contient : projet KiCad routé, `BOM.csv`, `ACHATS.md` (liste
d'achat), gerbers prêts fabricant dans `exports/`, vue Fritzing, et le
générateur qui reproduit le tout. La carte commune ajoute les feuilles
`ASSEMBLAGE-N3PP.md` / `ASSEMBLAGE-MSP.md` (quoi souder par profil).

## Les documents transverses

| Document | Contenu |
|---|---|
| **[TUTO_PCB.md](TUTO_PCB.md)** | comprendre et maîtriser nos PCB : anatomie d'une carte, largeurs de piste et netclasses, plans de masse/vias, électronique des blocs (relais, ponts, pull-ups, découplage), pièges ESP32 (strapping, ADC2/WiFi), zone 230 V, pipeline, recettes de modification, glossaire |
| **[COMMANDE_JLCPCB.md](COMMANDE_JLCPCB.md)** | commander chez JLCPCB : chaque paramètre du formulaire expliqué, choix par carte (1 oz vs 2 oz…), remarques à joindre |
| **[VERIFICATION.md](VERIFICATION.md)** | rapport daté des contrôles (ERC/DRC/gardes/3 mm/sérigraphie/gerbers) et comment reproduire chacun |

## Parcours conseillé

1. Nouvelle venue sur le sujet → **TUTO_PCB.md** (une lecture, puis référence) ;
2. modifier une carte → README du dossier concerné (pipeline + pièges) ;
3. commander → checklist du TUTO §9, puis **COMMANDE_JLCPCB.md** ;
4. souder → `ACHATS.md` puis `ASSEMBLAGE-*.md` du dossier.
