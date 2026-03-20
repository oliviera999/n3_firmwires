"""
Pre-build guard for uploadphotosserver.

If `firmwires/credentials.h` is missing, copy `firmwires/credentials.h.example`
to let first builds run without manual setup.
"""
import shutil
from pathlib import Path

Import("env")

project_dir = Path(env.get("PROJECT_DIR", ".")).resolve()
firmwires_root = project_dir.parent

credentials = firmwires_root / "credentials.h"
example = firmwires_root / "credentials.h.example"

if not credentials.exists() and example.exists():
    shutil.copy2(example, credentials)
    print("[pre-script] credentials.h absent : copie de credentials.h.example vers firmwires/credentials.h")
elif not example.exists():
    print("[pre-script] WARN: credentials.h.example introuvable a la racine firmwires/")
