# -*- coding: utf-8 -*-
"""Post-build (@buildprog) : refuse firmware WROOM trop petit ou sdkconfig SPIRAM incoherent."""
from __future__ import print_function

import re
import sys
from pathlib import Path

Import("env")  # noqa: F821

MIN_FIRMWARE_BYTES = 1200000
SKIP_ENVS = frozenset(
    {
        "wroom-tls-test",
        "wroom-s3-prod",
        "wroom-s3-test",
        "wroom-s3-test-psram",
        "wroom-s3-test-psram-v2",
        "wroom-s3-test-devkit",
        "wroom-s3-test-usb",
    }
)


def verify_wroom_sdkconfig(source, target, env):
    pioenv = env.get("PIOENV", "")
    if not pioenv.startswith("wroom-"):
        return
    if pioenv in SKIP_ENVS:
        return

    build_dir = Path(env.subst("$BUILD_DIR"))
    firmware = build_dir / "firmware.bin"
    errors = []

    def _read_sdkconfig_text():
        parts = []
        for rel in ("sdkconfig", "config/sdkconfig.h"):
            p = build_dir / rel
            if p.is_file():
                try:
                    parts.append(p.read_text(encoding="utf-8", errors="ignore"))
                except OSError:
                    pass
        artifacts = Path(env.subst("$PROJECT_DIR")) / ".pio_artifacts" / pioenv / "config" / "sdkconfig.h"
        if artifacts.is_file():
            try:
                parts.append(artifacts.read_text(encoding="utf-8", errors="ignore"))
            except OSError:
                pass
        return "\n".join(parts)

    if firmware.is_file():
        size = firmware.stat().st_size
        print("[verify-wroom] firmware.bin: %d octets" % size)
        if size < MIN_FIRMWARE_BYTES:
            errors.append(
                "firmware.bin trop petit (%d < %d): build incomplet, ne pas flasher"
                % (size, MIN_FIRMWARE_BYTES)
            )
    else:
        errors.append("firmware.bin introuvable: %s" % firmware)

    if not list(build_dir.rglob("app.cpp.o")):
        errors.append(
            "app.cpp.o absent: phase 2 FFP5CS non compilee (pioarduino). Rebuild complet requis."
        )

    text = _read_sdkconfig_text()
    if text:
        if re.search(r"(?m)^\s*CONFIG_ESP32_SPIRAM_SUPPORT=y", text):
            errors.append("CONFIG_ESP32_SPIRAM_SUPPORT=y dans sdkconfig du build")
        if re.search(r"(?m)^\s*CONFIG_SPIRAM_SUPPORT=y", text):
            errors.append("CONFIG_SPIRAM_SUPPORT=y dans sdkconfig du build")
        if re.search(r"(?m)^\s*CONFIG_SPIRAM_CACHE_WORKAROUND=y", text):
            errors.append("CONFIG_SPIRAM_CACHE_WORKAROUND=y dans sdkconfig du build")
        m = re.search(r"CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=(\d+)", text)
        if m and m.group(1) != "240":
            errors.append("CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=%s (attendu 240)" % m.group(1))

    if errors:
        print("[verify-wroom] ECHEC env=%s:" % pioenv)
        for e in errors:
            print("  -", e)
        env.Exit(1)

    print("[verify-wroom] OK env=%s" % pioenv)


env.AddPostAction("buildprog", verify_wroom_sdkconfig)
