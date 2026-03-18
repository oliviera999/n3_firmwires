# Shim : certains appels toolchain ESP32-S3 invoquent « python esptool.py » depuis la racine du projet.
# Délègue vers tool-esptoolpy installé par PlatformIO (~/.platformio/packages/tool-esptoolpy).
import os
import subprocess
import sys


def _packages_root():
    p = os.environ.get("PLATFORMIO_PACKAGES_DIR")
    if p and os.path.isdir(p):
        return p
    return os.path.join(os.path.expanduser("~"), ".platformio", "packages")


def main():
    td = os.path.join(_packages_root(), "tool-esptoolpy", "esptool.py")
    if not os.path.isfile(td):
        print("esptool shim: tool-esptoolpy introuvable. Lancez un build PlatformIO une fois.", file=sys.stderr)
        sys.exit(1)
    sys.exit(subprocess.call([sys.executable, td] + sys.argv[1:]))


if __name__ == "__main__":
    main()
