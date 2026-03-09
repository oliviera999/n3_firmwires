"""
Pre-build script : copie include/secrets.h.example vers include/secrets.h si secrets.h est absent.
Permet au build (CI, find-bugs, clone frais) de compiler sans etape manuelle.
Le fichier secrets.h reste dans .gitignore ; les valeurs reelles sont a mettre dans secrets.h en local.

Note : un echec ulterieur au link (DRAM segment overflow) est une contrainte memoire ESP32
du projet, independante de ce script.
"""
import shutil
from pathlib import Path

Import("env")

proj_dir = Path(env.get("PROJECT_DIR", "."))
example = proj_dir / "include" / "secrets.h.example"
target = proj_dir / "include" / "secrets.h"

if not target.exists() and example.exists():
    shutil.copy2(example, target)
    print("[pre-script] secrets.h absent : copie de secrets.h.example vers include/secrets.h")
elif not example.exists():
    print("[pre-script] WARN: include/secrets.h.example introuvable")
# Si target existe, rien a faire
