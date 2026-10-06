#!/usr/bin/env python3
"""Génère BOM + CPL au format JLCPCB / NextPCB pour prévisualisation PCBA.

Source de vérité :
  - positions / rotations / face : kicad/*.kicad_pcb
  - valeurs / empreintes / descriptions : BOM.csv (généré par generate.py)

Sorties (exports/) :
  - bom-jlcpcb.csv   colonnes Comment,Designator,Footprint,LCSC Part #
  - cpl-jlcpcb.csv   colonnes Designator,Mid X,Mid Y,Layer,Rotation
  - assembly-preview-NOTES.md  couverture LCSC + limites (THT, peuplement)

Ce n'est PAS un dossier d'assemblage prêt à commander : la carte est 100 %
traversante, peuplement conditionnel par profil, et beaucoup de pièces n'ont
pas d'équivalent « Basic parts » JLCPCB. Les C-numbers proposés sont des
suggestions à vérifier sur jlcpcb.com Parts avant toute commande réelle.

Usage : python3 export_assembly_preview.py
Prérequis : Python 3.10+ (pas de kicad-cli).
"""

from __future__ import annotations

import csv
import re
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
KICAD = ROOT / "kicad"
EXPORTS = ROOT / "exports"
BOM_SRC = ROOT / "BOM.csv"
_rev = re.search(r'^REV = "([^"]+)"', (HERE / "generate.py").read_text(encoding="utf-8"), re.M)
REV = _rev.group(1) if _rev else "0.1"

# Refs jamais assemblées (mécanique / doc).
SKIP_REFS = re.compile(r"^H\d+$")

# Lignes BOM.csv qui ne sont pas des empreintes PCB (modules enfichés, supports).
SKIP_BOM_REFS = re.compile(
    r"^\s*(A[12] \(supports\)|J[\d/]+ \(modules?\)|.*\(module\)|.*\(modules\))",
    re.I,
)

# Mapping (valeur normalisée, famille d'empreinte) -> LCSC Part #.
# UNIQUEMENT des codes contrôlés sur la fiche LCSC (ceux de
# exports/pcba/BOM-PCBA-socle.csv, vérifiés le 2026-08-28, + Q11/D9/PS1 le
# 2026-10-06). La table d'origine contenait des codes inventés (C72503 =
# condensateur CMS 22 µF pour le NDP6020P, C9135 = connecteur IDC pour le
# fusible, C293822 inexistant) : une case vide oblige JLCPCB à demander,
# un mauvais code fait monter la mauvaise pièce.
LCSC_MAP: dict[tuple[str, str], str] = {
    ("1n4744a", "d_do41"): "C238928",
    ("1n5822", "d_do201"): "C2476",
    ("p6ke18a", "d_do201"): "C1975053",
    ("bc337-40", "to92"): "C713611",
    ("bs250", "to92"): "C151450",
    ("ndp6020p", "to220"): "C878814",
    ("irf4905", "to220"): "C2564",
    ("srd-05vdc-sl-c", "relay"): "C35449",
    ("jst-xh", "jst3"): "C144394",
    ("hlk-20m05", "hilink"): "C465406",
    ("10d471k", "varistor"): "C111188",
}


def normalize_value(value: str) -> str:
    v = value.strip().rstrip("*").lower().replace(" ", "")
    return v


