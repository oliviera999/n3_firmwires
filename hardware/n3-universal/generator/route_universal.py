#!/usr/bin/env python3
"""Routage de la variante 230 V — pipeline reproductible.

Étapes :
 1. Copie temporaire de la carte : nets secteur (REL*_COM/NO/NC) retirés des pads
    + zones interdites (rule areas) posées sur la zone secteur → export Specctra DSN.
    L'autorouteur ne voit donc NI les nets secteur, NI la zone secteur.
 2. freerouting (headless, Xvfb) route uniquement la logique.
 3. Import de la session .ses dans la carte réelle (qui garde ses nets secteur).
 4. Routage SECTEUR déterministe, tracé en dur (2,5 mm, F.Cu) : lignes droites
    bornier <-> contacts, COM passant entre les broches bobine (>=3 mm partout).
 5. Remplissage des zones GND (limitées géométriquement hors zone secteur).
 6. Vérification : DRC KiCad (avec la règle .kicad_dru mains<->logique 3 mm)
    + contrôle géométrique indépendant en Python (distance min cuivre secteur
    <-> cuivre logique >= 3 mm).

Usage : python3 route_230v.py [--jar /chemin/freerouting.jar]
Prérequis : kicad-cli + module python pcbnew (KiCad 8), java, xvfb-run.
"""

from __future__ import annotations

import argparse
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pcbnew

HERE = Path(__file__).resolve().parent
KICAD = HERE.parent / "kicad"
BOARD_PATH = KICAD / "n3-universal.kicad_pcb"

CHANNELS = [(1, 58.0), (2, 92.0), (3, 126.0), (4, 160.0), (5, 194.0), (6, 228.0)]  # (n, x centre relais)
MAINS_NETS = ({f"REL{n}_{c}" for n in range(1, 7) for c in ("COM", "NO", "NC")}
              | {"MAINS_L", "MAINS_LF", "MAINS_N"})
# nets pré-routés à la main (retirés du DSN comme les nets secteur)
HAND_NETS = MAINS_NETS
MAINS_WIDTH_MM = 2.5
MIN_GAP_MM = 3.0

FMM = pcbnew.FromMM


def mk_rule_area(board, x0, y0, x1, y1):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowCopperPour(True)
    z.SetLayerSet(pcbnew.LSET.AllCuMask(2))
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    chain = pcbnew.SHAPE_LINE_CHAIN()
    for px, py in pts:
        chain.Append(pcbnew.VECTOR2I(FMM(px), FMM(py)))
    chain.SetClosed(True)
    z.Outline().AddOutline(chain)
    board.Add(z)
    return z


def export_logic_dsn(dsn_path: Path):
    """Carte temporaire sans nets secteur + keepouts → DSN pour freerouting."""
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td) / BOARD_PATH.name
        shutil.copy(BOARD_PATH, tmp)
        # le .kicad_pro doit suivre pour les netclasses
        shutil.copy(BOARD_PATH.with_suffix(".kicad_pro"),
                    tmp.with_suffix(".kicad_pro"))
        b = pcbnew.LoadBoard(str(tmp))
        n_removed = 0
        for fp in b.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() in HAND_NETS:
                    pad.SetNetCode(0)
                    n_removed += 1
        # bande secteur + boîtes autour du pad/piste COM de chaque canal
        mk_rule_area(b, 44, 40, 246, 74)
        for _n, x in CHANNELS:
            mk_rule_area(b, x - 4.6, 74, x + 4.6, 84)
        # bandes le long des bords : l'autorouteur doit respecter
        # l'edge clearance de 0.5 mm (le DSN ne la transmet pas)
        bx0, by0, bx1, by1 = 40, 40, 318, 160
        mk_rule_area(b, bx0, by0, bx1, by0 + 0.7)
        mk_rule_area(b, bx0, by1 - 0.7, bx1, by1)
        mk_rule_area(b, bx0, by0, bx0 + 0.7, by1)
        mk_rule_area(b, bx1 - 0.7, by0, bx1, by1)
        # zones SECTEUR interdites a l'autorouteur (bande contacts relais +
        # coin PSU Hi-Link) : les pistes secteur sont tracees en dur ensuite.
        mk_rule_area(b, 40, 40, 247, 70)
        mk_rule_area(b, 247, 40, 318, 73)
        # keepouts sur les fentes d'isolement (le DSN n'exporte pas les
        # découpes internes : sans ça l'autorouteur les traverse/frôle)
        import generate as g
        for sx0, sy0, sx1, sy1 in g.SLOTS:
            mk_rule_area(b, sx0 - 0.5, sy0 - 0.5, sx1 + 0.5, sy1 + 0.5)
        # retire les zones GND de la copie : freerouting doit router GND en
        # pistes réelles (sinon il se repose sur un plan idéal et le
        # remplissage réel laisse des pads GND dans des poches isolées)
        for z in [zz for zz in b.Zones() if not zz.GetIsRuleArea()]:
            b.Delete(z)
        pcbnew.SaveBoard(str(tmp), b)
        ok = pcbnew.ExportSpecctraDSN(b, str(dsn_path))
        print(f"DSN logique : {ok} ({n_removed} pads secteur masqués)")
        return ok


