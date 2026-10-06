#!/usr/bin/env python3
"""Exporte les fichiers de fabrication (Gerbers + perçages) prêts pour JLCPCB,
plus les rendus de revue (schéma PDF, SVG des deux faces).

Pourquoi un script plutôt qu'un `kicad-cli` à la main : sans liste de couches
explicite, kicad-cli exporte AUSSI les couches de documentation (F.Fab,
*.Courtyard, User.*, Margin). Le fabricant ne sait pas quoi en faire — au mieux
il les ignore, au pire sa DFM prend `Margin` ou `User.Drawings` pour un contour
de découpe. On n'envoie donc QUE les 7 couches utiles d'une carte 2 couches.

Garde-fou : un DRC KiCad (erreurs + parité schéma, règles du `.kicad_dru`
comprises) est lancé AVANT l'export ; la moindre erreur bloque le zip.

Sérigraphie : `--subtract-soldermask` retire la sérigraphie des ouvertures de
masque (pads) — ce que la DFM du fabricant ferait de toute façon, mais de
façon déterministe et visible dans les Gerbers livrés.

Perçages : fichiers PTH et NPTH séparés (`--excellon-separate-th`), origine
absolue, unités mm — les trous de fixation M3 restent ainsi non métallisés.

Usage : python export_fab.py [--rev 0.2] [--no-drc]
Prérequis : KiCad >= 9 (kicad-cli trouvé via kicad_tools : KICAD_CLI, PATH
ou C:\\Program Files\\KiCad\\<version>\\bin). Aucun outil `zip` requis.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from kicad_tools import kicad_cli

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
KICAD = ROOT / "kicad"
EXPORTS = ROOT / "exports"

# Les 7 seules couches attendues par un fabricant pour une carte 2 couches.
FAB_LAYERS = "F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,Edge.Cuts"


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if r.returncode != 0:
        sys.exit(f"ECHEC: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}")
    return r


def drc_gate(cli: str, pcb: Path) -> None:
    """DRC bloquant : erreurs + parité schéma. Les avertissements (sérigraphie
    écrêtée par le fabricant) sont rapportés mais ne bloquent pas."""
    with tempfile.TemporaryDirectory() as td:
        report = Path(td) / "drc.json"
        subprocess.run([cli, "pcb", "drc", "--schematic-parity", "--severity-all",
                        "--format", "json", "-o", str(report), str(pcb)],
                       capture_output=True, text=True)
        if not report.exists():
            sys.exit("ECHEC: le DRC n'a pas produit de rapport")
        d = json.loads(report.read_text(encoding="utf-8"))
    viol = d.get("violations", [])
    errors = [v for v in viol if v.get("severity") == "error"]
    warnings = len(viol) - len(errors)
    unconnected = len(d.get("unconnected_items", []))
    parity = len(d.get("schematic_parity", []))
    print(f"DRC : {len(errors)} erreur(s), {unconnected} non connecté(s), "
          f"{parity} écart(s) de parité, {warnings} avertissement(s)")
    if errors or unconnected or parity:
        for v in errors[:20]:
            print("   ", v.get("type"), "|",
                  " / ".join(i.get("description", "") for i in v.get("items", [])))
        sys.exit("ECHEC: DRC non vierge — corriger avant export (ou --no-drc)")


def main() -> None:
    pcbs = sorted(KICAD.glob("*.kicad_pcb"))
    if not pcbs:
        sys.exit(f"aucun .kicad_pcb dans {KICAD}")
    pcb = pcbs[0]
    project = pcb.stem
    sch = pcb.with_suffix(".kicad_sch")

    ap = argparse.ArgumentParser()
    ap.add_argument("--rev", help="révision du zip (défaut : REV de generate.py)")
    ap.add_argument("--no-drc", action="store_true",
                    help="ne pas bloquer sur le DRC (déconseillé)")
    args = ap.parse_args()
    rev = args.rev
    if not rev:
        m = re.search(r'^REV = "([^"]+)"', (HERE / "generate.py").read_text(
            encoding="utf-8"), re.M)
        rev = m.group(1) if m else "0.1"

    cli = kicad_cli()
    if not args.no_drc:
        drc_gate(cli, pcb)

    EXPORTS.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        out = Path(td)
        run([cli, "pcb", "export", "gerbers", "--layers", FAB_LAYERS,
             "--no-netlist", "--subtract-soldermask", "--check-zones",
             "-o", f"{out}/", str(pcb)])
        run([cli, "pcb", "export", "drill", "--format", "excellon",
             "--excellon-separate-th", "--excellon-units", "mm",
             "--drill-origin", "absolute", "-o", f"{out}/", str(pcb)])
        zip_path = EXPORTS / f"gerbers-{project}-v{rev}.zip"
        files = sorted(f for f in out.iterdir() if f.is_file())
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
            for f in files:
                z.write(f, f.name)
    print(f"OK  {zip_path.name} ({len(files)} fichiers)")
    for f in files:
        print("   ", f.name)

    # rendus de revue (hors fabrication)
    if sch.exists():
        run([cli, "sch", "export", "pdf", "-o", str(EXPORTS / "schema.pdf"), str(sch)])
    # --exclude-drawing-sheet : sans lui le cartouche de page est dessiné
    # par-dessus la carte dans le SVG (il n'est jamais dans les Gerbers).
    for name, layers, extra in [
            ("pcb-face-avant.svg", "F.Cu,F.SilkS,Edge.Cuts", []),
            ("pcb-face-arriere.svg", "B.Cu,B.SilkS,Edge.Cuts", ["--mirror"]),
            ("pcb-serigraphie.svg", "F.SilkS,Edge.Cuts", [])]:
        run([cli, "pcb", "export", "svg", "--mode-single", "--layers", layers,
             "--page-size-mode", "2", "--exclude-drawing-sheet",
             "-o", str(EXPORTS / name), str(pcb)] + extra)
    print("OK  schema.pdf + pcb-face-avant/arriere.svg + pcb-serigraphie.svg")


if __name__ == "__main__":
    main()
