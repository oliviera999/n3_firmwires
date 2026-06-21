# Shim : certains appels toolchain (env *-cam, espressif32 6.x) invoquent « python esptool.py »
# depuis la racine du projet. Délègue vers tool-esptoolpy installé par PlatformIO.
import glob
import os
import subprocess
import sys


def _packages_root():
    p = os.environ.get("PLATFORMIO_PACKAGES_DIR")
    if p and os.path.isdir(p):
        return p
    return os.path.join(os.path.expanduser("~"), ".platformio", "packages")


def _resolve_esptool():
    pkg_dir = os.environ.get("PLATFORMIO_PACKAGES_DIR", "").strip()
    if pkg_dir:
        candidate = os.path.join(pkg_dir, "esptool.py")
        if os.path.isfile(candidate):
            return candidate

    root = _packages_root()
    for pattern in (
        os.path.join(root, "tool-esptoolpy", "esptool.py"),
        os.path.join(root, "tool-esptoolpy*", "esptool.py"),
    ):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[-1]

    return os.path.join(root, "tool-esptoolpy", "esptool.py")


def main():
    td = _resolve_esptool()
    if not os.path.isfile(td):
        print(
            "esptool shim: tool-esptoolpy introuvable (%s). Lancez un build PlatformIO une fois."
            % td,
            file=sys.stderr,
        )
        sys.exit(1)
    sys.exit(subprocess.call([sys.executable, td] + sys.argv[1:]))


if __name__ == "__main__":
    main()
