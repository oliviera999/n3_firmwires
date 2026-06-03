"""Sauvegarde bootloader/partitions/config avant idf_lib_copy (rmtree du BUILD_DIR)."""

import shutil

from pathlib import Path



Import("env")





def _find_in_build(build_dir, name):

    direct = build_dir / name

    if direct.is_file():

        return direct

    found = next(build_dir.rglob(name), None)

    return found if found else direct





def save_idf_boot_artifacts(source, target, env):

    pioenv = env.get("PIOENV", "")

    if not pioenv.startswith("wroom-"):

        return

    build_dir = Path(env.subst("$BUILD_DIR"))

    dest = Path(env.subst("$PROJECT_DIR")) / ".pio_artifacts" / pioenv

    dest.mkdir(parents=True, exist_ok=True)

    for name in ("bootloader.bin", "partitions.bin", "ota_data_initial.bin"):

        src = _find_in_build(build_dir, name)

        if src.is_file():

            shutil.copy2(src, dest / name)

            print("[post-script] FFP5CS: sauvegarde", name)

    config_src = build_dir / "config"

    if config_src.is_dir():

        config_dest = dest / "config"

        if config_dest.exists():

            shutil.rmtree(config_dest)

        shutil.copytree(config_src, config_dest)

        print("[post-script] FFP5CS: sauvegarde config/")





# Avant checkprogsize → avant la post-action idf_lib_copy qui rmtree le BUILD_DIR.

env.AddPreAction("checkprogsize", save_idf_boot_artifacts)