def run_freerouting(jar: Path, dsn: Path, ses: Path):
    cmd = ["xvfb-run", "-a", "java", "-jar", str(jar), "-de", str(dsn),
           "-do", str(ses), "-mp", "24", "-dr"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=3300)
    tail = "\n".join(r.stdout.splitlines()[-3:])
    print(tail)
    if not ses.exists():
        sys.exit("freerouting n'a pas produit de .ses")


def add_mains_tracks(b):
    """Pistes secteur rectilignes. Bornier (pads y=50) : 1=NC 2=COM 3=NO.
    Relais rot 90 en (x, 78) : COM (x,78), NO (x+6.05,63.85), NC (x-6,63.8)."""
    nets = b.GetNetsByName()

    def seg(x0, y0, x1, y1, netname):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(pcbnew.VECTOR2I(FMM(x0), FMM(y0)))
        t.SetEnd(pcbnew.VECTOR2I(FMM(x1), FMM(y1)))
        t.SetWidth(FMM(MAINS_WIDTH_MM))
        t.SetLayer(pcbnew.F_Cu)
        t.SetNet(nets[netname])
        b.Add(t)

    for n, x in CHANNELS:
        # COM : tout droit entre les broches bobine puis entre NO/NC
        seg(x, 78, x, 50, f"REL{n}_COM")
        # NO : contact haut-droit -> borne 3 (x+5.08)
        seg(x + 6.05, 63.85, x + 6.05, 58, f"REL{n}_NO")
        seg(x + 6.05, 58, x + 5.08, 54, f"REL{n}_NO")
        seg(x + 5.08, 54, x + 5.08, 50, f"REL{n}_NO")
        # NC : contact haut-gauche -> borne 1 (x-5.08)
        seg(x - 6.0, 63.8, x - 6.0, 58, f"REL{n}_NC")
        seg(x - 6.0, 58, x - 5.08, 54, f"REL{n}_NC")
        seg(x - 5.08, 54, x - 5.08, 50, f"REL{n}_NC")
    # Bloc PSU secteur : J27 (250/255.08,46) -> F1 (262,44)/(262,66.5)
    # -> RV1 (270,50)/(270,55) -> HLK PS1 (300,46)=L / (291,46)=N.
    psu = [
        ("MAINS_L", 250, 46, 250, 42.5), ("MAINS_L", 250, 42.5, 266, 42.5),
        ("MAINS_L", 266, 42.5, 266, 44),
        ("MAINS_N", 255.08, 46, 255.08, 58), ("MAINS_N", 255.08, 58, 253, 60),
        ("MAINS_N", 253, 60, 253, 62),
        ("MAINS_N", 255.08, 50, 290, 50), ("MAINS_N", 290, 50, 294, 46),
        ("MAINS_LF", 266, 66.5, 260, 66.5), ("MAINS_LF", 260, 66.5, 258, 64),
        ("MAINS_LF", 258, 64, 258, 62),
        ("MAINS_LF", 266, 66.5, 270, 66.5), ("MAINS_LF", 270, 66.5, 270, 54),
        ("MAINS_LF", 270, 54, 299, 54), ("MAINS_LF", 299, 54, 303, 50),
        ("MAINS_LF", 303, 50, 303, 46),
    ]
    ]
    for netname, x0, y0, x1, y1 in psu:
        seg(x0, y0, x1, y1, netname)
    print("pistes secteur ajoutées :", 7 * len(CHANNELS) + len(psu))



def copper_items(b):
    """(net, [(x0,y0,x1,y1,demi-largeur en mm)]) pour pistes ; pads en pseudo-segments."""
    items = []
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA":
            p = t.GetPosition()
            r = pcbnew.ToMM(t.GetWidth()) / 2
            items.append((t.GetNetname(),
                          pcbnew.ToMM(p.x), pcbnew.ToMM(p.y),
                          pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), r))
        else:
            s, e = t.GetStart(), t.GetEnd()
            items.append((t.GetNetname(),
                          pcbnew.ToMM(s.x), pcbnew.ToMM(s.y),
                          pcbnew.ToMM(e.x), pcbnew.ToMM(e.y),
                          pcbnew.ToMM(t.GetWidth()) / 2))
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if not pad.GetNetname():
                continue
            p = pad.GetPosition()
            sz = pad.GetSize()
            r = max(pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y)) / 2
            items.append((pad.GetNetname(),
                          pcbnew.ToMM(p.x), pcbnew.ToMM(p.y),
                          pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), r))
    return items


