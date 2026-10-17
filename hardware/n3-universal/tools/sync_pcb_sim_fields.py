#!/usr/bin/env python3
"""Propage les champs `Sim.*` du schéma (rev 0.2.1, `generate.sim_fields`) aux
empreintes du PCB routé — sinon le DRC de parité KiCad 10 signale un
« Missing symbol field 'Sim.Device' in footprint » par composant simulable.

Même logique que `annotate_pcb_roles.py` (le PCB routé n'est pas régénéré,
seuls les champs sont réécrits), mais via l'API pcbnew : il faut le Python de
KiCad (Linux : python3 avec le paquet kicad ; Windows :
"C:\\Program Files\\KiCad\\10.0\\bin\\python.exe").

Usage : <python KiCad> tools/sync_pcb_sim_fields.py [--check]   (code 1 si dérive avec --check)
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "generator"))

import pcbnew  # noqa: E402
import generate as G  # noqa: E402

PCB = ROOT / "kicad" / f"{G.PROJECT}.kicad_pcb"


def main() -> int:
    check = "--check" in sys.argv
    board = pcbnew.LoadBoard(str(PCB))
    comps = {c["ref"]: c for c in G.COMPONENTS}
    drift, written, footprints = [], 0, 0
    for fp in board.GetFootprints():
        c = comps.get(fp.GetReferenceAsString())
        if not c:
            continue
        sim = G.sim_fields(c)
        if sim is None:
            continue
        footprints += 1
        for k, v in sim.items():
            if fp.HasField(k) and fp.GetFieldText(k) == v:
                continue
            drift.append(f"{c['ref']}.{k}")
            if not check:
                fp.SetField(k, v)
                f = fp.GetField(k)
                f.SetVisible(False)
                f.SetLayer(pcbnew.F_Fab)
                written += 1
    if check:
        if drift:
            print(f"ECHEC : {len(drift)} champ(s) Sim.* absent(s) ou différent(s) sur le PCB : "
                  + ", ".join(drift[:12]) + (" …" if len(drift) > 12 else ""))
            return 1
        print(f"OK : les {footprints} empreintes simulables portent leurs champs Sim.*")
        return 0
    if written:
        pcbnew.SaveBoard(str(PCB), board)
    print(f"{written} champ(s) Sim.* écrit(s) sur {footprints} empreintes simulables ({PCB.name})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
