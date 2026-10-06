#!/usr/bin/env python3
"""Range la sérigraphie : écarte les étiquettes de câblage (« NO COM NC »,
« !! DANGER 230V !! »…) puis les repères de composants (J3, K4, R21…) de tout
ce qui les rendrait illisibles ou serait supprimé par le fabricant.

Pourquoi : les deux textes portent une information complémentaire et doivent
rester lisibles une fois la carte imprimée — le **repère** sert à l'assemblage
(il fait le lien avec `BOM.csv` et les feuilles `ASSEMBLAGE-*.md`), l'**étiquette**
sert au câblage. Les fabricants (dont JLCPCB) suppriment la sérigraphie qui
déborde sur un pad ou une fente ; un texte posé sur le contour d'un composant
devient illisible.

Obstacles (géométrie réelle, pas des boîtes englobantes de composants) :
pads de la même face (+ marge), vias, traits de sérigraphie des empreintes
(test de collision exact), textes visibles des empreintes, autres étiquettes,
contours Edge.Cuts (bord de carte et fentes d'isolement).

Ordre : 1) étiquettes, déplacement court (MAX_SHIFT_LABEL, leur position porte
le sens : juste au-dessus du bon bornier) ; les repères ne les bloquent pas ;
2) repères, en tenant les étiquettes pour fixes.

Les étiquettes viennent de `PCB_TEXTS` (generate.py) et sont recréées par
finalize_board.py : lancer ce script APRÈS finalize_board.py et le remplissage
des zones, AVANT export_fab.py. Déterministe (parcours trié, décalages dans un
ordre fixe) et idempotent : relancer ne bouge plus rien.

Usage : python tidy_silkscreen.py [--dry-run]   (Python de KiCad)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import pcbnew

HERE = Path(__file__).resolve().parent
KICAD = HERE.parent / "kicad"

FM, TM = pcbnew.FromMM, pcbnew.ToMM
MAX_SHIFT_REF = 6.0     # au-delà, le repère ne désigne plus clairement son composant
MAX_SHIFT_LABEL = 5.0
STEP = 0.25
EDGE_MARGIN = 0.5       # sérigraphie <-> Edge.Cuts (règle DRC silk_edge_clearance)
PAD_MARGIN = 0.15       # sérigraphie <-> pad (règle DRC silk_over_copper)
SILK_MARGIN = 0.1       # sérigraphie <-> sérigraphie (règle DRC silk_overlap)


def inflate(box, mm):
    b = pcbnew.BOX2I(box.GetPosition(), box.GetSize())
    b.Inflate(FM(mm))
    return b


class Obstacles:
    """Tout ce qui est figé sur une face de sérigraphie donnée."""

    def __init__(self, board, silk_layer):
        front = silk_layer == board.GetLayerID("F.SilkS")
        cu = board.GetLayerID("F.Cu" if front else "B.Cu")
        self.layer = silk_layer
        self.pads = [inflate(p.GetBoundingBox(), PAD_MARGIN)
                     for fp in board.GetFootprints() for p in fp.Pads()
                     if p.IsOnLayer(cu)]
        self.pads += [inflate(t.GetBoundingBox(), PAD_MARGIN)
                      for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
        self.shapes = [g for fp in board.GetFootprints() for g in fp.GraphicalItems()
                       if g.GetClass() == "PCB_SHAPE" and g.GetLayer() == silk_layer]
        self.fp_texts = [g for fp in board.GetFootprints() for g in fp.GraphicalItems()
                         if g.GetClass() == "PCB_TEXT" and g.GetLayer() == silk_layer
                         and g.IsVisible()]
        edge = board.GetLayerID("Edge.Cuts")
        self.edges = [d for d in board.GetDrawings() if d.GetLayer() == edge]
        self.board_box = board.GetBoardEdgesBoundingBox()

    def inside_board(self, box) -> bool:
        bb, m = self.board_box, FM(EDGE_MARGIN)
        return (box.GetLeft() > bb.GetLeft() + m and box.GetRight() < bb.GetRight() - m
                and box.GetTop() > bb.GetTop() + m and box.GetBottom() < bb.GetBottom() - m)

    def blocked(self, box, texts) -> bool:
        if not self.inside_board(box):
            return True
        if any(box.Intersects(p) for p in self.pads):
            return True
        if any(e.HitTest(box, False, FM(EDGE_MARGIN)) for e in self.edges):
            return True
        if any(s.HitTest(box, False, FM(SILK_MARGIN)) for s in self.shapes):
            return True
        grown = inflate(box, SILK_MARGIN)
        return any(grown.Intersects(t.GetBoundingBox()) for t in self.fp_texts + texts)


def offsets(max_shift):
    """Verticaux d'abord (au-dessus/en dessous), puis horizontaux, puis
    diagonales — du plus petit au plus grand."""
    out, d = [(0.0, 0.0)], STEP
    while d <= max_shift + 1e-9:
        out += [(0, -d), (0, d), (-d, 0), (d, 0), (-d, -d), (d, -d), (-d, d), (d, d)]
        d += STEP
    return out


def place(items, fixed_of, obstacles_of, max_shift):
    """Déplace chaque texte vers la première position libre. Renvoie
    (nb déplacés, textes sans place)."""
    moved, stuck = 0, []
    cand = offsets(max_shift)
    for t in items:
        obs = obstacles_of[t.GetLayer()]
        fixed = fixed_of(t)
        origin = t.GetPosition()
        for dx, dy in cand:
            t.SetPosition(pcbnew.VECTOR2I(origin.x + FM(dx), origin.y + FM(dy)))
            if not obs.blocked(t.GetBoundingBox(), fixed):
                moved += (dx, dy) != (0.0, 0.0)
                break
        else:
            t.SetPosition(origin)
            stuck.append(t.GetText())
    return moved, stuck


def tidy(path: Path, dry: bool) -> tuple[int, int, list[str]]:
    board = pcbnew.LoadBoard(str(path))
    silk = [board.GetLayerID("F.SilkS"), board.GetLayerID("B.SilkS")]
    obstacles_of = {lay: Obstacles(board, lay) for lay in silk}
    labels = sorted((d for d in board.GetDrawings()
                     if d.GetClass() == "PCB_TEXT" and d.GetLayer() in silk),
                    key=lambda t: (t.GetText(), t.GetPosition().x, t.GetPosition().y))
    refs = sorted((fp.Reference() for fp in board.GetFootprints()
                   if fp.Reference().IsVisible() and fp.Reference().GetLayer() in silk),
                  key=lambda t: t.GetText())
    n_lab, stuck_lab = place(
        labels, lambda t: [o for o in labels if o is not t and o.GetLayer() == t.GetLayer()],
        obstacles_of, MAX_SHIFT_LABEL)
    n_ref, stuck_ref = place(
        refs, lambda t: [o for o in labels + refs if o is not t and o.GetLayer() == t.GetLayer()],
        obstacles_of, MAX_SHIFT_REF)
    if (n_lab or n_ref) and not dry:
        pcbnew.SaveBoard(str(path), board)
    return n_lab, n_ref, [f"étiquette {s!r}" for s in stuck_lab] + [f"repère {s}" for s in stuck_ref]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    pcbs = sorted(KICAD.glob("*.kicad_pcb"))
    if not pcbs:
        sys.exit(f"aucun .kicad_pcb dans {KICAD}")
    n_lab, n_ref, stuck = tidy(pcbs[0], args.dry_run)
    print(f"{pcbs[0].name}: {n_lab} étiquette(s), {n_ref} repère(s) déplacé(s)"
          + (" (dry-run, rien écrit)" if args.dry_run else ""))
    if stuck:
        print("  ATTENTION, pas de place trouvée pour :")
        for s in stuck:
            print("   -", s)
        print("  -> ajuster la position/la taille dans PCB_TEXTS (generate.py)")


if __name__ == "__main__":
    main()
