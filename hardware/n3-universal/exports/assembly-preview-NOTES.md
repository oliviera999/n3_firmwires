# Prévisualisation assemblage JLCPCB / NextPCB — n3-universal

Fichiers générés par `generator/export_assembly_preview.py` :

| Fichier | Pour | Rôle |
|---------|------|------|
| `gerbers-n3-universal-v*.zip` | les deux | Gerbers (`export_fab.py`) |
| `bom-jlcpcb.csv` | **JLCPCB** | `Comment,Designator,Footprint,LCSC Part #` |
| `cpl-jlcpcb.csv` | **JLCPCB** | Pick & Place mm |
| `bom-nextpcb.csv` | **NextPCB** | template NextPCB (Quantity + Manufacturer Part Number) |
| `cpl-nextpcb.zip` | **NextPCB** | même CPL, **zippé** (NextPCB refuse le `.csv` brut pour le centroid) |

Couverture : **128** placements CPL, **45** lignes BOM,
**25/128** avec LCSC suggéré
(20 %).

---

## JLCPCB — où charger (le piège)

Sur la page **Order PCB** classique, **il n’y a pas** d’upload BOM/CPL.
Il faut **activer l’assemblage** :

1. Aller sur [jlcpcb.com](https://jlcpcb.com) → **Order now** / Instant Quote.
2. Uploader **uniquement** `gerbers-n3-universal-v0.1.2.zip` (Add gerber file).
3. Configurer la carte (2 couches, **2 oz**, 1,6 mm, etc.).
4. **Descendre en bas de page** → section **PCB Assembly** → basculer le
   interrupteur sur **ON** (Economic ou Standard, face Top).
5. Cliquer **Next** (ne pas payer la PCB avant cette étape).
6. Page suivante : **Upload BOM** → `bom-jlcpcb.csv`, **Upload CPL** →
   `cpl-jlcpcb.csv`, puis **Process BOM & CPL**.
7. Le viewer 3D / placement apparaît après le traitement.

Doc officielle : [How do I place a PCBA order?](https://jlcpcb.com/help/article/how-do-i-place-a-pcba-order)

---

## NextPCB — pourquoi les CSV « ne passent pas »

Sur [nextpcb.com/pcb-assembly-quote](https://www.nextpcb.com/pcb-assembly-quote) :

| Slot | Formats acceptés | Notre fichier |
|------|------------------|---------------|
| Gerber | zip / rar | `gerbers-….zip` |
| **Centroid** | **zip / rar / xlsx / xls** — **pas de `.csv` nu** | `cpl-nextpcb.zip` |
| BOM | xls / xlsx / **csv** | `bom-nextpcb.csv` |

Étapes :

1. **Step 1** : Gerbers → Next.
2. **Step 2.1** : Centroid → `cpl-nextpcb.zip` (pas le `.csv`).
3. **Step 2.2** : BOM → `bom-nextpcb.csv`.

Si le BOM est refusé : ouvrir `bom-nextpcb.csv` dans Excel → **Enregistrer sous
.xlsx**, puis uploader le `.xlsx`.

---

## Limites

- Carte **100 % THT** : usage **aperçu**, pas commande d’assemblage usine.
- C-numbers = suggestions ; pièces hors stock → choisir manuellement ou laisser
  « Customer Supply ».
- A1/A2 (DevKits) sans LCSC : marqués `C` (customer supply) dans le BOM NextPCB.
- Le viewer peut mal orienter le THT : normal, à ignorer pour une simple revue.

## Sans LCSC

- `A1` — ESP32 DevKit V1 / ESP32_DevKit_V1_30pin
- `A2` — ESP32-S3-DevKitC-1 / ESP32_S3_DevKitC_1_44pin
- `C1` — 1000u/16V / CP_Radial_D10.0mm_P5.00mm
- `C2` — 470u/16V / CP_Radial_D8.0mm_P3.50mm
- `C3` — 100n / C_Disc_D5.0mm_W2.5mm_P5.00mm
- `C4` — 100n / C_Disc_D5.0mm_W2.5mm_P5.00mm
- `C5` — 470u/25V / CP_Radial_D10.0mm_P5.00mm
- `D1` — 1N4007 / D_DO-41_SOD81_P10.16mm_Horizontal
- `D2` — 1N4007 / D_DO-41_SOD81_P10.16mm_Horizontal
- `D3` — 1N4007 / D_DO-41_SOD81_P10.16mm_Horizontal
- `D4` — 1N4007 / D_DO-41_SOD81_P10.16mm_Horizontal
- `D6` — 1N4007 / D_DO-41_SOD81_P10.16mm_Horizontal
- `D7` — 1N4007 / D_DO-41_SOD81_P10.16mm_Horizontal
- `F1` — Clip porte-fusible 5x20 à souder / s'insèrent dans les fentes 1,3x2,6 de Fuse_5x20_Horizontal
- `J1` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J11` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J12` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J13` — Support OLED / PinSocket_1x04_P2.54mm_Vertical
- `J14` — Support I2C ext / PinSocket_1x04_P2.54mm_Vertical
- `J15` — Header servo / PinHeader_1x03_P2.54mm_Vertical
- `J16` — Header servo / PinHeader_1x03_P2.54mm_Vertical
- `J17` — Header service / PinHeader_1x06_P2.54mm_Vertical
- `J18` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J19` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J2` — Jack 5.5/2.1 / BarrelJack_Horizontal
- `J20` — Header alim / PinHeader_1x06_P2.54mm_Vertical
- `J21` — Support I2C libre / PinSocket_1x04_P2.54mm_Vertical
- `J22` — Support I2C libre / PinSocket_1x04_P2.54mm_Vertical
- `J23` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J24` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J25` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J26` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J27` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J28` — Support I2C libre / PinSocket_1x04_P2.54mm_Vertical
- `J3` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J31` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J32` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J33` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J34` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J35` — Support module microSD / PinSocket_1x06_P2.54mm_Vertical
- `J36` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J37` — Bornier_5.08 / TerminalBlock_bornier-2_P5.08mm
- `J4` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J5` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J6` — Bornier_5.08 / TerminalBlock_bornier-3_P5.08mm
- `J7` — JST-XH / JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical
- `J8` — JST-XH / JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical
- `J9` — JST-XH / JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical
- `JP1` — Jumper BYPASS / PinHeader_1x03_P2.54mm_Vertical
- `JP2` — Jumper SD CS / PinHeader_1x03_P2.54mm_Vertical
- `JP3` — Jumper SD SCK / PinHeader_1x03_P2.54mm_Vertical
- `JP4` — Jumper SD MOSI / PinHeader_1x03_P2.54mm_Vertical
- `LED1` — rouge / LED_D5.0mm
- `LED2` — rouge / LED_D5.0mm
- `LED3` — rouge / LED_D5.0mm
- `LED4` — rouge / LED_D5.0mm
- `LED5` — verte / LED_D5.0mm
- `LED6` — rouge / LED_D5.0mm
- `LED7` — rouge / LED_D5.0mm
- `R1` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R10` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R11` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R12` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R13` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R14` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R15` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R16` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R17` — 2k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R18` — 2k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R19` — 2k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R2` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R20` — 220 / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R21` — 220 / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R23` — 10k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R24` — 4.7k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R25` — 4.7k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R26` — 4.7k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R27` — 10k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R28` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R29` — 10k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R3` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R30` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R31` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R32` — 10k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R33` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R34` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R35` — 100k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R36` — 100k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R37` — 100k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R38` — 100k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R39` — 22k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R4` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R40` — 100k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R43` — 10k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R44` — 10k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R45` — 10k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R46` — 10k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R47` — 10k* / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R5` — 10k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R6` — 10k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R7` — 10k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R8` — 10k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
- `R9` — 1k / R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal
