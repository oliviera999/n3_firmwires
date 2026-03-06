#!/usr/bin/env python3
"""
Utilitaire pour ouvrir le port série en forçant sa libération si occupé (Windows).
À utiliser par les scripts Python qui ouvrent le port COM directement.
"""

import os
import platform
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial  # type: ignore
except ImportError:
    serial = None


def _get_project_root() -> Path:
    """Racine du projet (répertoire contenant platformio.ini)."""
    p = Path(__file__).resolve()
    # tools/monitor/serial_utils.py -> remonter de 2 niveaux
    for _ in range(2):
        p = p.parent
    return p


def release_com_port_windows(port: str, baud: int = 115200) -> bool:
    """
    Appelle le script PowerShell Release-ComPort.ps1 pour libérer le port.
    Retourne True si le script a été invoqué, False sinon (ex. non-Windows).
    """
    if platform.system() != "Windows":
        return False
    root = _get_project_root()
    script = root / "scripts" / "Release-ComPort.ps1"
    if not script.exists():
        return False
    try:
        cmd = [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-Command",
            f"& {{ . '{script}'; Release-ComPortIfNeeded -Port '{port}' -Baud {baud} }}",
        ]
        subprocess.run(cmd, capture_output=True, timeout=30, check=False)
        return True
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
        return False


def open_serial_with_release(port: str, baudrate: int = 115200, **kwargs):
    """
    Ouvre le port série. Si l'ouverture échoue (port occupé), tente de libérer
    le port via le script PowerShell (Windows) puis réessaie une fois.

    Retourne une instance serial.Serial (le gestionnaire de contexte ferme le port).
    """
    if serial is None:
        raise RuntimeError("pyserial is required. Install with: pip install pyserial")

    def open_once():
        return serial.Serial(port, baudrate=baudrate, **kwargs)

    try:
        return open_once()
    except (OSError, serial.SerialException) as e:
        err_msg = str(e).lower()
        if "access is denied" in err_msg or "could not open port" in err_msg or "permission" in err_msg:
            if platform.system() == "Windows" and release_com_port_windows(port, baudrate):
                time.sleep(4)
                return open_once()
        raise
