#!/usr/bin/env python3
"""
Patch le sdkconfig du variant qio_opi (framework Arduino ESP32) pour eviter TG1WDT au boot.
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
        print("[pio_s3_psram_patch_iwdt] framework dir not found")
        return
    sdk_h = os.path.join(pkg_dir, "tools", "sdk", "esp32s3", "qio_opi", "include", "sdkconfig.h")
    if not os.path.isfile(sdk_h):
        print("[pio_s3_psram_patch_iwdt] sdkconfig.h not found:", sdk_h)
        return
    with open(sdk_h, "r", encoding="utf-8") as f:
        content = f.read()
    changed = False
    # Desactiver IWDT : CONFIG_ESP_INT_WDT 1 -> 0
    content, n = re.subn(
        r"#define\s+CONFIG_ESP_INT_WDT\s+1\s*$",
        "#define CONFIG_ESP_INT_WDT 0",
        content,
        count=1,
        flags=re.MULTILINE,
    )
    if n:
        changed = True
    if changed:
        with open(sdk_h, "w", encoding="utf-8") as f:
            f.write(content)
        print("[pio_s3_psram_patch_iwdt] CONFIG_ESP_INT_WDT=0 (IWDT desactive, qio_opi)")
    else:
        print("[pio_s3_psram_patch_iwdt] already patched or pattern not found (qio_opi)")

patch_qio_opi_iwdt()
