#!/usr/bin/env python3
"""
Moniteur série ESP32 jusqu'à détection d'un crash/reboot
Capture tous les logs et fait une analyse complète

Usage:
  python monitor_until_crash.py --port COM6 --baud 115200 --post-reboot 60 --max-wait 3600
"""

import argparse
import sys
import time
import threading
import re
from datetime import datetime
from queue import Queue, Empty

try:
    import serial  # type: ignore
except Exception as exc:
    sys.stderr.write("pyserial manquant. Installer avec: pip install pyserial\n")
    raise


class CrashMonitor:
    def __init__(self, port, baud, post_reboot_duration=60, max_wait_time=3600):
        self.port = port
        self.baud = baud
        self.post_reboot_duration = post_reboot_duration
        self.max_wait_time = max_wait_time
        self.running = True
        self.data_queue = Queue(maxsize=10000)
        self.serial_conn = None
        self.reboot_detected = False
        self.reboot_time = None
        self.start_time = time.time()
        self.all_lines = []
        
        # Patterns de détection de reboot
        self.reboot_patterns = [
            r"=== BOOT FFP5CS",
            r"rst:0x",
            r"RST:",
            r"reset reason",
            r"Redémarrage",
            r"Démarrage FFP5CS",
            r"App start v",
            r"Guru Meditation",
            r"Panic",
            r"Brownout",
            r"Core dump",
            r"Guru.*Error",
            r"panic'ed",
            r"PANIC"
        ]
        
    def timestamp(self):
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    def check_reboot(self, line):
        """Vérifier si une ligne indique un reboot/crash"""
        for pattern in self.reboot_patterns:
            if re.search(pattern, line, re.IGNORECASE):
                return True
        return False
    
    def serial_reader(self):
        """Thread pour lire les données série"""
        try:
            self.serial_conn = serial.Serial(
                self.port, 
                self.baud, 
                timeout=0.1,
                write_timeout=0.1,
                inter_byte_timeout=0.1
            )
            
            print(f"[{self.timestamp()}] Connexion série établie sur {self.port} @ {self.baud} bps")
            print(f"[{self.timestamp()}] En attente d'un crash/reboot...")
            print()
            
            buffer = b''
            while self.running:
                try:
                    if self.serial_conn.in_waiting > 0:
                        chunk = self.serial_conn.read(self.serial_conn.in_waiting)
                        if chunk:
                            buffer += chunk
                            
                            while b'\n' in buffer:
                                line, buffer = buffer.split(b'\n', 1)
                                try:
                                    decoded = line.decode('utf-8', errors='ignore').strip()
                                    if decoded:
                                        # Vérifier si c'est un reboot
                                        if not self.reboot_detected and self.check_reboot(decoded):
                                            self.reboot_detected = True
                                            self.reboot_time = time.time()
                                            elapsed = self.reboot_time - self.start_time
                                            print()
                                            print(f"[{self.timestamp()}] *** REBOOT/CRASH DETECTE ! ***")
                                            print(f"[{self.timestamp()}] Ligne: {decoded}")
                                            print(f"[{self.timestamp()}] Temps ecoule: {elapsed:.1f} secondes")
                                            print()
                                        
                                        self.data_queue.put(decoded, timeout=0.1)
                                        self.all_lines.append(decoded)
                                except Exception as e:
                                    pass
                    else:
                        time.sleep(0.01)
                        
                except Exception as e:
                    time.sleep(0.1)
                    
        except Exception as e:
            print(f"[{self.timestamp()}] Erreur connexion série: {e}")
            self.running = False
    
    def run(self):
        """Lancer le monitoring"""
        timestamp_str = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        log_file = f"monitor_until_crash_{timestamp_str}.log"
        analysis_file = f"monitor_until_crash_analysis_{timestamp_str}.txt"
        
        print("=== MONITORING ESP32 JUSQU'AU CRASH/REBOOT ===")
        print(f"Debut: {self.timestamp()}")
        print(f"Port: {self.port}")
        print(f"Baud rate: {self.baud}")
        print(f"Duree post-reboot: {self.post_reboot_duration} secondes")
        print(f"Temps max d'attente: {self.max_wait_time} secondes")
        print(f"Log file: {log_file}")
        print(f"Analysis file: {analysis_file}")
        print("=" * 50)
        print()
        
        # Démarrer le thread de lecture
        reader_thread = threading.Thread(target=self.serial_reader, daemon=True)
        reader_thread.start()
        
        # Attendre la détection d'un reboot ou timeout
        try:
            while not self.reboot_detected and self.running:
                elapsed = time.time() - self.start_time
                if elapsed > self.max_wait_time:
                    print()
                    print(f"[{self.timestamp()}] Temps maximum atteint ({self.max_wait_time} secondes)")
                    print(f"[{self.timestamp()}] Aucun reboot detecte pendant cette periode")
                    break
                
                time.sleep(1)
                
                # Afficher progression toutes les 10 secondes
                if int(elapsed) % 10 == 0 and int(elapsed) > 0:
                    remaining = self.max_wait_time - elapsed
                    print(f"[{self.timestamp()}] Temps ecoule: {int(elapsed)}s (reste {int(remaining)}s)")
            
            # Si reboot détecté, continuer le monitoring post-reboot
            if self.reboot_detected:
                print()
                print(f"[{self.timestamp()}] Continuation du monitoring post-reboot ({self.post_reboot_duration} secondes)...")
                
                post_reboot_start = time.time()
                post_reboot_end = post_reboot_start + self.post_reboot_duration
                
                while time.time() < post_reboot_end:
                    time.sleep(1)
                    remaining = int(post_reboot_end - time.time())
                    if remaining % 10 == 0 and remaining > 0:
                        print(f"[{self.timestamp()}] Post-reboot: {remaining}s restantes")
                
                print(f"[{self.timestamp()}] Monitoring post-reboot termine")
        
        except KeyboardInterrupt:
            print(f"\n[{self.timestamp()}] Arret demande par l'utilisateur")
        
        finally:
            # Arrêter proprement
            self.running = False
            time.sleep(1)  # Laisser le temps aux threads de finir
            
            # Sauvegarder tous les logs
            print()
            print(f"[{self.timestamp()}] Sauvegarde des logs...")
            with open(log_file, 'w', encoding='utf-8') as f:
                for line in self.all_lines:
                    f.write(line + '\n')
            
            print(f"[{self.timestamp()}] {len(self.all_lines)} lignes sauvegardees dans {log_file}")
            
            # Générer l'analyse
            print(f"[{self.timestamp()}] Generation de l'analyse...")
            self.analyze_logs(log_file, analysis_file)
            
            print()
            print("=== MONITORING TERMINE ===")
            print(f"Fin: {self.timestamp()}")
    
    def analyze_logs(self, log_file, analysis_file):
        """Analyser les logs et générer un rapport"""
        report = []
        report.append("=" * 50)
        report.append("RAPPORT D'ANALYSE - MONITORING JUSQU'AU CRASH")
        report.append("=" * 50)
        report.append(f"Date: {self.timestamp()}")
        report.append(f"Port: {self.port}")
        if self.reboot_detected:
            elapsed = self.reboot_time - self.start_time
            report.append(f"Temps jusqu'au reboot: {elapsed:.1f} secondes")
        else:
            report.append("Aucun reboot detecte")
        report.append(f"Duree post-reboot: {self.post_reboot_duration} secondes")
        report.append(f"Fichier de log: {log_file}")
        report.append("=" * 50)
        report.append("")
        
        if not self.all_lines:
            report.append("ERREUR: Aucun log capture")
            with open(analysis_file, 'w', encoding='utf-8') as f:
                f.write('\n'.join(report))
            return
        
        # PRIORITE 1: ERREURS CRITIQUES
        report.append("PRIORITE 1: ERREURS CRITIQUES")
        report.append("=" * 50)
        report.append("")
        
        # Guru Meditation / Panic
        guru_lines = [line for line in self.all_lines if re.search(r"Guru Meditation|Guru.*Error|panic'ed|PANIC", line, re.IGNORECASE)]
        report.append(f"1. GURU MEDITATION / PANIC: {len(guru_lines)} occurrence(s)")
        if guru_lines:
            report.append("   CRITIQUE: Des crashes PANIC ont ete detectes !")
            report.append("   Details:")
            for line in guru_lines[:10]:
                report.append(f"   {line}")
            if len(guru_lines) > 10:
                report.append(f"   ... et {len(guru_lines) - 10} autre(s) occurrence(s)")
        else:
            report.append("   OK: Aucun crash PANIC detecte")
        report.append("")
        
        # Brownout
        brownout_lines = [line for line in self.all_lines if re.search(r"Brownout|BROWNOUT|brownout detector", line, re.IGNORECASE)]
        report.append(f"2. BROWNOUT (Alimentation): {len(brownout_lines)} occurrence(s)")
        if brownout_lines:
            report.append("   CRITIQUE: Probleme d'alimentation detecte !")
            for line in brownout_lines[:5]:
                report.append(f"   {line}")
        else:
            report.append("   OK: Aucun brownout detecte")
        report.append("")
        
        # Core Dump
        coredump_lines = [line for line in self.all_lines if re.search(r"Core dump|COREDUMP|coredump", line, re.IGNORECASE)]
        report.append(f"3. CORE DUMP: {len(coredump_lines)} occurrence(s)")
        if coredump_lines:
            report.append("   CRITIQUE: Core dump genere !")
            for line in coredump_lines[:5]:
                report.append(f"   {line}")
        else:
            report.append("   OK: Aucun core dump detecte")
        report.append("")
        
        # Stack overflow
        stack_lines = [line for line in self.all_lines if re.search(r"stack overflow|Stack overflow|STACK.*overflow", line, re.IGNORECASE)]
        report.append(f"4. STACK OVERFLOW: {len(stack_lines)} occurrence(s)")
        if stack_lines:
            report.append("   CRITIQUE: Debordement de pile detecte !")
            for line in stack_lines[:5]:
                report.append(f"   {line}")
        else:
            report.append("   OK: Aucun stack overflow detecte")
        report.append("")
        
        # PRIORITE 2: WATCHDOG
        report.append("PRIORITE 2: WATCHDOG ET TIMEOUTS")
        report.append("=" * 50)
        report.append("")
        
        watchdog_lines = [line for line in self.all_lines if re.search(r"watchdog|WDT|Watchdog|WDT timeout|task wdt|interrupt wdt", line, re.IGNORECASE)]
        report.append(f"1. WATCHDOG TIMEOUT: {len(watchdog_lines)} occurrence(s)")
        if watchdog_lines:
            report.append("   ATTENTION: Timeouts watchdog detectes")
            for line in watchdog_lines[:5]:
                report.append(f"   {line}")
        else:
            report.append("   OK: Aucun timeout watchdog detecte")
        report.append("")
        
        # PRIORITE 3: MEMOIRE
        report.append("PRIORITE 3: MEMOIRE (HEAP/STACK)")
        report.append("=" * 50)
        report.append("")
        
        heap_lines = [line for line in self.all_lines if re.search(r"heap|Heap|free heap|Free heap|HEAP", line, re.IGNORECASE)]
        report.append(f"1. HEAP (Memoire libre): {len(heap_lines)} reference(s)")
        
        # Extraire les valeurs de heap
        heap_values = []
        for line in self.all_lines:
            matches = re.findall(r'(\d+)\s*(?:bytes|Bytes)?\s*(?:free|Free)', line, re.IGNORECASE)
            for match in matches:
                val = int(match)
                if 0 < val < 1000000:  # Filtrer valeurs aberrantes
                    heap_values.append(val)
        
        if heap_values:
            min_heap = min(heap_values)
            max_heap = max(heap_values)
            avg_heap = sum(heap_values) // len(heap_values)
            report.append(f"   - Minimum: {min_heap} bytes ({min_heap/1024:.1f} KB)")
            report.append(f"   - Maximum: {max_heap} bytes ({max_heap/1024:.1f} KB)")
            report.append(f"   - Moyenne: {avg_heap} bytes ({avg_heap/1024:.1f} KB)")
            report.append(f"   - Echantillons: {len(heap_values)}")
            
            if min_heap < 50000:
                report.append("   CRITIQUE: Heap minimum tres faible (< 50KB) !")
            elif min_heap < 80000:
                report.append("   ATTENTION: Heap minimum faible (< 80KB)")
            else:
                report.append("   OK: Heap dans des valeurs acceptables")
        else:
            report.append("   INFO: Aucune valeur de heap extraite")
        report.append("")
        
        # PRIORITE 4: WIFI ET WEBSOCKET
        report.append("PRIORITE 4: WIFI ET WEBSOCKET")
        report.append("=" * 50)
        report.append("")
        
        wifi_errors = [line for line in self.all_lines if re.search(r"WiFi.*error|WiFi.*fail|WiFi.*timeout|WIFI.*ERROR", line, re.IGNORECASE)]
        wifi_disconnects = [line for line in self.all_lines if re.search(r"WiFi.*disconnect|WiFi.*lost|WIFI.*DISCONNECT", line, re.IGNORECASE)]
        report.append(f"1. WIFI: {len(wifi_errors)} erreur(s), {len(wifi_disconnects)} deconnexion(s)")
        if wifi_errors or wifi_disconnects:
            report.append("   ATTENTION: Problemes WiFi detectes")
        else:
            report.append("   OK: Pas de probleme WiFi majeur detecte")
        report.append("")
        
        ws_errors = [line for line in self.all_lines if re.search(r"WebSocket.*error|WebSocket.*fail|WebSocket.*timeout", line, re.IGNORECASE)]
        ws_disconnects = [line for line in self.all_lines if re.search(r"WebSocket.*disconnect|WebSocket.*close", line, re.IGNORECASE)]
        report.append(f"2. WEBSOCKET: {len(ws_errors)} erreur(s), {len(ws_disconnects)} deconnexion(s)")
        if ws_errors or ws_disconnects:
            report.append("   ATTENTION: Problemes WebSocket detectes")
        else:
            report.append("   OK: Pas de probleme WebSocket majeur detecte")
        report.append("")
        
        # RESUMÉ
        report.append("RESUME ET RECOMMANDATIONS")
        report.append("=" * 50)
        report.append("")
        
        critical_issues = 0
        warnings = 0
        
        if guru_lines or brownout_lines or coredump_lines or stack_lines:
            critical_issues += 1
        if watchdog_lines:
            warnings += 1
        if heap_values and min(heap_values) < 50000:
            critical_issues += 1
        elif heap_values and min(heap_values) < 80000:
            warnings += 1
        if len(wifi_errors) > 5 or len(ws_errors) > 5:
            warnings += 1
        
        report.append(f"Problemes critiques detectes: {critical_issues}")
        report.append(f"Avertissements: {warnings}")
        report.append("")
        
        if critical_issues > 0:
            report.append("ACTIONS IMMEDIATES REQUISES:")
            report.append("=" * 50)
            if guru_lines:
                report.append("1. Analyser les logs Guru Meditation pour identifier la tache/core concerne")
                report.append("2. Verifier les stack sizes des taches FreeRTOS")
                report.append("3. Verifier les timeouts watchdog")
                report.append("4. Ajouter plus de esp_task_wdt_reset() dans les boucles longues")
            if brownout_lines:
                report.append("5. Verifier l'alimentation 5V (doit etre stable)")
                report.append("6. Verifier la consommation de courant")
            if stack_lines:
                report.append("7. Augmenter les stack sizes des taches concernees")
                report.append("8. Reduire l'utilisation de la pile (variables locales)")
            if heap_values and min(heap_values) < 50000:
                report.append("9. Reduire l'utilisation de String Arduino (utiliser char[] a la place)")
                report.append("10. Verifier les fuites memoire (allocations non liberees)")
            report.append("")
        elif warnings > 0:
            report.append("ACTIONS RECOMMANDEES:")
            report.append("=" * 50)
            if watchdog_lines:
                report.append("1. Verifier que esp_task_wdt_reset() est appele regulierement")
            if heap_values and min(heap_values) < 80000:
                report.append("2. Monitorer l'evolution du heap sur plusieurs heures")
            report.append("")
        else:
            report.append("OK: AUCUN PROBLEME MAJEUR DETECTE")
            report.append("Le systeme semble stable pendant cette periode de monitoring.")
            report.append("")
        
        # Statistiques
        report.append("STATISTIQUES GENERALES")
        report.append("=" * 50)
        report.append(f"Total lignes loggees: {len(self.all_lines)}")
        report.append(f"Duree totale: {time.time() - self.start_time:.1f} secondes")
        report.append("")
        
        # Sauvegarder le rapport
        with open(analysis_file, 'w', encoding='utf-8') as f:
            f.write('\n'.join(report))
        
        # Afficher le rapport
        print('\n'.join(report))
        print()
        print(f"Rapport sauvegarde dans: {analysis_file}")


def main():
    parser = argparse.ArgumentParser(description="Moniteur série ESP32 jusqu'à crash/reboot")
    parser.add_argument("--port", required=True, help="Port série (ex: COM6)")
    parser.add_argument("--baud", type=int, default=115200, help="Vitesse de transmission")
    parser.add_argument("--post-reboot", type=int, default=60, help="Durée post-reboot en secondes")
    parser.add_argument("--max-wait", type=int, default=3600, help="Temps max d'attente en secondes")
    
    args = parser.parse_args()
    
    monitor = CrashMonitor(
        port=args.port,
        baud=args.baud,
        post_reboot_duration=args.post_reboot,
        max_wait_time=args.max_wait
    )
    
    monitor.run()


if __name__ == "__main__":
    main()
