#!/usr/bin/env python3
"""
Analyse un log série ESP32 et produit un court bilan de santé.

Usage:
  python tools/monitor/monitor_summary.py monitor_live.log
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from collections import Counter, defaultdict


def read_lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    try:
        return path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except Exception:
        return path.read_bytes().decode("utf-8", errors="ignore").splitlines()


def extract_after_bracket_timestamp(line: str) -> str:
    # Lines from serial_capture.py are prefixed with [YYYY-mm-dd HH:MM:SS]
    m = re.match(r"^\[[^\]]+\]\s*(.*)$", line)
    return m.group(1) if m else line


def analyze(lines: list[str]) -> dict:
    summary: dict[str, object] = {}

    # Strip timestamps
    clean = [extract_after_bracket_timestamp(l) for l in lines]

    # Buckets
    errors = [l for l in clean if re.search(r"\b(ERROR|ERR|FATAL|panic|Guru Meditation)\b", l, re.I)]
    warnings = [l for l in clean if re.search(r"\b(WARN|WARNING)\b", l, re.I)]
    resets = [l for l in clean if re.search(r"\brst:\b|Reset reason|Watchdog", l, re.I)]

    wifi = {
        "connecting": any(re.search(r"Wi-?Fi.*(connect|joining)", l, re.I) for l in clean),
        "connected": any(re.search(r"Wi-?Fi.*(connected|got ip|IP address)", l, re.I) for l in clean),
        "disconnected": any(re.search(r"Wi-?Fi.*(disconnect|lost)", l, re.I) for l in clean),
        "ip": None,
    }
    for l in clean:
        m = re.search(r"IP(?:v4)?\s*[:=]\s*([0-9]{1,3}(?:\.[0-9]{1,3}){3})", l, re.I)
        if m:
            wifi["ip"] = m.group(1)
            break

    time_sync = any(re.search(r"(SNTP|NTP|time sync|settimeofday)", l, re.I) for l in clean)

    web_server = any(re.search(r"(Web ?Server|HTTP server).*(start|listening|port)", l, re.I) for l in clean)
    ota_events = [l for l in clean if re.search(r"\bOTA\b|partition|firmware", l, re.I)]

    sensor_err = [l for l in clean if re.search(r"(sensor|DS18|I2C|ADC).*(fail|not found|error|timeout)", l, re.I)]

    heap_lines = [l for l in clean if re.search(r"(heap|free heap|mem|malloc)\b", l, re.I)]

    # Count frequent tags
    tag_counts = Counter()
    for l in clean:
        if "[E]" in l or ":E (" in l:
            tag_counts["E"] += 1
        if "[W]" in l or ":W (" in l:
            tag_counts["W"] += 1

    summary.update(
        {
            "total_lines": len(lines),
            "errors": errors[:50],  # cap for display
            "warnings": warnings[:50],
            "resets": resets,
            "wifi": wifi,
            "time_sync": time_sync,
            "web_server": web_server,
            "ota_events": ota_events[:20],
            "sensor_errors": sensor_err[:50],
            "heap_lines": heap_lines[:10],
            "tag_counts": dict(tag_counts),
        }
    )

    return summary


def print_report(summary: dict) -> None:
    print("=== BILAN SANTÉ ESP32 (log série) ===")
    print(f"Lignes capturées: {summary['total_lines']}")

    wifi = summary["wifi"]
    state = (
        "connecté" if wifi.get("connected") else "en cours de connexion" if wifi.get("connecting") else "déconnecté"
    )
    ip = wifi.get("ip")
    print(f"- WiFi: {state}{(' | IP: ' + ip) if ip else ''}")
    print(f"- Synchronisation temps (NTP): {'OK' if summary['time_sync'] else 'inconnue'}")
    print(f"- Serveur Web local: {'actif' if summary['web_server'] else 'inconnu'}")

    if summary["ota_events"]:
        print(f"- Événements OTA détectés: {len(summary['ota_events'])}")
    else:
        print("- Événements OTA: aucun")

    if summary["sensor_errors"]:
        print(f"- Erreurs capteurs: {len(summary['sensor_errors'])} (attendu si aucun capteur connecté)")
    else:
        print("- Erreurs capteurs: aucune")

    if summary["resets"]:
        print(f"- Signaux de reset/watchdog: {len(summary['resets'])}")
    else:
        print("- Signaux de reset/watchdog: aucun")

    e_count = len(summary["errors"]) or summary["tag_counts"].get("E", 0)
    w_count = len(summary["warnings"]) or summary["tag_counts"].get("W", 0)
    print(f"- Erreurs: {e_count} | Avertissements: {w_count}")

    if summary["heap_lines"]:
        print("- Indications mémoire/heap présentes")

    if summary["errors"]:
        print("\nPrincipales erreurs (extraits):")
        for l in summary["errors"][:5]:
            print(f"  • {l}")

    if summary["warnings"]:
        print("\nAvertissements (extraits):")
        for l in summary["warnings"][:5]:
            print(f"  • {l}")


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: python tools/monitor/monitor_summary.py <fichier_log>")
        return 2
    log_path = Path(sys.argv[1])
    lines = read_lines(log_path)
    if not lines:
        print("Log vide ou introuvable.")
        return 1
    summary = analyze(lines)
    print_report(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


