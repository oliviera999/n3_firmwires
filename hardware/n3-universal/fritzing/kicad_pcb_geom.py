"""Lecture pure Python (sans pcbnew) de la géométrie utile d'un `.kicad_pcb` routé :
position / rotation des empreintes, position absolue de chaque pastille, boîte englobante
du contour de cour (F.CrtYd) ou, à défaut, des pastilles. Sert au générateur Fritzing pour
dessiner la carte avec ses connecteurs à leur vraie place — en CI sans KiCad.

Convention KiCad (axe y vers le bas, rotation θ en degrés, sens trigonométrique à l'écran) :
    abs = pos + ( dx·cosθ + dy·sinθ ,  −dx·sinθ + dy·cosθ )
Vérifié contre pcbnew (tools de ce dossier) sur les 179 empreintes de la rev 0.2.2.
"""
from __future__ import annotations

import math
import re
from pathlib import Path

_TOK = re.compile(r'"((?:[^"\\]|\\.)*)"|\(|\)|[^\s()"]+')


def parse_sexpr(text: str):
    """S-expression -> listes imbriquées de str (les chaînes entre guillemets perdent leurs guillemets)."""
    stack = [[]]
    for m in _TOK.finditer(text):
        tok = m.group(0)
        if tok == "(":
            stack.append([])
        elif tok == ")":
            node = stack.pop()
            stack[-1].append(node)
        elif m.group(1) is not None:
            stack[-1].append(m.group(1))
        else:
            stack[-1].append(tok)
    return stack[0]


def _children(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def _first(node, key):
    c = _children(node, key)
    return c[0] if c else None


def _at(node):
    a = _first(node, "at")
    if not a:
        return 0.0, 0.0, 0.0
    x, y = float(a[1]), float(a[2])
    rot = float(a[3]) if len(a) > 3 and not isinstance(a[3], list) else 0.0
    return x, y, rot


def _rot(dx, dy, deg):
    t = math.radians(deg)
    c, s = math.cos(t), math.sin(t)
    return dx * c + dy * s, -dx * s + dy * c


def _layer(node):
    lay = _first(node, "layer")
    return lay[1] if lay else None


def load_footprints(pcb_path: Path) -> dict[str, dict]:
    """{ref: {pos, rot, layer, fpid, pads: {num: (x, y)}, bbox: (x0, y0, x1, y1), crt: bbox F.CrtYd ou None}}"""
    tree = parse_sexpr(Path(pcb_path).read_text(encoding="utf-8"))
    board = tree[0]
    out = {}
    for fp in _children(board, "footprint"):
        fpid = fp[1]
        x, y, rot = _at(fp)
        ref = None
        for prop in _children(fp, "property"):
            if prop[1] == "Reference":
                ref = prop[2]
        if ref is None:
            continue
        pads, xs, ys = {}, [], []
        for pad in _children(fp, "pad"):
            dx, dy, _ = _at(pad)
            px, py = _rot(dx, dy, rot)
            ax, ay = x + px, y + py
            pads[pad[1]] = (round(ax, 3), round(ay, 3))
            size = _first(pad, "size")
            sx, sy = (float(size[1]), float(size[2])) if size else (1.0, 1.0)
            r = max(sx, sy) / 2
            xs += [ax - r, ax + r]
            ys += [ay - r, ay + r]
        crt_x, crt_y = [], []
        for shp in _children(fp, "fp_line") + _children(fp, "fp_rect") + _children(fp, "fp_poly") + _children(fp, "fp_circle"):
            if _layer(shp) != "F.CrtYd":
                continue
            pts = []
            for key in ("start", "end", "center"):
                p = _first(shp, key)
                if p:
                    pts.append((float(p[1]), float(p[2])))
            ptsn = _first(shp, "pts")
            if ptsn:
                pts += [(float(p[1]), float(p[2])) for p in _children(ptsn, "xy")]
            if shp[0] == "fp_circle":
                c, e = _first(shp, "center"), _first(shp, "end")
                cx, cy, ex, ey = float(c[1]), float(c[2]), float(e[1]), float(e[2])
                rr = math.hypot(ex - cx, ey - cy)
                pts = [(cx - rr, cy - rr), (cx + rr, cy + rr)]
            for dx, dy in pts:
                px, py = _rot(dx, dy, rot)
                crt_x.append(x + px)
                crt_y.append(y + py)
        crt = (min(crt_x), min(crt_y), max(crt_x), max(crt_y)) if crt_x else None
        bbox = (min(xs), min(ys), max(xs), max(ys)) if xs else (x, y, x, y)
        out[ref] = dict(pos=(x, y), rot=rot, layer=_layer(fp), fpid=fpid, pads=pads, bbox=bbox, crt=crt)
    return out


def load_edge(pcb_path: Path):
    """Contour (gr_line/gr_rect sur Edge.Cuts) -> liste de segments ((x0,y0),(x1,y1))."""
    tree = parse_sexpr(Path(pcb_path).read_text(encoding="utf-8"))
    segs = []
    for g in _children(tree[0], "gr_line") + _children(tree[0], "gr_rect"):
        if _layer(g) != "Edge.Cuts":
            continue
        s, e = _first(g, "start"), _first(g, "end")
        if s and e:
            segs.append(((float(s[1]), float(s[2])), (float(e[1]), float(e[2]))))
    return segs


if __name__ == "__main__":
    import json
    import sys

    fps = load_footprints(Path(sys.argv[1]))
    print(json.dumps({k: v for k, v in list(fps.items())[:3]}, indent=1))
