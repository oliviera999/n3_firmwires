import os
from pathlib import Path

Import("env")

# Diagnostic: confirme que le builder a demarré (si ce message n'apparait pas, le blocage est avant le pre script)
print("FFP5CS: pre script started")

# Forcer C++17 pour tout le build (sources + libs + framework) pour éviter warnings
# "inline variables are only available with -std=c++17" et erreurs d'archive (WiFi.cpp.o).
# Append systématique : à ce stade CXXFLAGS peut ne pas encore contenir les build_flags.
env.Append(CXXFLAGS=["-std=gnu++17"])

def add_mklittlefs_to_path():
    home = Path(os.environ.get("USERPROFILE") or os.environ.get("HOME") or "").expanduser()
    candidates = [
        home / ".platformio" / "packages" / "tool-mklittlefs" / "mklittlefs.exe",
        home / ".platformio" / "packages" / "tool-mklittlefs4" / "mklittlefs.exe",
    ]
    for exe in candidates:
        if exe.exists():
            tool_dir = str(exe.parent)
            old_path = os.environ.get("PATH", "")
            if tool_dir not in old_path:
                os.environ["PATH"] = tool_dir + os.pathsep + old_path
            # Ajouter aussi au PATH SCons pour les sous-processus
            try:
                env.PrependENVPath("PATH", tool_dir)
            except Exception as e:
                print("[pre-script] WARN: env.PrependENVPath failed:", e)
            # Certaines toolchains regardent une variable dédiée
            os.environ.setdefault("MKLITTLEFS", str(exe))
            # Remplacer la commande utilisée par le builder LittleFS si applicable
            try:
                env.Replace(MKLITTLEFS=str(exe))
                env.Replace(MKFS_LITTLEFS=str(exe))
            except Exception as e:
                print("[pre-script] WARN: env.Replace failed:", e)
            print("[pre-script] mklittlefs détecté:", exe)
            return
    print("[pre-script] mklittlefs introuvable dans packages PlatformIO")


def add_littlefs_include_for_wroom():
    """Pour wroom-* (WROOM et S3) : ajouter le LittleFS du framework au include path (ESP Mail Client).
    Évite 'LittleFS was not declared' avec pioarduino / arduino-esp32 3.x."""
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-"):
        return
    home = Path(os.environ.get("USERPROFILE") or os.environ.get("HOME") or "").expanduser()
    try:
        framework_dir = env.subst("$PIOFRAMEWORK_DIR")
    except Exception:
        framework_dir = None
    if not framework_dir or not os.path.isdir(framework_dir):
        home = Path(os.environ.get("USERPROFILE") or os.environ.get("HOME") or "").expanduser()
        base = home / ".platformio" / "packages"
        if base.is_dir():
            for name in os.listdir(base):
                if "framework" in name.lower() and "arduino" in name.lower() and "esp" in name.lower():
                    candidate = base / name / "libraries" / "LittleFS" / "src"
                    if candidate.is_dir():
                        framework_dir = str(base / name)
                        break
    if framework_dir:
        littlefs_include = os.path.join(framework_dir, "libraries", "LittleFS", "src")
        fs_include = os.path.join(framework_dir, "libraries", "FS", "src")
        if os.path.isdir(littlefs_include):
            env.Prepend(CPPPATH=[littlefs_include])
            env.Prepend(CPPFLAGS=["-I" + littlefs_include])
            # Propager aux libs (pioarduino/CMake n'utilise pas toujours CPPPATH pour les libs)
            try:
                env.Append(BUILD_FLAGS=["-I" + littlefs_include])
            except Exception:
                pass
            msg_suffix = " (S3)" if pioenv.startswith("wroom-s3") else ""
            print("[pre-script] FFP5CS: LittleFS include ajouté%s:" % msg_suffix, littlefs_include)
            if os.path.isdir(fs_include):
                env.Prepend(CPPPATH=[fs_include])
                env.Prepend(CPPFLAGS=["-I" + fs_include])
                try:
                    env.Append(BUILD_FLAGS=["-I" + fs_include])
                except Exception:
                    pass
            # S3 uniquement : esp_littlefs.h, core Arduino et BuildSources LittleFS (WROOM utilise SPIFFS, pas de link LittleFS).
            # Ne pas ajouter de chemins esp32s3 ni BuildSources pour wroom-test / wroom-prod / wroom-beta.
            if pioenv.startswith("wroom-s3"):
                # esp_littlefs.h requis par LittleFS.cpp quand CONFIG_LITTLEFS_PAGE_SIZE est défini (S3)
                esp_littlefs_candidates = [
                    os.path.join(framework_dir, "tools", "sdk", "esp32s3", "include", "esp_littlefs", "include"),
                    os.path.join(home, ".platformio", "packages", "framework-arduinoespressif32-libs",
                                 "esp32s3", "include", "joltwallet__littlefs", "include"),
                ]
                esp_littlefs_inc = None
                for cand in esp_littlefs_candidates:
                    if os.path.isdir(cand):
                        esp_littlefs_inc = cand
                        break
                if esp_littlefs_inc:
                    env.Prepend(CPPPATH=[esp_littlefs_inc])
                    env.Prepend(CPPFLAGS=["-I" + esp_littlefs_inc])
                    print("[pre-script] FFP5CS S3: esp_littlefs.h include ajouté:", esp_littlefs_inc)
                # BuildSources n'hérite pas toujours des includes framework : ajouter le core Arduino (Arduino.h)
                try:
                    core_candidates = [
                        os.path.join(framework_dir, "cores", "esp32s3"),
                        os.path.join(framework_dir, "cores", "esp32"),
                        os.path.join(framework_dir, "cores", "arduino"),
                    ]
                    for core_dir in core_candidates:
                        if os.path.isdir(core_dir):
                            env.Prepend(CPPPATH=[core_dir])
                            env.Prepend(CPPFLAGS=["-I" + core_dir])
                            print("[pre-script] FFP5CS S3: core Arduino include ajouté (Arduino.h):", core_dir)
                            break
                    build_subdir = os.path.join(env.subst("$BUILD_DIR"), "LittleFS_fw")
                    env.BuildSources(build_subdir, littlefs_include)
                    print("[pre-script] FFP5CS S3: LittleFS sources ajoutées au build:", littlefs_include)
                except Exception as e:
                    print("[pre-script] FFP5CS S3: BuildSources LittleFS failed:", e)


