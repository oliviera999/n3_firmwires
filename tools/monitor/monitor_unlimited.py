#!/usr/bin/env python3
"""
Moniteur série sans limitations pour ESP32 - Version améliorée
Résout le problème d'arrêt du moniteur après quelques centaines de lignes

Usage:
  python monitor_unlimited.py --port COM6 --baud 115200 --duration 600
  python monitor_unlimited.py --port COM6 --duration 86400 --output-dir monitor_long_24h_2025-02-28 --rotate-after-seconds 3600
"""

import argparse
import os
import re
import sys
import time
import threading
from datetime import datetime
from pathlib import Path
from queue import Queue, Empty

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import serial  # type: ignore
except Exception as exc:
    sys.stderr.write("pyserial manquant. Installer avec: pip install pyserial\n")
    raise

from serial_utils import open_serial_with_release

# Patterns pour fichier events (lignes critiques)
EVENT_PATTERNS = [
    re.compile(r"rst:0x[0-9a-f]+\s*\(", re.I),
    re.compile(r"\[E\]|ERROR|ERREUR", re.I),
    re.compile(r"Guru Meditation|panic'ed|panic\s*\(", re.I),
    re.compile(r"Brownout|Stack overflow|STACK OVERFLOW", re.I),
    re.compile(r"CRITICAL.*Heap|WARN.*Heap.*faible|heap.*critique", re.I),
    re.compile(r"WiFi.*disconnect|WiFi.*deconnect|Connexion.*perdue", re.I),
    re.compile(r"POST.*échec|POST.*error|POST.*failed", re.I),
    re.compile(r"\[netRPC\]\s*Timeout", re.I),
]


def line_is_event(line: str) -> bool:
    for pat in EVENT_PATTERNS:
        if pat.search(line):
            return True
    return False


