# -*- coding: utf-8 -*-
"""
Redirection du dossier de build PlatformIO hors du workspace (Windows par défaut).

- Sous Windows : par défaut C:\\pio-builds\\<slug-projet>\\<env>\\
  (évite chemins longs / espaces dans IOT_n3).
- Linux/macOS / CI : pas de redirection sauf si N3_PIO_BUILD_ROOT est défini.

Désactiver explicitement : N3_PIO_BUILD_REDIRECT=0 (ou false/no/off).
Forcer une racine : N3_PIO_BUILD_ROOT=D:\\mes-builds

Le slug est le dernier segment du chemin du projet, espaces remplacés par des tirets
(ex. « test psram s3 » -> test-psram-s3).
"""

from __future__ import print_function

import os
from pathlib import Path

Import("env")  # noqa: F821  # SCons / PlatformIO


def _should_redirect_root():
    flag = os.environ.get("N3_PIO_BUILD_REDIRECT", "").strip().lower()
    if flag in ("0", "false", "no", "off"):
        return None
    custom = os.environ.get("N3_PIO_BUILD_ROOT", "").strip()
    if custom:
        return Path(os.path.expandvars(custom)).expanduser()
    if os.name == "nt":
        return Path(r"C:\pio-builds")
    return None


def _project_slug(project_dir):
    name = Path(project_dir).name
    return "-".join(name.split())


root = _should_redirect_root()
if root:
    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    slug = _project_slug(str(project_dir))
    pioenv = env["PIOENV"]
    new_build_dir = str((root / slug / pioenv).resolve())
    try:
        os.makedirs(new_build_dir, exist_ok=True)
    except OSError as e:
        print("N3: WARN impossible de creer BUILD_DIR %s: %s" % (new_build_dir, e))
    env.Replace(BUILD_DIR=new_build_dir)
    print("N3: BUILD_DIR -> %s" % new_build_dir)
