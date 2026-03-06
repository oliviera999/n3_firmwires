#!/usr/bin/env python3
"""
Patch le core Arduino (framework-arduinoespressif32) pour appeler ffp5cs_diag_after_nvs()
juste après le bloc NVS dans initArduino(). Diagnostic boot S3 (localiser blocage).

À exécuter après patch_arduino_early_init_variant.py. Idempotent.
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

# Weak + défaut vide (C)
DIAG_BEFORE_NVS_BLOCK = (
    "void ffp5cs_diag_before_nvs(void) __attribute__((weak));\n"
    "void ffp5cs_diag_before_nvs(void) {}\n"
)
DIAG_AFTER_NVS_BLOCK = (
    "void ffp5cs_diag_after_nvs(void) __attribute__((weak));\n"
    "void ffp5cs_diag_after_nvs(void) {}\n"
)
INIT_VARIANT_BLOCK = "void initVariant() __attribute__((weak));\nvoid initVariant() {}"

# Après bloc PSRAM, avant APP_ROLLBACK — pour localiser blocage psramAddToHeap
DIAG_AFTER_PSRAM_BLOCK = (
    "void ffp5cs_diag_after_psram(void) __attribute__((weak));\n"
    "void ffp5cs_diag_after_psram(void) {}\n"
)
AFTER_PSRAM_PATTERNS = (
    ('#endif\n#endif\n#ifdef CONFIG_APP_ROLLBACK_ENABLE', '#endif\n#endif\n  ffp5cs_diag_after_psram();\n#ifdef CONFIG_APP_ROLLBACK_ENABLE'),
)
# Avant nvs_flash_init() — pour localiser blocage (PSRAM/APP_ROLLBACK vs NVS)
BEFORE_NVS_PATTERNS = (
    ('  esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);\n  esp_err_t err = nvs_flash_init();', '  esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);\n  ffp5cs_diag_before_nvs();\n  esp_err_t err = nvs_flash_init();'),
    ('esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);\n  esp_err_t err = nvs_flash_init();', 'esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);\n  ffp5cs_diag_before_nvs();\n  esp_err_t err = nvs_flash_init();'),
)

# Après le bloc "if (err) { log_e(...); }" dans initArduino(), avant #if BLUEDROID / #ifdef CONFIG_BT
# (pioarduino @src- utilise 2 espaces et CONFIG_BLUEDROID_ENABLED)
NVS_PATTERNS = (
    ('  if (err) {\n    log_e("Failed to initialize NVS! Error: %u", err);\n  }\n#if (defined(CONFIG_BLUEDROID_ENABLED)', '  if (err) {\n    log_e("Failed to initialize NVS! Error: %u", err);\n  }\n  ffp5cs_diag_after_nvs();\n#if (defined(CONFIG_BLUEDROID_ENABLED)'),
    ('log_e("Failed to initialize NVS! Error: %u", err);\n    }\n#ifdef CONFIG_BT_ENABLED', 'log_e("Failed to initialize NVS! Error: %u", err);\n    }\n    ffp5cs_diag_after_nvs();\n#ifdef CONFIG_BT_ENABLED'),
    ('if (err) {\n  log_e("Failed to initialize NVS! Error: %u", err);\n}\n#if', 'if (err) {\n  log_e("Failed to initialize NVS! Error: %u", err);\n}\n  ffp5cs_diag_after_nvs();\n#if'),
    ('if (err) {\n log_e("Failed to initialize NVS! Error: %u", err);\n}\n#if', 'if (err) {\n log_e("Failed to initialize NVS! Error: %u", err);\n}\n ffp5cs_diag_after_nvs();\n#if'),
    ('log_e("Failed to initialize NVS! Error: %u", err);\n }\n#if', 'log_e("Failed to initialize NVS! Error: %u", err);\n }\n ffp5cs_diag_after_nvs();\n#if'),
)

def patch_one(hal_misc):
    with open(hal_misc, "r", encoding="utf-8") as f:
        content = f.read()
    changes = []
    # Déclaration after_psram (localiser blocage psramAddToHeap)
    if "ffp5cs_diag_after_psram" not in content:
        if INIT_VARIANT_BLOCK in content:
            content = content.replace(INIT_VARIANT_BLOCK, INIT_VARIANT_BLOCK + "\n" + DIAG_AFTER_PSRAM_BLOCK, 1)
        else:
            content = DIAG_AFTER_PSRAM_BLOCK + "\n" + content
        changes.append("decl ffp5cs_diag_after_psram")
    # Appel after_psram (après bloc PSRAM)
    if "ffp5cs_diag_after_psram();" not in content:
        for old, new in AFTER_PSRAM_PATTERNS:
            if old in content:
                content = content.replace(old, new, 1)
                changes.append("call ffp5cs_diag_after_psram() after PSRAM block")
                break
    # Déclarations before_nvs + after_nvs
    if "ffp5cs_diag_before_nvs" not in content:
        insert = DIAG_BEFORE_NVS_BLOCK
        if "ffp5cs_diag_after_nvs" not in content:
            insert += "\n" + DIAG_AFTER_NVS_BLOCK
        if INIT_VARIANT_BLOCK in content:
            content = content.replace(INIT_VARIANT_BLOCK, INIT_VARIANT_BLOCK + "\n" + insert, 1)
        else:
            content = insert + "\n" + content
        changes.append("decl ffp5cs_diag_before_nvs" + (" + ffp5cs_diag_after_nvs" if "ffp5cs_diag_after_nvs" in insert else ""))
    elif "ffp5cs_diag_after_nvs" not in content:
        if INIT_VARIANT_BLOCK in content:
            content = content.replace(INIT_VARIANT_BLOCK, INIT_VARIANT_BLOCK + "\n" + DIAG_AFTER_NVS_BLOCK, 1)
        else:
            content = DIAG_AFTER_NVS_BLOCK + "\n" + content
        changes.append("decl ffp5cs_diag_after_nvs")
    # Appel before_nvs (avant nvs_flash_init)
    if "ffp5cs_diag_before_nvs();" not in content:
        for old, new in BEFORE_NVS_PATTERNS:
            if old in content:
                content = content.replace(old, new, 1)
                changes.append("call ffp5cs_diag_before_nvs() before nvs_flash_init")
                break
    if "ffp5cs_diag_after_nvs();" not in content:
        for old, new in NVS_PATTERNS:
            if old in content:
                content = content.replace(old, new, 1)
                changes.append("call ffp5cs_diag_after_nvs() after NVS block")
                break
    if changes:
        with open(hal_misc, "w", encoding="utf-8") as f:
            f.write(content)
        return changes
    return None

def main():
    fw_dirs = find_framework_dirs()
    if not fw_dirs:
        print("patch_arduino_diag_after_nvs: framework-arduinoespressif32 (cores/esp32) non trouvé", file=sys.stderr)
        return 1
    patched = 0
    for fw_dir in fw_dirs:
        hal_misc = os.path.join(fw_dir, "cores", "esp32", "esp32-hal-misc.c")
        changes = patch_one(hal_misc)
        if changes:
            print("patch_arduino_diag_after_nvs: appliqué sur", hal_misc, ":", ", ".join(changes))
            patched += 1
        else:
            print("patch_arduino_diag_after_nvs: déjà appliqué ou format inattendu:", hal_misc)
    if patched == 0 and len(fw_dirs) > 0:
        print("patch_arduino_diag_after_nvs: aucun fichier à patcher (tous déjà à jour)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
