#!/usr/bin/env python3
"""Contrôle géométrique « corps 3D » de la carte n3-universal — audit `GEN-08`.

Le garde-fou historique (`check_pcb_overlaps` de `generator/generate.py`) ne
comparait que la bounding-box des **pads** + 2 mm : il ignorait le corps réel
des composants et le volume nécessaire à l'enfichage des connecteurs. C'est
ainsi que le jack **J2**, dont l'ouverture pointait vers l'intérieur de la
carte, est passé au travers de la génération ET de la revue
(cf. `AUDIT-2026-08-28.md`, `GEN-02` / `GEN-08`).

Ce contrôle lit le **PCB routé** — donc la carte réellement envoyée au
fabricant, et pas seulement la table `COMPONENTS` du générateur — et vérifie :

1. **Corps 3D (courtyards)** — le contour `F.CrtYd` / `B.CrtYd` d'une empreinte
   est la projection normalisée du boîtier (IPC-7351 « courtyard ») : deux
   courtyards qui se recouvrent, ce sont deux composants qui se gênent
   physiquement. Les paires volontairement exclusives (peuplement XOR) sont
   déclarées dans `EXCLUSIVE`, avec leur justification.
2. **Couloirs d'insertion** — un connecteur à enfichage rigide a besoin d'un
   volume LIBRE devant son ouverture, jusqu'au bord de la carte. `MATING`
   décrit ce couloir dans le repère local de l'empreinte ; le contrôle le
   transporte à la position et à la rotation réelles, puis vérifie qu'il ne
   heurte aucun corps et qu'il sort bien du contour de carte.

Deux niveaux : `severity="erreur"` (enfichage rigide — un jack ne se plie pas)
fait échouer le contrôle ; `severity="avis"` (fil souple sur bornier à vis)
n'est qu'un signalement d'ergonomie d'assemblage.

Usage :

    python3 tools/check_pcb_clearance.py [carte.kicad_pcb]   # contrôle
    python3 tools/check_pcb_clearance.py --selftest           # non-régression

Code de retour : 0 si la carte passe, 1 sinon.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "generator"))

import generate as G  # noqa: E402  (S-expression + géométrie du générateur)

DEFAULT_PCB = ROOT / "kicad" / f"{G.PROJECT}.kicad_pcb"

# Deux courtyards jointifs (passifs alignés au pas) ne sont pas un défaut.
TOL_MM = 0.01
# Sous ce seuil, un couloir « gêné » relève du bruit de modélisation.
ADVISORY_MM = 1.0

# ---------------------------------------------------------------------------
# Couloirs d'insertion, décrits dans le repère LOCAL de l'empreinte.
#   dir      : direction d'enfichage (vecteur unitaire local, sens sortant)
#   depth    : longueur du volume à réserver devant l'ouverture (mm)
#   half     : demi-largeur du couloir (mm)
#   severity : "erreur" (corps rigide) ou "avis" (fil souple)
#   what     : libellé du volume réservé, pour le message
# ---------------------------------------------------------------------------
MATING = {
    # Jack DC 5,5/2,1 horizontal. La broche 1 (TIP) touche la pointe de la
    # fiche, donc le FOND du fût ; la broche 2 (sleeve) est côté façade. Dans
    # cette empreinte pad 1 = (0, 0) et pad 2 = (-6, 0) : l'ouverture est du
    # côté -X. La fiche moulée d'un bloc 5 V fait ~20 mm, plus le rayon de
    # courbure du câble → 25 mm de couloir, aussi large que le fût (9,5 mm).
    "BarrelJack_Horizontal": dict(
        dir=(-1.0, 0.0), depth=25.0, half=4.75, severity="erreur",
        what="fiche jack moulée (~20 mm) devant l'ouverture"),
    # Borniers à vis : la face d'entrée des fils est le côté +Y (bande de
    # sérigraphie à y = +2,54 des empreintes « bornier »). Un fil souple
    # accepte un coude et la vis se serre par le dessus : simple ergonomie
    # d'assemblage, pas un blocage — donc "avis". À confirmer sur l'exemplaire
    # réel (même précaution que l'ordre des broches des modules, cf. README).
    "TerminalBlock_bornier-2_P5.08mm": dict(
        dir=(0.0, 1.0), depth=6.0, half=5.08, severity="avis",
        what="présentation du fil devant la face d'entrée"),
    "TerminalBlock_bornier-3_P5.08mm": dict(
        dir=(0.0, 1.0), depth=6.0, half=7.62, severity="avis",
        what="présentation du fil devant la face d'entrée"),
}

# Paires de références autorisées à se recouvrir : peuplement mutuellement
# exclusif (jamais les deux sur la même unité). Une exclusion sans
# justification est un défaut masqué — la raison est obligatoire.
EXCLUSIVE: dict[frozenset, str] = {
    frozenset(("A1", "A2")):
        "sites MCU exclusifs : un seul module posé (WROOM XOR S3)",
}


# ---------------------------------------------------------------------------
# Lecture du PCB
# ---------------------------------------------------------------------------

class Part:
    """Une empreinte posée : référence, position, corps 3D (courtyard)."""

    def __init__(self, ref, fp, x, y, rot, local_crtyd):
        self.ref, self.fp = ref, fp
        self.x, self.y, self.rot = x, y, rot
        self.local = local_crtyd
        self.hull = G.convex_hull(G.place_points(local_crtyd, x, y, rot))

    def __repr__(self):
        return f"{self.ref} ({self.fp} @ {self.x},{self.y} {self.rot:g}°)"


def read_parts(pcb_tree) -> tuple[list[Part], list[str]]:
    """Empreintes du PCB + liste des empreintes sans courtyard (incontrôlables)."""
    parts, blind = [], []
    for fp in G.sx_find_all(pcb_tree, G.Sym("footprint")):
        ref = next((p[2] for p in G.sx_find_all(fp, G.Sym("property"))
                    if p[1] == "Reference"), "?")
        at = G.sx_find_all(fp, G.Sym("at"))[0]
        rot = float(at[3]) if len(at) > 3 else 0.0
        name = str(fp[1]).split(":")[-1]
        pts = G.courtyard_points(fp)
        if not pts:
            blind.append(f"{ref} ({name})")
            continue
        parts.append(Part(ref, name, float(at[1]), float(at[2]), rot, pts))
    return parts, blind


def board_outline(pcb_tree):
    """Bounding-box du contour de carte (Edge.Cuts) : (x0, y0, x1, y1)."""
    xs, ys = [], []
    for item in pcb_tree:
        if not isinstance(item, list) or not item:
            continue
        layers = G.sx_find_all(item, G.Sym("layer"))
        if not layers or str(layers[0][1]) != "Edge.Cuts":
            continue
        for key in ("start", "mid", "end", "center"):
            found = G.sx_find_all(item, G.Sym(key))
            if found:
                xs.append(float(found[0][1]))
                ys.append(float(found[0][2]))
    if not xs:
        raise SystemExit("ERREUR : aucun contour Edge.Cuts dans le PCB")
    return min(xs), min(ys), max(xs), max(ys)


# ---------------------------------------------------------------------------
# Contrôles
# ---------------------------------------------------------------------------

def check_courtyards(parts) -> tuple[list[str], list[str]]:
    errors, notes = [], []
    for i, a in enumerate(parts):
        for b in parts[i + 1:]:
            depth = G.overlap_depth(a.hull, b.hull)
            if depth <= TOL_MM:
                continue
            why = EXCLUSIVE.get(frozenset((a.ref, b.ref)))
            if why:
                notes.append(f"{a.ref} / {b.ref} : recouvrement toléré — {why}")
                continue
            errors.append(
                f"CORPS : {a.ref} et {b.ref} se recouvrent sur {depth:.2f} mm "
                f"({a.fp} / {b.fp})")
    return errors, notes


def corridor_polygon(part: Part, spec: dict):
    """Volume d'enfichage devant l'ouverture, en coordonnées carte."""
    dx, dy = spec["dir"]
    # Façade = point du courtyard le plus avancé dans la direction d'enfichage.
    front = max(px * dx + py * dy for px, py in part.local)
    half, depth = spec["half"], spec["depth"]
    vx, vy = -dy, dx                      # perpendiculaire au sens d'enfichage
    corners = [((front + d) * dx + s * half * vx,
                (front + d) * dy + s * half * vy)
               for s, d in ((-1, 0.0), (1, 0.0), (1, depth), (-1, depth))]
    return G.convex_hull(G.place_points(corners, part.x, part.y, part.rot))


def check_mating(parts, outline) -> tuple[list[str], list[str]]:
    """Couloirs d'insertion : (erreurs bloquantes, avis d'assemblage)."""
    x0, y0, x1, y1 = outline
    errors, advisories = [], []
    for part in parts:
        spec = MATING.get(part.fp)
        if not spec:
            continue
        blocking = spec["severity"] == "erreur"
        out = errors if blocking else advisories
        corridor = corridor_polygon(part, spec)
        for other in parts:
            if other is part:
                continue
            depth = G.overlap_depth(corridor, other.hull)
            if depth <= (TOL_MM if blocking else ADVISORY_MM):
                continue
            out.append(
                f"COULOIR : {part.ref} — {other.ref} occupe le volume "
                f"d'enfichage sur {depth:.2f} mm ({spec['what']})")
        # Une ouverture qui pointe vers l'intérieur est inutilisable même si
        # rien ne la bloque aujourd'hui — contrôle réservé aux corps rigides
        # (un fil souple, lui, peut être ramené par-dessus la carte).
        if blocking and all(x0 - TOL_MM <= px <= x1 + TOL_MM
                            and y0 - TOL_MM <= py <= y1 + TOL_MM
                            for px, py in corridor):
            errors.append(
                f"COULOIR : {part.ref} — ouverture tournée vers l'intérieur de "
                f"la carte (couloir de {spec['depth']:g} mm entièrement dans le "
                f"contour) ; {spec['what']}")
    return errors, advisories


def run(pcb_path: Path, verbose: bool = True) -> list[str]:
    """Contrôle une carte ; renvoie la liste des erreurs bloquantes."""
    tree = G.sx_parse(pcb_path.read_text(encoding="utf-8"))
    parts, blind = read_parts(tree)
    outline = board_outline(tree)
    crt_err, crt_notes = check_courtyards(parts)
    mate_err, advisories = check_mating(parts, outline)
    errors = crt_err + mate_err

    if verbose:
        print(f"{pcb_path.name} : {len(parts)} corps 3D contrôlés, contour "
              f"({outline[0]:g}, {outline[1]:g}) → ({outline[2]:g}, {outline[3]:g})")
        for ref in blind:
            print(f"  ATTENTION : {ref} sans courtyard — corps non contrôlable")
        for note in crt_notes:
            print(f"  (toléré) {note}")
        for err in errors:
            print(f"  {err}")
        for adv in advisories:
            print(f"  (avis) {adv}")
        if errors:
            print(f"ECHEC : {len(errors)} défaut(s) géométrique(s)")
        else:
            print("OK : aucun recouvrement de corps, "
                  "couloirs d'enfichage libres")
    return errors


# ---------------------------------------------------------------------------
# Non-régression : la carte actuelle passe, la faute historique est vue
# ---------------------------------------------------------------------------

def selftest(pcb_path: Path) -> int:
    """Rejoue le défaut `GEN-02` : J2 tourné, ouverture vers l'intérieur.

    Le jack 5 V avait été posé à 270° — ouverture vers l'intérieur de la carte,
    enfichage impossible — sans que rien ne le signale. Le contrôle doit être
    propre à 0° et refuser les trois autres quarts de tour.
    """
    tree = G.sx_parse(pcb_path.read_text(encoding="utf-8"))
    parts, _ = read_parts(tree)
    outline = board_outline(tree)
    jack = next((p for p in parts if p.fp == "BarrelJack_Horizontal"), None)
    if jack is None:
        print("SELFTEST : aucun jack sur cette carte — contrôle sans objet")
        return 0
    others = [p for p in parts if p is not jack]
    failures = []
    for rot in (0.0, 90.0, 180.0, 270.0):
        moved = Part(jack.ref, jack.fp, jack.x, jack.y, rot, jack.local)
        errs, _ = check_mating([moved] + others, outline)
        seen = [e for e in errs if e.startswith(f"COULOIR : {jack.ref} ")]
        expected = (rot == jack.rot)     # seule l'orientation réelle est saine
        status = "libre" if not seen else f"{len(seen)} défaut(s)"
        print(f"  {jack.ref} à {rot:3.0f}° : {status}"
              f"{'  <- orientation de la carte' if expected else ''}")
        if expected and seen:
            failures.append(f"{jack.ref} à {rot:g}° devrait passer")
        if not expected and not seen:
            failures.append(f"{jack.ref} à {rot:g}° devrait être refusé")
    if failures:
        for f in failures:
            print(f"  SELFTEST ECHEC : {f}")
        return 1
    print("SELFTEST OK : le contrôle attrape bien un jack mal orienté (GEN-02)")
    return 0


def main(argv) -> int:
    args = [a for a in argv[1:] if not a.startswith("--")]
    pcb_path = Path(args[0]) if args else DEFAULT_PCB
    if "--selftest" in argv:
        return selftest(pcb_path)
    return 1 if run(pcb_path) else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
