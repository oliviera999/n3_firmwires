# -*- coding: utf-8 -*-
"""Apres 'pio run -t clean' : repare la jonction .pio/build (Windows)."""
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
        try:
            path.unlink()
        except OSError:
            pass


def _repair_junction(project_dir, pioenv, new_build_dir):
    if os.name != "nt":
        return
    default_build = Path(project_dir) / ".pio" / "build" / pioenv
    default_build.parent.mkdir(parents=True, exist_ok=True)
    _remove_build_entry(default_build)
    Path(new_build_dir).mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["cmd", "/c", "mklink", "/J", str(default_build), str(new_build_dir)],
        check=False,
        capture_output=True,
    )


def after_clean(source, target, env):
    root = _should_redirect_root()
    if not root:
        return
    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    pioenv = env["PIOENV"]
    new_build_dir = (root / _project_slug(str(project_dir)) / pioenv).resolve()
    _repair_junction(project_dir, pioenv, new_build_dir)
    print("[post-clean] jonction .pio/build reparee pour %s" % pioenv)


env.AddPostAction("clean", after_clean)
