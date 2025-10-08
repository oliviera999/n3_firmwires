import os
from pathlib import Path

Import("env")

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

add_mklittlefs_to_path()


