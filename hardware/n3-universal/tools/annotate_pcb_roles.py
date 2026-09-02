#!/usr/bin/env python3
"""Écrit le RÔLE de chaque composant dans le PCB routé (champ Description).

Au clic sur une empreinte, pcbnew n'affichait que la description générique de
la bibliothèque KiCad (« Resistor, Axial_DIN0207 series… ») : rien sur ce que
le composant FAIT dans l'unité. Les 131 empreintes de la carte avaient une
propriété `Description` vide. Cet outil la remplit avec
`generate.described()` — description générique + rôle ffp5cs, msp et n3pp —
la même chaîne que le champ Description des symboles du schéma.

Édition CHIRURGICALE, volontairement textuelle : seule la ligne
`(property "Description" "…"` de chaque bloc `footprint` est réécrite. Rien
d'autre n'est touché — ni pad, ni piste, ni zone, ni net, ni UUID, ni mise en
forme. C'est ce qui permet de l'appliquer à un PCB **routé** (le régénérer
avec `generate.main()` produirait une carte non routée). L'opération est
idempotente : relancer ne change plus rien.

Usage :
    python3 tools/annotate_pcb_roles.py            # écrit dans kicad/
    python3 tools/annotate_pcb_roles.py --check    # ne modifie rien (CI)
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "generator"))

import generate as G  # noqa: E402

PCB = ROOT / "kicad" / f"{G.PROJECT}.kicad_pcb"
DESC_RE = re.compile(r'\(property "Description" "((?:[^"\\]|\\.)*)"')


def esc(text: str) -> str:
    """Échappement S-expression d'une valeur de propriété."""
    return text.replace("\\", "\\\\").replace('"', '\\"')


def blocks(text: str, head: str = '\t(footprint "'):
    """Positions (début, fin) de chaque bloc `footprint` du fichier."""
    out, pos = [], 0
    while True:
        start = text.find(head, pos)
        if start < 0:
            return out
        i, depth, in_str = start, 0, False
        while i < len(text):
            ch = text[i]
            if in_str:
                if ch == "\\":
                    i += 1
                elif ch == '"':
                    in_str = False
            elif ch == '"':
                in_str = True
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out.append((start, i + 1))
        pos = i + 1


def annotate(text: str) -> tuple[str, list[str], list[str]]:
    """Renvoie (texte annoté, refs mises à jour, anomalies)."""
    desc_by_ref = {c["ref"]: G.described(c["ref"], c["desc"])
                   for c in G.COMPONENTS}
    out, last, changed, problems = [], 0, [], []
    found = blocks(text)
    # Garde-fou : un repérage textuel qui ne trouve plus les empreintes (mise
    # en forme du fichier changée) doit ECHOUER, jamais annoncer « rien à faire ».
    if len(found) != len(G.COMPONENTS):
        problems.append(f"{len(found)} bloc(s) footprint repérés dans le PCB "
                        f"pour {len(G.COMPONENTS)} composants attendus")
    for start, end in found:
        block = text[start:end]
        ref_m = re.search(r'\(property "Reference" "([^"]*)"', block)
        if not ref_m:
            problems.append(f"bloc footprint sans Reference à l'offset {start}")
            continue
        ref = ref_m.group(1)
        wanted = desc_by_ref.get(ref)
        if wanted is None:
            problems.append(f"{ref} : absent de COMPONENTS (PCB désynchronisé ?)")
            continue
        m = DESC_RE.search(block)
        if not m:
            problems.append(f"{ref} : pas de propriété Description dans l'empreinte")
            continue
        if m.group(1) == esc(wanted):
            continue
        out.append(text[last:start + m.start(1)])
        out.append(esc(wanted))
        last = start + m.end(1)
        changed.append(ref)
    out.append(text[last:])
    return "".join(out), changed, problems


def main(argv) -> int:
    check = "--check" in argv
    text = PCB.read_text(encoding="utf-8")
    new, changed, problems = annotate(text)

    for p in problems:
        print(f"  ATTENTION {p}")
    if check:
        if changed:
            print(f"ECHEC : {len(changed)} empreinte(s) sans le rôle à jour "
                  f"({', '.join(changed[:8])}{'…' if len(changed) > 8 else ''}) "
                  f"— lancer tools/annotate_pcb_roles.py")
        else:
            print(f"OK : les {len(G.COMPONENTS)} empreintes portent leur rôle")
        return 1 if (changed or problems) else 0

    if not changed:
        print(f"OK : rien à faire, les {len(G.COMPONENTS)} rôles sont à jour")
        return 1 if problems else 0

    G.sx_parse(new)   # auto-validation syntaxique avant écriture
    diff = sum(1 for a, b in zip(text.splitlines(), new.splitlines()) if a != b)
    if len(text.splitlines()) != len(new.splitlines()):
        print("ECHEC : le nombre de lignes a changé — annotation abandonnée")
        return 1
    PCB.write_text(new, encoding="utf-8")
    print(f"{len(changed)} rôle(s) écrit(s) dans {PCB.name} "
          f"({diff} ligne(s) modifiée(s), toutes des Description)")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
