#!/usr/bin/env python3
"""Garde d'orientation des MOSFET canal P : la diode interne ne doit pas
court-circuiter la fonction.

Simulation SPICE rev 0.2 (`simulation/SIMULATION.md` §3) : Q7, interrupteur
high-side du rail capteurs, était câblé drain = +5V / source = LDO_IN, comme les
anti-inversion Q11/Q12. Pour un canal P la diode de substrat va du DRAIN vers la
SOURCE : elle conduisait en permanence et le rail ne se coupait jamais (LDO_IN
4,6 V, +3V3_SW 3,3 V avec GPIO13 bas). ERC et DRC ne voient pas ce défaut : la
netlist est « correcte », c'est le circuit qui est faux.

Règle, par rôle et net AMONT déclarés dans `FET_ROLES` (le rail ou l'entrée qui
alimente le FET ; +5V est l'amont de Q7 mais l'aval de Q12) :
  switch  : interrupteur high-side -> la SOURCE est sur le net amont (le rail),
            le DRAIN vers la charge (diode interne bloquée quand le canal est ouvert) ;
  reverse : anti-inversion « diode idéale » -> le DRAIN est sur le net amont
            (l'entrée), la SOURCE vers le rail protégé (la diode interne conduit
            au démarrage, le canal la court-circuite ensuite, elle bloque en inversion).
Un P-MOSFET sans rôle déclaré est une erreur. Vérifié deux fois : dans `COMPONENTS`
(générateur) et dans les étiquettes du `.kicad_sch` écrit sur le disque.

Usage : python3 tools/check_fet_orientation.py [schema.kicad_sch]   (code 1 si défaut)
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "generator"))

import generate as G  # noqa: E402

DEFAULT_SCH = ROOT / "kicad" / f"{G.PROJECT}.kicad_sch"

# (rôle, net amont) de chaque canal P de la carte — à compléter si un FET est ajouté.
FET_ROLES = {
    "Q7": ("switch", "+5V"),          # commutation du 5 V vers le LDO capteurs (GPIO13 via Q8)
    "Q11": ("reverse", "VBAT12_IN"),  # anti-inversion bus 12 V (J26)
    "Q12": ("reverse", "VIN_RAW"),    # anti-inversion entrée 5 V (J1/J2/J37)
}
PMOS_SYMS = {"PMOS_GDS", "PMOS_DGS"}


def fet_pins(comp: dict) -> dict[str, str]:
    """{'G': num, 'D': num, 'S': num} d'après les noms de broches du symbole."""
    meta = G.SYMBOLS[comp["sym"]]
    return {name: num for num, name, _y in meta.get("left", []) + meta.get("right", [])}


def sch_nets(sch_text: str, comp: dict) -> dict[str, str | None]:
    """Net (étiquette globale) de chaque broche du composant dans le .kicad_sch."""
    out = {}
    for num in comp["nets"]:
        u = G.uid("label", comp["ref"], num)
        m = re.search(r'\(global_label "([^"]*)"[^\n]*\n(?:[^\n]*\n){0,3}?[^\n]*\(uuid "' + re.escape(u) + r'"\)', sch_text)
        out[num] = m.group(1) if m else None
    return out


def check(nets: dict[str, str | None], pins: dict[str, str], role: str, upstream: str) -> str | None:
    d, s = nets.get(pins["D"]), nets.get(pins["S"])
    if d is None or s is None:
        return f"broche D ou S sans net (D={d!r}, S={s!r})"
    up_d, up_s = d == upstream, s == upstream
    if up_d == up_s:
        return f"le net amont {upstream} doit être sur D ou sur S (D={d}, S={s})"
    if role == "switch" and not up_s:
        return (f"interrupteur câblé à l'envers : D={d} (amont) / S={s} — la diode interne D->S "
                f"alimente la charge en permanence ; mettre la SOURCE sur {d}")
    if role == "reverse" and not up_d:
        return (f"anti-inversion câblé à l'envers : S={s} (amont) / D={d} — mettre le DRAIN "
                f"sur l'entrée {s}")
    return None


def main() -> int:
    sch_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SCH
    sch = sch_path.read_text(encoding="utf-8")
    errors = []
    seen = set()
    for c in G.COMPONENTS:
        if c["sym"] not in PMOS_SYMS:
            continue
        ref = c["ref"]
        seen.add(ref)
        if ref not in FET_ROLES:
            errors.append(f"{ref} : P-MOSFET sans (rôle, net amont) dans FET_ROLES")
            continue
        role, upstream = FET_ROLES[ref]
        pins = fet_pins(c)
        for origin, nets in (("generate.py", c["nets"]), (sch_path.name, sch_nets(sch, c))):
            err = check(nets, pins, role, upstream)
            tag = "OK " if err is None else "ERR"
            print(f"{tag} {ref:<4} {role:<8} {origin:<24} G={nets.get(pins['G'])} "
                  f"D={nets.get(pins['D'])} S={nets.get(pins['S'])}")
            if err:
                errors.append(f"{ref} ({origin}) : {err}")
    for ref in FET_ROLES:
        if ref not in seen:
            errors.append(f"{ref} : déclaré dans FET_ROLES mais absent de COMPONENTS")
    if errors:
        print("\nDEFAUTS :")
        for e in errors:
            print("  - " + e)
        return 1
    print(f"\nOK : {len(seen)} P-MOSFET, orientation conforme au rôle (générateur et schéma)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
