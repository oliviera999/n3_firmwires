#!/usr/bin/env python3
"""Routage de la carte n3-universal — pipeline reproductible (rev 0.3 : plus de 230 V).

Étapes :
 0. (option --seed) AMORCE : le cuivre d'une carte déjà validée (la 0.2.3) est repris
    sur la carte fraîchement régénérée — pistes et vias des nets qui existent encore.
    Ce qui heurte une pastille déplacée ou sort du nouveau contour est retiré (verdict
    du DRC KiCad), puis les bouts de piste laissés sans extrémité raccordée.
 1. Copie temporaire de la carte, zones GND retirées, bandes interdites le long des
    bords → export Specctra DSN.
 2. freerouting (headless, Xvfb) complète le routage ; il conserve l'amorce.
 3. Import de la session .ses, ponts des rangées doublées A1/A2.
 4. Remplissage des zones GND, vias de couture, pastilles GND mal reliées.
 5. Bilan DRC KiCad (erreurs, liaisons non routées).

Jusqu'à la 0.2.3 ce script traçait aussi en dur les pistes secteur (relais, Hi-Link) et
contrôlait leur écart au reste : ces étapes ont disparu avec le 230 V (rev 0.3).

Usage : python3 route_universal.py [--jar freerouting.jar] [--seed ancienne_carte.kicad_pcb]
Prérequis : kicad-cli + module python pcbnew (KiCad 8 à 10 ; kicad-cli localisé
par kicad_tools), java, xvfb-run (Linux). Sur la carte livrée, NE PAS relancer :
le routage est figé, les retouches passent par finalize_board.py.
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pcbnew

from kicad_tools import kicad_cli

HERE = Path(__file__).resolve().parent
KICAD = HERE.parent / "kicad"
BOARD_PATH = KICAD / "n3-universal.kicad_pcb"

FMM = pcbnew.FromMM
TMM = pcbnew.ToMM


def via_width_mm(v) -> float:
    """Diamètre d'un via : KiCad >= 9 exige une couche (padstack par couche),
    KiCad 8 n'en accepte pas."""
    try:
        return pcbnew.ToMM(v.GetWidth(pcbnew.F_Cu))
    except TypeError:
        return pcbnew.ToMM(v.GetWidth())


def set_via_width(v, width_iu) -> None:
    try:
        v.SetWidth(pcbnew.F_Cu, width_iu)
    except TypeError:
        v.SetWidth(width_iu)

# Amorces ponctuelles : pistes posées AVANT le routage (freerouting les préserve).
# Vide en rev 0.3 : l'amorce « EN » de la 0.2.3 (96,5 ; 111) -> (100 ; 111) restait sans
# extrémité raccordée une fois le net routé autrement (revue du 2026-11-18).
SEED_TRACKS: list[tuple] = []


def apply_seed_tracks():
    b = pcbnew.LoadBoard(str(BOARD_PATH))
    existing = {(t.GetNetname(), t.GetStart().x, t.GetStart().y)
                for t in b.GetTracks()}
    added = 0
    for entry in SEED_TRACKS:
        net_name, x0, y0, x1, y1 = entry[:5]
        layer = pcbnew.B_Cu if len(entry) > 5 and entry[5] == "B" else pcbnew.F_Cu
        if (net_name, FMM(x0), FMM(y0)) in existing:
            continue
        tr = pcbnew.PCB_TRACK(b)
        tr.SetStart(pcbnew.VECTOR2I(FMM(x0), FMM(y0)))
        tr.SetEnd(pcbnew.VECTOR2I(FMM(x1), FMM(y1)))
        tr.SetWidth(FMM(0.4))
        tr.SetLayer(layer)
        tr.SetNet(b.GetNetsByName()[net_name])
        b.Add(tr)
        added += 1
    if added:
        pcbnew.SaveBoard(str(BOARD_PATH), b)
    print(f"amorces : {added} posée(s)")


def mk_rule_area(board, x0, y0, x1, y1):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    # KiCad 10 : SetDoNotAllowCopperPour -> SetDoNotAllowZoneFills
    (getattr(z, 'SetDoNotAllowZoneFills', None) or z.SetDoNotAllowCopperPour)(True)
    z.SetLayerSet(pcbnew.LSET.AllCuMask(2))
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    chain = pcbnew.SHAPE_LINE_CHAIN()
    for px, py in pts:
        chain.Append(pcbnew.VECTOR2I(FMM(px), FMM(py)))
    chain.SetClosed(True)
    z.Outline().AddOutline(chain)
    board.Add(z)
    return z


