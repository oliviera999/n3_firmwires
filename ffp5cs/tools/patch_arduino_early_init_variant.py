#!/usr/bin/env python3
"""
Patch le core Arduino (framework-arduinoespressif32) pour appeler earlyInitVariant()
au tout début de initArduino(), avant nvs_flash_init(). Permet de désactiver le
Task WDT (TG0WDT) avant les init longues sur ESP32-S3.

À exécuter après installation/mise à jour du package framework Arduino.
Idempotent.
"""
from __future__ import annotations

import os
import sys

def find_framework_dirs():
    """Retourne la liste des chemins framework-arduinoespressif32 (cores/esp32) trouvés."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    candidates = [
        os.path.join(project_dir, ".platformio", "packages"),
        os.path.join(os.environ.get("PLATFORMIO_CORE_DIR", ""), "packages"),
        os.path.join(os.path.expanduser("~"), ".platformio", "packages"),
    ]
    found = []
    for pkg_dir in candidates:
        if not pkg_dir or not os.path.isdir(pkg_dir):
            continue
        for name in os.listdir(pkg_dir):
            if name.startswith("framework-arduinoespressif32") and not name.endswith("-libs"):
                path = os.path.join(pkg_dir, name)
                hal_misc = os.path.join(path, "cores", "esp32", "esp32-hal-misc.c")
                if os.path.isfile(hal_misc):
                    found.append(path)
    return found

# Déclaration weak + défaut vide (C, donc void f(void))
EARLY_INIT_BLOCK = (
    "void earlyInitVariant(void) __attribute__((weak));\n"
    "void earlyInitVariant(void) {}\n"
)
INIT_VARIANT_BLOCK = "void initVariant() __attribute__((weak));\nvoid initVariant() {}"
# Plusieurs formats possibles selon la version du core
INIT_ARDUINO_OPEN_1 = "void initArduino() {"
INIT_ARDUINO_OPEN_2 = "void initArduino()\n{"
INIT_ARDUINO_OPEN_PATCH_1 = "void initArduino() {\n  earlyInitVariant();"
INIT_ARDUINO_OPEN_PATCH_2 = "void initArduino()\n{\n  earlyInitVariant();"

def patch_one(hal_misc):
    with open(hal_misc, "r", encoding="utf-8") as f:
        content = f.read()
    changes = []
    if "earlyInitVariant" not in content:
        if INIT_VARIANT_BLOCK in content:
            content = content.replace(
                INIT_VARIANT_BLOCK,
                EARLY_INIT_BLOCK + "\n" + INIT_VARIANT_BLOCK,
                1
            )
            changes.append("decl earlyInitVariant")
        else:
            content = EARLY_INIT_BLOCK + "\n" + content
            changes.append("decl earlyInitVariant")
    if INIT_ARDUINO_OPEN_PATCH_2 not in content and INIT_ARDUINO_OPEN_PATCH_1 not in content:
        if INIT_ARDUINO_OPEN_2 in content:
            content = content.replace(INIT_ARDUINO_OPEN_2, INIT_ARDUINO_OPEN_PATCH_2, 1)
            changes.append("call earlyInitVariant() at start of initArduino()")
        elif INIT_ARDUINO_OPEN_1 in content:
            content = content.replace(INIT_ARDUINO_OPEN_1, INIT_ARDUINO_OPEN_PATCH_1, 1)
            changes.append("call earlyInitVariant() at start of initArduino()")
    if changes:
        with open(hal_misc, "w", encoding="utf-8") as f:
            f.write(content)
        return changes
    return None

def main():
    fw_dirs = find_framework_dirs()
    if not fw_dirs:
        print("patch_arduino_early_init_variant: framework-arduinoespressif32 (cores/esp32) non trouvé", file=sys.stderr)
        return 1
    patched = 0
    for fw_dir in fw_dirs:
        hal_misc = os.path.join(fw_dir, "cores", "esp32", "esp32-hal-misc.c")
        changes = patch_one(hal_misc)
        if changes:
            print("patch_arduino_early_init_variant: appliqué sur", hal_misc, ":", ", ".join(changes))
            patched += 1
        else:
            print("patch_arduino_early_init_variant: déjà appliqué ou format inattendu:", hal_misc)
    if patched == 0 and len(fw_dirs) > 0:
        print("patch_arduino_early_init_variant: aucun fichier à patcher (tous déjà à jour)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
