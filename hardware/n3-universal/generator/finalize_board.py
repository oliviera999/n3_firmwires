#!/usr/bin/env python3
"""⛔ OBSOLÈTE depuis la rev 0.2 (2026-10-07) — NE PAS LANCER sur le PCB 0.2.

La 0.2 est entièrement régénérée (`generate.py --regen-pcb` → `route_universal.py`
→ `tidy_silkscreen.py` → `export_fab.py`) : ses empreintes (JST-XH, BS250, PCF8574,
fusible horizontal…) et ses retouches ciblent la géométrie 0.1.x routée et
n'existent plus. Conservé pour l'historique des rev 0.1 → 0.1.2 (audit §11-12).

Retouches pré-commande du PCB ROUTÉ (rev 0.1 -> 0.1.2), sans re-routage.

Pourquoi un script plutôt que des clics : chaque retouche est chiffrée par
l'audit (`AUDIT-2026-08-28.md`) et doit pouvoir être rejouée et relue. Le PCB
routé reste la référence géométrique : `generate.py main()` produirait une
carte NON routée, on ne le relance donc pas. Les mêmes corrections sont
portées dans `generate.py` / `route_universal.py` pour qu'une régénération
complète ne les perde pas.

Retouches (idempotentes — relancer ne change plus rien) :
  - H1-H4 « board only » (pas de symbole : sinon écart de parité schéma/PCB) ;
  - pads des broches non câblées sur leur net « unconnected-(…) » (parité) ;
  - empreintes ajoutées ou changées dans `generate.py` posées sur la carte
    routée, valeurs resynchronisées (0.1.2 : Q11 IRF4905, zener D9, RV1 au
    pas 7,5 mm, trou nylon H5) ;
  - pistes de D9 et amorces de RV1 sur ses nouveaux pads (0.1.2) ;
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
import sys as _sys

if "--i-know-this-is-0-1-x" not in _sys.argv:
    _sys.exit("finalize_board.py est obsolète depuis la rev 0.2 (chaîne régénérée) ; "
              "voir README.md. Forcer avec --i-know-this-is-0-1-x sur un PCB 0.1.x.")
_sys.argv = [a for a in _sys.argv if a != "--i-know-this-is-0-1-x"]
import re
import subprocess
import sys
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


def sync_footprints(b) -> list[str]:
    changed = []
    # FindFootprintByReference renvoie un SwigPyObject non typé (KiCad 10)
    # quand la référence est absente : on indexe GetFootprints().
    by_ref = {fp.GetReference(): fp for fp in b.GetFootprints()}
    for c in g.COMPONENTS:
        old = by_ref.get(c["ref"])
        if old and old.GetFPIDAsString().split(":")[-1] == c["fp"]:
            if old.GetValue() != c["value"]:
                old.SetValue(c["value"])
                changed.append(f"{c['ref']}={c['value']}")
            desc = g.described(c["ref"], c["desc"])
            if old.GetFieldText("Description") != desc:
                old.SetField("Description", desc)
                changed.append(f"{c['ref']}.desc")
            continue
        fp = pcbnew.FootprintLoad(str(g.FP_DIR), c["fp"])
        fp.SetFPID(pcbnew.LIB_ID("n3u", c["fp"]))
        fp.SetReference(c["ref"])
        fp.SetValue(c["value"])
        fp.SetField("Description", g.described(c["ref"], c["desc"]))
        b.Add(fp)
        x, y, rot = c["pcb"]
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(pcbnew.VECTOR2I(FM(x), FM(y)))
        fp.SetPath(pcbnew.KIID_PATH(f"/{g.uid('sym', c['ref'])}"))
        if not c["sym"]:
            fp.SetBoardOnly(True)
        for pad in fp.Pads():
            net = c["nets"].get(pad.GetNumber())
            if net:
                pad.SetNet(b.FindNet(net))
        if old:
            b.Remove(old)
        changed.append(f"{c['ref']}:{c['fp']}")
    return changed


# Pads de RV1 sur l'ancienne empreinte radiale 5,0 mm (rev 0.1.1).
RV1_OLD_PADS = {"MAINS_N": (253.0, 62.0), "MAINS_LF": (258.0, 62.0)}
# (départ, arrivée, net, largeur) — pistes de la zener D9 sous Q11, en F.Cu.
V012_TRACKS = [
    ((232.0, 146.0), (232.0, 150.5), "QP_G", 0.4),
    ((237.08, 146.0), (237.08, 150.5), "VBAT12_PROT", 0.5),
]


def route_v012(b) -> int:
    n = 0
    rv1 = {p.GetNetname(): p.GetPosition() for p in b.FindFootprintByReference("RV1").Pads()}
    for t in b.GetTracks():
        net = t.GetNetname()
        if t.GetClass() != "PCB_TRACK" or net not in RV1_OLD_PADS:
            continue
        ox, oy = RV1_OLD_PADS[net]
        for get, put in ((t.GetStart, t.SetStart), (t.GetEnd, t.SetEnd)):
            ex, ey = mm(get())
            if math.hypot(ex - ox, ey - oy) < 0.01:
                put(rv1[net])
                n += 1
    have = {(t.GetNetname(), mm(t.GetStart()), mm(t.GetEnd()))
            for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"}
    for (x0, y0), (x1, y1), net, w in V012_TRACKS:
        key = (net, (x0, y0), (x1, y1))
        if any(k[0] == net and math.dist(k[1], key[1]) < 0.01 and math.dist(k[2], key[2]) < 0.01
               for k in have):
            continue
        t = pcbnew.PCB_TRACK(b)
        t.SetLayer(pcbnew.F_Cu)
        t.SetStart(pcbnew.VECTOR2I(FM(x0), FM(y0)))
        t.SetEnd(pcbnew.VECTOR2I(FM(x1), FM(y1)))
        t.SetWidth(FM(w))
        t.SetNet(b.FindNet(net))
        b.Add(t)
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
    changed = sync_footprints(b)
    print("empreintes / valeurs  :", changed)
    if any(":" in c for c in changed):
        # Après FootprintLoad, les objets pcbnew de ce processus reviennent en
        # SwigPyObject non typés (KiCad 10) : on repart d'un processus neuf.
        pcbnew.SaveBoard(str(BOARD_PATH), b)
        subprocess.run([sys.executable, __file__], check=True)
        return
    print("pistes 0.1.2          :", route_v012(b))
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
