"""
Upload WROOM (ESP32 classique) : flash jeu cohérent bootloader + partitions + otadata + firmware.

Évite le panic « Cache error » après erase quand PIO n'écrivait que firmware.bin (phase 2).
"""
from pathlib import Path

Import("env")

_MIN_FIRMWARE_BYTES = 1_200_000
_FLASH_BIN_ALIASES = {
    "bootloader.bin": ("bootloader.bin",),
    "partitions.bin": ("partitions.bin", "partition-table.bin"),
    "ota_data_initial.bin": ("ota_data_initial.bin", "boot_app0.bin"),
}


def _wroom_classic_env(pioenv):
    if not pioenv.startswith("wroom-"):
        return False
    if pioenv.startswith("wroom-s3") or pioenv == "wroom-tls-test":
        return False
    return True


def _project_dir():
    return Path(env.subst("$PROJECT_DIR"))


def _build_dir():
    return Path(env.subst("$BUILD_DIR"))


def _artifacts_dir(pioenv):
    return _project_dir() / ".pio_artifacts" / pioenv


def _resolve_bin(pioenv, canonical):
    names = _FLASH_BIN_ALIASES.get(canonical, (canonical,))
    for base in (_build_dir(), _artifacts_dir(pioenv)):
        if not base.is_dir():
            continue
        for name in names:
            direct = base / name
            if direct.is_file():
                return direct
        if base == _build_dir():
            for name in names:
                found = next(_build_dir().rglob(name), None)
                if found and found.is_file():
                    return found
    return None


def _quote(path):
    return '"%s"' % str(path).replace('"', '\\"')


def _resolve_esptool(env):
    for var in ("UPLOADER", "UPLOADTOOL"):
        try:
            path = env.subst("$" + var)
        except Exception:
            continue
        if path and Path(path).is_file():
            return path
    pkg = env.PioPlatform().get_package_dir("tool-esptoolpy")
    if pkg:
        for name in ("esptool.exe", "esptool.py", "esptool"):
            candidate = Path(pkg) / name
            if candidate.is_file():
                return str(candidate)
    raise RuntimeError("FFP5CS upload: esptool introuvable (tool-esptoolpy)")


def before_upload(source, target, env):
    pioenv = env.get("PIOENV", "")
    if not _wroom_classic_env(pioenv):
        return

    firmware = _build_dir() / "firmware.bin"
    if not firmware.is_file():
        raise RuntimeError("FFP5CS upload: firmware.bin introuvable dans BUILD_DIR")

    fw_size = firmware.stat().st_size
    if fw_size < _MIN_FIRMWARE_BYTES:
        raise RuntimeError(
            "FFP5CS upload: firmware.bin trop petit (%d o) — build pioarduino incomplet (phase 1 stub ?)"
            % fw_size
        )

    bootloader = _resolve_bin(pioenv, "bootloader.bin")
    partitions = _resolve_bin(pioenv, "partitions.bin")
    ota = _resolve_bin(pioenv, "ota_data_initial.bin")

    if bootloader is None or partitions is None:
        raise RuntimeError(
            "FFP5CS upload: bootloader.bin ou partitions.bin manquant — "
            "lancer un build complet (pio run -e %s) avant flash" % pioenv
        )

    pairs = [
        ("0x1000", bootloader),
        ("0x8000", partitions),
    ]
    if ota is not None:
        pairs.append(("0xe000", ota))
    pairs.append(("0x10000", firmware))

    esptool = _resolve_esptool(env)
    pythonexe = env.subst("$PYTHONEXE")
    upload_port = env.subst("$UPLOAD_PORT")
    speed = env.get("UPLOAD_SPEED", 921600)

    # esptool.exe (v5) : invocation directe ; esptool.py : via python.
    if esptool.lower().endswith(".py"):
        launcher = [_quote(pythonexe), _quote(esptool)]
    else:
        launcher = [_quote(esptool)]

    parts = launcher + [
        "--chip", "esp32",
        "--port", _quote(upload_port),
        "--baud", str(speed),
        "--before", "default-reset",
        "--after", "hard-reset",
        "write-flash",
        "-z",
        "--flash-mode", "dio",
        "--flash-freq", "40m",
        "--flash-size", "4MB",
    ]
    for addr, path in pairs:
        parts.extend([addr, _quote(path)])

    cmd = " ".join(parts)
    print("[pre-script] FFP5CS: upload bundle WROOM (%s)" % pioenv)
    print("[pre-script]   bootloader: %s" % bootloader)
    print("[pre-script]   partitions: %s" % partitions)
    if ota is not None:
        print("[pre-script]   otadata: %s" % ota)
    print("[pre-script]   firmware: %s (%d o)" % (firmware, fw_size))

    env.Replace(UPLOADCMD=cmd)


env.AddPreAction("upload", before_upload)