add_mklittlefs_to_path()
add_littlefs_include_for_wroom()


def add_wno_error_for_wroom():
    """Éviter échec wpa_supplicant : flags C++ (-fno-rtti, etc.) propagés aux .c IDF avec -Werror.
    Appliqué à tous les envs wroom-* (pioarduino et espressif32)."""
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-"):
        return
    env.Append(CFLAGS=["-Wno-error"], CXXFLAGS=["-Wno-error"])
    print("[pre-script] FFP5CS wroom: -Wno-error ajouté (CFLAGS/CXXFLAGS)")


def ensure_build_subdirs():
    """Crée le répertoire de build et sous-dossiers (src, src/automatism) pour éviter
    'opening dependency file ... No such file or directory' et .sconsign*.tmp en compilation parallèle.
    Utilise BUILD_DIR (redirection C:\\pio-builds possible via pio_redirect_build_dir.py)."""
    try:
        proj = env.subst("$PROJECT_DIR")
        pioenv = env.get("PIOENV", "")
        if not proj or not pioenv:
            return
        try:
            build_root = os.path.normpath(env.subst("$BUILD_DIR"))
        except Exception:
            build_root = os.path.normpath(os.path.join(proj, ".pio", "build", pioenv))
        for sub in ("", "src", os.path.join("src", "automatism")):
            d = os.path.join(build_root, sub) if sub else build_root
            os.makedirs(d, exist_ok=True)
        print("[pre-script] ensure_build_subdirs: %s" % build_root)
    except Exception as e:
        print("[pre-script] ensure_build_subdirs:", e)


def _run_ensure_build_subdirs(source, target, env):
    """Callback PreAction : recréer les répertoires juste avant la compilation."""
    ensure_build_subdirs()

# Création au chargement du script
ensure_build_subdirs()
# PreAction avant build : recrée src/ et src/automatism si besoin (après clean ou race)
for alias in ("buildprog", "buildelf", "build"):
    try:
        env.AddPreAction(alias, _run_ensure_build_subdirs)
        break
    except Exception:
        continue

add_wno_error_for_wroom()