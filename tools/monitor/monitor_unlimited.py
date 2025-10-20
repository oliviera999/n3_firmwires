#!/usr/bin/env python3
"""
Moniteur série sans limitations pour ESP32 - Version améliorée
Résout le problème d'arrêt du moniteur après quelques centaines de lignes

Usage:
  python monitor_unlimited.py --port COM6 --baud 115200 --duration 600
"""

import argparse
import sys
import time
import threading
from datetime import datetime
from queue import Queue, Empty

try:
    import serial  # type: ignore
except Exception as exc:
    sys.stderr.write("pyserial manquant. Installer avec: pip install pyserial\n")
    raise


class UnlimitedSerialMonitor:
    def __init__(self, port, baud, duration=600, output_file=None):
        self.port = port
        self.baud = baud
        self.duration = duration
        self.output_file = output_file
        self.running = True
        self.data_queue = Queue(maxsize=10000)  # Buffer plus grand
        self.serial_conn = None
        
    def timestamp(self):
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    def serial_reader(self):
        """Thread pour lire les données série sans bloquer"""
        try:
            self.serial_conn = serial.Serial(
                self.port, 
                self.baud, 
                timeout=0.1,  # Timeout court pour éviter les blocages
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
                    time.sleep(0.1)
                    
        except Exception as e:
            print(f"[{self.timestamp()}] Erreur connexion série: {e}")
            self.running = False
    
    def data_writer(self):
        """Thread pour écrire les données (fichier + console)"""
        output_handle = None
        if self.output_file:
            try:
                output_handle = open(self.output_file, 'w', encoding='utf-8', buffering=1)
                output_handle.write(f"# Monitoring sans limites démarré à {self.timestamp()}\n")
                output_handle.write(f"# Port: {self.port}, Baud: {self.baud}, Durée: {self.duration}s\n")
                output_handle.flush()
            except Exception as e:
                print(f"[{self.timestamp()}] Erreur ouverture fichier: {e}")
        
        try:
            while self.running:
                try:
                    # Attendre une ligne avec timeout
                    line = self.data_queue.get(timeout=1.0)
                    timestamped_line = f"[{self.timestamp()}] {line}"
                    
                    # Afficher sur console
                    print(timestamped_line)
                    
                    # Écrire dans fichier si ouvert
                    if output_handle:
                        output_handle.write(timestamped_line + '\n')
                        output_handle.flush()
                        
                except Empty:
                    continue  # Timeout normal, continuer
                except Exception as e:
                    print(f"[{self.timestamp()}] Erreur écriture: {e}")
                    
        finally:
            if output_handle:
                output_handle.write(f"# Monitoring terminé à {self.timestamp()}\n")
                output_handle.close()
    
    def run(self):
        """Lancer le monitoring"""
        print(f"=== MONITEUR SÉRIE SANS LIMITES ===")
        print(f"Port: {self.port}")
        print(f"Baud: {self.baud}")
        print(f"Durée: {self.duration} secondes")
        print(f"Fichier: {self.output_file or 'Console uniquement'}")
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
                self.serial_conn.close()
            
            # Attendre que les threads se terminent
            reader_thread.join(timeout=2)
            writer_thread.join(timeout=2)
            
            print(f"\n[{self.timestamp()}] Monitoring terminé")


def main():
    parser = argparse.ArgumentParser(description="Moniteur série ESP32 sans limitations")
    parser.add_argument("--port", required=True, help="Port série (ex: COM6)")
    parser.add_argument("--baud", type=int, default=115200, help="Vitesse de transmission")
    parser.add_argument("--duration", type=int, default=600, help="Durée en secondes")
    parser.add_argument("--output", help="Fichier de sortie (optionnel)")
    
    args = parser.parse_args()
    
    monitor = UnlimitedSerialMonitor(
        port=args.port,
        baud=args.baud,
        duration=args.duration,
        output_file=args.output
    )
    
    monitor.run()


if __name__ == "__main__":
    main()