def seg_dist(a, b_):
    """Distance min entre 2 segments 2D."""
    ax0, ay0, ax1, ay1 = a
    bx0, by0, bx1, by1 = b_

    def pt_seg(px, py, x0, y0, x1, y1):
        dx, dy = x1 - x0, y1 - y0
        l2 = dx * dx + dy * dy
        if l2 == 0:
            return math.hypot(px - x0, py - y0)
        t = max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / l2))
        return math.hypot(px - (x0 + t * dx), py - (y0 + t * dy))

    def inter(p, q, r, s):
        d = (q[0] - p[0]) * (s[1] - r[1]) - (q[1] - p[1]) * (s[0] - r[0])
        if d == 0:
            return False
        t = ((r[0] - p[0]) * (s[1] - r[1]) - (r[1] - p[1]) * (s[0] - r[0])) / d
        u = ((r[0] - p[0]) * (q[1] - p[1]) - (r[1] - p[1]) * (q[0] - p[0])) / d
        return 0 <= t <= 1 and 0 <= u <= 1

    if inter((ax0, ay0), (ax1, ay1), (bx0, by0), (bx1, by1)):
        return 0.0
    return min(pt_seg(ax0, ay0, *b_), pt_seg(ax1, ay1, *b_),
               pt_seg(bx0, by0, *a), pt_seg(bx1, by1, *a))


def check_mains_gap(b) -> int:
    items = copper_items(b)
    mains = [i for i in items if i[0] in MAINS_NETS]
    logic = [i for i in items if i[0] not in MAINS_NETS]
    worst = []
    for mn, mx0, my0, mx1, my1, mr in mains:
        for ln, lx0, ly0, lx1, ly1, lr in logic:
            # filtre grossier
            if (min(lx0, lx1) - lr > max(mx0, mx1) + mr + MIN_GAP_MM or
                    max(lx0, lx1) + lr < min(mx0, mx1) - mr - MIN_GAP_MM or
                    min(ly0, ly1) - lr > max(my0, my1) + mr + MIN_GAP_MM or
                    max(ly0, ly1) + lr < min(my0, my1) - mr - MIN_GAP_MM):
                continue
            d = seg_dist((mx0, my0, mx1, my1), (lx0, ly0, lx1, ly1)) - mr - lr
            if d < MIN_GAP_MM:
                worst.append((round(d, 2), mn, ln,
                              round((mx0 + mx1) / 2, 1), round((my0 + my1) / 2, 1)))
    worst.sort()
    for d, mn, ln, x, y in worst[:20]:
        print(f"  ECART {d} mm < {MIN_GAP_MM} : {mn} <-> {ln} vers ({x},{y})")
    return len(worst)


def add_stitching_vias(b):
    """Grille de vias GND : lie les deux plans de masse et résorbe les îlots
    créés par les faisceaux de pistes. Placement seulement là où c'est légal
    (écart aux autres nets), jamais dans la zone secteur."""
    items = [(n, x0, y0, x1, y1, r) for (n, x0, y0, x1, y1, r)
             in copper_items(b) if n != "GND"]
    nets = b.GetNetsByName()
    slots_margin = []
    import generate as g
    for sx0, sy0, sx1, sy1 in g.SLOTS:
        slots_margin.append((sx0 - 1.2, sy0 - 1.2, sx1 + 1.2, sy1 + 1.2))
    spots = [(x, y) for x in range(48, 243, 12) for y in range(90, 158, 10)]
    spots += [(x, y) for x in (282, 294, 306) for y in range(106, 158, 10)]
    # jamais dans les zones antenne (A1 et A2) ni dans le coin PSU secteur
    keepouts = [(95.6, 100.5, 129.8, 110.5), (245.2, 73.5, 277.7, 85.5),
                (245, 40, 318, 104)]
    spots = [(x, y) for x, y in spots
             if not any(kx0 <= x <= kx1 and ky0 <= y <= ky1
                        for kx0, ky0, kx1, ky1 in keepouts)]
    added = 0
    for x, y in spots:
        if any(sx0 <= x <= sx1 and sy0 <= y <= sy1 for sx0, sy0, sx1, sy1 in slots_margin):
            continue
        ok = True
        for _n, x0, y0, x1, y1, r in items:
            if seg_dist((x, y, x, y), (x0, y0, x1, y1)) - r - 0.35 < 0.55:
                ok = False
                break
        if not ok:
            continue
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(pcbnew.VECTOR2I(FMM(float(x)), FMM(float(y))))
        v.SetWidth(FMM(0.7))
        v.SetDrill(FMM(0.35))
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNet(nets["GND"])
        b.Add(v)
        added += 1
    print("vias de couture GND :", added)