# Rangées doublées des sites modules (rev 0.2.3) : empreinte -> x local de la
# rangée de SECOURS (mêmes numéros et nets que la rangée principale à 25,4 mm).
ALT_ROWS = {"ESP32_DevKitC_38pin": 22.86, "ESP32_S3_DevKitC_1_44pin": 22.86}


def rename_devkit_alt_pads(pcb_path: Path) -> int:
    """Sites A1/A2 : la rangée de secours (22,86 mm) duplique les numéros de la
    rangée principale (25,4 mm) et freerouting refuse les doublons. Dans la copie
    exportée en DSN, elle est renumérotée « nA » en GARDANT ses nets : le routeur
    la voit comme des pastilles du même net (cibles ou obstacles), au lieu de
    router des pistes d'autres nets par-dessus (0.2, premier essai : 26 courts-
    circuits sur la rangée doublée). Édition textuelle : modifier un pad par
    l'API laisse des objets SWIG non typés (KiCad 10). add_devkit_alt_tracks
    ponte ensuite les deux rangées."""
    import generate as g
    tree = g.sx_parse(pcb_path.read_text(encoding="utf-8"))
    n = 0
    for fp in g.sx_find_all(tree, g.Sym("footprint")):
        alt_x = next((x for name, x in ALT_ROWS.items() if str(fp[1]).endswith(name)), None)
        if alt_x is None:
            continue
        for item in fp:
            if isinstance(item, list) and item and item[0] == "pad":
                at = g.sx_find_all(item, g.Sym("at"))[0]
                if abs(float(at[1]) - alt_x) < 1e-6:
                    item[1] = f"{item[1]}A"
                    n += 1
    pcb_path.write_text(g.sx_dump(tree), encoding="utf-8")
    return n


def export_logic_dsn(dsn_path: Path):
    """Carte temporaire sans zones GND + bandes interdites aux bords → DSN pour freerouting."""
    import generate as g
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td) / BOARD_PATH.name
        shutil.copy(BOARD_PATH, tmp)
        rename_devkit_alt_pads(tmp)
        # le .kicad_pro doit suivre pour les netclasses
        shutil.copy(BOARD_PATH.with_suffix(".kicad_pro"),
                    tmp.with_suffix(".kicad_pro"))
        b = pcbnew.LoadBoard(str(tmp))
        # bandes le long des bords : l'autorouteur doit respecter
        # l'edge clearance de 0.5 mm (le DSN ne la transmet pas)
        bx0, by0, bx1, by1 = g.BOARD['x0'], g.BOARD['y0'], g.BOARD['x1'], g.BOARD['y1']
        mk_rule_area(b, bx0, by0, bx1, by0 + 0.7)
        mk_rule_area(b, bx0, by1 - 0.7, bx1, by1)
        mk_rule_area(b, bx0, by0, bx0 + 0.7, by1)
        mk_rule_area(b, bx1 - 0.7, by0, bx1, by1)
        # retire les zones GND de la copie : freerouting doit router GND en
        # pistes réelles (sinon il se repose sur un plan idéal et le
        # remplissage réel laisse des pads GND dans des poches isolées)
        for z in [zz for zz in b.Zones() if not zz.GetIsRuleArea()]:
            b.Delete(z)
        pcbnew.SaveBoard(str(tmp), b)
        ok = pcbnew.ExportSpecctraDSN(b, str(dsn_path))
        print(f"DSN : {ok}")
        return ok


