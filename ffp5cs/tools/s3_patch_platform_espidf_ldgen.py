"""
FFP5CS S3 : patch du builder plateforme pour éviter "La ligne de commande est trop longue".
La fonction generate_project_ld_script() dans builder/frameworks/espidf.py appelle ldgen avec
--fragments et toute la liste des chemins linker.lf en ligne de commande → dépasse 8191 caractères
sous Windows. On fait écrire la liste dans un fichier et on utilise --fragments-list-file.
"""
import os
Import("env")

def _get_platform_espidf_paths():
    """Retourne la liste des chemins builder/frameworks/espidf.py des plateformes espressif32."""
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-s3"):
        return []
    out = []
    try:
        platform = env.PioPlatform()
        try:
            platform_dir = platform.get_dir()
            if platform_dir:
                path = os.path.join(platform_dir, "builder", "frameworks", "espidf.py")
                if os.path.isfile(path):
                    out.append(path)
        except Exception:
            pass
        home = os.environ.get("USERPROFILE") or os.environ.get("HOME") or ""
        base = os.path.join(home, ".platformio", "platforms")
        if os.path.isdir(base):
            for name in os.listdir(base):
                if name.startswith("espressif32"):
                    path = os.path.join(base, name, "builder", "frameworks", "espidf.py")
                    if os.path.isfile(path) and path not in out:
                        out.append(path)
    except Exception:
        pass
    return out


def _patch_espidf_py(filepath):
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    # Déjà patché ?
    if "ldgen_fragments_list_pio.txt" in content and "fragments-list-file" in content:
        return False

    # Bloc à remplacer : écriture de la liste fragments + args avec "fragments" + cmd avec --fragments
    old = """    # Create a new file to avoid automatically generated library entry as files
    # from this library are built internally by PlatformIO
    libraries_list = create_custom_libraries_list(
        str(Path(BUILD_DIR) / "ldgen_libraries"), ignore_targets
    )

    args = {
        "script": str(Path(FRAMEWORK_DIR) / "tools" / "ldgen" / "ldgen.py"),
        "config": SDKCONFIG_PATH,
        "fragments": " ".join(
            ['"%s"' % fs.to_unix_path(f) for f in linker_script_fragments]
        ),
        "kconfig": str(Path(FRAMEWORK_DIR) / "Kconfig"),
        "env_file": str(Path("$BUILD_DIR") / "config.env"),
        "libraries_list": libraries_list,
        "objdump": str(Path(TOOLCHAIN_DIR) / "bin" / env.subst("$CC").replace("-gcc", "-objdump")),
    }

    cmd = (
        '"$ESPIDF_PYTHONEXE" "{script}" --input $SOURCE '
        '--config "{config}" --fragments {fragments} --output $TARGET '
        '--kconfig "{kconfig}" --env-file "{env_file}" '
        '--libraries-file "{libraries_list}" '
        '--objdump "{objdump}"'
    ).format(**args)"""

    new = """    # Create a new file to avoid automatically generated library entry as files
    # from this library are built internally by PlatformIO
    libraries_list = create_custom_libraries_list(
        str(Path(BUILD_DIR) / "ldgen_libraries"), ignore_targets
    )

    # FFP5CS S3: éviter "ligne de commande trop longue" sous Windows (--fragments liste trop longue)
    fragments_list_file = str(Path(BUILD_DIR) / "ldgen_fragments_list_pio.txt")
    with open(fragments_list_file, "w", encoding="utf-8") as fp:
        fp.write(";".join(fs.to_unix_path(f) for f in linker_script_fragments))

    args = {
        "script": str(Path(FRAMEWORK_DIR) / "tools" / "ldgen" / "ldgen.py"),
        "config": SDKCONFIG_PATH,
        "fragments_list_file": fragments_list_file,
        "kconfig": str(Path(FRAMEWORK_DIR) / "Kconfig"),
        "env_file": str(Path("$BUILD_DIR") / "config.env"),
        "libraries_list": libraries_list,
        "objdump": str(Path(TOOLCHAIN_DIR) / "bin" / env.subst("$CC").replace("-gcc", "-objdump")),
    }

    cmd = (
        '"$ESPIDF_PYTHONEXE" "{script}" --input $SOURCE '
        '--config "{config}" --fragments-list-file "{fragments_list_file}" --output $TARGET '
        '--kconfig "{kconfig}" --env-file "{env_file}" '
        '--libraries-file "{libraries_list}" '
        '--objdump "{objdump}"'
    ).format(**args)"""

    if old not in content:
        return False
    content = content.replace(old, new)
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(content)
    print("FFP5CS S3: plateforme espidf.py patché (ldgen --fragments-list-file)")
    return True


def _patch():
    for path in _get_platform_espidf_paths():
        _patch_espidf_py(path)


_patch()
