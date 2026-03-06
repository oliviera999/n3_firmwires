#!/usr/bin/env python3
"""
Patch le sdkconfig du variant qio_opi (framework Arduino ESP32) pour eviter TG1WDT au boot
lors du build wroom-s3-test-psram (boot avec PSRAM depasse 300 ms).
Desactive l'IWDT (CONFIG_ESP_INT_WDT=0) pour que le boot et les tests PSRAM ne declenchent pas de reset.
"""
from __future__ import annotations

import os
import re

Import("env")

def patch_qio_opi_iwdt():
    platform = env.PioPlatform()
    pkg_dir = platform.get_package_dir("framework-arduinoespressif32")
    if not pkg_dir or not os.path.isdir(pkg_dir):
        print("[pio_s3_psram_patch_iwdt] ERREUR: framework dir not found (framework-arduinoespressif32)")
        return
    sdk_h = os.path.join(pkg_dir, "tools", "sdk", "esp32s3", "qio_opi", "include", "sdkconfig.h")
    if not os.path.isfile(sdk_h):
        print("[pio_s3_psram_patch_iwdt] ERREUR: sdkconfig.h qio_opi absent:", sdk_h)
        print("[pio_s3_psram_patch_iwdt] Verifiez que la plateforme fournit le variant qio_opi pour esp32s3.")
        return
    with open(sdk_h, "r", encoding="utf-8") as f:
        content = f.read()
    changed = False
    # Desactiver IWDT : CONFIG_ESP_INT_WDT 1 -> 0
    has_iwdt_1 = bool(re.search(r"#define\s+CONFIG_ESP_INT_WDT\s+1\s*$", content, re.MULTILINE))
    content, n = re.subn(
        r"#define\s+CONFIG_ESP_INT_WDT\s+1\s*$",
        "#define CONFIG_ESP_INT_WDT 0",
        content,
        count=1,
        flags=re.MULTILINE,
    )
    if n:
        changed = True
    # TG0WDT : desactiver le TWDT au boot (app.cpp le reinit en setup() avec 300s).
    has_twdt_1 = bool(re.search(r"#define\s+CONFIG_ESP_TASK_WDT\s+1\s*$", content, re.MULTILINE))
    content, n_twdt = re.subn(
        r"#define\s+CONFIG_ESP_TASK_WDT\s+1\s*$",
        "#define CONFIG_ESP_TASK_WDT 0",
        content,
        count=1,
        flags=re.MULTILINE,
    )
    if n_twdt:
        changed = True
    # TWDT timeout 5s -> 300s (si encore 5) pour laisser le temps au boot (nvs, PSRAM).
    has_timeout_5 = bool(re.search(r"#define\s+CONFIG_ESP_TASK_WDT_TIMEOUT_S\s+5\s*$", content, re.MULTILINE))
    content, n2 = re.subn(
        r"#define\s+CONFIG_ESP_TASK_WDT_TIMEOUT_S\s+5\s*$",
        "#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 300",
        content,
        count=1,
        flags=re.MULTILINE,
    )
    if n2:
        changed = True
    if changed:
        with open(sdk_h, "w", encoding="utf-8") as f:
            f.write(content)
        msg = "[pio_s3_psram_patch_iwdt] qio_opi sdkconfig.h:"
        if n:
            msg += " CONFIG_ESP_INT_WDT=0;"
        if n_twdt:
            msg += " CONFIG_ESP_TASK_WDT=0;"
        if n2:
            msg += " CONFIG_ESP_TASK_WDT_TIMEOUT_S=300;"
        print(msg)
    else:
        parts = []
        if not has_iwdt_1:
            parts.append("CONFIG_ESP_INT_WDT non trouve ou deja 0")
        if not has_twdt_1:
            parts.append("CONFIG_ESP_TASK_WDT non trouve ou deja 0")
        if not has_timeout_5:
            parts.append("CONFIG_ESP_TASK_WDT_TIMEOUT_S non 5 ou absent")
        print("[pio_s3_psram_patch_iwdt] deja patche ou motif absent (qio_opi): " + "; ".join(parts))

patch_qio_opi_iwdt()