class UnlimitedSerialMonitor:
    def __init__(self, port, baud, duration=600, output_file=None, output_dir=None, rotate_after_seconds=None, events_file=None):
        self.port = port
        self.baud = baud
        self.duration = duration
        self.output_file = output_file
        self.output_dir = output_dir
        self.rotate_after_seconds = rotate_after_seconds or 0
        self.events_file = events_file
        self.running = True
        self.serial_lost = False
        self.data_queue = Queue(maxsize=10000)
        self.serial_conn = None
        self._writer_start_time = None
        self._part_index = 0
        self._current_handle = None
        self._events_handle = None
        
    def timestamp(self):
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    def serial_reader(self):
        """Thread pour lire les données série sans bloquer"""
        try:
            self.serial_conn = open_serial_with_release(
                self.port,
                self.baud,
                timeout=0.1,
                write_timeout=0.1,
                inter_byte_timeout=0.1
            )
            
            print(f"[{self.timestamp()}] Connexion série établie sur {self.port} @ {self.baud} bps")
            
            buffer = b''
            while self.running:
                try:
                    # Lire avec un timeout court
                    if self.serial_conn.in_waiting > 0:
                        chunk = self.serial_conn.read(self.serial_conn.in_waiting)
                        if chunk:
                            buffer += chunk
                            
                            # Traiter les lignes complètes
                            while b'\n' in buffer:
                                line, buffer = buffer.split(b'\n', 1)
                                try:
                                    decoded = line.decode('utf-8', errors='ignore').strip()
                                    if decoded:
                                        self.data_queue.put(decoded, timeout=0.1)
                                except Exception as e:
                                    print(f"[{self.timestamp()}] Erreur décodage: {e}")
                    else:
                        time.sleep(0.01)  # Pause courte pour éviter la surcharge CPU
                        
                except Exception as e:
                    print(f"[{self.timestamp()}] Erreur lecture série: {e}")
                    if self.running:
                        self.serial_lost = True
                    time.sleep(0.1)
                    
        except Exception as e:
            print(f"[{self.timestamp()}] Erreur connexion série: {e}")
            if self.running:
                self.serial_lost = True
            self.running = False
    
    def _next_output_path(self):
        if self.output_dir:
            self._part_index += 1
            return os.path.join(self.output_dir, f"part_{self._part_index:03d}.log")
        return self.output_file

    def _open_output(self, path, is_first=False):
        try:
            h = open(path, "w", encoding="utf-8", buffering=1)
            h.write(f"# Monitoring démarré à {self.timestamp()}\n")
            h.write(f"# Port: {self.port}, Baud: {self.baud}\n")
            if is_first and self.rotate_after_seconds:
                h.write(f"# Rotation toutes les {self.rotate_after_seconds}s\n")
            h.flush()
            return h
        except Exception as e:
            print(f"[{self.timestamp()}] Erreur ouverture {path}: {e}")
            return None

    def data_writer(self):
        """Écrit fichier + console, avec rotation et optionnel events."""
        self._writer_start_time = time.time()
        if self.output_dir:
            os.makedirs(self.output_dir, exist_ok=True)
            self._part_index = 1
            first_path = os.path.join(self.output_dir, "part_001.log")
            self._current_handle = self._open_output(first_path, is_first=True)
        elif self.output_file:
            self._current_handle = self._open_output(self.output_file, is_first=True)
        else:
            self._current_handle = None

        if self.events_file:
            try:
                self._events_handle = open(self.events_file, "w", encoding="utf-8", buffering=1)
                self._events_handle.write(f"# Events (lignes critiques) - {self.timestamp()}\n")
                self._events_handle.flush()
            except Exception as e:
                print(f"[{self.timestamp()}] Erreur ouverture events: {e}")
                self._events_handle = None
        else:
            self._events_handle = None

        try:
            while self.running:
                try:
                    line = self.data_queue.get(timeout=1.0)
                    timestamped_line = f"[{self.timestamp()}] {line}"
                    print(timestamped_line)

                    if self._current_handle:
                        if self.rotate_after_seconds > 0 and self._writer_start_time is not None:
                            elapsed = time.time() - self._writer_start_time
                            if elapsed >= self.rotate_after_seconds:
                                self._current_handle.write(f"# Rotation à {self.timestamp()}\n")
                                self._current_handle.close()
                                self._current_handle = None
                                self._writer_start_time = time.time()
                                next_path = self._next_output_path()
                                self._current_handle = self._open_output(next_path, is_first=False)
                                if self._current_handle:
                                    print(f"[{self.timestamp()}] Rotation -> {next_path}")
                        if self._current_handle:
                            self._current_handle.write(timestamped_line + "\n")
                            self._current_handle.flush()
                        if self._events_handle and line_is_event(line):
                            self._events_handle.write(timestamped_line + "\n")
                            self._events_handle.flush()
                except Empty:
                    continue
                except Exception as e:
                    print(f"[{self.timestamp()}] Erreur écriture: {e}")
        finally:
            if self._current_handle:
                self._current_handle.write(f"# Monitoring terminé à {self.timestamp()}\n")
                self._current_handle.close()
            if self._events_handle:
                self._events_handle.close()
    
    def run(self):
        """Lancer le monitoring"""
        out_desc = self.output_dir or self.output_file or "Console uniquement"
        print(f"=== MONITEUR SÉRIE SANS LIMITES ===")
        print(f"Port: {self.port}")
        print(f"Baud: {self.baud}")
        print(f"Durée: {self.duration} secondes")
        print(f"Sortie: {out_desc}")
        if self.rotate_after_seconds:
            print(f"Rotation: toutes les {self.rotate_after_seconds}s")
        if self.events_file:
            print(f"Events: {self.events_file}")
        print(f"Démarrage: {self.timestamp()}")
        print()
        
        # Démarrer les threads
        reader_thread = threading.Thread(target=self.serial_reader, daemon=True)
        writer_thread = threading.Thread(target=self.data_writer, daemon=True)
        
        reader_thread.start()
        writer_thread.start()
        
        try:
            # Attendre la durée spécifiée
            time.sleep(self.duration)
            
        except KeyboardInterrupt:
            print(f"\n[{self.timestamp()}] Arrêt demandé par l'utilisateur")
            
        finally:
            # Arrêter proprement
            self.running = False
            if self.serial_conn:
                try:
                    self.serial_conn.close()
                except Exception:
                    pass
                self.serial_conn = None
            
            # Attendre que les threads se terminent
            reader_thread.join(timeout=2)
            writer_thread.join(timeout=2)
            
            if self.serial_lost and (self.output_file or self.output_dir):
                try:
                    path = self.output_file or (os.path.join(self.output_dir, "capture_interrompue.txt") if self.output_dir else None)
                    if path:
                        with open(path, "a", encoding="utf-8") as f:
                            f.write(f"# Connexion serie perdue a {self.timestamp()} - capture interrompue\n")
                except Exception as e:
                    print(f"[{self.timestamp()}] Impossible d'ecrire marqueur déconnexion: {e}")
            
            print(f"\n[{self.timestamp()}] Monitoring terminé")


def main():
    parser = argparse.ArgumentParser(description="Moniteur série ESP32 sans limitations")
    parser.add_argument("--port", required=True, help="Port série (ex: COM6)")
    parser.add_argument("--baud", type=int, default=115200, help="Vitesse de transmission")
    parser.add_argument("--duration", type=int, default=600, help="Durée totale en secondes")
    parser.add_argument("--output", help="Fichier de sortie (mode single file)")
    parser.add_argument("--output-dir", help="Dossier de sortie (mode rotation: part_001.log, part_002.log...)")
    parser.add_argument("--rotate-after-seconds", type=int, default=0, help="Rotation: nouveau fichier toutes les N secondes (utiliser avec --output-dir)")
    parser.add_argument("--events-file", help="Fichier optionnel pour lignes critiques uniquement")
    args = parser.parse_args()

    if args.rotate_after_seconds and not args.output_dir:
        parser.error("--rotate-after-seconds requiert --output-dir")
    if args.output_dir and args.output:
        parser.error("Utiliser soit --output soit --output-dir, pas les deux")

    monitor = UnlimitedSerialMonitor(
        port=args.port,
        baud=args.baud,
        duration=args.duration,
        output_file=args.output,
        output_dir=args.output_dir,
        rotate_after_seconds=args.rotate_after_seconds or None,
        events_file=args.events_file,
    )
    monitor.run()


if __name__ == "__main__":
    main()




