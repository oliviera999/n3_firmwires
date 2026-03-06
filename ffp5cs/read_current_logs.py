#!/usr/bin/env python3
"""Lecture des logs série actuels pour détecter un crash récent"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "tools" / "monitor"))

import serial
import time
import re
from datetime import datetime

from serial_utils import open_serial_with_release

def read_recent_logs(port='COM4', baud=115200, duration=10):
    """Lit les logs série et cherche des traces de crash"""
    try:
        ser = open_serial_with_release(port, baud, timeout=2)
        time.sleep(1)
        ser.reset_input_buffer()
        
        print("=" * 60)
        print("LECTURE DES LOGS SÉRIE ACTUELS")
        print("=" * 60)
        print(f"Port: {port}, Baud: {baud}")
        print(f"Début: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print("=" * 60)
        print()
        
        lines = []
        crash_patterns = [
            r"rst:0x",
            r"Reset reason",
            r"reset reason",
            r"BOOT FFP5CS",
            r"Guru Meditation",
            r"Panic",
            r"PANIC",
            r"Brownout",
            r"watchdog.*timeout",
            r"WDT.*timeout",
            r"stack overflow"
        ]
        
        start_time = time.time()
        crashes_found = []
        
        print("Lecture en cours...")
        print()
        
        while time.time() - start_time < duration:
            if ser.in_waiting > 0:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        lines.append(line)
                        if len(lines) > 100:
                            lines.pop(0)
                        
                        # Chercher des patterns de crash
                        for pattern in crash_patterns:
                            if re.search(pattern, line, re.IGNORECASE):
                                crashes_found.append({
                                    'pattern': pattern,
                                    'line': line,
                                    'time': datetime.now().strftime('%H:%M:%S')
                                })
                                print(f"⚠️  CRASH DÉTECTÉ à {crashes_found[-1]['time']}:")
                                print(f"   Pattern: {pattern}")
                                print(f"   Ligne: {line}")
                                print()
                except Exception as e:
                    pass
            else:
                time.sleep(0.1)
        
        ser.close()
        
        print("=" * 60)
        print("RÉSUMÉ")
        print("=" * 60)
        print(f"Lignes lues: {len(lines)}")
        print(f"Crashes détectés: {len(crashes_found)}")
        print()
        
        if crashes_found:
            print("CRASHES DÉTECTÉS:")
            for i, crash in enumerate(crashes_found, 1):
                print(f"{i}. {crash['time']} - {crash['pattern']}")
                print(f"   {crash['line']}")
                print()
        else:
            print("Aucun crash détecté dans les logs récents")
            print()
            print("Dernières lignes lues:")
            for line in lines[-10:]:
                print(f"  {line}")
        
        return crashes_found, lines
        
    except Exception as e:
        print(f"Erreur: {e}")
        return [], []

if __name__ == "__main__":
    crashes, lines = read_recent_logs('COM4', 115200, 10)
