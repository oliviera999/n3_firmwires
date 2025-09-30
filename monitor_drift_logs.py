#!/usr/bin/env python3
"""
Script de monitoring des logs série pour capturer les corrections de dérive temporelle
"""

import serial
import time
import datetime
import re
import sys

class DriftLogMonitor:
    def __init__(self, port='COM6', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.drift_corrections = []
        self.start_time = time.time()
        
    def connect(self):
        """Établit la connexion série"""
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"✅ Connexion série établie sur {self.port} à {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"❌ Erreur de connexion série: {e}")
            return False
    
    def parse_drift_message(self, line):
        """Parse un message de correction de dérive"""
        # Pattern pour: [Power] Correction de dérive par défaut appliquée: -15 s (dérive configurée: -25.00 PPM)
        pattern = r'\[Power\] Correction de dérive par défaut appliquée: (-?\d+) s \(dérive configurée: (-?\d+\.?\d*) PPM\)'
        match = re.search(pattern, line)
        
        if match:
            correction_seconds = int(match.group(1))
            drift_ppm = float(match.group(2))
            
            return {
                'timestamp': datetime.datetime.now(),
                'correction_seconds': correction_seconds,
                'drift_ppm': drift_ppm,
                'elapsed_minutes': (time.time() - self.start_time) / 60
            }
        return None
    
    def monitor(self, duration_minutes=3):
        """Surveille les logs pendant la durée spécifiée"""
        if not self.connect():
            return
        
        print(f"=== MONITORING DES LOGS DE DÉRIVE TEMPORELLE ===")
        print(f"Port: {self.port}")
        print(f"Durée: {duration_minutes} minutes")
        print(f"Début: {datetime.datetime.now().strftime('%H:%M:%S %d/%m/%Y')}")
        print("Recherche des messages de correction de dérive...")
        print("=" * 60)
        
        end_time = time.time() + (duration_minutes * 60)
        
        try:
            while time.time() < end_time:
                if self.serial_conn.in_waiting > 0:
                    line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                    
                    if line:
                        print(f"[{datetime.datetime.now().strftime('%H:%M:%S')}] {line}")
                        
                        # Vérifier si c'est un message de correction de dérive
                        drift_info = self.parse_drift_message(line)
                        if drift_info:
                            self.drift_corrections.append(drift_info)
                            print(f"🎯 CORRECTION DÉTECTÉE: {drift_info['correction_seconds']}s "
                                  f"(dérive: {drift_info['drift_ppm']} PPM, "
                                  f"après {drift_info['elapsed_minutes']:.1f}min)")
                
                time.sleep(0.1)
                
        except KeyboardInterrupt:
            print("\n⏹️  Monitoring interrompu par l'utilisateur")
        except Exception as e:
            print(f"❌ Erreur pendant le monitoring: {e}")
        finally:
            if self.serial_conn:
                self.serial_conn.close()
                print("🔌 Connexion série fermée")
        
        self.print_analysis()
    
    def print_analysis(self):
        """Affiche l'analyse des corrections détectées"""
        print("\n" + "=" * 60)
        print("=== ANALYSE DES CORRECTIONS DE DÉRIVE ===")
        
        if not self.drift_corrections:
            print("❌ Aucune correction de dérive détectée")
            print("\nCauses possibles:")
            print("- ESP32 non connecté ou non flashé")
            print("- Pas de dérive significative (< 1 seconde)")
            print("- Correction désactivée dans la configuration")
            print("- ESP32 connecté au WiFi (pas de correction par défaut)")
            return
        
        print(f"✅ {len(self.drift_corrections)} correction(s) détectée(s)")
        
        print("\nDétail des corrections:")
        for i, correction in enumerate(self.drift_corrections, 1):
            print(f"  {i}. {correction['timestamp'].strftime('%H:%M:%S')} - "
                  f"{correction['correction_seconds']}s "
                  f"(dérive: {correction['drift_ppm']} PPM, "
                  f"après {correction['elapsed_minutes']:.1f}min)")
        
        # Analyse de la cohérence
        drift_values = [c['drift_ppm'] for c in self.drift_corrections]
        if len(set(drift_values)) == 1:
            print(f"\n✅ Valeur de dérive cohérente: {drift_values[0]} PPM")
        else:
            print(f"\n⚠️  Valeurs de dérive variables: {drift_values}")
        
        # Calcul de la fréquence des corrections
        total_time = self.drift_corrections[-1]['elapsed_minutes']
        frequency = len(self.drift_corrections) / total_time if total_time > 0 else 0
        print(f"📊 Fréquence des corrections: {frequency:.2f} corrections/minute")
        
        # Recommandations
        print("\n=== RECOMMANDATIONS ===")
        if len(self.drift_corrections) == 0:
            print("1. Vérifier que l'ESP32 est hors-ligne (pas de WiFi)")
            print("2. Attendre plus longtemps (corrections toutes les 5 minutes)")
            print("3. Vérifier la configuration dans project_config.h")
        elif len(self.drift_corrections) < 2:
            print("1. Monitoring trop court - étendre à 10-15 minutes")
            print("2. Vérifier que l'ESP32 reste hors-ligne")
        else:
            print("1. ✅ Correction de dérive fonctionnelle")
            print("2. Surveiller la précision sur de plus longues périodes")
            print("3. Ajuster DEFAULT_DRIFT_CORRECTION_PPM si nécessaire")

def main():
    """Fonction principale"""
    print("Monitor de logs de dérive temporelle ESP32")
    print("=" * 50)
    
    # Configuration
    port = 'COM6'  # Port série par défaut
    duration = 3   # Durée en minutes
    
    # Vérifier les arguments
    if len(sys.argv) > 1:
        port = sys.argv[1]
    if len(sys.argv) > 2:
        duration = int(sys.argv[2])
    
    print(f"Configuration:")
    print(f"  Port série: {port}")
    print(f"  Durée: {duration} minutes")
    print()
    
    # Lancer le monitoring
    monitor = DriftLogMonitor(port, 115200)
    monitor.monitor(duration)

if __name__ == "__main__":
    main()
