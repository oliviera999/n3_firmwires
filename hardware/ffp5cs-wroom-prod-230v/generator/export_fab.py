#!/usr/bin/env python3
"""Exporte les fichiers de fabrication (Gerbers + perçages) prêts pour JLCPCB,
plus les rendus de revue (schéma PDF, SVG des deux faces).

Pourquoi un script plutôt qu'un `kicad-cli` à la main : sans liste de couches
explicite, kicad-cli exporte AUSSI les couches de documentation (F.Fab,
*.Courtyard, User.*, Margin). Le fabricant ne sait pas quoi en faire — au mieux
il les ignore, au pire sa DFM prend `Margin` ou `User.Drawings` pour un contour
de découpe. On n'envoie donc QUE les 7 couches utiles d'une carte 2 couches.

Perçages : fichiers PTH et NPTH séparés (`--excellon-separate-th`), origine
absolue, unités mm — les trous de fixation M3 restent ainsi non métallisés.

Usage : python3 export_fab.py [--rev 0.2]
Prérequis : kicad-cli (KiCad 8), zip.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
KICAD = ROOT / "kicad"
EXPORTS = ROOT / "exports"

# Les 7 seules couches attendues par un fabricant pour une carte 2 couches.
FAB_LAYERS = "F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,Edge.Cuts"


def run(cmd: list[str]) -> None:
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"ECHEC: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}")


def main() -> None:
    pcbs = sorted(KICAD.glob("*.kicad_pcb"))
    if not pcbs:
        sys.exit(f"aucun .kicad_pcb dans {KICAD}")
    pcb = pcbs[0]
    project = pcb.stem
    sch = pcb.with_suffix(".kicad_sch")

    ap = argparse.ArgumentParser()
    ap.add_argument("--rev", help="révision du zip (défaut : REV de generate.py)")
    args = ap.parse_args()
    rev = args.rev
    if not rev:
        m = re.search(r'^REV = "([^"]+)"', (HERE / "generate.py").read_text(
            encoding="utf-8"), re.M)
        rev = m.group(1) if m else "0.1"

    EXPORTS.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        out = Path(td)
        run(["kicad-cli", "pcb", "export", "gerbers", "--layers", FAB_LAYERS,
             "--no-netlist", "-o", f"{out}/", str(pcb)])
        run(["kicad-cli", "pcb", "export", "drill", "--format", "excellon",
             "--excellon-separate-th", "--excellon-units", "mm",
             "--drill-origin", "absolute", "-o", f"{out}/", str(pcb)])
        zip_path = EXPORTS / f"gerbers-{project}-v{rev}.zip"
        if zip_path.exists():
            zip_path.unlink()
        run(["zip", "-q", "-j", "-r", str(zip_path)] + [str(f) for f in sorted(out.iterdir())])
        files = sorted(f.name for f in out.iterdir())
    print(f"OK  {zip_path.name} ({len(files)} fichiers)")
    for f in files:
        print("   ", f)

    # rendus de revue (hors fabrication)
    if sch.exists():
        run(["kicad-cli", "sch", "export", "pdf", "-o",
             str(EXPORTS / "schema.pdf"), str(sch)])
    # --exclude-drawing-sheet : sans lui le cartouche de page est dessiné
    # par-dessus la carte dans le SVG (il n'est jamais dans les Gerbers).
    for name, layers, extra in [
            ("pcb-face-avant.svg", "F.Cu,F.SilkS,Edge.Cuts", []),
            ("pcb-face-arriere.svg", "B.Cu,B.SilkS,Edge.Cuts", ["--mirror"]),
            ("pcb-serigraphie.svg", "F.SilkS,Edge.Cuts", [])]:
        run(["kicad-cli", "pcb", "export", "svg", "--layers", layers,
             "--page-size-mode", "2", "--exclude-drawing-sheet",
             "-o", str(EXPORTS / name), str(pcb)] + extra)
    print("OK  schema.pdf + pcb-face-avant/arriere.svg + pcb-serigraphie.svg")


if __name__ == "__main__":
    main()
