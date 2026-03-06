#!/usr/bin/env python3
"""
Patch main.cpp du core Arduino pour appeler earlyInitVariant() au tout debut de
app_main(), avant Serial.begin() / USB, afin d'eviter TG0WDT pendant les inits longues.
Idempotent.
"""
from __future__ import annotations

import os
import sys

def find_framework_dirs():
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
                main_cpp = os.path.join(path, "cores", "esp32", "main.cpp")
                if os.path.isfile(main_cpp):
                    found.append(path)
    return found

# Déclaration + appel au début de app_main (earlyInitVariant fourni par l'app)
APP_MAIN_OPEN_1 = 'extern "C" void app_main()\n{'
APP_MAIN_PATCH_1 = (
    'extern "C" void earlyInitVariant(void);\n'
    'extern "C" void app_main()\n'
    '{\n'
    '    earlyInitVariant();\n'
)

def patch_one(main_cpp):
    with open(main_cpp, "r", encoding="utf-8") as f:
        content = f.read()
    if "earlyInitVariant();" in content and content.find("earlyInitVariant();") < content.find("initArduino()"):
        return None  # déjà patché
    if APP_MAIN_PATCH_1 in content:
        return None
    if APP_MAIN_OPEN_1 not in content:
        # Diagnostic si format inattendu (framework 3.x peut varier)
        idx = content.find("app_main")
        if idx >= 0:
            snippet = content[max(0, idx - 40) : idx + 80].replace("\n", "\\n")
            print("patch_arduino_app_main_early_wdt: format inattendu (motif app_main()\\n{{ non trouvé). Extrait: ...%s..." % snippet, file=sys.stderr)
        else:
            print("patch_arduino_app_main_early_wdt: app_main() introuvable dans %s" % main_cpp, file=sys.stderr)
        return None
    content = content.replace(APP_MAIN_OPEN_1, APP_MAIN_PATCH_1, 1)
    with open(main_cpp, "w", encoding="utf-8") as f:
        f.write(content)
    return ["call earlyInitVariant() at start of app_main()"]

def main():
    fw_dirs = find_framework_dirs()
    if not fw_dirs:
        print("patch_arduino_app_main_early_wdt: framework non trouvé", file=sys.stderr)
        return 1
    patched = 0
    for fw_dir in fw_dirs:
        main_cpp = os.path.join(fw_dir, "cores", "esp32", "main.cpp")
        changes = patch_one(main_cpp)
        if changes:
            print("patch_arduino_app_main_early_wdt: appliqué sur", main_cpp, ":", ", ".join(changes))
            patched += 1
        else:
            print("patch_arduino_app_main_early_wdt: déjà appliqué ou format inattendu:", main_cpp)
    return 0

if __name__ == "__main__":
    sys.exit(main())
