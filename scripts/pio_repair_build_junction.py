# -*- coding: utf-8 -*-
"""
Repare la jonction .pio/build/<env> avant/apres clean (Windows, redirection C:\\pio-builds).
Appele en pre (chaque build) et post@clean.
"""
from __future__ import print_function

import os
import shutil
import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821


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
    return "-".join(Path(project_dir).name.split())


def _remove_build_entry(path):
    if not path.exists():
        return
    is_junction = getattr(path, "is_junction", lambda: False)()
    if path.is_symlink() or is_junction:
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path, ignore_errors=True)
    else:
        path.unlink(missing_ok=True)


def repair_junction(project_dir, pioenv, new_build_dir, strict=False):
    if os.name != "nt":
        return True
    default_build = Path(project_dir) / ".pio" / "build" / pioenv
    default_build.parent.mkdir(parents=True, exist_ok=True)
    _remove_build_entry(default_build)
    Path(new_build_dir).mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(
        ["cmd", "/c", "mklink", "/J", str(default_build), str(new_build_dir)],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        msg = (proc.stderr or proc.stdout or "").strip()
        print("N3: ERREUR mklink /J %s -> %s : %s" % (default_build, new_build_dir, msg))
        if strict and pioenv.startswith("wroom-"):
            return False
        return True
    print("N3: jonction OK %s -> %s" % (default_build, new_build_dir))
    return True


root = _should_redirect_root()
if not root:
    sys.exit(0)

project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
pioenv = env["PIOENV"]
new_build_dir = (root / _project_slug(str(project_dir)) / pioenv).resolve()
strict = pioenv.startswith("wroom-") and pioenv not in ("wroom-tls-test",)
if not repair_junction(project_dir, pioenv, new_build_dir, strict=strict):
    env.Exit(1)
