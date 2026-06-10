"""Restaure config/ (sdkconfig.h) en phase 2 Arduino après rmtree idf_lib_copy."""
import shutil
from pathlib import Path

Import("env")

pioenv = env.get("PIOENV", "")
if pioenv.startswith("wroom-"):
    build_dir = Path(env.subst("$BUILD_DIR"))
    # Phase 1 (CMake) : CMakeCache présent → ne pas restaurer (et surtout ne pas SystemExit).
    if not (build_dir / "CMakeCache.txt").is_file():
        artifacts = Path(env.subst("$PROJECT_DIR")) / ".pio_artifacts" / pioenv
        config_src = artifacts / "config"
        if config_src.is_dir():
            config_dest = build_dir / "config"
            build_dir.mkdir(parents=True, exist_ok=True)
            if config_dest.exists():
                shutil.rmtree(config_dest)
            shutil.copytree(config_src, config_dest)
            print("[pre-script] FFP5CS: config restauré pour phase 2 Arduino")
