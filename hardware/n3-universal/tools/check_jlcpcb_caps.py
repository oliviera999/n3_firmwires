#!/usr/bin/env python3
"""Garde « capacités JLCPCB » sur le PCB routé — ce que le DRC KiCad ne borne pas
par défaut, aux minima du fabricant pour une carte 2 couches **2 oz** HASL.

Seuils (jlcpcb.com/capabilities, 2 couches, cuivre extérieur 2 oz) :
  piste / isolement        >= 0,20 mm  (0,127 en 1 oz ; l'isolement est déjà la règle DRC 0,2)
  perçage mini (PTH/via)   >= 0,20 mm
  via : perçage / diamètre >= 0,30 / 0,45 mm
  anneau (via et pastille) >= 0,13 mm        (= (diamètre - perçage) / 2)
  trou à trou (bord à bord)>= 0,50 mm        (DRC KiCad par défaut : 0,25)
  cuivre - bord de carte   >= 0,30 mm        (DRC KiCad par défaut : 0,50 -> déjà couvert)
  fente fraisée (largeur)  >= 0,65 mm        (fentes d'isolement et porte-fusible : Edge.Cuts)
  sérigraphie : trait >= 0,15 mm (défaut), hauteur de texte >= 1,0 mm (avertissement seulement)
  job Gerber : épaisseur de cuivre 0,07 mm (2 oz) dans le .gbrjob, 10 fichiers dans le zip

Usage : <python KiCad> tools/check_jlcpcb_caps.py [--zip exports/gerbers-...zip]   (code 1 si défaut)
"""

from __future__ import annotations

import json
import math
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "generator"))

import pcbnew  # noqa: E402
import generate as G  # noqa: E402

PCB = ROOT / "kicad" / f"{G.PROJECT}.kicad_pcb"
MM = 1e6  # unités internes KiCad (nm) -> mm

LIM = dict(track=0.20, drill=0.20, via_drill=0.30, via_dia=0.45, annular=0.13, hole2hole=0.50,
           slot=0.65, silk_w=0.15, silk_h=1.0, copper_oz_mm=0.07)


