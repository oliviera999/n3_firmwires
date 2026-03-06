#!/usr/bin/env python3
"""Pre-build: applique les patches Arduino pour les envs S3 (earlyInitVariant, diag_nvs)."""
import os
import subprocess
import sys

Import("env")

pioenv = env.get("PIOENV", "")
if pioenv.startswith("wroom-s3") and pioenv != "wroom-s3-test":
    script_dir = env.subst("$PROJECT_DIR") + "/tools"
    patches = [
        "patch_arduino_early_init_variant.py",
        "patch_arduino_app_main_early_wdt.py",
        "patch_arduino_diag_after_nvs.py",
        "patch_arduino_libs_s3_wdt.py",
    ]
    for name in patches:
        try:
            r = subprocess.run([sys.executable, name], cwd=script_dir, capture_output=True, text=True, timeout=30)
            if r.stdout:
                print(r.stdout, end="")
            if r.returncode != 0 and r.stderr:
                print(r.stderr, file=sys.stderr)
        except Exception as e:
            print("[pre] WARN patch %s: %s" % (name, e), file=sys.stderr)
    # wroom-s3-test-psram : forcer recompilation des .o contenant earlyInitVariant (éviter cache sans patch).
    if pioenv == "wroom-s3-test-psram":
        build_dir = env.subst("$BUILD_DIR")
        for name in ("main.cpp.o", "esp32-hal-misc.c.o"):
            path = os.path.join(build_dir, "FrameworkArduino", name)
            if os.path.isfile(path):
                try:
                    os.remove(path)
                    print("[pre] removed %s to force rebuild" % path)
                except OSError as e:
                    print("[pre] WARN could not remove %s: %s" % (path, e), file=sys.stderr)
