#!/usr/bin/env python3
"""
Capture boot logs from ESP32 over a serial port for a fixed duration.

- Toggles DTR/RTS to trigger a reset
- Reads lines for about CAPTURE_SECONDS seconds
- Prints all lines to stdout and writes them to tools/boot_serial.log

Usage (PowerShell):
  python tools/capture_boot_serial.py COM6 115200 90
Defaults:
  port = COM6, baud = 115200, seconds = 90
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import serial  # pyserial
except ImportError:
    sys.stderr.write("pyserial not installed. Install with: pip install pyserial\n")
    sys.exit(1)

from serial_utils import open_serial_with_release


def capture(port: str = "COM6", baud: int = 115200, seconds: int = 90) -> int:
    out_dir = Path(__file__).parent
    out_path = out_dir / "boot_serial.log"
    start_ts = time.strftime("%Y-%m-%d %H:%M:%S")
    sys.stdout.write(f"Starting serial capture on {port} @ {baud} for ~{seconds}s (start {start_ts})\n")
    sys.stdout.flush()

    try:
        ser = open_serial_with_release(port=port, baudrate=baud, timeout=0.05)
    except Exception as e:
        sys.stderr.write(f"Failed to open serial port {port}: {e}\n")
        return 2

    # Try to reset via DTR/RTS
    try:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.2)
        ser.dtr = True
        ser.rts = False
    except Exception:
        pass

    end_time = time.time() + max(1, int(seconds))
    lines = []

    try:
        with out_path.open("w", encoding="utf-8") as logf:
            while time.time() < end_time:
                try:
                    raw = ser.readline()
                except Exception as e:
                    sys.stderr.write(f"Serial read error: {e}\n")
                    break
                if not raw:
                    # No data available right now
                    time.sleep(0.01)
                    continue
                try:
                    line = raw.decode("utf-8", errors="ignore")
                except Exception:
                    line = repr(raw) + "\n"
                lines.append(line)
                sys.stdout.write(line)
                logf.write(line)
    finally:
        try:
            ser.close()
        except Exception:
            pass

    sys.stdout.flush()

    # Print a short summary focused on OTA lines
    ota_lines = [ln.rstrip() for ln in lines if "[OTA]" in ln]
    if ota_lines:
        sys.stdout.write("\n--- OTA lines detected ---\n")
        for ln in ota_lines[-15:]:
            sys.stdout.write(ln + "\n")
    else:
        sys.stdout.write("\n(No [OTA] lines detected during capture)\n")

    sys.stdout.write(f"\nSaved full log to: {out_path}\n")
    return 0


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM6"
    try:
        baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    except ValueError:
        baud = 115200
    try:
        seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 90
    except ValueError:
        seconds = 90
    return capture(port, baud, seconds)


if __name__ == "__main__":
    sys.exit(main())