def main() -> int:
    b = pcbnew.LoadBoard(str(PCB))
    errors: list[str] = []
    notes: list[str] = []

    # --- pistes ---------------------------------------------------------------
    tracks = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]
    vias = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
    wmin = min(t.GetWidth() for t in tracks) / MM
    notes.append(f"piste la plus fine : {wmin:.3f} mm ({len(tracks)} segments)")
    if wmin < LIM["track"]:
        errors.append(f"piste {wmin:.3f} mm < {LIM['track']} mm (2 oz)")

    # --- vias -----------------------------------------------------------------
    holes = []  # (x, y, rx, ry) : trou approximé par son rectangle englobant (mm)
    for v in vias:
        d, w = v.GetDrillValue() / MM, v.GetWidth() / MM
        if d < LIM["via_drill"]:
            errors.append(f"via perçage {d:.2f} < {LIM['via_drill']} à {v.GetPosition()}")
        if w < LIM["via_dia"]:
            errors.append(f"via diamètre {w:.2f} < {LIM['via_dia']} à {v.GetPosition()}")
        if (w - d) / 2 < LIM["annular"]:
            errors.append(f"via anneau {(w - d) / 2:.3f} < {LIM['annular']} à {v.GetPosition()}")
        p = v.GetPosition()
        holes.append((p.x / MM, p.y / MM, d / 2, d / 2, f"via {v.GetNetname()}"))
    if vias:
        notes.append(f"vias : {len(vias)}, perçage {min(v.GetDrillValue() for v in vias) / MM:.2f} / "
                     f"diamètre {min(v.GetWidth() for v in vias) / MM:.2f} mm")

    # --- pastilles -------------------------------------------------------------
    dmin, ring_min = 99.0, 99.0
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            ds = pad.GetDrillSize()
            if ds.x == 0:
                continue
            dx, dy = ds.x / MM, ds.y / MM
            sz = pad.GetSize()
            sx, sy = sz.x / MM, sz.y / MM
            ref = f"{fp.GetReferenceAsString()}-{pad.GetNumber()}"
            dmin = min(dmin, dx, dy)
            if min(dx, dy) < LIM["drill"]:
                errors.append(f"{ref} : perçage {min(dx, dy):.2f} < {LIM['drill']}")
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                ring = min((sx - dx) / 2, (sy - dy) / 2)
                ring_min = min(ring_min, ring)
                if ring < LIM["annular"]:
                    errors.append(f"{ref} : anneau {ring:.3f} < {LIM['annular']}")
            if dx != dy and min(dx, dy) < LIM["slot"]:
                errors.append(f"{ref} : fente plaquée {min(dx, dy):.2f} < {LIM['slot']}")
            p = pad.GetPosition()
            # rotation : rectangle englobant conservateur (max des deux côtés)
            r = max(dx, dy) / 2 if dx != dy else dx / 2
            holes.append((p.x / MM, p.y / MM, r, r, ref))
    notes.append(f"perçage composant mini : {dmin:.2f} mm ; anneau PTH mini : {ring_min:.3f} mm")

    # --- trou à trou (bord à bord, cercles englobants : conservateur) -----------
    h2h_min, pair = 99.0, ""
    for i in range(len(holes)):
        x1, y1, r1, _, n1 = holes[i]
        for j in range(i + 1, len(holes)):
            x2, y2, r2, _, n2 = holes[j]
            if abs(x1 - x2) > 6 or abs(y1 - y2) > 6:
                continue
            d = math.hypot(x1 - x2, y1 - y2) - r1 - r2
            if d < h2h_min:
                h2h_min, pair = d, f"{n1} / {n2}"
    notes.append(f"trou à trou mini : {h2h_min:.2f} mm ({pair})")
    if h2h_min < LIM["hole2hole"]:
        errors.append(f"trou à trou {h2h_min:.2f} < {LIM['hole2hole']} mm ({pair})")

    # --- fentes Edge.Cuts internes (largeur) -----------------------------------
    # Les fentes du générateur sont des rectangles (x0,y0,x1,y1) : largeur = min des côtés.
    slot_min = min(min(x1 - x0, y1 - y0) for x0, y0, x1, y1 in G.SLOTS) if getattr(G, "SLOTS", None) else None
    if slot_min is not None:
        notes.append(f"fentes fraisées : {len(G.SLOTS)}, largeur mini {slot_min:.2f} mm")
        if slot_min < LIM["slot"]:
            errors.append(f"fente {slot_min:.2f} < {LIM['slot']} mm")

    # --- sérigraphie -----------------------------------------------------------
    silk = {pcbnew.F_SilkS, pcbnew.B_SilkS}
    tw_min, th_min, lw_min = 99.0, 99.0, 99.0
    items = list(b.GetDrawings())
    for fp in b.GetFootprints():
        items += list(fp.GraphicalItems())
        items += [f for f in fp.GetFields()]
    for it in items:
        if it.GetLayer() not in silk:
            continue
        cls = it.GetClass()
        if "TEXT" in cls or "FIELD" in cls:
            if not it.IsVisible():
                continue
            tw_min = min(tw_min, it.GetTextThickness() / MM)
            th_min = min(th_min, it.GetTextHeight() / MM)
        elif "SHAPE" in cls:
            w = it.GetWidth() / MM
            if w > 0:
                lw_min = min(lw_min, w)
    notes.append(f"sérigraphie : trait texte mini {tw_min:.2f}, hauteur mini {th_min:.2f}, trait graphique mini {lw_min:.2f} mm")
    if tw_min < LIM["silk_w"] or lw_min < LIM["silk_w"]:
        errors.append(f"sérigraphie trait {min(tw_min, lw_min):.2f} < {LIM['silk_w']} mm")
    if th_min < LIM["silk_h"]:
        # JLCPCB recommande >= 1 mm pour la lisibilité ; en dessous ce n'est pas un refus
        # (le rapport JLCDFM v0.1 ne le signale pas) : avertissement, pas défaut. Concerne
        # les libellés de broches des empreintes maison (G D S, GND OUT IN, K) à 0,8 mm.
        notes.append(f"AVERTISSEMENT : hauteur de texte sérigraphie {th_min:.2f} < {LIM['silk_h']} mm "
                     "(libellés de broches des empreintes TO-220/LED/diode : lisibilité, pas un refus)")

    # --- job Gerber / zip ----------------------------------------------------------
    zips = sorted((ROOT / "exports").glob("gerbers-*.zip"))
    if "--zip" in sys.argv:
        zips = [Path(sys.argv[sys.argv.index("--zip") + 1])]
    if zips:
        z = zipfile.ZipFile(zips[-1])
        names = z.namelist()
        job = [n for n in names if n.endswith(".gbrjob")]
        if len(names) != 10 or not job:
            errors.append(f"{zips[-1].name} : {len(names)} fichiers (10 attendus) / job {'présent' if job else 'ABSENT'}")
        else:
            j = json.loads(z.read(job[0]))
            cu = [l for l in j.get("MaterialStackup", []) if l.get("Type") == "Copper"]
            th = {round(l.get("Thickness", 0), 3) for l in cu}
            size = j.get("GeneralSpecs", {}).get("Size", {})
            notes.append(f"{zips[-1].name} : {len(names)} fichiers, cuivre {sorted(th)} mm, "
                         f"{size.get('X')} x {size.get('Y')} mm, {j.get('GeneralSpecs', {}).get('LayerNumber')} couches")
            if th != {LIM["copper_oz_mm"]}:
                errors.append(f"job Gerber : épaisseur de cuivre {sorted(th)} != {LIM['copper_oz_mm']} mm (2 oz)")

    for n in notes:
        print("  ", n)
    if errors:
        print("\nDEFAUTS :")
        for e in errors:
            print("  -", e)
        return 1
    print("\nOK : capacités JLCPCB 2 oz respectées")
    return 0


if __name__ == "__main__":
    sys.exit(main())