def prepare_freerouting_settings() -> None:
    """freerouting 2.1 (InteractiveActionThread.autorouterFinished) ouvre une
    boîte de dialogue modale « profil utilisateur » après le 5e routage tant
    que profile.email est vide — sous xvfb personne ne la ferme et le .ses
    n'est jamais écrit (0.2, 2e essai : 25 min bloqué après « Optimization
    was completed »). Le mode sans GUI (gui.enabled=false) n'est pas une
    option : il ignore -mp (187 passes observées) et ne restaure pas la
    meilleure carte. On renseigne donc un e-mail factice et on coupe la
    télémétrie dans son freerouting.json (<tmp>/freerouting sous Linux,
    %TEMP%\\freerouting sous Windows)."""
    import json
    for cand in (Path(tempfile.gettempdir()) / "freerouting" / "freerouting.json",
                 Path.home() / ".freerouting" / "freerouting.json"):
        if cand.exists():
            try:
                cfg = json.loads(cand.read_text(encoding="utf-8"))
                prof = cfg.setdefault("profile", {})
                if not prof.get("email"):
                    prof["email"] = "noreply@n3-universal.invalid"
                prof["allow_telemetry"] = False
                prof["allow_contact"] = False
                cfg.setdefault("gui", {})["enabled"] = True
                cfg.setdefault("usage_and_diagnostic_data", {})["disable_analytics"] = True
                cand.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
                print(f"freerouting : profil/télémétrie réglés dans {cand}")
            except (OSError, ValueError) as e:
                print(f"freerouting : freerouting.json illisible ({e})")


def run_freerouting(jar: Path, dsn: Path, ses: Path, passes: int = 60):
    prepare_freerouting_settings()
    cmd = ["xvfb-run", "-a", "java", "-jar", str(jar), "-de", str(dsn),
           "-do", str(ses), "-mp", str(passes), "-mt", "1", "-dr"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=3300)
    tail = "\n".join(r.stdout.splitlines()[-3:])
    print(tail)
    if not ses.exists():
        sys.exit("freerouting n'a pas produit de .ses")


def add_devkit_alt_tracks(b) -> int:
    """Ponts rangée principale (25,4) -> secours (22,86) des sites A1 et A2 :
    une piste de 2,54 mm par numéro doublé (même net), en F.Cu, largeur 0,4.
    Idempotent (rev 0.2.3) : posé AVANT le routage — freerouting garde ces ponts
    et ne fait passer aucune piste étrangère dans l'espace de 0,84 mm entre les
    deux rangées — puis revérifié après l'import du .ses."""
    nets = b.GetNetsByName()
    existing = set()
    for t in b.GetTracks():
        if t.GetClass() == "PCB_TRACK":
            s_, e_ = t.GetStart(), t.GetEnd()
            existing.add(frozenset({(s_.x, s_.y), (e_.x, e_.y)}))
    n = 0
    for fp in b.GetFootprints():
        if not any(fp.GetFPIDAsString().endswith(name) for name in ALT_ROWS):
            continue
        by_num = {}
        for pad in fp.Pads():
            by_num.setdefault(pad.GetNumber(), []).append(pad)
        for num, pads in by_num.items():
            if len(pads) != 2:
                continue
            a, c = sorted(pads, key=lambda p: (p.GetPosition().x, p.GetPosition().y))
            net = a.GetNetname() or c.GetNetname()
            if not net or net.startswith("unconnected-"):   # GPIO0 / flash : rien à ponter
                continue
            pa, pc = a.GetPosition(), c.GetPosition()
            if frozenset({(pa.x, pa.y), (pc.x, pc.y)}) in existing:
                continue
            t = pcbnew.PCB_TRACK(b)
            t.SetStart(a.GetPosition())
            t.SetEnd(c.GetPosition())
            t.SetWidth(FMM(0.4))
            t.SetLayer(pcbnew.F_Cu)
            t.SetNet(nets[net])
            b.Add(t)
            c.SetNet(nets[net])
            n += 1
    return n


def _drc(board_path: Path, extra: tuple[str, ...] = ()) -> dict:
    """DRC KiCad au format JSON (toutes sévérités)."""
    rpt = Path(tempfile.mkdtemp()) / "drc.json"
    subprocess.run([kicad_cli(), "pcb", "drc", "--severity-all", "--format", "json", *extra,
                    "-o", str(rpt), str(board_path)], capture_output=True, text=True)
    return json.loads(rpt.read_text(encoding="utf-8"))


