#!/usr/bin/env python3
"""
Simple serial capture helper for ESP32 logs.

Usage examples:
  python serial_capture.py --port COM6 --baud 115200 --duration 600 --out monitor_serial.log

Writes both to file and stdout so you can watch while it records.
"""

import argparse
import sys
import time
from datetime import datetime

try:
    import serial  # type: ignore
except Exception as exc:  # pragma: no cover
    sys.stderr.write("pyserial missing. Install with: pip install pyserial\n")
    raise


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="ESP32 serial capture")
    parser.add_argument("--port", required=True, help="Serial port, e.g., COM6")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument(
        "--duration", type=int, default=600, help="Capture duration in seconds"
    )
    parser.add_argument("--out", default="monitor_serial.log", help="Output file path")
    parser.add_argument(
        "--quiet", action="store_true", help="Do not echo to stdout while capturing"
    )
    return parser.parse_args()


def timestamp() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def main() -> int:
    args = parse_args()
    start_time = time.time()
    end_time = start_time + args.duration

    with serial.Serial(args.port, args.baud, timeout=1) as ser, open(
        args.out, "a", encoding="utf-8", newline="\n"
    ) as f:
        f.write(f"# Serial capture started at {timestamp()} on {args.port} {args.baud}bps\n")
        f.flush()

        while time.time() < end_time:
            try:
                if ser.in_waiting:
                    raw = ser.readline()
                    try:
                        line = raw.decode("utf-8", errors="ignore").rstrip("\r\n")
                    except Exception:
                        line = str(raw)
                    # write with timestamp
                    msg = f"[{timestamp()}] {line}\n"
                    f.write(msg)
                    if not args.quiet:
                        sys.stdout.write(msg)
                else:
                    time.sleep(0.02)
            except KeyboardInterrupt:
                break
            except Exception as exc:
                err = f"[{timestamp()}] ERROR reading serial: {exc}\n"
                f.write(err)
                if not args.quiet:
                    sys.stdout.write(err)
                time.sleep(0.25)

        f.write(f"# Serial capture ended at {timestamp()}\n")
        f.flush()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())


