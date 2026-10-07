#!/usr/bin/env python3
"""Garde de sérigraphie : chaque REPÈRE (R5, Q1, JP7…) désigne SON composant.

Audit `SILK-REF-01` (rev 0.1.2) : 21 repères s'étaient retrouvés imprimés dans le
corps du composant VOISIN (« R1 » sur la diode D1, « R5 » à la place de la base…) —
monté d'après la sérigraphie, un canal relais ne collait jamais. Le DRC KiCad ne
voit pas ce défaut (un texte au-dessus d'un contour n'est pas une violation).

Règle vérifiée sur le PCB routé, repère par repère (texte visible en F/B.SilkS) :
 1. le centre du repère n'est DANS le courtyard d'aucune AUTRE empreinte
    (les paires à peuplement exclusif A1/A2 exceptées) ;
 2. le repère est dans son propre courtyard OU à moins de `MAX_DIST_MM` de lui
    (connecteurs et modules portent leur repère juste au-dessus du corps).
Idem pour le texte de valeur (`${VALUE}`) quand il existe.

Usage : python3 tools/check_silk_refs.py [carte.kicad_pcb]   (code 1 si défaut)
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "generator"))

import generate as G  # noqa: E402

DEFAULT_PCB = ROOT / "kicad" / f"{G.PROJECT}.kicad_pcb"
MAX_DIST_MM = 3.0
EXCLUSIVE = {frozenset(("A1", "A2"))}


def _rot_pt(px, py, x0, y0, rot):
    return G.place_points([(px, py)], x0, y0, rot)[0]


def _inside(hull, p) -> bool:
    """Point strictement dans un polygone convexe (orientation quelconque)."""
    n = len(hull)
    if n < 3:
        return False
    sign = 0
    for i in range(n):
        (x0, y0), (x1, y1) = hull[i], hull[(i + 1) % n]
        cross = (x1 - x0) * (p[1] - y0) - (y1 - y0) * (p[0] - x0)
        if abs(cross) < 1e-9:
            continue
        s = 1 if cross > 0 else -1
        if sign == 0:
            sign = s
        elif s != sign:
            return False
    return True


def _dist_to_hull(hull, p) -> float:
    if _inside(hull, p):
        return 0.0
    best = float("inf")
    n = len(hull)
    for i in range(n):
        (x0, y0), (x1, y1) = hull[i], hull[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        l2 = dx * dx + dy * dy
        t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((p[0] - x0) * dx + (p[1] - y0) * dy) / l2))
        best = min(best, math.hypot(p[0] - (x0 + t * dx), p[1] - (y0 + t * dy)))
    return best


def texts_of(fp_tree, ref: str):
    """[(libellé, x_local, y_local, visible)] : repère + valeur sérigraphiée."""
    out = []
    for prop in G.sx_find_all(fp_tree, G.Sym("property")):
        if prop[1] != "Reference":
            continue
        layer = G.sx_find_all(prop, G.Sym("layer"))
        if not layer or "SilkS" not in str(layer[0][1]):
            continue
        hidden = any(isinstance(h, list) and h and h[0] == "hide" and str(h[1]) == "yes"
                     for h in prop) or any(
            isinstance(e, list) and e and e[0] == "effects" and
            any(isinstance(h, list) and h and h[0] == "hide" and str(h[1]) == "yes" for h in e)
            for e in prop)
        at = G.sx_find_all(prop, G.Sym("at"))[0]
        out.append((ref, float(at[1]), float(at[2]), not hidden))
    for txt in G.sx_find_all(fp_tree, G.Sym("fp_text")):
        if len(txt) > 2 and str(txt[1]) == "user" and txt[2] == "${VALUE}":
            at = G.sx_find_all(txt, G.Sym("at"))[0]
            out.append((f"{ref} (valeur)", float(at[1]), float(at[2]), True))
    return out


def run(pcb_path: Path) -> int:
    tree = G.sx_parse(pcb_path.read_text(encoding="utf-8"))
    parts = []
    for fp in G.sx_find_all(tree, G.Sym("footprint")):
        ref = next((p[2] for p in G.sx_find_all(fp, G.Sym("property")) if p[1] == "Reference"), "?")
        at = G.sx_find_all(fp, G.Sym("at"))[0]
        x0, y0 = float(at[1]), float(at[2])
        rot = float(at[3]) if len(at) > 3 else 0.0
        pts = G.courtyard_points(fp)
        hull = G.convex_hull(G.place_points(pts, x0, y0, rot)) if pts else []
        parts.append((ref, hull, [(lbl, *_rot_pt(tx, ty, x0, y0, rot), vis)
                                  for lbl, tx, ty, vis in texts_of(fp, ref)]))
    errors, checked = [], 0
    for ref, hull, texts in parts:
        for lbl, tx, ty, vis in texts:
            if not vis or ref.startswith("H"):
                continue
            checked += 1
            p = (tx, ty)
            for other, ohull, _ in parts:
                if other == ref or not ohull or frozenset((ref, other)) in EXCLUSIVE:
                    continue
                if _inside(ohull, p):
                    errors.append(f"{lbl} ({tx:.1f},{ty:.1f}) est DANS le corps de {other}")
            if hull:
                d = _dist_to_hull(hull, p)
                if d > MAX_DIST_MM:
                    errors.append(f"{lbl} ({tx:.1f},{ty:.1f}) à {d:.1f} mm de son composant (> {MAX_DIST_MM})")
    print(f"{pcb_path.name} : {checked} repères/valeurs contrôlés sur {len(parts)} empreintes")
    for e in errors:
        print("  ", e)
    print("ECHEC" if errors else "OK : chaque repère désigne son composant")
    return 1 if errors else 0


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    sys.exit(run(Path(args[0]) if args else DEFAULT_PCB))
