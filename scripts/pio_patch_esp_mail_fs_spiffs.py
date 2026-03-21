# -*- coding: utf-8 -*-
"""
Patch ESP Mail Client : sur ESP32 Arduino 3.x, ESP_Mail_FS.h force LittleFS par defaut,
or les firmwares n3pp/msp utilisent la partition min_spiffs (SPIFFS uniquement).

Remplace le bloc ESP_ARDUINO_VERSION (LittleFS) par SPIFFS une seule fois dans
.pio/libdeps/<env>/ESP Mail Client/src/ESP_Mail_FS.h.

Marqueur : N3_IOT_ESP_MAIL_SPIFFS_PATCH dans le fichier (idempotent).
"""
from __future__ import print_function

import os
import re
from pathlib import Path

Import("env")

MARKER = "N3_IOT_ESP_MAIL_SPIFFS_PATCH"


def _patch():
    proj = env.subst("$PROJECT_DIR")
    pioenv = env.get("PIOENV", "")
    path = Path(proj) / ".pio" / "libdeps" / pioenv / "ESP Mail Client" / "src" / "ESP_Mail_FS.h"
    if not path.is_file():
        print("[pre] ESP_Mail patch: fichier absent (premier build ?):", path)
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    if MARKER in text:
        return
    # Bloc tel que mobizt/ESP Mail Client 3.4.x (Arduino-ESP32 3.x)
    pattern = re.compile(
        r"#elif defined\(ESP_ARDUINO_VERSION\) /\* ESP32 core >= v2\.0\.x \*/ /\* ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL\(2, 0, 0\) \*/\s*\n"
        r"\s*#include <LittleFS\.h>\s*\n"
        r"\s*#define ESP_MAIL_DEFAULT_FLASH_FS LittleFS\s*\n",
        re.MULTILINE,
    )
    repl = (
        "#elif defined(ESP_ARDUINO_VERSION) /* ESP32 core >= v2.0.x */ /* %s : SPIFFS (min_spiffs) */\n"
        "#include <SPIFFS.h>\n"
        "#define ESP_MAIL_DEFAULT_FLASH_FS SPIFFS\n" % MARKER
    )
    new, n = pattern.subn(repl, text, count=1)
    if n:
        path.write_text(new, encoding="utf-8")
        print("[pre] ESP_Mail_FS.h patche -> SPIFFS:", path)
    else:
        print("[pre] WARN ESP_Mail_FS.h: motif LittleFS introuvable (version lib differente ?)")


_patch()
