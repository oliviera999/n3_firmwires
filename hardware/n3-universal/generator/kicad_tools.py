"""Localisation des outils KiCad, identique sous Linux (CI) et Windows.

Sous Windows l'installeur KiCad n'ajoute ni `kicad-cli` ni son Python au PATH :
on les cherche dans `C:\\Program Files\\KiCad\\<version>\\bin`, version la plus
récente d'abord. Variable d'environnement `KICAD_CLI` prioritaire.
"""

from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path


def _kicad_bins() -> list[Path]:
    roots = [Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "KiCad"]
    bins = []
    for root in roots:
        if root.is_dir():
            versions = sorted((d for d in root.iterdir() if d.is_dir()),
                              key=lambda d: [int(p) if p.isdigit() else 0
                                             for p in d.name.split(".")],
                              reverse=True)
            bins += [v / "bin" for v in versions]
    return bins


def kicad_cli() -> str:
    env = os.environ.get("KICAD_CLI")
    if env:
        return env
    found = shutil.which("kicad-cli")
    if found:
        return found
    for b in _kicad_bins():
        exe = b / "kicad-cli.exe"
        if exe.exists():
            return str(exe)
    sys.exit("kicad-cli introuvable (installer KiCad ou définir KICAD_CLI)")


def kicad_python() -> str:
    """Python embarqué de KiCad (porte le module pcbnew) — Windows seulement ;
    sous Linux, le python3 système voit pcbnew."""
    for b in _kicad_bins():
        exe = b / "python.exe"
        if exe.exists():
            return str(exe)
    return sys.executable