def footprint_family(fp: str) -> str:
    f = fp.lower()
    if "r_axial" in f:
        return "r_axial"
    if "c_disc" in f:
        return "c_disc"
    if "cp_radial" in f:
        return "cp_radial"
    if "d_do-41" in f or "d_do41" in f:
        return "d_do41"
    if "d_do-201" in f or "d_do201" in f:
        return "d_do201"
    if "to-92" in f or "to92" in f:
        return "to92"
    if "to-220" in f or "to220" in f:
        return "to220"
    if "led_d5" in f:
        return "led"
    if "relay" in f:
        return "relay"
    if "bornier-2" in f or "bornier_2" in f:
        return "bornier2"
    if "bornier-3" in f or "bornier_3" in f:
        return "bornier3"
    if "jst_xh_b3b" in f:
        return "jst3"
    if "jst_xh_b4b" in f:
        return "jst4"
    if "barreljack" in f:
        return "barrel"
    if "pinheader_1x03" in f:
        return "pinheader3"
    if "pinheader_1x06" in f:
        return "pinheader6"
    if "pinsocket_1x04" in f:
        return "pinsocket4"
    if "pinsocket_1x06" in f:
        return "pinsocket6"
    if "hlk" in f or "hi-link" in f or "converter_acdc" in f:
        return "hilink"
    if "fuse" in f:
        return "fuse"
    if "rv_disc" in f:
        return "varistor"
    if "esp32_devkit" in f:
        return "devkit_wroom"
    if "esp32_s3" in f:
        return "devkit_s3"
    return "other"


def lookup_lcsc(value: str, footprint: str) -> str:
    key = (normalize_value(value), footprint_family(footprint))
    return LCSC_MAP.get(key, "")


def tokenize_sexp(text: str) -> list[str]:
    # Même idée que generate.py : tokens sexp simples.
    return re.findall(r'"[^"]*"|[()]|[^\s()]+', text)


def parse_sexp(tokens: list[str]):
    pos = 0

    def read():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok == "(":
            node = []
            while tokens[pos] != ")":
                node.append(read())
            pos += 1
            return node
        if tok.startswith('"') and tok.endswith('"'):
            return tok[1:-1]
        return tok

    tree = read()
    if pos != len(tokens):
        # Fichier PCB = une seule liste racine (kicad_pcb ...)
        pass
    return tree


def find_all(node, name: str):
    out = []
    if isinstance(node, list) and node:
        if node[0] == name:
            out.append(node)
        for child in node[1:]:
            if isinstance(child, list):
                out.extend(find_all(child, name))
    return out


def parse_pcb_placements(pcb_path: Path) -> dict[str, dict]:
    """ref -> {x, y, rot, layer, footprint, value}."""
    text = pcb_path.read_text(encoding="utf-8")
    tree = parse_sexp(tokenize_sexp(text))
    placements: dict[str, dict] = {}
    for fp in find_all(tree, "footprint"):
        # (footprint "lib:name" (layer ...) (at x y [rot]) ... (property "Reference" "R1" ...) ...)
        fp_name = fp[1] if len(fp) > 1 else ""
        if isinstance(fp_name, str) and ":" in fp_name:
            fp_name = fp_name.split(":", 1)[1]

        layer = "Top"
        at = None
        ref = None
        value = None
        for child in fp[2:]:
            if not isinstance(child, list) or not child:
                continue
            head = child[0]
            if head == "layer" and len(child) > 1:
                lyr = str(child[1])
                layer = "Bottom" if lyr.startswith("B.") else "Top"
            elif head == "at" and at is None and len(child) >= 3:
                try:
                    x = float(child[1])
                    y = float(child[2])
                    rot = float(child[3]) if len(child) > 3 else 0.0
                    at = (x, y, rot)
                except (TypeError, ValueError):
                    pass
            elif head == "property" and len(child) >= 3:
                pname = child[1]
                pval = child[2]
                if pname == "Reference":
                    ref = str(pval)
                elif pname == "Value":
                    value = str(pval)

        if not ref or at is None:
            continue
        if SKIP_REFS.match(ref):
            continue
        placements[ref] = {
            "x": at[0],
            "y": at[1],
            "rot": at[2] % 360.0,
            "layer": layer,
            "footprint": fp_name,
            "value": value or "",
        }
    return placements


