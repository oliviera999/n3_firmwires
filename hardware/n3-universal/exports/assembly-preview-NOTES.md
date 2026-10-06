# Prévisualisation assemblage JLCPCB / NextPCB — n3-universal

Fichiers générés par `generator/export_assembly_preview.py` :

| Fichier | Pour | Rôle |
|---------|------|------|
| `gerbers-n3-universal-v*.zip` | les deux | Gerbers (`export_fab.py`) |
| `bom-jlcpcb.csv` | **JLCPCB** | `Comment,Designator,Footprint,LCSC Part #` |
| `cpl-jlcpcb.csv` | **JLCPCB** | Pick & Place mm |
| `bom-nextpcb.csv` | **NextPCB** | template NextPCB (Quantity + Manufacturer Part Number) |
| `cpl-nextpcb.zip` | **NextPCB** | même CPL, **zippé** (NextPCB refuse le `.csv` brut pour le centroid) |

Couverture : **127** placements CPL, **43** lignes BOM,
**124/127** avec LCSC suggéré
(98 %).

---

## JLCPCB — où charger (le piège)

Sur la page **Order PCB** classique, **il n’y a pas** d’upload BOM/CPL.
Il faut **activer l’assemblage** :

1. Aller sur [jlcpcb.com](https://jlcpcb.com) → **Order now** / Instant Quote.
2. Uploader **uniquement** `gerbers-n3-universal-v0.1.1.zip` (Add gerber file).
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
- `F1` — Clip porte-fusible 5x20 à souder / s'insèrent dans les fentes 1,3x2,6 de Fuse_5x20_Horizontal
