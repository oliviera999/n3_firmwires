import os
from pathlib import Path

Import("env")

# Diagnostic: confirme que le builder a demarré (si ce message n'apparait pas, le blocage est avant le pre script)
print("FFP5CS: pre script started")

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


def add_littlefs_include_for_s3():
    """Pour wroom-s3-test : ajouter le LittleFS du framework au include path (ESP Mail Client)."""
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-s3"):
        return
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
            print("[pre-script] FFP5CS S3: LittleFS include ajouté:", littlefs_include)
            if os.path.isdir(fs_include):
                env.Prepend(CPPPATH=[fs_include])
                env.Prepend(CPPFLAGS=["-I" + fs_include])
            # esp_littlefs.h requis par LittleFS.cpp quand CONFIG_LITTLEFS_PAGE_SIZE est défini (SDK dans framework Arduino)
            esp_littlefs_inc = os.path.join(framework_dir, "tools", "sdk", "esp32s3", "include", "esp_littlefs", "include")
            if os.path.isdir(esp_littlefs_inc):
                env.Prepend(CPPPATH=[esp_littlefs_inc])
                env.Prepend(CPPFLAGS=["-I" + esp_littlefs_inc])
            # Forcer compilation + link du LittleFS du framework (évite undefined reference au link)
            try:
                build_subdir = os.path.join(env.subst("$BUILD_DIR"), "LittleFS_fw")
                env.BuildSources(build_subdir, littlefs_include)
                print("[pre-script] FFP5CS S3: LittleFS sources ajoutées au build:", littlefs_include)
            except Exception as e:
                print("[pre-script] FFP5CS S3: BuildSources LittleFS failed:", e)


add_mklittlefs_to_path()
add_littlefs_include_for_s3()


def add_wno_error_for_s3():
    """Éviter échec wpa_supplicant : flags C++ propagés aux .c IDF avec -Werror."""
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-s3"):
        return
    env.Append(CFLAGS=["-Wno-error"], CXXFLAGS=["-Wno-error"])
    print("[pre-script] FFP5CS S3: -Wno-error ajouté (CFLAGS/CXXFLAGS)")


add_wno_error_for_s3()