#!/usr/bin/env python3
"""
Upload ESP32 avec reset de la partition otadata.
Enchaîne : write_flash (sans reset) -> erase_region otadata (0xE000) -> run.
Garantit qu'après un flash USB le prochain boot se fait depuis app0 (firmware flashé).
Inspiré de ffp5cs/tools/force_ota_partition.py, intégré à l'upload.
"""
import os
import subprocess
import sys

OTADATA_OFFSET = "0xE000"
OTADATA_SIZE = "0x2000"


def run_esptool(port, args, check=True):
    cmd = ["python", "-m", "esptool", "--port", port] + args
    print("[upload_otadata] " + " ".join(cmd))
    r = subprocess.run(cmd)
    if check and r.returncode != 0:
        sys.exit(r.returncode)
    return r.returncode


def main():
    if len(sys.argv) >= 3:
        port = sys.argv[1]
        build_dir = sys.argv[2]
    else:
        port = os.environ.get("UPLOAD_PORT", "")
        build_dir = os.environ.get("PROJECT_BUILD_DIR", "")
    if not port or not build_dir:
        print("Usage: upload_esp32_otadata_reset.py <UPLOAD_PORT> <PROJECT_BUILD_DIR>", file=sys.stderr)
        print("  or set UPLOAD_PORT and PROJECT_BUILD_DIR", file=sys.stderr)
        sys.exit(1)

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")
    for p in (bootloader, partitions, firmware):
        if not os.path.isfile(p):
            print("Fichier manquant: " + p, file=sys.stderr)
            sys.exit(1)

    # 1) write-flash sans reset pour garder le chip en bootloader (--after avant la sous-commande)
    run_esptool(port, [
        "--after", "no-reset",
        "write-flash",
        "0x1000", bootloader,
        "0x8000", partitions,
        "0x10000", firmware,
    ])
    # 2) effacer otadata -> prochain boot sera app0
    run_esptool(port, ["erase-region", OTADATA_OFFSET, OTADATA_SIZE])
    # 3) reset
    run_esptool(port, ["run"])
    print("[upload_otadata] OK: firmware flashe, otadata effacee, boot sur app0 au prochain demarrage.")


if __name__ == "__main__":
    main()
