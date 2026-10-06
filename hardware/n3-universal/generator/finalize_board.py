#!/usr/bin/env python3
"""Retouches pré-commande du PCB ROUTÉ (rev 0.1 -> 0.1.1), sans re-routage.

Pourquoi un script plutôt que des clics : chaque retouche est chiffrée par
l'audit (`AUDIT-2026-08-28.md`) et doit pouvoir être rejouée et relue. Le PCB
routé reste la référence géométrique : `generate.py main()` produirait une
carte NON routée, on ne le relance donc pas. Les mêmes corrections sont
portées dans `generate.py` / `route_universal.py` pour qu'une régénération
complète ne les perde pas.

Retouches (idempotentes — relancer ne change plus rien) :
  - H1-H4 « board only » (pas de symbole : sinon écart de parité schéma/PCB) ;
  - pads des broches non câblées sur leur net « unconnected-(…) » (parité) ;
  - plan GND de la bande relais repoussé de y84 à y86 (SEC-CRP-01) ;
  - vias GND tombés dans le perçage d'un pad GND supprimés (GERB-01) ;
  - amorces 230 V de RV1 ramenées à 2,0 mm (SEC-01) ;
  - traits de sérigraphie des empreintes portés à 0,15 mm (GBR-02) ;
  - libellés de carte resynchronisés sur `PCB_TEXTS` (nouveaux marquages
    SEC-02 / SEC-COM-01 / NET-03 / DEG-05, REV dans le texte de version) ;
  - cartouche (révision, date), `.kicad_dru` et `fp-lib-table` régénérés.

Le remplissage des zones est laissé à `kicad-cli pcb drc --refill-zones`
(seul chemin qui applique les règles personnalisées du `.kicad_dru`).
Ensuite : `tidy_silkscreen.py` place les libellés hors pads / trous.

Usage (Python embarqué de KiCad, qui porte pcbnew) :
  "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" finalize_board.py
"""

from __future__ import annotations

import math
import re
from pathlib import Path

import pcbnew

import generate as g
from route_universal import RV1_STUB_MM

HERE = Path(__file__).resolve().parent
BOARD_PATH = HERE.parent / "kicad" / f"{g.PROJECT}.kicad_pcb"
FM, TM = pcbnew.FromMM, pcbnew.ToMM
OLD_RELAY_Y = 84.0


def mm(p):
    return TM(p.x), TM(p.y)


def board_only_holes(b) -> int:
    n = 0
    for c in g.COMPONENTS:
        if c["sym"]:
            continue
        fp = b.FindFootprintByReference(c["ref"])
        if fp and not fp.IsBoardOnly():
            fp.SetBoardOnly(True)
            n += 1
    return n


def tag_unconnected_pads(b) -> int:
    n = 0
    for c in g.COMPONENTS:
        pins = g.unconnected_pins(c)
        if not pins:
            continue
        fp = b.FindFootprintByReference(c["ref"])
        for num, name in pins.items():
            pad = fp.FindPadByNumber(num) if fp else None
            if pad is None or pad.GetNetname() == name:
                continue
            net = b.FindNet(name)
            if net is None:
                net = pcbnew.NETINFO_ITEM(b, name)
                b.Add(net)
            pad.SetNet(net)
            n += 1
    return n


def push_gnd_plane(b) -> int:
    n = 0
    for z in b.Zones():
        if z.GetIsRuleArea() or z.GetNetname() != "GND":
            continue
        outl = z.Outline()
        for i in range(outl.TotalVertices()):
            x, y = mm(outl.CVertex(i))
            if abs(y - OLD_RELAY_Y) < 1e-3:
                outl.SetVertex(i, pcbnew.VECTOR2I(FM(x), FM(g.GND_PLANE_RELAY_Y)))
                n += 1
    return n


def drop_vias_in_holes(b) -> list[tuple[float, float]]:
    holes = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            d = p.GetDrillSize()
            if d.x > 0:
                x, y = mm(p.GetPosition())
                holes.append((x, y, TM(max(d.x, d.y)) / 2))
    gone = []
    for t in list(b.GetTracks()):
        if t.GetClass() != "PCB_VIA":
            continue
        vx, vy = mm(t.GetPosition())
        vr = TM(t.GetDrillValue()) / 2
        if any(math.hypot(vx - hx, vy - hy) < hr + vr + 0.2 for hx, hy, hr in holes):
            b.Remove(t)
            gone.append((vx, vy))
    return gone


