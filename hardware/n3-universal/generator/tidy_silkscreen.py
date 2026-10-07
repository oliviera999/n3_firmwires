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
contours Edge.Cuts (bord de carte et fentes d'isolement), et — depuis la
rev 0.2 — le **courtyard de tout composant voisin** : un repère poussé sous
le corps d'un relais ou d'une barrette désignerait ce composant-là une fois
la carte montée (constat SILK-REF-01 de l'audit §13 sur la 0.1.2). Un repère
reste donc dans le courtyard de sa propre empreinte ou à l'extérieur de tous
les autres ; `tools/check_silk_refs.py` le vérifie après coup.

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
MAX_SHIFT_REF = 6.0     # rayon de recherche ; la position finale doit en plus rester
MAX_DIST_REF = 3.0      # à <= 3 mm du corps (courtyard) de SON composant (= tools/check_silk_refs.py)
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
        # vias (tentés : la sérigraphie peut les recouvrir sans défaut DRC, mais
        # le perçage troue le texte) : évités en première intention, tolérés en
        # repli — voir place()
        self.vias = [inflate(t.GetBoundingBox(), PAD_MARGIN)
                     for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
        self.avoid_vias = True
        self.shapes = [g for fp in board.GetFootprints() for g in fp.GraphicalItems()
                       if g.GetClass() == "PCB_SHAPE" and g.GetLayer() == silk_layer]
        self.fp_texts = [g for fp in board.GetFootprints() for g in fp.GraphicalItems()
                         if g.GetClass() == "PCB_TEXT" and g.GetLayer() == silk_layer
                         and g.IsVisible()]
        edge = board.GetLayerID("Edge.Cuts")
        self.edges = [d for d in board.GetDrawings() if d.GetLayer() == edge]
        self.board_box = board.GetBoardEdgesBoundingBox()
        crt = board.GetLayerID("F.CrtYd" if front else "B.CrtYd")
        # courtyards (polygones) par repère : un texte ne doit pas entrer
        # dans le corps d'un AUTRE composant
        self.courtyards = {}
        for fp in board.GetFootprints():
            poly = fp.GetCourtyard(crt)
            if poly.OutlineCount():
                self.courtyards[fp.GetReference()] = poly

    def inside_board(self, box) -> bool:
        bb, m = self.board_box, FM(EDGE_MARGIN)
        return (box.GetLeft() > bb.GetLeft() + m and box.GetRight() < bb.GetRight() - m
                and box.GetTop() > bb.GetTop() + m and box.GetBottom() < bb.GetBottom() - m)

    def in_foreign_courtyard(self, box, owner: str) -> bool:
        for ref, poly in self.courtyards.items():
            if ref == owner:
                continue
            bb = poly.BBox()
            if not bb.Intersects(box):
                continue
            # test exact : un des coins ou le centre du texte dans le polygone
            pts = [(box.GetLeft(), box.GetTop()), (box.GetRight(), box.GetTop()),
                   (box.GetLeft(), box.GetBottom()), (box.GetRight(), box.GetBottom()),
                   (box.GetCenter().x, box.GetCenter().y)]
            if any(poly.Contains(pcbnew.VECTOR2I(x, y)) for x, y in pts):
                return True
        return False

    def too_far_from_owner(self, box, owner: str) -> bool:
        """Le repère doit rester dans le courtyard de son composant ou à
        MAX_DIST_REF au plus (distance boîte à boîte)."""
        poly = self.courtyards.get(owner)
        if poly is None:
            return False
        # même mesure que tools/check_silk_refs.py : du CENTRE du texte au
        # corps (ici sa boîte englobante), avec 0,2 mm de marge
        bb = poly.BBox()
        c = box.GetCenter()
        dx = max(bb.GetLeft() - c.x, c.x - bb.GetRight(), 0)
        dy = max(bb.GetTop() - c.y, c.y - bb.GetBottom(), 0)
        return (dx * dx + dy * dy) ** 0.5 > FM(MAX_DIST_REF - 0.1)

    def blocked(self, box, texts, owner: str = "") -> bool:
        if not self.inside_board(box):
            return True
        if owner and self.too_far_from_owner(box, owner):
            return True
        if any(box.Intersects(p) for p in self.pads):
            return True
        if self.avoid_vias and any(box.Intersects(v) for v in self.vias):
            return True
        # règle « pas dans le corps d'un autre composant » : pour les repères
        # (owner = leur empreinte) ; les étiquettes de câblage (PCB_TEXTS) sont
        # placées à la main, souvent contre un bornier, et restent libres
        if owner and self.in_foreign_courtyard(box, owner):
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
        owner = t.GetParentFootprint().GetReference() if t.GetParentFootprint() else ""
        found = False
        for avoid_vias in (True, False):      # repli : accepter un via tenté sous le texte
            obs.avoid_vias = avoid_vias
            for dx, dy in cand:
                t.SetPosition(pcbnew.VECTOR2I(origin.x + FM(dx), origin.y + FM(dy)))
                if not obs.blocked(t.GetBoundingBox(), fixed, owner):
                    moved += (dx, dy) != (0.0, 0.0)
                    found = True
                    break
            if found:
                break
        obs.avoid_vias = True
        if not found:
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