def fix_starved_thermals(max_iters=3):
    """Pads GND signalés starved/isolés par le DRC -> connexion pleine à la zone."""
    import re as _re
    rpt = Path(tempfile.mkdtemp()) / "drc.rpt"
    for it in range(max_iters):
        subprocess.run(["kicad-cli", "pcb", "drc", "--severity-error",
                        "-o", str(rpt), str(BOARD_PATH)],
                       capture_output=True, text=True)
        txt = rpt.read_text()
        pads = set(_re.findall(
            r"PTH pad (\d+) \[[^]]*\] of (\w+)\n(?=.*)", ""))
        flagged = set()
        for m in _re.finditer(r"\[(?:starved_thermal|unconnected_items)\][^@]*"
                              r"(?:@\([^)]*\): PTH pad (\d+) \[GND\] of (\w+)[^@]*)+", txt):
            pass
        for m in _re.finditer(r"PTH pad (\d+) \[GND\] of (\w+)", txt):
            flagged.add((m.group(2), m.group(1)))
        # vias de couture orphelins (poche isolée) -> suppression
        via_xy = set()
        for m in _re.finditer(r"@\(([\d.]+) mm, ([\d.]+) mm\): Via \[GND\]", txt):
            via_xy.add((float(m.group(1)), float(m.group(2))))
        if via_xy:
            b = pcbnew.LoadBoard(str(BOARD_PATH))
            killed = 0
            for t in list(b.GetTracks()):
                if t.GetClass() == "PCB_VIA" and t.GetNetname() == "GND":
                    pos = t.GetPosition()
                    xy = (round(pcbnew.ToMM(pos.x), 4), round(pcbnew.ToMM(pos.y), 4))
                    if any(abs(xy[0] - vx) < 0.01 and abs(xy[1] - vy) < 0.01
                           for vx, vy in via_xy):
                        b.Remove(t)
                        killed += 1
            pcbnew.ZONE_FILLER(b).Fill(b.Zones())
            pcbnew.SaveBoard(str(BOARD_PATH), b)
            print(f"thermals: {killed} via(s) de couture orphelin(s) retiré(s)")
            continue
        if not flagged:
            print(f"thermals: rien à corriger (itération {it})")
            return
        b = pcbnew.LoadBoard(str(BOARD_PATH))
        n = 0
        for fp in b.GetFootprints():
            for pad in fp.Pads():
                if (fp.GetReference(), str(pad.GetNumber())) in flagged:
                    pad.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
                    n += 1
        pcbnew.ZONE_FILLER(b).Fill(b.Zones())
        pcbnew.SaveBoard(str(BOARD_PATH), b)
        print(f"thermals: {n} pads GND passés en connexion pleine (itération {it})")
    print("thermals: itérations épuisées — vérifier le DRC")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jar", default="/tmp/claude-0/-home-user/81366818-0d88-5ef1-967f-3eda81659c1a/scratchpad/fr.jar",
                    help="chemin du jar freerouting")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp())
    dsn, ses = work / "logic.dsn", work / "logic.ses"

    if not export_logic_dsn(dsn):
        sys.exit("échec export DSN")
    run_freerouting(Path(args.jar), dsn, ses)

    b = pcbnew.LoadBoard(str(BOARD_PATH))
    ok = pcbnew.ImportSpecctraSES(b, str(ses))
    print("SES importé :", ok, "-", len(b.GetTracks()), "segments logiques")
    add_mains_tracks(b)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(str(BOARD_PATH), b)

    b = pcbnew.LoadBoard(str(BOARD_PATH))
    add_stitching_vias(b)
    for z in b.Zones():
        if not z.GetIsRuleArea():
            # supprime les îlots de cuivre non connectés (sinon flaggés par le DRC)
            z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(str(BOARD_PATH), b)

    fix_starved_thermals()

    print("--- contrôle indépendant écart secteur/logique ---")
    b2 = pcbnew.LoadBoard(str(BOARD_PATH))
    bad = check_mains_gap(b2)
    print("violations 3 mm :", bad)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