def narrow_rv1_stubs(b) -> int:
    rv1 = b.FindFootprintByReference("RV1")
    pads = {p.GetNetname(): mm(p.GetPosition()) for p in rv1.Pads()}
    n = 0
    for t in b.GetTracks():
        if t.GetClass() != "PCB_TRACK" or t.GetNetname() not in pads:
            continue
        px, py = pads[t.GetNetname()]
        ends = [mm(t.GetStart()), mm(t.GetEnd())]
        if any(math.hypot(ex - px, ey - py) < 0.01 for ex, ey in ends):
            if abs(TM(t.GetWidth()) - RV1_STUB_MM) > 1e-3:
                t.SetWidth(FM(RV1_STUB_MM))
                n += 1
    return n


def widen_fp_silk(b) -> int:
    silk = {pcbnew.F_SilkS, pcbnew.B_SilkS}
    n = 0
    for fp in b.GetFootprints():
        for d in fp.GraphicalItems():
            if d.GetLayer() in silk and d.GetClass() == "PCB_SHAPE":
                if TM(d.GetWidth()) < g.SILK_MIN_STROKE - 1e-6:
                    d.SetWidth(FM(g.SILK_MIN_STROKE))
                    n += 1
    return n


def sync_labels(b) -> int:
    silk = {pcbnew.F_SilkS, pcbnew.B_SilkS}
    for d in list(b.GetDrawings()):
        if d.GetClass() == "PCB_TEXT" and d.GetLayer() in silk:
            b.Remove(d)
    for entry in g.PCB_TEXTS:
        x, y, txt, s = entry[:4]
        layer = entry[4] if len(entry) > 4 else "F.SilkS"
        h = max(float(s), g.SILK_MIN_H)
        t = pcbnew.PCB_TEXT(b)
        t.SetText(txt)
        t.SetLayer(b.GetLayerID(layer))
        t.SetTextSize(pcbnew.VECTOR2I(FM(h), FM(h)))
        t.SetTextThickness(FM(round(h * g.SILK_RATIO, 2)))
        t.SetPosition(pcbnew.VECTOR2I(FM(x), FM(y)))
        t.SetTextAngleDegrees(float(entry[5]) if len(entry) > 5 else 0.0)
        t.SetMirrored(layer.startswith("B."))
        b.Add(t)
    return len(g.PCB_TEXTS)


def main() -> None:
    b = pcbnew.LoadBoard(str(BOARD_PATH))
    print("trous board-only      :", board_only_holes(b))
    print("pads non câblés       :", tag_unconnected_pads(b))
    print("sommets plan GND y86  :", push_gnd_plane(b))
    print("vias dans un perçage  :", drop_vias_in_holes(b))
    print("amorces RV1 à 2,0 mm  :", narrow_rv1_stubs(b))
    print("traits sérigr. 0,15   :", widen_fp_silk(b))
    print("libellés (PCB_TEXTS)  :", sync_labels(b))
    pcbnew.SaveBoard(str(BOARD_PATH), b)
    # GetTitleBlock() n'est pas exposé proprement par SWIG (KiCad 10) : le
    # cartouche est réécrit dans le fichier sauvegardé.
    txt = BOARD_PATH.read_text(encoding="utf-8")
    txt = re.sub(r'(\(title_block[\s\S]*?\(rev )"[^"]*"', rf'\1"{g.REV}"', txt, count=1)
    txt = re.sub(r'(\(title_block[\s\S]*?\(date )"[^"]*"', rf'\1"{g.REV_DATE}"', txt, count=1)
    BOARD_PATH.write_text(txt, encoding="utf-8", newline="\n")
    kicad = BOARD_PATH.parent
    (kicad / f"{g.PROJECT}.kicad_dru").write_text(g.DRU_RULES, encoding="utf-8")
    (kicad / "fp-lib-table").write_text(g.FP_LIB_TABLE, encoding="utf-8")
    print(f"OK {BOARD_PATH.name} (rev {g.REV}) — lancer ensuite le refill "
          "(kicad-cli pcb drc --refill-zones --save-board) puis tidy_silkscreen.py")


if __name__ == "__main__":
    main()
