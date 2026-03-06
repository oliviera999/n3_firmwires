#!/usr/bin/env python3
"""
Patch platformio/espressif32 so that custom_sdkconfig in env does NOT add "espidf"
to frameworks. That allows HandleArduinoIDFsettings to run (Arduino-only build with
custom sdkconfig), which regenerates sdkconfig.defaults with CONFIG_ESP_INT_WDT_*
and fixes the ESP32-S3 TG1WDT boot loop.

Run once before building wroom-s3-test, or from your workflow before pio run:
  python tools/patch_espressif32_custom_sdkconfig_only.py

Idempotent: safe to run multiple times.
"""
from __future__ import annotations

import os
import sys

# Platform path: core_dir/platforms/espressif32
def find_platform_dir():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    candidates = [
        os.path.join(project_dir, ".platformio", "platforms", "espressif32"),
        os.path.join(os.environ.get("PLATFORMIO_CORE_DIR", ""), "platforms", "espressif32"),
        os.path.join(os.path.expanduser("~"), ".platformio", "platforms", "espressif32"),
    ]
    for path in candidates:
        if path and os.path.isdir(path) and os.path.isfile(os.path.join(path, "platform.py")):
            return path
    return None

ORIGINAL = '        if custom_sdkconfig is not None or len(str(board_sdkconfig)) > 3:\n'
PATCHED  = '        if len(str(board_sdkconfig)) > 3:  # custom_sdkconfig alone: do not add espidf (S3 IWDT boot fix)\n'

# Diagnostic dans le builder Arduino (pour comprendre si call_compile_libs est appelé)
ARDUINO_ORIG = "if flag_custom_sdkconfig and not flag_any_custom_sdkconfig:\n    call_compile_libs()"
ARDUINO_PATCH = '''try:
    open(join(project_dir, "pio_arduino_flags.txt"), "w").write("flag_custom_sdkconfig=%s flag_any_custom_sdkconfig=%s\\n" % (flag_custom_sdkconfig, flag_any_custom_sdkconfig))
except Exception:
    pass
print("*** FFP5CS S3: flag_custom_sdkconfig=%s flag_any_custom_sdkconfig=%s" % (flag_custom_sdkconfig, flag_any_custom_sdkconfig))
if flag_custom_sdkconfig and not flag_any_custom_sdkconfig:
    call_compile_libs()'''

def main():
    platform_dir = find_platform_dir()
    if not platform_dir:
        print("patch_espressif32_custom_sdkconfig_only: platform espressif32 not found", file=sys.stderr)
        sys.exit(1)
    platform_py = os.path.join(platform_dir, "platform.py")
    with open(platform_py, "r", encoding="utf-8") as f:
        content = f.read()
    if PATCHED.strip() in content or "# custom_sdkconfig alone" in content:
        print("patch_espressif32_custom_sdkconfig_only: platform.py already patched")
    elif ORIGINAL in content:
        content = content.replace(ORIGINAL, PATCHED, 1)
        with open(platform_py, "w", encoding="utf-8") as f:
            f.write(content)
        print("patch_espressif32_custom_sdkconfig_only: patched platform.py (custom_sdkconfig will not add espidf)")
    else:
        print("patch_espressif32_custom_sdkconfig_only: platform.py format changed, skip", file=sys.stderr)

    # Patch diagnostic builder Arduino (écrit pio_arduino_flags.txt + print)
    arduino_py = os.path.join(platform_dir, "builder", "frameworks", "arduino.py")
    if os.path.isfile(arduino_py):
        with open(arduino_py, "r", encoding="utf-8") as f:
            ac = f.read()
        if "pio_arduino_flags.txt" in ac:
            print("patch_espressif32_custom_sdkconfig_only: arduino.py diagnostic (file) already present")
        elif ARDUINO_ORIG in ac:
            ac = ac.replace(ARDUINO_ORIG, ARDUINO_PATCH, 1)
            with open(arduino_py, "w", encoding="utf-8") as f:
                f.write(ac)
            print("patch_espressif32_custom_sdkconfig_only: added diagnostic (file+print) in arduino.py")
        elif "if flag_custom_sdkconfig and not flag_any_custom_sdkconfig:" in ac and "call_compile_libs()" in ac:
            # déjà patché avec print seulement → ajouter écriture fichier
            old_block = 'print("*** FFP5CS S3: flag_custom_sdkconfig=%s flag_any_custom_sdkconfig=%s" % (flag_custom_sdkconfig, flag_any_custom_sdkconfig))\nif flag_custom_sdkconfig and not flag_any_custom_sdkconfig:\n    call_compile_libs()'
            if old_block in ac:
                ac = ac.replace(old_block, ARDUINO_PATCH, 1)
                with open(arduino_py, "w", encoding="utf-8") as f:
                    f.write(ac)
                print("patch_espressif32_custom_sdkconfig_only: added file write to arduino.py diagnostic")
        else:
            print("patch_espressif32_custom_sdkconfig_only: arduino.py format changed, skip diagnostic", file=sys.stderr)
    return 0

if __name__ == "__main__":
    sys.exit(main())
