"""Sauvegarde bootloader/partitions/config avant idf_lib_copy (rmtree du BUILD_DIR)."""

import shutil
import time
from pathlib import Path

Import("env")

# Noms produits par CMake IDF (phase 1 pioarduino) → noms canoniques du bundle flash.
_FLASH_BIN_ALIASES = {
    "bootloader.bin": ("bootloader.bin",),
    "partitions.bin": ("partitions.bin", "partition-table.bin"),
    "ota_data_initial.bin": ("ota_data_initial.bin", "boot_app0.bin"),
}


def _find_in_build(build_dir, canonical_name):
    for name in _FLASH_BIN_ALIASES.get(canonical_name, (canonical_name,)):
        direct = build_dir / name
        if direct.is_file():
            return direct
        found = next(build_dir.rglob(name), None)
        if found and found.is_file():
            return found
    return None


def _copy2_retry(src, dest, retries=8, delay=0.3):
    last_err = None
    for attempt in range(retries):
        try:
            shutil.copy2(src, dest)
            return
        except OSError as err:
            last_err = err
            if attempt + 1 < retries:
                time.sleep(delay)
    raise last_err


def _copytree_retry(src, dest, retries=5, delay=0.5):
    last_err = None
    for attempt in range(retries):
        try:
            if dest.exists():
                shutil.rmtree(dest)
            shutil.copytree(src, dest)
            return
        except OSError as err:
            last_err = err
            if attempt + 1 < retries:
                time.sleep(delay)
    raise last_err


def save_idf_boot_artifacts(source, target, env):
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-"):
        return

    build_dir = Path(env.subst("$BUILD_DIR"))
    # Phase 2 Arduino : artifacts déjà sauvegardés en phase 1 (évite verrous Windows ici).
    if not (build_dir / "CMakeCache.txt").is_file():
        return

    artifacts = Path(env.subst("$PROJECT_DIR")) / ".pio_artifacts" / pioenv
    artifacts.mkdir(parents=True, exist_ok=True)

    try:
        for canonical in _FLASH_BIN_ALIASES:
            src = _find_in_build(build_dir, canonical)
            if not src or not src.is_file():
                continue
            _copy2_retry(src, artifacts / canonical)
            _copy2_retry(src, build_dir / canonical)
            print("[post-script] FFP5CS: sauvegarde", canonical)

        config_src = build_dir / "config"
        if config_src.is_dir():
            _copytree_retry(config_src, artifacts / "config")
            print("[post-script] FFP5CS: sauvegarde config/")
    except OSError as err:
        print("[post-script] FFP5CS: WARN sauvegarde artifacts:", err)


def sync_artifacts_to_build(source, target, env):
    """Phase 2 : recopie le bundle phase 1 vers BUILD_DIR (cohérent avec firmware.bin)."""
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-") or pioenv.startswith("wroom-s3"):
        return

    build_dir = Path(env.subst("$BUILD_DIR"))
    firmware = build_dir / "firmware.bin"
    if not firmware.is_file():
        return

    artifacts = Path(env.subst("$PROJECT_DIR")) / ".pio_artifacts" / pioenv
    if not artifacts.is_dir():
        return

    try:
        for canonical in _FLASH_BIN_ALIASES:
            src = artifacts / canonical
            if src.is_file():
                _copy2_retry(src, build_dir / canonical)
                print("[post-script] FFP5CS: sync artifact -> build:", canonical)
    except OSError as err:
        print("[post-script] FFP5CS: WARN sync artifacts -> build:", err)


# Avant checkprogsize → avant la post-action idf_lib_copy qui rmtree le BUILD_DIR.
env.AddPreAction("checkprogsize", save_idf_boot_artifacts)

for _alias in ("buildprog", "buildelf", "build"):
    try:
        env.AddPostAction(_alias, sync_artifacts_to_build)
        break
    except Exception:
        continue
