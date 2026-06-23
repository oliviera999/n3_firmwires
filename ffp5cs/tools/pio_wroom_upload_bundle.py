"""
Upload WROOM (ESP32 classique) : flash jeu cohérent bootloader + partitions + otadata + firmware.

Évite le panic « Cache error » après erase quand bootloader/partitions obsolètes restent dans
BUILD_DIR alors que firmware.bin vient d'une phase 2 Arduino récente.
"""
import sys
from pathlib import Path

Import("env")

_tools = Path(env.subst("$PROJECT_DIR")) / "tools"
if str(_tools) not in sys.path:
    sys.path.insert(0, str(_tools))

from pio_flash_bundle import resolve_bundle_paths, write_bundle_manifest  # noqa: E402

_MIN_FIRMWARE_BYTES = 1_200_000


def _wroom_classic_env(pioenv):
    if not pioenv.startswith("wroom-"):
        return False
    if pioenv.startswith("wroom-s3") or pioenv == "wroom-tls-test":
        return False
    return True


def _build_dir():
    return Path(env.subst("$BUILD_DIR"))


def _artifacts_dir(pioenv):
    return Path(env.subst("$PROJECT_DIR")) / ".pio_artifacts" / pioenv


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

    build_dir = _build_dir()
    artifacts = _artifacts_dir(pioenv)
    bootloader, partitions, ota, firmware = resolve_bundle_paths(
        build_dir, pioenv, artifacts if artifacts.is_dir() else None
    )

    fw_size = firmware.stat().st_size
    if fw_size < _MIN_FIRMWARE_BYTES:
        raise RuntimeError(
            "FFP5CS upload: firmware.bin trop petit (%d o) — build pioarduino incomplet (phase 1 stub ?)"
            % fw_size
        )

    write_bundle_manifest(build_dir, pioenv)

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
