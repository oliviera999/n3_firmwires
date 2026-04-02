#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Capture moniteur serie ESP32-CAM (adaptateur USB–TTL / CH340).
Desactive DTR et RTS a l'ouverture : sinon Windows met souvent la carte en
bootloader ou reset, ou le moniteur reste vide.

Usage (depuis firmwires/uploadphotosserver) :
  python tools/monitor_serial_cam.py COM5
  python tools/monitor_serial_cam.py COM5 -s 120
"""
from __future__ import annotations

import argparse
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description="Moniteur serie uploadphotosserver (DTR/RTS off).")
    parser.add_argument("port", nargs="?", default="COM5", help="Port serie (ex. COM5)")
    parser.add_argument(
        "-s",
        "--seconds",
        type=int,
        default=120,
        metavar="N",
        help="Duree capture en secondes (defaut: 120)",
    )
    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=115200,
        help="Debit (defaut: 115200)",
    )
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("Installez pyserial : pip install pyserial", file=sys.stderr)
        return 1

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.15)
    except Exception as e:
        print(f"Ouverture {args.port!r} impossible : {e}", file=sys.stderr)
        return 1

    ser.dtr = False
    ser.rts = False
    time.sleep(0.4)
    ser.reset_input_buffer()

    print(
        f"--- {args.port} @ {args.baud} pendant {args.seconds}s (CTRL+C pour arreter) ---",
        file=sys.stderr,
        flush=True,
    )
    t0 = time.time()
    total = 0
    try:
        while time.time() - t0 < args.seconds:
            n = ser.in_waiting
            if n:
                chunk = ser.read(n)
                total += len(chunk)
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
            else:
                time.sleep(0.03)
    except KeyboardInterrupt:
        print("\n--- Interrompu ---", file=sys.stderr)
    finally:
        ser.close()
    print(f"--- Fin : {total} octets ---", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
