# `hardware/` — cartes porteuses ESP32 de l'écosystème n³

Trois cartes KiCad **entièrement générées depuis le code des firmwares**
(pipeline Python : génération → routage → sérigraphie → export fabricant),
avec gardes anti-dérive qui relisent les `#define` des firmwares à chaque passe.

## Les cartes

| Dossier | Pour | Charges relais | État |
|---|---|---|---|
| [`n3pp-msp-commun/`](n3pp-msp-commun/) | serre **n3pp** OU météo **msp** — un seul PCB, le profil d'assemblage décide | 12/24 V (6 canaux dont 4 extensions) | routée, DRC 0, gerbers v0.1 |
| [`n3-universal/`](n3-universal/) | **UNIVERSELLE msp + n3pp + ffp5cs** — bi-module WROOM/S3, 6 relais 230 V, 4 profils d'alim (5 V, solaire 1S, bus 12 V, secteur Hi-Link), SD/DS3231/INA, forçage manuel par cavalier, header CMD SENSE | 230 V ≤ 7 A + 12/24 V (6 canaux, zone isolée, **1 oz** depuis le 2026-10-30 : ≤ 1 kW résistif / 3 A moteur par canal) | **rev 0.2.3 (2026-10-31) : à commander** (site A1 en DevKitC 38 broches, carte reroutée, gerbers v0.2.3) ; simplification à l'étude avant commande : [`SIMPLIFICATION_PROPOSEE.md`](n3-universal/SIMPLIFICATION_PROPOSEE.md). Historique : la simulation SPICE ([`simulation/SIMULATION.md`](n3-universal/simulation/SIMULATION.md)) a trouvé Q7 câblé à l'envers (0.2.1), puis [`ALIMENTATION.md`](n3-universal/ALIMENTATION.md) a fixé les règles de sobriété (servos sur le 5 V commuté, D5 en pont de fil, R39 18k, LED5 par profil) ; carte reroutée en 0.2.2 puis 0.2.3, DRC KiCad 10 0 / 0 / 0 ; voir [`COMMANDE.md`](n3-universal/COMMANDE.md) |
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
| **[n3-universal/ALIMENTATION.md](n3-universal/ALIMENTATION.md)** | sobriété et robustesse des quatre alimentations de la carte universelle (18650 solaire, chargeur 5 V, secteur Hi-Link, AGM 12 V + PWM) : fuites en sommeil, budgets Wh/jour, points durs, conséquences pour la commande unique |
| **[n3-universal/SIMPLIFICATION_PROPOSEE.md](n3-universal/SIMPLIFICATION_PROPOSEE.md)** | étude du 2026-11-18, en attente d'arbitrage : sortir de la carte universelle l'alimentation 230 V et les relais (module du commerce piloté par l'étage à transistor conservé) — mesures sur la 0.2.3, scénarios, essai sur table, décisions ouvertes, fichiers à répercuter ; prompt de lancement dans `PROMPT_REV_0.3.md` |
| **[n3-universal/achats/achats-n3-universal.xlsx](n3-universal/achats/achats-n3-universal.xlsx)** | liste d'achats des 4 projets (msp1, n3pp, ffp5cs 230 V, ffp5cs 12 V) : quantités par profil, consignes de pose, prix des boutiques marocaines et liens, total global arrondi aux lots — générée depuis la BOM (`achats/build_achats.py`), vérifiée en CI |
| **[n3-universal/fritzing/README.md](n3-universal/fritzing/README.md)** | vues de câblage Fritzing de la carte universelle, une par profil (msp1, n3pp, ffp5cs 230 V, ffp5cs 12 V) : carte à l'échelle avec posé / non posé et cavaliers, périphériques et alimentation reliés aux bons borniers, aperçus PNG — générées depuis le PCB et l'inventaire, vérifiées en CI |
| **[n3-universal/simulation/SIMULATION.md](n3-universal/simulation/SIMULATION.md)** | simulation SPICE (ngspice) des blocs analogiques de la carte universelle : étage relais, rail capteurs, entrées d'alim, ponts de mesure, dynamique du rail 5 V — verdicts, marges, limites des modèles |

## Parcours conseillé

1. Nouvelle venue sur le sujet → **TUTO_PCB.md** (une lecture, puis référence) ;
2. modifier une carte → README du dossier concerné (pipeline + pièges) ;
3. commander → checklist du TUTO §9, puis **COMMANDE_JLCPCB.md** ;
4. souder → `ACHATS.md` puis `ASSEMBLAGE-*.md` du dossier.
