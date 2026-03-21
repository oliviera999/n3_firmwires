"""
FFP5CS S3 : contourner "La ligne de commande est trop longue" (Windows) lors de la
génération de sections.ld. On fait passer la liste des fragments via un fichier au lieu
de --fragments-list en ligne de commande.
"""
import os
Import("env")

def _get_all_espidf_pkg_dirs():
    """Retourne la liste de tous les répertoires framework-espidf (pour tous les patcher)."""
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-s3"):
        return []
    seen = set()
    out = []
    try:
        platform = env.PioPlatform()
        for name in ("framework-espidf", "framework-espidf@src-1e666e6a68efaa9903d5a0984e93801f"):
            try:
                pkg_dir = platform.get_package_dir(name)
                if pkg_dir and os.path.isdir(pkg_dir) and pkg_dir not in seen:
                    seen.add(pkg_dir)
                    out.append(pkg_dir)
            except Exception:
                continue
        home = os.environ.get("USERPROFILE") or os.environ.get("HOME") or ""
        base = os.path.join(home, ".platformio", "packages")
        if os.path.isdir(base):
            for n in os.listdir(base):
                if n.startswith("framework-espidf"):
                    pkg_dir = os.path.join(base, n)
                    if pkg_dir not in seen:
                        seen.add(pkg_dir)
                        out.append(pkg_dir)
    except Exception:
        pass
    return out

def _patch_ldgen_skip_missing_libs(pkg_dir):
    """Évite l'échec ldgen quand un .a listé dans ldgen_libraries n'existe pas encore (ordre de build)."""
    path = os.path.join(pkg_dir, "tools", "ldgen", "ldgen.py")
    if not os.path.isfile(path):
        return False
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    if "skip missing library" in content or "ldgen: skip missing" in content:
        return False
    old_loop = """        for library in libraries_file:
            library = library.strip()
            if library:
                new_env = os.environ.copy()
                new_env['LC_ALL'] = 'C'
                dump = StringIO(subprocess.check_output([objdump, '-h', library], env=new_env).decode())"""
    new_loop = """        for library in libraries_file:
            library = library.strip()
            if library:
                if not os.path.isfile(library):
                    sys.stderr.write("ldgen: skip missing library '%s'\\n" % library)
                    continue
                new_env = os.environ.copy()
                new_env['LC_ALL'] = 'C'
                dump = StringIO(subprocess.check_output([objdump, '-h', library], env=new_env).decode())"""
    if new_loop in content:
        return False
    if old_loop not in content:
        return False
    content = content.replace(old_loop, new_loop)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print("FFP5CS S3: ldgen.py patché (skip libs manquantes)")
    return True


def _patch_ldgen_py(pkg_dir):
    path = os.path.join(pkg_dir, "tools", "ldgen", "ldgen.py")
    if not os.path.isfile(path):
        return False
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    # Déjà patché ?
    if "--fragments-list-file" in content and "fragments_list_file" in content:
        return False
    # Ajouter l'option après --fragments-list
    old = '    fragments_group.add_argument(\n        \'--fragments-list\',\n        help=\'Input fragment files as a semicolon-separated list\',\n        type=str\n    )\n\n    argparser.add_argument('
    new = '    fragments_group.add_argument(\n        \'--fragments-list\',\n        help=\'Input fragment files as a semicolon-separated list\',\n        type=str\n    )\n\n    fragments_group.add_argument(\n        \'--fragments-list-file\',\n        help=\'File containing fragment paths (one per line)\',\n        type=str\n    )\n\n    argparser.add_argument('
    if old not in content:
        return False
    content = content.replace(old, new)
    # Traiter la nouvelle option dans la récupération des fragment_files
    old2 = "    fragment_files = []\n    if args.fragments_list:\n        fragment_files = args.fragments_list.split(';')\n    elif args.fragments:\n        fragment_files = args.fragments"
    new2 = "    fragment_files = []\n    if args.fragments_list_file:\n        with open(args.fragments_list_file, 'r', encoding='utf-8') as f:\n            fragment_files = [p.strip() for p in f.read().split(';') if p.strip()]\n    elif args.fragments_list:\n        fragment_files = args.fragments_list.split(';')\n    elif args.fragments:\n        fragment_files = args.fragments"
    if old2 not in content:
        return False
    content = content.replace(old2, new2)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print("FFP5CS S3: ldgen.py patché (--fragments-list-file)")
    return True

def _patch_ldgen_cmake(pkg_dir):
    path = os.path.join(pkg_dir, "tools", "cmake", "ldgen.cmake")
    if not os.path.isfile(path):
        return False
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    if "ldgen_fragments_list.txt" in content and "fragments-list-file" in content:
        return False
    # Générer le fichier liste des fragments (évite ligne de commande trop longue sur Windows)
    old_block = """    idf_build_get_property(ldgen_fragment_files __LDGEN_FRAGMENT_FILES GENERATOR_EXPRESSION)

    # Create command to invoke the linker script generator tool."""
    new_block = """    idf_build_get_property(ldgen_fragment_files __LDGEN_FRAGMENT_FILES GENERATOR_EXPRESSION)

    # Write fragment list to file to avoid "command line too long" on Windows (FFP5CS S3)
    file(GENERATE OUTPUT ${build_dir}/ldgen_fragments_list.txt CONTENT "$<JOIN:${ldgen_fragment_files},;>")
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${build_dir}/ldgen_fragments_list.txt")

    # Create command to invoke the linker script generator tool."""
    if new_block in content:
        return False
    if old_block not in content:
        return False
    content = content.replace(old_block, new_block)
    # Remplacer --fragments-list par --fragments-list-file
    content = content.replace(
        '--fragments-list "${ldgen_fragment_files}"',
        '--fragments-list-file "${build_dir}/ldgen_fragments_list.txt"'
    )
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print("FFP5CS S3: ldgen.cmake patché (fragments via fichier)")
    return True

def _invalidate_ldgen_build_cache(pioenv):
    """Force reconfig pour que Ninja utilise --fragments-list-file au lieu de la longue ligne."""
    try:
        try:
            build_dir = env.subst("$BUILD_DIR")
        except Exception:
            proj = env.subst("$PROJECT_DIR")
            build_dir = os.path.join(proj, ".pio", "build", pioenv)
        cache = os.path.join(build_dir, "CMakeCache.txt")
        ninja = os.path.join(build_dir, "build.ninja")
        for p in (cache, ninja):
            if os.path.isfile(p):
                os.remove(p)
                print("FFP5CS S3: supprimé %s pour forcer reconfig ldgen" % os.path.basename(p))
    except Exception:
        pass

def _patch_ldgen():
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-s3"):
        return
    pkg_dirs = _get_all_espidf_pkg_dirs()
    if not pkg_dirs:
        return
    any_cmake_patched = False
    for pkg_dir in pkg_dirs:
        _patch_ldgen_skip_missing_libs(pkg_dir)
        _patch_ldgen_py(pkg_dir)
        if _patch_ldgen_cmake(pkg_dir):
            any_cmake_patched = True
    # Invalider seulement quand on vient de patcher (pour que CMake régénère build.ninja
    # avec --fragments-list-file). Les runs suivants gardent le cache (esp_insights n'invalide plus non plus).
    if any_cmake_patched:
        _invalidate_ldgen_build_cache(pioenv)

_patch_ldgen()