def remove_dangling(b) -> int:
    """Retire, jusqu'à stabilité, les pistes dont une extrémité n'est raccordée à rien
    (ni pastille, ni via, ni autre piste du même net sur la même couche) et les vias
    qui ne relient plus rien. Les vias GND libres partent aussi : add_stitching_vias
    reposera la grille de couture."""
    pads_by_net: dict[str, list] = {}
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            pads_by_net.setdefault(pad.GetNetname(), []).append(pad)
    total = 0
    while True:
        segs = [t for t in b.GetTracks() if t.GetClass() != "PCB_VIA"]
        vias = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
        by_net: dict[str, list] = {}
        for t in segs:
            by_net.setdefault(t.GetNetname(), []).append(t)
        vias_by_net: dict[str, list] = {}
        for v in vias:
            vias_by_net.setdefault(v.GetNetname(), []).append(v)

        def anchored(t, p) -> bool:
            net, layer = t.GetNetname(), t.GetLayer()
            for pad in pads_by_net.get(net, ()):
                if pad.IsOnLayer(layer) and pad.HitTest(p):
                    return True
            px, py = TMM(p.x), TMM(p.y)
            for v in vias_by_net.get(net, ()):
                q = v.GetPosition()
                if math.hypot(px - TMM(q.x), py - TMM(q.y)) <= via_width_mm(v) / 2:
                    return True
            for o in by_net.get(net, ()):
                if o is t or o.GetLayer() != layer:
                    continue
                s, e = o.GetStart(), o.GetEnd()
                if seg_dist((px, py, px, py), (TMM(s.x), TMM(s.y), TMM(e.x), TMM(e.y))) <= TMM(o.GetWidth()) / 2:
                    return True
            return False

        dead = [t for t in segs if not (anchored(t, t.GetStart()) and anchored(t, t.GetEnd()))]
        dead_ids = {id(t) for t in dead}
        for v in vias:
            net = v.GetNetname()
            q = v.GetPosition()
            qx, qy, r = TMM(q.x), TMM(q.y), via_width_mm(v) / 2
            layers = set()
            for o in by_net.get(net, ()):
                if id(o) in dead_ids:
                    continue
                for p in (o.GetStart(), o.GetEnd()):
                    if math.hypot(TMM(p.x) - qx, TMM(p.y) - qy) <= r:
                        layers.add(o.GetLayer())
            on_pad = any(pad.HitTest(q) for pad in pads_by_net.get(net, ()))
            if len(layers) < 2 and not (layers and on_pad):
                dead.append(v)
        if not dead:
            return total
        for t in dead:
            b.Delete(t)
        total += len(dead)


