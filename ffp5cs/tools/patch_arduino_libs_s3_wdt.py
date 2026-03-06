#!/usr/bin/env python3
"""
Patch le sdkconfig source du package framework-arduinoespressif32-libs (ESP32-S3)
pour CONFIG_ESP_INT_WDT_TIMEOUT_MS=300000, CONFIG_ESP_TASK_WDT_TIMEOUT_S=300,
et CONFIG_ESP_TASK_WDT_INIT=n (TG0WDT boot loop: ne pas démarrer le TWDT au boot).
Cela ne change pas les .a précompilés ; pour que le correctif soit pris en compte
il faut que la plateforme recompile la lib Arduino (HandleArduinoIDFsettings).
Exécuter avant un build S3 : python tools/patch_arduino_libs_s3_wdt.py
"""
from __future__ import annotations

import os
import sys

def find_libs_dir():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    candidates = [
        os.path.join(project_dir, ".platformio", "packages", "framework-arduinoespressif32-libs"),
        os.path.join(os.environ.get("PLATFORMIO_CORE_DIR", ""), "packages", "framework-arduinoespressif32-libs"),
        os.path.join(os.path.expanduser("~"), ".platformio", "packages", "framework-arduinoespressif32-libs"),
    ]
    for path in candidates:
        if path and os.path.isdir(path):
            s3_sdk = os.path.join(path, "esp32s3", "sdkconfig")
            if os.path.isfile(s3_sdk):
                return path
    return None

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    libs_dir = find_libs_dir()
    if not libs_dir:
        print("patch_arduino_libs_s3_wdt: framework-arduinoespressif32-libs/esp32s3 non trouvé", file=sys.stderr)
        sys.exit(1)
    s3_sdk = os.path.join(libs_dir, "esp32s3", "sdkconfig")
    sdk_root = os.path.join(libs_dir, "sdkconfig")
    with open(s3_sdk, "r", encoding="utf-8") as f:
        content = f.read()
    changes = []
    if "CONFIG_ESP_INT_WDT_TIMEOUT_MS=300\n" in content and "CONFIG_ESP_INT_WDT_TIMEOUT_MS=300000" not in content:
        content = content.replace("CONFIG_ESP_INT_WDT_TIMEOUT_MS=300\n", "CONFIG_ESP_INT_WDT_TIMEOUT_MS=300000\n", 1)
        changes.append("CONFIG_ESP_INT_WDT_TIMEOUT_MS=300000")
    if "CONFIG_ESP_TASK_WDT_TIMEOUT_S=5\n" in content and "CONFIG_ESP_TASK_WDT_TIMEOUT_S=300" not in content:
        content = content.replace("CONFIG_ESP_TASK_WDT_TIMEOUT_S=5\n", "CONFIG_ESP_TASK_WDT_TIMEOUT_S=300\n", 1)
        changes.append("CONFIG_ESP_TASK_WDT_TIMEOUT_S=300")
    # TG0WDT boot loop: ne pas démarrer le TWDT au boot (on le réinit en setup() avec 300s)
    if "CONFIG_ESP_TASK_WDT_INIT=y\n" in content and "# CONFIG_ESP_TASK_WDT_INIT is not set" not in content:
        content = content.replace("CONFIG_ESP_TASK_WDT_INIT=y\n", "# CONFIG_ESP_TASK_WDT_INIT is not set\n", 1)
        changes.append("CONFIG_ESP_TASK_WDT_INIT=n")
    # Coredump: désactiver (pas de partition coredump, évite blocage au boot)
    if "CONFIG_ESP_COREDUMP_ENABLE=y\n" in content and "# CONFIG_ESP_COREDUMP_ENABLE is not set" not in content:
        content = content.replace("CONFIG_ESP_COREDUMP_ENABLE=y\n", "# CONFIG_ESP_COREDUMP_ENABLE is not set\n", 1)
        changes.append("CONFIG_ESP_COREDUMP_ENABLE=n")
    if changes:
        with open(s3_sdk, "w", encoding="utf-8") as f:
            f.write(content)
        print("patch_arduino_libs_s3_wdt: appliqué sur esp32s3/sdkconfig:", ", ".join(changes))
    else:
        print("patch_arduino_libs_s3_wdt: esp32s3/sdkconfig inchangé (déjà appliqué ou format inattendu)")
    # Ne PAS avoir sdkconfig à la racine: flag_any_custom_sdkconfig=False
    # permet call_compile_libs() direct (CONFIG_SPIRAM_BOOT_INIT, etc.).
    removed = False
    if os.path.isfile(sdk_root):
        os.remove(sdk_root)
        print("patch_arduino_libs_s3_wdt: supprimé sdkconfig racine package (force call_compile_libs)")
        removed = True
    proj_libs = os.path.join(project_dir, ".platformio", "packages", "framework-arduinoespressif32-libs", "sdkconfig")
    if os.path.isfile(proj_libs):
        os.remove(proj_libs)
        print("patch_arduino_libs_s3_wdt: supprimé sdkconfig racine projet")
        removed = True
    if not removed:
        print("patch_arduino_libs_s3_wdt: pas de sdkconfig racine à supprimer (build S3 utilisera call_compile_libs si clean)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