def load_bom_by_ref(bom_path: Path) -> dict[str, dict]:
    """ref -> {value, footprint, desc} depuis BOM.csv (lignes multi-refs expansées)."""
    by_ref: dict[str, dict] = {}
    with bom_path.open(encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f, delimiter=";")
        for row in reader:
            refs_raw = (row.get("Refs") or "").strip()
            if not refs_raw or SKIP_BOM_REFS.match(refs_raw):
                continue
            value = (row.get("Valeur") or "").strip()
            fp = (row.get("Empreinte") or "").strip()
            desc = (row.get("Description") or "").strip()
            if fp.startswith("monte sur") or fp.startswith("s'enfich"):
                continue
            for ref in refs_raw.split():
                if SKIP_REFS.match(ref):
                    continue
                by_ref[ref] = {"value": value, "footprint": fp, "desc": desc}
    return by_ref


def write_cpl(placements: dict[str, dict], path: Path) -> int:
    rows = []
    def ref_key(r: str):
        m = re.search(r"(\d+)$", r)
        return (re.sub(r"\d+$", "", r), int(m.group(1)) if m else 0)

    for ref in sorted(placements, key=ref_key):
        p = placements[ref]
        rows.append({
            "Designator": ref,
            "Mid X": f"{p['x']:.4f}",
            "Mid Y": f"{p['y']:.4f}",
            "Layer": p["layer"],
            "Rotation": f"{p['rot']:.0f}",
        })
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(
            f, fieldnames=["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        w.writeheader()
        w.writerows(rows)
    return len(rows)


def write_bom(by_ref: dict[str, dict], placements: dict[str, dict], path: Path) -> tuple[int, int, list[dict]]:
    """Regroupe par (Comment, Footprint, LCSC) comme le BOM JLCPCB.
    Retourne aussi les lignes regroupées pour les autres formats."""
    groups: dict[tuple[str, str, str], list[str]] = defaultdict(list)
    matched = 0
    for ref in sorted(placements):
        meta = by_ref.get(ref)
        if meta:
            comment = meta["value"]
            fp = meta["footprint"]
        else:
            comment = placements[ref]["value"] or ref
            fp = placements[ref]["footprint"]
        lcsc = lookup_lcsc(comment, fp)
        if lcsc:
            matched += 1
        groups[(comment, fp, lcsc)].append(ref)

    rows = []
    for (comment, fp, lcsc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
        rows.append({
            "Comment": comment,
            "Designator": ",".join(refs),
            "Footprint": fp,
            "LCSC Part #": lcsc,
            "Quantity": str(len(refs)),
        })
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(
            f, fieldnames=["Comment", "Designator", "Footprint", "LCSC Part #"],
            extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    return len(rows), matched, rows


def write_bom_nextpcb(rows: list[dict], path: Path) -> None:
    """BOM NextPCB : Designator, Quantity, Manufacturer Part Number (+ extras)."""
    out = []
    for i, r in enumerate(rows, start=1):
        lcsc = r["LCSC Part #"]
        # Sans C-number : marquer Customer Supply (C) pour ne pas bloquer le parseur.
        procurement = "" if lcsc else "C"
        out.append({
            "S/N": str(i),
            "Designator": r["Designator"],
            "Quantity": r["Quantity"],
            "Manufacturer Part Number": lcsc or r["Comment"],
            "Footprint": r["Footprint"],
            "Comment": r["Comment"],
            "Procurement Type": procurement,
            "Customer Note": "" if lcsc else "preview: no LCSC — customer supply / placeholder",
        })
    fields = [
        "S/N", "Designator", "Quantity", "Manufacturer Part Number",
        "Footprint", "Comment", "Procurement Type", "Customer Note",
    ]
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(out)


def zip_file(src: Path, zip_path: Path) -> None:
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.write(src, arcname=src.name)


def write_notes(
    path: Path,
    n_cpl: int,
    n_bom_lines: int,
    n_matched: int,
    placements: dict[str, dict],
    by_ref: dict[str, dict],
) -> None:
    unmatched = []
    for ref in sorted(placements):
        meta = by_ref.get(ref, {})
        comment = meta.get("value") or placements[ref]["value"] or ref
        fp = meta.get("footprint") or placements[ref]["footprint"]
        if not lookup_lcsc(comment, fp):
            unmatched.append(f"- `{ref}` — {comment} / {fp}")

    path.write_text(
        f"""# Prévisualisation assemblage JLCPCB / NextPCB — n3-universal

Fichiers générés par `generator/export_assembly_preview.py` :

| Fichier | Pour | Rôle |
|---------|------|------|
| `gerbers-n3-universal-v*.zip` | les deux | Gerbers (`export_fab.py`) |
| `bom-jlcpcb.csv` | **JLCPCB** | `Comment,Designator,Footprint,LCSC Part #` |
| `cpl-jlcpcb.csv` | **JLCPCB** | Pick & Place mm |
| `bom-nextpcb.csv` | **NextPCB** | template NextPCB (Quantity + Manufacturer Part Number) |
| `cpl-nextpcb.zip` | **NextPCB** | même CPL, **zippé** (NextPCB refuse le `.csv` brut pour le centroid) |

Couverture : **{n_cpl}** placements CPL, **{n_bom_lines}** lignes BOM,
**{n_matched}/{n_cpl}** avec LCSC suggéré
({100.0 * n_matched / n_cpl if n_cpl else 0:.0f} %).

---

## JLCPCB — où charger (le piège)

Sur la page **Order PCB** classique, **il n’y a pas** d’upload BOM/CPL.
Il faut **activer l’assemblage** :

1. Aller sur [jlcpcb.com](https://jlcpcb.com) → **Order now** / Instant Quote.
2. Uploader **uniquement** `gerbers-n3-universal-v{REV}.zip` (Add gerber file).
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

{chr(10).join(unmatched) if unmatched else "_Aucune._"}
""",
        encoding="utf-8",
    )


def main() -> None:
    pcbs = sorted(KICAD.glob("*.kicad_pcb"))
    if not pcbs:
        sys.exit(f"aucun .kicad_pcb dans {KICAD}")
    if not BOM_SRC.exists():
        sys.exit(f"BOM manquant : {BOM_SRC} (lancer generate.py)")

    placements = parse_pcb_placements(pcbs[0])
    by_ref = load_bom_by_ref(BOM_SRC)
    if not placements:
        sys.exit("aucune empreinte lue dans le PCB")

    EXPORTS.mkdir(exist_ok=True)
    cpl_path = EXPORTS / "cpl-jlcpcb.csv"
    bom_path = EXPORTS / "bom-jlcpcb.csv"
    bom_np = EXPORTS / "bom-nextpcb.csv"
    cpl_np_zip = EXPORTS / "cpl-nextpcb.zip"
    notes_path = EXPORTS / "assembly-preview-NOTES.md"

    n_cpl = write_cpl(placements, cpl_path)
    n_bom, n_matched, rows = write_bom(by_ref, placements, bom_path)
    write_bom_nextpcb(rows, bom_np)
    zip_file(cpl_path, cpl_np_zip)
    write_notes(notes_path, n_cpl, n_bom, n_matched, placements, by_ref)

    only_pcb = sorted(set(placements) - set(by_ref))
    only_bom = sorted(set(by_ref) - set(placements))
    print(f"OK  {cpl_path.name} ({n_cpl} placements)")
    print(f"OK  {bom_path.name} ({n_bom} lignes, {n_matched}/{n_cpl} avec LCSC)")
    print(f"OK  {bom_np.name} (format NextPCB)")
    print(f"OK  {cpl_np_zip.name} (centroid zippé pour NextPCB)")
    print(f"OK  {notes_path.name}")
    if only_pcb:
        print(f"  info: refs PCB absentes du BOM.csv : {', '.join(only_pcb[:12])}"
              + ("…" if len(only_pcb) > 12 else ""))
    if only_bom:
        print(f"  info: refs BOM absentes du PCB : {', '.join(only_bom[:12])}"
              + ("…" if len(only_bom) > 12 else ""))


if __name__ == "__main__":
    main()
