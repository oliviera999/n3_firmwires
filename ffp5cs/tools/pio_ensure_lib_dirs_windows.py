# Pre-script Windows : pré-crée les répertoires de build des libs dont le nom contient des espaces.
# Évite "opening dependency file ... No such file or directory" sous Windows (compilateur / SCons).
# À utiliser pour les env S3 (ex. wroom-s3-test-psram) quand .pio/libdeps contient des noms avec espaces.
import os
import sys

Import("env")

def ensure_lib_build_dirs_with_spaces():
    if sys.platform != "win32":
        return
    try:
        proj = env.subst("$PROJECT_DIR")
        pioenv = env.get("PIOENV", "")
        build_dir = env.subst("$BUILD_DIR")
        if not proj or not pioenv or not build_dir:
            return
        libdeps_dir = os.path.join(proj, ".pio", "libdeps", pioenv)
        if not os.path.isdir(libdeps_dir):
            return
        names_with_spaces = []
        for name in os.listdir(libdeps_dir):
            path = os.path.join(libdeps_dir, name)
            if os.path.isdir(path) and " " in name and not name.startswith("."):
                names_with_spaces.append(name)
        if not names_with_spaces:
            return
        # PlatformIO utilise lib + 3 caractères hex (0x0..0xfff) pour les dossiers de libs.
        created = 0
        for h in range(0x1000):
            lib_sub = os.path.join(build_dir, "lib%03x" % h)
            for name in names_with_spaces:
                d = os.path.join(lib_sub, name)
                try:
                    os.makedirs(d, exist_ok=True)
                    created += 1
                except OSError:
                    pass
        if created > 0:
            print("[pre-script] ensure_lib_dirs_windows: %d repertoires (noms avec espaces) pour %s" % (created, pioenv))
    except Exception as e:
        print("[pre-script] ensure_lib_dirs_windows:", e)

ensure_lib_build_dirs_with_spaces()
