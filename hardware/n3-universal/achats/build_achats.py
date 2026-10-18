#!/usr/bin/env python3
"""Liste d'achats n3-universal : régénère le classeur Excel et la photographie des quantités,
ou vérifie (--check, en CI) qu'ils suivent la BOM.

Chaîne : BOM.csv (générée par generator/generate.py)
         + inventaire.py  (repères posés par profil, articles hors carte, consignes)
         + catalogue.py   (désignations, catégories, rechange conseillée)
         + prix.py        (relevé de prix des boutiques marocaines, liens)
      -> achats-n3-universal.xlsx (Lisez-moi, Global, Prix, msp1, n3pp, ffp5cs 230V, ffp5cs 12V)
      -> quantites.csv (photographie texte, versionnée : c'est elle que la CI compare)

Usage (depuis hardware/n3-universal) :
  python3 achats/build_achats.py            # régénère xlsx + quantites.csv (recalcul LibreOffice si présent)
  python3 achats/build_achats.py --check    # code 1 si la BOM a changé sans que la liste d'achats suive

Le --check échoue si :
  - un repère de la BOM n'est posé sur aucun profil et n'est pas déclaré dans inventaire.NEVER
    (composant ajouté au générateur sans décider où il se pose) ;
  - un article n'a pas d'entrée dans catalogue.py ou prix.py ;
  - les quantités recalculées diffèrent de quantites.csv (valeur, ajout ou retrait d'un composant).
"""
from __future__ import annotations

import csv
import io
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import inventaire as I  # noqa: E402
import catalogue as K  # noqa: E402
from prix import PRIX  # noqa: E402

XLSX = HERE / "achats-n3-universal.xlsx"
SNAP = HERE / "quantites.csv"


def snapshot_text() -> str:
    from classeur import profile_rows
    buf = io.StringIO()
    w = csv.writer(buf, delimiter=";", lineterminator="\n")
    w.writerow(["profil", "article", "statut", "qte", "reperes"])
    for prof in I.PROFILES:
        for r in profile_rows(prof):
            w.writerow([prof, r["id"], r["statut"], r["qty"], r["refs"]])
    return buf.getvalue()


def coverage_errors() -> list[str]:
    errs = []
    used = set().union(*I.POP.values())
    orphan = sorted(set(I.bom) - used - I.NEVER, key=I.natkey)
    if orphan:
        errs.append("repères de la BOM posés sur aucun profil (compléter inventaire.POP ou NEVER) : " + ", ".join(orphan))
    ghost = sorted(used - set(I.bom), key=I.natkey)
    if ghost:
        errs.append("repères d'inventaire.POP absents de la BOM : " + ", ".join(ghost))
    articles = set()
    for prof in I.PROFILES:
        articles |= set(I.board_items(prof))
        articles |= {o[0] for o in I.OFF[prof]}
    articles |= {s[0] for s in K.SHARED}
    for a in sorted(articles):
        if a not in K.CAT and a not in {s[0] for s in K.SHARED}:
            errs.append(f"article {a} absent de catalogue.py")
        if a not in PRIX:
            errs.append(f"article {a} absent de prix.py")
    return errs


def recalc(path: Path) -> None:
    """Calcule les formules avec LibreOffice (valeurs en cache pour les aperçus) ; sinon Excel le fera à l'ouverture."""
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        print("  (LibreOffice absent : formules calculées à l'ouverture dans Excel)")
        return
    with tempfile.TemporaryDirectory() as td:
        r = subprocess.run([soffice, "--headless", "--calc", "--convert-to", "xlsx", "--outdir", td, str(path)],
                           capture_output=True, text=True, timeout=180)
        out = Path(td) / path.name
        if r.returncode == 0 and out.exists():
            shutil.copy(out, path)
            print("  formules recalculées (LibreOffice)")


def main() -> int:
    errs = coverage_errors()
    if errs:  # article inconnu ou repère orphelin : inutile de comparer les quantités
        print("ECHEC liste d'achats :")
        for e in errs:
            print(" -", e)
        return 1
    if "--check" in sys.argv:
        snap = snapshot_text()
        if not SNAP.exists():
            errs.append("quantites.csv absent : lancer python3 achats/build_achats.py")
        elif SNAP.read_text(encoding="utf-8") != snap:
            old = SNAP.read_text(encoding="utf-8").splitlines()
            new = snap.splitlines()
            diff = [f"  - {l}" for l in old if l not in new][:8] + [f"  + {l}" for l in new if l not in old][:8]
            errs.append("la liste d'achats ne suit plus la BOM / l'inventaire — régénérer avec "
                        "`python3 achats/build_achats.py` et committer :\n" + "\n".join(diff))
        if errs:
            print("ECHEC liste d'achats :")
            for e in errs:
                print(" -", e)
            return 1
        print(f"OK : liste d'achats synchronisée avec la BOM ({len(I.bom)} repères, {len(snap.splitlines()) - 1} lignes)")
        return 0
    from classeur import build
    n = build(XLSX)
    recalc(XLSX)
    SNAP.write_text(snapshot_text(), encoding="utf-8")
    print(f"écrit {XLSX.name} ({n} lignes globales) et {SNAP.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
