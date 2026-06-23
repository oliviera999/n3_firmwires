"""Sauvegarde bootloader/partitions/config avant idf_lib_copy (rmtree du BUILD_DIR)."""

import sys
import shutil
import time
from pathlib import Path

Import("env")

_tools = Path(env.subst("$PROJECT_DIR")) / "tools"
if str(_tools) not in sys.path:
    sys.path.insert(0, str(_tools))

from pio_flash_bundle import (  # noqa: E402
    FLASH_BIN_ALIASES,
    MAX_FIRMWARE_LEAD_SEC,
    copy2_retry,
    refresh_bundle_in_build_dir,
    write_bundle_manifest,
)


def _find_in_build(build_dir, canonical_name):
    for name in FLASH_BIN_ALIASES.get(canonical_name, (canonical_name,)):
        direct = build_dir / name
        if direct.is_file():
            return direct
        found = next(build_dir.rglob(name), None)
        if found and found.is_file():
            return found
    return None


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
    if not (build_dir / "CMakeCache.txt").is_file():
        return

    artifacts = Path(env.subst("$PROJECT_DIR")) / ".pio_artifacts" / pioenv
    artifacts.mkdir(parents=True, exist_ok=True)

    try:
        for canonical in FLASH_BIN_ALIASES:
            src = _find_in_build(build_dir, canonical)
            if not src or not src.is_file():
                continue
            copy2_retry(src, artifacts / canonical)
            copy2_retry(src, build_dir / canonical)
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
    art_dir = artifacts if artifacts.is_dir() else None

    try:
        fw_mtime = firmware.stat().st_mtime
        if art_dir is not None:
            for canonical in FLASH_BIN_ALIASES:
                src = artifacts / canonical
                if not src.is_file():
                    continue
                if src.stat().st_mtime < fw_mtime - MAX_FIRMWARE_LEAD_SEC:
                    print(
                        "[post-script] FFP5CS: skip sync artifact obsolete:",
                        canonical,
                    )
                    continue
                copy2_retry(src, build_dir / canonical)
                print("[post-script] FFP5CS: sync artifact -> build:", canonical)
        refresh_bundle_in_build_dir(build_dir, art_dir, pioenv)
        write_bundle_manifest(build_dir, pioenv)
    except OSError as err:
        print("[post-script] FFP5CS: WARN sync artifacts -> build:", err)


env.AddPreAction("checkprogsize", save_idf_boot_artifacts)

for _alias in ("buildprog", "buildelf", "build"):
    try:
        env.AddPostAction(_alias, sync_artifacts_to_build)
        break
    except Exception:
        continue