def seed_from_board(old_path: Path) -> None:
    """Amorce : reprend le cuivre d'une carte validée (méthode de la 0.2.2, README).

    Sont repris les pistes et vias dont le net existe encore sous le même nom. Le DRC
    KiCad désigne ensuite ce qui ne tient plus (pastille nouvelle ou déplacée, contour
    réduit, perçage) : ces éléments sont retirés, puis les tronçons devenus orphelins.
    """
    import generate as g
    b = pcbnew.LoadBoard(str(BOARD_PATH))
    if any(True for _ in b.GetTracks()):
        sys.exit("amorce : la carte porte déjà des pistes (régénérer avec generate.py --regen-pcb)")
    old = pcbnew.LoadBoard(str(old_path))
    x0, y0, x1, y1 = (g.BOARD[k] for k in ("x0", "y0", "x1", "y1"))

    def inside(p, margin):
        return x0 + margin <= TMM(p.x) <= x1 - margin and y0 + margin <= TMM(p.y) <= y1 - margin

    kept = lost_net = lost_edge = 0
    for t in old.GetTracks():
        ni = b.FindNet(t.GetNetname()) if t.GetNetname() else None
        if ni is None:
            lost_net += 1
            continue
        if t.GetClass() == "PCB_VIA":
            if not inside(t.GetPosition(), 0.5 + via_width_mm(t) / 2):
                lost_edge += 1
                continue
            v = pcbnew.PCB_VIA(b)
            v.SetPosition(t.GetPosition())
            set_via_width(v, FMM(via_width_mm(t)))
            v.SetDrill(t.GetDrill())
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(ni)
            b.Add(v)
        else:
            m = 0.5 + TMM(t.GetWidth()) / 2
            if not (inside(t.GetStart(), m) and inside(t.GetEnd(), m)):
                lost_edge += 1
                continue
            n = pcbnew.PCB_TRACK(b)
            n.SetStart(t.GetStart())
            n.SetEnd(t.GetEnd())
            n.SetWidth(t.GetWidth())
            n.SetLayer(t.GetLayer())
            n.SetNet(ni)
            b.Add(n)
        kept += 1
    # Changements de couche qui passaient par une pastille traversante disparue : une
    # piste F.Cu et une piste B.Cu du même net s'y rejoignaient (le tronc +5V de la 0.2.3
    # changeait ainsi de couche sur les pastilles des résistances de LED). Un via les
    # relie de nouveau, sinon tout le tronc part au tri des tronçons orphelins.
    new_pads = {(pad.GetPosition().x, pad.GetPosition().y) for fp in b.GetFootprints() for pad in fp.Pads()}
    ends: dict[tuple, set] = {}
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA":
            continue
        for q in (t.GetStart(), t.GetEnd()):
            ends.setdefault((t.GetNetname(), q.x, q.y), set()).add(t.GetLayer())
    wide = {"+5V", "GND", "+3V3_SW", "VIN_5V", "VIN_RAW", "LDO_IN", "VBAT12_IN", "VBAT12_PROT"}
    n_via = 0
    for fp in old.GetFootprints():
        for pad in fp.Pads():
            q, net = pad.GetPosition(), pad.GetNetname()
            if not net or pad.GetDrillSize().x == 0 or (q.x, q.y) in new_pads:
                continue
            if len(ends.get((net, q.x, q.y), ())) < 2 or not inside(q, 1.0):
                continue
            v = pcbnew.PCB_VIA(b)
            v.SetPosition(q)
            set_via_width(v, FMM(1.0 if net in wide else 0.7))
            v.SetDrill(FMM(0.5 if net in wide else 0.35))
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(b.FindNet(net))
            b.Add(v)
            new_pads.add((q.x, q.y))
            n_via += 1
    pcbnew.SaveBoard(str(BOARD_PATH), b)
    print(f"amorce : {kept} éléments repris, {lost_net} sur des nets disparus, {lost_edge} hors du nouveau contour ; "
          f"{n_via} vias à la place de pastilles disparues")
    # Raccords ponctuels AVANT le tri : ils sauvent les tronçons qu'ils prolongent. En
    # cas de conflit avec un reste de l'ancien cuivre, c'est l'ancien qui part.
    apply_seed_tracks()
    patch = {(n, FMM(xa), FMM(ya), FMM(xb), FMM(yb)) for n, xa, ya, xb, yb, *_ in SEED_TRACKS}

    def is_patch(t) -> bool:
        if t.GetClass() == "PCB_VIA":
            return False
        s_, e_ = t.GetStart(), t.GetEnd()
        return (t.GetNetname(), s_.x, s_.y, e_.x, e_.y) in patch

    for it in range(8):
        rep = _drc(BOARD_PATH)
        bad = {i.get("uuid") for v in rep.get("violations", []) if v.get("severity") == "error"
               for i in v.get("items", [])}
        b = pcbnew.LoadBoard(str(BOARD_PATH))
        victims = [t for t in b.GetTracks() if t.m_Uuid.AsString() in bad and not is_patch(t)]
        if it == 0:   # qui gêne ? (aide au placement : nombre de conflits par composant)
            who: dict[str, int] = {}
            for v in rep.get("violations", []):
                if v.get("severity") != "error":
                    continue
                for i in v.get("items", []):
                    d = i.get("description", "")
                    if " of " in d and "pad" in d:
                        who[d.rsplit(" of ", 1)[1]] = who.get(d.rsplit(" of ", 1)[1], 0) + 1
            print("amorce : conflits par composant :", ", ".join(f"{k} {n}" for k, n in sorted(who.items())))
        if not victims:
            if bad:
                print(f"amorce : {len(bad)} élément(s) encore en conflit, à traiter à la main")
            break
        for t in victims:
            b.Delete(t)
        pcbnew.SaveBoard(str(BOARD_PATH), b)
        print(f"amorce : {len(victims)} éléments en conflit retirés (passe {it + 1})")
    # Une piste coupée par une pastille nouvelle reste en place des deux côtés de la
    # coupure : l'autorouteur n'a plus qu'à refaire le raccord (quelques millimètres) au
    # lieu de retracer toute la liaison. Seuls partent ici les îlots qui ne touchent plus
    # aucune pastille ; les bouts morts sont retirés APRES le routage (prune_dangling_drc).
    b = pcbnew.LoadBoard(str(BOARD_PATH))
    n = remove_floating(b)
    pcbnew.SaveBoard(str(BOARD_PATH), b)
    left = sum(1 for _ in b.GetTracks())
    print(f"amorce : {n} éléments en îlots sans pastille retirés ; {left} éléments de cuivre conservés")


