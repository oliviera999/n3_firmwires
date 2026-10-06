#!/usr/bin/env python3
"""Fichiers d'assemblage JLCPCB (`exports/pcba/`) dérivés de la BOM et du CPL.

Pourquoi un script : en 0.1.2 ces trois CSV étaient tenus à la main et se sont
désynchronisés du PCB (positions, Q11, RV1). Ils sont maintenant dérivés de
`BOM.csv` (generate.py) et de `exports/cpl-jlcpcb.csv` (export_assembly_preview.py),
donc toujours alignés sur la révision exportée.

  BOM-PCBA-socle.csv          pièces posées sur TOUTES les unités, format JLCPCB
                              (`Comment,Designator,Footprint,LCSC Part #`)
  BOM-PCBA-conditionnels.csv  pièces à pose MANUELLE selon le profil d'alimentation
                              (bloc secteur) — ne jamais les faire assembler
  CPL-n3-universal-top.csv    positions des pièces du socle (repère des gerbers)

Exclus des deux BOM : A1/A2 (modules enfichés), PS1 (module secteur, à mesurer),
H* (trous), TP* (points de test = pads nus).

Usage : python3 export_pcba.py   (après export_assembly_preview.py)
"""

from __future__ import annotations

import csv
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
EXPORTS = ROOT / "exports"
PCBA = EXPORTS / "pcba"

# Pose manuelle, par profil : raison affichée dans la colonne Comment.
CONDITIONAL = {
    "J27": "Bornier ENTREE SECTEUR 230V (1=N 2=L) — PROFIL (d) SECTEUR UNIQUEMENT, avec PS1/F1/RV1. "
           "NE PAS POSER sur une unite 5V, batterie 1S ou bus 12V",
    "F1": "Porte-fusible 5x20 a capot (BLX-A, pas 22,0) ou clips (pas 22,5) + cartouche T1A CERAMIQUE — "
          "PROFIL (d) SECTEUR UNIQUEMENT",
    "RV1": "Varistance 14D471K/10D471K 300VAC (disque, pas 7,5 mm) — PROFIL (d) SECTEUR UNIQUEMENT, "
           "pose main avec PS1/J27/F1",
    "J26": "Bornier BUS 12V (+/GND) — PROFIL (c) bus 12V ; inutile ailleurs",
    "Q11": "IRF4905 anti-inversion bus 12V — PROFIL (c) uniquement",
    "D8": "TVS 1.5KE18A bus 12V — PROFIL (c) uniquement",
    "D9": "Zener 15V grille Q11 — PROFIL (c) uniquement",
    "R40": "100k grille Q11 — PROFIL (c) uniquement",
    "C5": "100u/25V apres Q11 — PROFIL (c) uniquement",
    "J36": "Bornier sortie 12V protegee vers buck externe — PROFIL (c) uniquement",
    "J37": "Bornier sortie 12V protegee vers buck externe — PROFIL (c) uniquement",
}
EXCLUDED = {"A1", "A2", "PS1"}


def load_bom() -> list[dict]:
    with (ROOT / "BOM.csv").open(encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f, delimiter=";"))
    # lignes « extra » (supports, cavaliers, visserie) : pas de repère PCB
    return [r for r in rows if r.get("Refs") and r.get("Empreinte")]


def load_cpl() -> dict[str, dict]:
    with (EXPORTS / "cpl-jlcpcb.csv").open(encoding="utf-8-sig", newline="") as f:
        return {r["Designator"]: r for r in csv.DictReader(f)}


def main() -> None:
    bom, cpl = load_bom(), load_cpl()
    PCBA.mkdir(parents=True, exist_ok=True)
    socle: dict[tuple[str, str, str], list[str]] = defaultdict(list)
    cond: list[tuple[str, str, str, str]] = []
    n_socle = 0
    for row in bom:
        refs = [r.strip() for r in row["Refs"].split(",") if r.strip()]
        for ref in refs:
            if ref in EXCLUDED or ref.startswith(("H", "TP")):
                continue
            if ref in CONDITIONAL:
                cond.append((CONDITIONAL[ref], ref, row["Empreinte"], row.get("LCSC", "")))
                continue
            socle[(row["Valeur"], row["Empreinte"], row.get("LCSC", ""))].append(ref)
            n_socle += 1
    missing = [r for refs in socle.values() for r in refs if r not in cpl]
    if missing:
        sys.exit(f"ECHEC: repères sans position dans cpl-jlcpcb.csv : {missing}")

    with (PCBA / "BOM-PCBA-socle.csv").open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (val, fp, lcsc), refs in sorted(socle.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            w.writerow([val, ",".join(sorted(refs, key=natural)), fp, lcsc])
    with (PCBA / "BOM-PCBA-conditionnels.csv").open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for comment, ref, fp, lcsc in sorted(cond, key=lambda c: natural(c[1])):
            w.writerow([comment, ref, fp, lcsc])
    with (PCBA / "CPL-n3-universal-top.csv").open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for ref in sorted((r for refs in socle.values() for r in refs), key=natural):
            c = cpl[ref]
            w.writerow([ref, c["Mid X"], c["Mid Y"], c["Layer"], c["Rotation"]])
    print(f"OK  pcba/ : socle {len(socle)} groupes / {n_socle} pièces, "
          f"{len(cond)} conditionnelles, CPL {n_socle} lignes")


def natural(ref: str):
    head = ref.rstrip("0123456789")
    tail = ref[len(head):]
    return (head, int(tail) if tail else 0)


if __name__ == "__main__":
    main()
