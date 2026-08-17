#!/usr/bin/env python3
"""Range la sérigraphie : écarte les repères de composants (J3, K4, R21…) qui
recouvrent une étiquette de câblage ou un autre repère.

Pourquoi : les deux textes portent une information complémentaire et doivent
rester lisibles une fois la carte imprimée — le **repère** sert à l'assemblage
(il fait le lien avec `BOM.csv` et les feuilles `ASSEMBLAGE-*.md`), l'**étiquette**
sert au câblage (« NO COM NC », « ZONE 230V - DANGER »…). Superposés, on perd
les deux. On déplace donc le repère — court et facile à recaser — jamais
l'étiquette, dont la position porte le sens (juste au-dessus du bon bornier).

L'empilement des textes par-dessus les pastilles est aussi évité : la plupart
des fabricants (dont JLCPCB) suppriment la sérigraphie qui déborde sur un pad.

Déterministe (parcours trié, décalages essayés dans un ordre fixe) et idempotent :
relancer ne bouge plus rien. À lancer APRÈS route_*.py et AVANT export_fab.py.

Usage : python3 tidy_silkscreen.py [--dry-run]
"""

from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path

import pcbnew

HERE = Path(__file__).resolve().parent
KICAD = HERE.parent / "kicad"

FM, TM = pcbnew.FromMM, pcbnew.ToMM
MAX_SHIFT = 6.0    # au-delà, le repère ne désigne plus clairement son composant
STEP = 0.5
EDGE_MARGIN = 0.5


def silk_layers(board):
    return {board.GetLayerID("F.SilkS"), board.GetLayerID("B.SilkS")}


def collect(board):
    """(repères déplaçables, boîtes fixes) par couche de sérigraphie."""
    layers = silk_layers(board)
    refs, labels = [], []
    for dr in board.GetDrawings():
        if dr.GetClass() == "PCB_TEXT" and dr.GetLayer() in layers:
            labels.append(dr)
    for fp in board.GetFootprints():
        t = fp.Reference()
        if t.IsVisible() and t.GetLayer() in layers:
            refs.append(t)
    return sorted(refs, key=lambda t: t.GetText()), labels


def pad_boxes(board):
    out = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            out.append(p.GetBoundingBox())
    return out


def hits(box, others) -> bool:
    return any(box.Intersects(o) for o in others)


def tidy(path: Path, dry: bool) -> tuple[int, list[str]]:
    board = pcbnew.LoadBoard(str(path))
    refs, labels = collect(board)
    pads = pad_boxes(board)
    bb = board.GetBoardEdgesBoundingBox()

    def fixed_boxes(exclude):
        b = [t.GetBoundingBox() for t in labels]
        b += [t.GetBoundingBox() for t in refs if t is not exclude]
        return b + pads

    # décalages essayés : d'abord verticaux (au-dessus/en dessous du composant),
    # puis horizontaux, puis diagonales — du plus petit au plus grand
    offsets = []
    d = STEP
    while d <= MAX_SHIFT + 1e-9:
        offsets += [(0, -d), (0, d), (-d, 0), (d, 0), (-d, -d), (d, -d),
                    (-d, d), (d, d)]
        d += STEP

    moved, stuck = 0, []
    for t in refs:
        others = fixed_boxes(t)
        if not hits(t.GetBoundingBox(), others):
            continue
        origin = t.GetPosition()
        placed = False
        for dx, dy in offsets:
            t.SetPosition(pcbnew.VECTOR2I(origin.x + FM(dx), origin.y + FM(dy)))
            box = t.GetBoundingBox()
            inside = (box.GetLeft() > bb.GetLeft() + FM(EDGE_MARGIN)
                      and box.GetRight() < bb.GetRight() - FM(EDGE_MARGIN)
                      and box.GetTop() > bb.GetTop() + FM(EDGE_MARGIN)
                      and box.GetBottom() < bb.GetBottom() - FM(EDGE_MARGIN))
            if inside and not hits(box, others):
                placed = True
                moved += 1
                break
        if not placed:
            t.SetPosition(origin)
            stuck.append(t.GetText())
    if moved and not dry:
        pcbnew.SaveBoard(str(path), board)
    # audit final, couche par couche : ce qui reste ne peut être qu'un conflit
    # entre deux étiquettes -> à corriger à la main dans PCB_TEXTS (generate.py)
    rest = []
    for lname in ("F.SilkS", "B.SilkS"):
        lay = board.GetLayerID(lname)
        items = [(t.GetText(), t.GetBoundingBox()) for t in labels + refs
                 if t.GetLayer() == lay]
        rest += [f"{a[0]!r} <-> {c[0]!r} ({lname})"
                 for i, a in enumerate(items) for c in items[i + 1:]
                 if a[1].Intersects(c[1])]
    return moved, stuck + rest


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    pcbs = sorted(KICAD.glob("*.kicad_pcb"))
    if not pcbs:
        sys.exit(f"aucun .kicad_pcb dans {KICAD}")
    moved, stuck = tidy(pcbs[0], args.dry_run)
    print(f"{pcbs[0].name}: {moved} repère(s) écarté(s)"
          + (" (dry-run, rien écrit)" if args.dry_run else ""))
    if stuck:
        print("  ATTENTION, pas de place trouvée pour :", ", ".join(stuck))
        print("  -> déplacer l'étiquette correspondante dans PCB_TEXTS (generate.py)")


if __name__ == "__main__":
    main()