def remove_floating(b) -> int:
    """Retire les pistes et vias qui ne sont reliés, de proche en proche, à aucune pastille
    de leur net (freerouting chercherait à raccorder ces îlots comme s'ils étaient utiles)."""
    items = list(b.GetTracks())
    parent = list(range(len(items)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def geom(t):
        if t.GetClass() == "PCB_VIA":
            q = t.GetPosition()
            return (TMM(q.x), TMM(q.y), TMM(q.x), TMM(q.y), via_width_mm(t) / 2, None)
        s_, e_ = t.GetStart(), t.GetEnd()
        return (TMM(s_.x), TMM(s_.y), TMM(e_.x), TMM(e_.y), TMM(t.GetWidth()) / 2, t.GetLayer())

    by_net: dict[str, list[int]] = {}
    g_ = [geom(t) for t in items]
    for i, t in enumerate(items):
        by_net.setdefault(t.GetNetname(), []).append(i)
    for idx in by_net.values():
        for a_i, i in enumerate(idx):
            xi0, yi0, xi1, yi1, ri, li = g_[i]
            for j in idx[a_i + 1:]:
                xj0, yj0, xj1, yj1, rj, lj = g_[j]
                if li is not None and lj is not None and li != lj:
                    continue
                if (min(xi0, xi1) - ri > max(xj0, xj1) + rj or max(xi0, xi1) + ri < min(xj0, xj1) - rj or
                        min(yi0, yi1) - ri > max(yj0, yj1) + rj or max(yi0, yi1) + ri < min(yj0, yj1) - rj):
                    continue
                if seg_dist((xi0, yi0, xi1, yi1), (xj0, yj0, xj1, yj1)) <= max(ri, rj):
                    parent[find(i)] = find(j)
    rooted = set()
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            for i in by_net.get(pad.GetNetname(), ()):
                t = items[i]
                if t.GetClass() == "PCB_VIA":
                    if pad.HitTest(t.GetPosition()):
                        rooted.add(find(i))
                elif pad.IsOnLayer(t.GetLayer()) and (pad.HitTest(t.GetStart()) or pad.HitTest(t.GetEnd())):
                    rooted.add(find(i))
    dead = [t for i, t in enumerate(items) if find(i) not in rooted]
    for t in dead:
        b.Delete(t)
    return len(dead)


def prune_dangling_drc(max_iters: int = 80) -> int:
    """Second tri, au verdict de KiCad : remove_dangling() tient pour raccordé un bout
    de piste court dont l'extrémité libre tombe dans la largeur du tronçon voisin. Le DRC
    (track_dangling / via_dangling) ne s'y trompe pas ; on retire ce qu'il désigne,
    jusqu'à stabilité (une chaîne morte se défait d'un tronçon par passe)."""
    total = 0
    for _ in range(max_iters):
        rep = _drc(BOARD_PATH)
        bad = {i.get("uuid") for v in rep.get("violations", [])
               if v.get("type") in ("track_dangling", "via_dangling") for i in v.get("items", [])}
        if not bad:
            break
        b = pcbnew.LoadBoard(str(BOARD_PATH))
        victims = [t for t in b.GetTracks() if t.m_Uuid.AsString() in bad]
        if not victims:
            break
        for t in victims:
            b.Delete(t)
        total += len(victims) + remove_dangling(b)
        pcbnew.SaveBoard(str(BOARD_PATH), b)
    return total



def copper_items(b):
    """(net, [(x0,y0,x1,y1,demi-largeur en mm)]) pour pistes ; pads en pseudo-segments."""
    items = []
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA":
            p = t.GetPosition()
            r = via_width_mm(t) / 2
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
    # Zones remplies (plans GND, etc.) : chaque arete des polygones remplis
    # devient un pseudo-segment de demi-largeur nulle. Sans cela le controle
    # etait aveugle au cuivre coule (trou decouvert a l'audit final rev 0.1 :
    # le plan GND penetrait le coin PSU sans etre vu).
    return items + zone_edges(b)


def zone_edges(b):
    items = []
    for z in b.Zones():
        if z.GetIsRuleArea() or not z.GetNetname():
            continue
        for lyr in z.GetLayerSet().CuStack():
            polys = z.GetFilledPolysList(lyr)
            for i in range(polys.OutlineCount()):
                outl = polys.Outline(i)
                n = outl.PointCount()
                for j in range(n):
                    p0 = outl.CPoint(j)
                    p1 = outl.CPoint((j + 1) % n)
                    items.append((z.GetNetname(),
                                  pcbnew.ToMM(p0.x), pcbnew.ToMM(p0.y),
                                  pcbnew.ToMM(p1.x), pcbnew.ToMM(p1.y), 0.0))
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


def add_stitching_vias(b):
    """Grille de vias GND : lie les deux plans de masse et résorbe les îlots
    créés par les faisceaux de pistes. Placement seulement là où c'est légal
    (écart aux autres nets)."""
    items = [(n, x0, y0, x1, y1, r) for (n, x0, y0, x1, y1, r)
             in copper_items(b) if n != "GND"]
    # Pads GND exclus ci-dessus (même net) mais leurs PERÇAGES restent
    # interdits : un via de couture était tombé dans la fente plaquée de J2
    # (48, 110) — « holes overlap » à la DFM JLCPCB (audit GERB-01).
    items += [("GND", pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y),
               pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y),
               max(pcbnew.ToMM(p.GetSize().x), pcbnew.ToMM(p.GetSize().y)) / 2)
              for fp in b.GetFootprints() for p in fp.Pads()
              if p.GetNetname() == "GND" and p.GetDrillSize().x > 0]
    nets = b.GetNetsByName()
    slots_margin = []
    import generate as g
    for sx0, sy0, sx1, sy1 in g.SLOTS:
        slots_margin.append((sx0 - 1.2, sy0 - 1.2, sx1 + 1.2, sy1 + 1.2))
    spots = [(x, y) for x in range(48, 243, 12) for y in range(90, 172, 10)]
    spots += [(x, y) for x in range(250, 316, 12) for y in range(134, 172, 10)]
    # rev 0.3 : bande de commande (y 86) et coin haut droit, libéré par le bloc secteur
    spots += [(x, y) for x in range(54, 316, 12) for y in (86,)]
    spots += [(x, y) for x in (252, 264, 276, 300) for y in (88, 96)]
    # jamais dans les zones antenne (A1 et A2) ni sous les modules (rev 0.2.3 —
    # A1 DevKitC 38 : x97,6..127,8 y103,5..160,5 ; A2 couché : x248,5..319,5 y101,9..131,1)
    keepouts = [(95.6, 100.5, 129.8, 161.5), (246.5, 105.5, 265, 127.5),
                (246, 100.9, 320, 131.6)]
    spots = [(x, y) for x, y in spots
             if not any(kx0 <= x <= kx1 and ky0 <= y <= ky1
                        for kx0, ky0, kx1, ky1 in keepouts)]
    added = 0
    # pas de via sous le corps d'un composant (0.2 : un via de la grille tombait
    # sous le repère de R28, que tidy_silkscreen ne pouvait plus placer)
    bodies = []
    for fp in b.GetFootprints():
        bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        if bb.GetWidth():
            bodies.append((TMM(bb.GetLeft()) - 0.6, TMM(bb.GetTop()) - 0.6,
                           TMM(bb.GetRight()) + 0.6, TMM(bb.GetBottom()) + 0.6))
    # ni sous une étiquette de sérigraphie (PCB_TEXTS) : un via sous un texte
    # le rend illisible et bloque tidy_silkscreen
    for d in b.GetDrawings():
        if d.GetClass() == "PCB_TEXT" and "Silk" in b.GetLayerName(d.GetLayer()):
            bb = d.GetBoundingBox()
            bodies.append((TMM(bb.GetLeft()) - 0.6, TMM(bb.GetTop()) - 0.6,
                           TMM(bb.GetRight()) + 0.6, TMM(bb.GetBottom()) + 0.6))
    for x, y in spots:
        if any(sx0 <= x <= sx1 and sy0 <= y <= sy1 for sx0, sy0, sx1, sy1 in slots_margin):
            continue
        if any(bx0 <= x <= bx1 and by0 <= y <= by1 for bx0, by0, bx1, by1 in bodies):
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
        set_via_width(v, FMM(0.7))
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
        subprocess.run([kicad_cli(), "pcb", "drc", "--severity-error",
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
                        b.Delete(t)
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
                    (getattr(pad, 'SetLocalZoneConnection', None) or pad.SetZoneConnection)(pcbnew.ZONE_CONNECTION_FULL)
                    n += 1
        pcbnew.ZONE_FILLER(b).Fill(b.Zones())
        pcbnew.SaveBoard(str(BOARD_PATH), b)
        print(f"thermals: {n} pads GND passés en connexion pleine (itération {it})")
    print("thermals: itérations épuisées — vérifier le DRC")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jar", default=str(Path.home() / "freerouting.jar"),
                    help="chemin du jar freerouting")
    ap.add_argument("--seed", help="carte déjà routée dont le cuivre sert d'amorce "
                                   "(la carte courante doit sortir de generate.py --regen-pcb)")
    ap.add_argument("--passes", type=int, default=60, help="passes d'autoroutage (défaut 60)")
    ap.add_argument("--ses", help="réimporter un .ses déjà calculé (freerouting lancé à part, "
                                  "p. ex. plusieurs instances en parallèle, on garde la meilleure) "
                                  "au lieu de relancer le routeur")
    ap.add_argument("--dsn-only", help="exporter le DSN à ce chemin et s'arrêter")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp())
    dsn, ses = work / "logic.dsn", work / "logic.ses"

    # generate.py écrit un fichier au format KiCad 8 avec des commentaires : un passage
    # par pcbnew le normalise (sinon rename_devkit_alt_pads rend un fichier illisible).
    b0 = pcbnew.LoadBoard(str(BOARD_PATH))
    pcbnew.SaveBoard(str(BOARD_PATH), b0)
    if args.seed:
        seed_from_board(Path(args.seed))
    apply_seed_tracks()
    b0 = pcbnew.LoadBoard(str(BOARD_PATH))
    n_alt = add_devkit_alt_tracks(b0)
    if n_alt:
        pcbnew.SaveBoard(str(BOARD_PATH), b0)
    print("ponts rangées doublées A1/A2 posés avant routage :", n_alt)
    if args.dsn_only:
        if not export_logic_dsn(Path(args.dsn_only)):
            sys.exit("échec export DSN")
        print("DSN écrit :", args.dsn_only)
        return
    if args.ses:
        ses = Path(args.ses)
        if not ses.exists():
            sys.exit(f"{ses} introuvable")
        print("SES fourni :", ses)
    else:
        if not export_logic_dsn(dsn):
            sys.exit("échec export DSN")
        run_freerouting(Path(args.jar), dsn, ses, args.passes)

    b = pcbnew.LoadBoard(str(BOARD_PATH))
    ok = pcbnew.ImportSpecctraSES(b, str(ses))
    print("SES importé :", ok, "-", len(b.GetTracks()), "segments")
    print("ponts DevKit A/A' :", add_devkit_alt_tracks(b))
    # bouts morts de l'amorce (pistes qui allaient vers une pastille disparue). On
    # recharge d'abord la carte : après ImportSpecctraSES, l'API renvoie des objets SWIG
    # non typés (KiCad 10).
    pcbnew.SaveBoard(str(BOARD_PATH), b)
    b = pcbnew.LoadBoard(str(BOARD_PATH))
    n_dead = remove_dangling(b)
    pcbnew.SaveBoard(str(BOARD_PATH), b)
    n_dead += prune_dangling_drc()
    print("bouts de piste sans extrémité raccordée retirés :", n_dead)
    b = pcbnew.LoadBoard(str(BOARD_PATH))
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

    rep = _drc(BOARD_PATH)
    errors = [v for v in rep.get("violations", []) if v.get("severity") == "error"]
    unconnected = rep.get("unconnected_items", [])
    print(f"--- DRC : {len(errors)} erreur(s), {len(unconnected)} liaison(s) non routée(s) ---")
    for v in errors[:20]:
        print("  ", v.get("type"), "-", v.get("description", "")[:100])
    for v in unconnected[:20]:
        print("   non routé :", " / ".join(i.get("description", "")[:60] for i in v.get("items", [])))
    sys.exit(1 if errors or unconnected else 0)


if __name__ == "__main__":
    main()
