"""
Jeu flash WROOM cohérent : bootloader + partitions + firmware issus du même build.

Évite le panic « Cache error » (esp_flash_read_chip_id / memset) quand un firmware.bin
récent est flashé avec un bootloader.bin ou partitions.bin obsolètes laissés dans BUILD_DIR
après une phase 2 pioarduino sans resynchronisation phase 1.
"""

import json
import shutil
import time
from pathlib import Path

FLASH_BIN_ALIASES = {
    "bootloader.bin": ("bootloader.bin",),
    "partitions.bin": ("partitions.bin", "partition-table.bin"),
    "ota_data_initial.bin": ("ota_data_initial.bin", "boot_app0.bin"),
}

_SKIP_RGLOB_PARTS = frozenset({"CMakeFiles", ".git"})

# Phase 1 (bootloader/partitions) puis phase 2 (firmware) : ecart typique 10-15 min sur pioarduino.
MAX_FIRMWARE_LEAD_SEC = 3600


def copy2_retry(src, dest, retries=8, delay=0.3):
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


def find_freshest_bin(build_dir, canonical):
    """Trouve le .bin le plus récent sous build_dir (hors CMakeFiles)."""
    build_dir = Path(build_dir)
    if not build_dir.is_dir():
        return None
    names = FLASH_BIN_ALIASES.get(canonical, (canonical,))
    candidates = []
    for name in names:
        direct = build_dir / name
        if direct.is_file():
            candidates.append(direct)
        for path in build_dir.rglob(name):
            if not path.is_file():
                continue
            if _SKIP_RGLOB_PARTS.intersection(path.parts):
                continue
            if path not in candidates:
                candidates.append(path)
    if not candidates:
        return None
    return max(candidates, key=lambda p: p.stat().st_mtime)


def write_bundle_manifest(build_dir, pioenv):
    build_dir = Path(build_dir)
    firmware = build_dir / "firmware.bin"
    if not firmware.is_file():
        return
    manifest = {
        "env": pioenv,
        "firmware_mtime": firmware.stat().st_mtime,
        "files": {},
    }
    for canonical in ("bootloader.bin", "partitions.bin", "ota_data_initial.bin", "firmware.bin"):
        path = build_dir / canonical
        if path.is_file():
            st = path.stat()
            manifest["files"][canonical] = {"size": st.st_size, "mtime": st.st_mtime}
    manifest_path = build_dir / "flash_bundle_manifest.json"
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")


def refresh_bundle_in_build_dir(build_dir, artifacts_dir=None, pioenv=""):
    """
    Recopie bootloader/partitions/otadata vers la racine de BUILD_DIR si absents ou plus
    anciens que firmware.bin. Sources : arbre de build (hors CMakeFiles), puis .pio_artifacts.
    """
    build_dir = Path(build_dir)
    firmware = build_dir / "firmware.bin"
    if not firmware.is_file():
        return False

    fw_mtime = firmware.stat().st_mtime
    artifacts_dir = Path(artifacts_dir) if artifacts_dir else None
    refreshed = False

    for canonical in ("bootloader.bin", "partitions.bin", "ota_data_initial.bin"):
        dest = build_dir / canonical
        dest_mtime = dest.stat().st_mtime if dest.is_file() else 0.0
        stale = (not dest.is_file()) or (dest_mtime < fw_mtime - MAX_FIRMWARE_LEAD_SEC)

        if not stale:
            continue

        src = find_freshest_bin(build_dir, canonical)
        if src is not None:
            src_mtime = src.stat().st_mtime
            if src.resolve() != dest.resolve() and src_mtime >= fw_mtime - MAX_FIRMWARE_LEAD_SEC:
                copy2_retry(src, dest)
                refreshed = True
                print("[flash-bundle] refresh %s <- %s" % (canonical, src))
                continue
            if dest.is_file() and src_mtime < fw_mtime - MAX_FIRMWARE_LEAD_SEC:
                # Candidat build tree aussi obsolete : ne pas recopier.
                pass

        if artifacts_dir is not None:
            art = artifacts_dir / canonical
            if art.is_file() and art.stat().st_mtime >= fw_mtime - MAX_FIRMWARE_LEAD_SEC:
                copy2_retry(art, dest)
                refreshed = True
                print("[flash-bundle] refresh %s <- artifacts" % canonical)

    if refreshed and pioenv:
        write_bundle_manifest(build_dir, pioenv)
    return refreshed


def assert_bundle_coherent(build_dir, pioenv):
    """Lève RuntimeError si le jeu flash est incomplet ou temporellement incohérent."""
    build_dir = Path(build_dir)
    firmware = build_dir / "firmware.bin"
    if not firmware.is_file():
        raise RuntimeError("FFP5CS flash bundle: firmware.bin introuvable dans %s" % build_dir)

    fw_mtime = firmware.stat().st_mtime
    fw_size = firmware.stat().st_size
    if fw_size < 1_200_000:
        raise RuntimeError(
            "FFP5CS flash bundle: firmware.bin trop petit (%d o) — build pioarduino incomplet"
            % fw_size
        )

    missing = []
    stale = []
    for canonical in ("bootloader.bin", "partitions.bin"):
        path = build_dir / canonical
        if not path.is_file():
            missing.append(canonical)
            continue
        if path.stat().st_mtime < fw_mtime - MAX_FIRMWARE_LEAD_SEC:
            stale.append(canonical)

    if missing:
        raise RuntimeError(
            "FFP5CS flash bundle: manquant %s dans %s — lancer 'pio run -e %s' avant flash"
            % (", ".join(missing), build_dir, pioenv)
        )
    if stale:
        raise RuntimeError(
            "FFP5CS flash bundle: %s plus ancien(s) que firmware.bin — jeu incohérent "
            "(risque panic Cache error au boot). Lancer 'pio run -e %s' (build complet) "
            "ou 'pio run -e %s -t clean' puis rebuild."
            % (", ".join(stale), pioenv, pioenv)
        )


def resolve_bundle_paths(build_dir, pioenv, artifacts_dir=None):
    """
    Retourne (bootloader, partitions, ota_or_none, firmware) depuis BUILD_DIR après refresh.
    """
    build_dir = Path(build_dir)
    if artifacts_dir is None:
        artifacts_dir = build_dir.parent.parent / ".pio_artifacts" / pioenv
        if not Path(str(artifacts_dir)).is_dir():
            # build_dir may be C:\pio-builds\ffp5cs\env — artifacts under project
            pass
    refresh_bundle_in_build_dir(build_dir, artifacts_dir if Path(artifacts_dir).is_dir() else None, pioenv)
    assert_bundle_coherent(build_dir, pioenv)

    ota = build_dir / "ota_data_initial.bin"
    return (
        build_dir / "bootloader.bin",
        build_dir / "partitions.bin",
        ota if ota.is_file() else None,
        build_dir / "firmware.bin",
    )
