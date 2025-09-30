#!/usr/bin/env python3
"""
Script de monitoring et analyse des logs de dérive temporelle
Capture les logs série pendant 3 minutes et analyse les corrections de dérive
"""

import serial
import time
import datetime
import re
import sys
import json
from collections import defaultdict

class DriftLogAnalyzer:
    def __init__(self, port='COM6', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.start_time = time.time()
        self.drift_corrections = []
        self.ntp_syncs = []
        self.power_events = []
        self.time_drift_events = []
        self.system_events = []
        self.log_count = 0
        
    def connect(self):
        """Établit la connexion série"""
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"✅ Connexion série établie sur {self.port} à {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"❌ Erreur de connexion série: {e}")
            return False
    
    def parse_drift_correction(self, line):
        """Parse un message de correction de dérive"""
        # Pattern pour: [Power] Correction de dérive par défaut appliquée: -2 s (dérive estimée: -25.00 PPM)
        pattern = r'\[Power\] Correction de dérive par défaut appliquée: (-?\d+) s \(dérive estimée: (-?\d+\.?\d*) PPM\)'
        match = re.search(pattern, line)
        if match:
            correction_seconds = int(match.group(1))
            drift_ppm = float(match.group(2))
            return {
                'timestamp': datetime.datetime.now().isoformat(),
                'correction_seconds': correction_seconds,
                'drift_ppm': drift_ppm,
                'line': line.strip()
            }
        return None
    
    def parse_ntp_sync(self, line):
        """Parse un message de synchronisation NTP"""
        if 'NTP' in line and ('sync' in line.lower() or 'synchron' in line.lower()):
            return {
                'timestamp': datetime.datetime.now().isoformat(),
                'line': line.strip()
            }
        return None
    
    def parse_time_drift_monitor(self, line):
        """Parse les messages du TimeDriftMonitor"""
        if '[TimeDrift]' in line:
            return {
                'timestamp': datetime.datetime.now().isoformat(),
                'line': line.strip()
            }
        return None
    
    def parse_system_events(self, line):
        """Parse les événements système importants"""
        important_keywords = ['WiFi', 'NTP', 'Power', 'TimeDrift', 'Correction', 'dérive']
        if any(keyword in line for keyword in important_keywords):
            return {
                'timestamp': datetime.datetime.now().isoformat(),
                'line': line.strip()
            }
        return None
    
    def analyze_logs(self, duration_minutes=3):
        """Analyse les logs pendant la durée spécifiée"""
        print(f"🔍 Début de l'analyse des logs pour {duration_minutes} minutes...")
        print("=" * 80)
        
        end_time = time.time() + (duration_minutes * 60)
        
        while time.time() < end_time:
            try:
                if self.serial_conn.in_waiting > 0:
                    line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        self.log_count += 1
                        
                        # Parse les différents types d'événements
                        drift_correction = self.parse_drift_correction(line)
                        if drift_correction:
                            self.drift_corrections.append(drift_correction)
                            print(f"🕐 CORRECTION DÉRIVE: {drift_correction['correction_seconds']}s (PPM: {drift_correction['drift_ppm']})")
                        
                        ntp_sync = self.parse_ntp_sync(line)
                        if ntp_sync:
                            self.ntp_syncs.append(ntp_sync)
                            print(f"🌐 NTP SYNC: {line.strip()}")
                        
                        time_drift_event = self.parse_time_drift_monitor(line)
                        if time_drift_event:
                            self.time_drift_events.append(time_drift_event)
                            print(f"⏰ TIME DRIFT: {line.strip()}")
                        
                        system_event = self.parse_system_events(line)
                        if system_event:
                            self.system_events.append(system_event)
                            # Afficher seulement les événements importants
                            if any(keyword in line for keyword in ['WiFi', 'NTP', 'Power']):
                                print(f"📡 SYSTEM: {line.strip()}")
                
                time.sleep(0.1)  # Petite pause pour éviter la surcharge CPU
                
            except KeyboardInterrupt:
                print("\n⏹️ Arrêt demandé par l'utilisateur")
                break
            except Exception as e:
                print(f"❌ Erreur lors de la lecture: {e}")
                break
        
        print("\n" + "=" * 80)
        print("📊 ANALYSE TERMINÉE")
        self.generate_report()
    
    def generate_report(self):
        """Génère un rapport d'analyse"""
        print(f"\n📈 RÉSULTATS DE L'ANALYSE:")
        print(f"   • Logs analysés: {self.log_count}")
        print(f"   • Corrections de dérive: {len(self.drift_corrections)}")
        print(f"   • Synchronisations NTP: {len(self.ntp_syncs)}")
        print(f"   • Événements TimeDrift: {len(self.time_drift_events)}")
        print(f"   • Événements système: {len(self.system_events)}")
        
        if self.drift_corrections:
            print(f"\n🕐 CORRECTIONS DE DÉRIVE DÉTECTÉES:")
            total_correction = sum(c['correction_seconds'] for c in self.drift_corrections)
            avg_drift_ppm = sum(c['drift_ppm'] for c in self.drift_corrections) / len(self.drift_corrections)
            
            print(f"   • Nombre de corrections: {len(self.drift_corrections)}")
            print(f"   • Correction totale: {total_correction} secondes")
            print(f"   • Dérive moyenne: {avg_drift_ppm:.2f} PPM")
            print(f"   • Intervalle entre corrections: {self.calculate_correction_interval():.1f} secondes")
            
            print(f"\n📋 DÉTAIL DES CORRECTIONS:")
            for i, correction in enumerate(self.drift_corrections, 1):
                print(f"   {i}. {correction['timestamp']}: {correction['correction_seconds']}s (PPM: {correction['drift_ppm']})")
        
        if self.ntp_syncs:
            print(f"\n🌐 SYNCHRONISATIONS NTP:")
            for sync in self.ntp_syncs:
                print(f"   • {sync['timestamp']}: {sync['line']}")
        
        # Sauvegarder les résultats
        self.save_results()
    
    def calculate_correction_interval(self):
        """Calcule l'intervalle moyen entre les corrections"""
        if len(self.drift_corrections) < 2:
            return 0
        
        intervals = []
        for i in range(1, len(self.drift_corrections)):
            prev_time = datetime.datetime.fromisoformat(self.drift_corrections[i-1]['timestamp'])
            curr_time = datetime.datetime.fromisoformat(self.drift_corrections[i]['timestamp'])
            interval = (curr_time - prev_time).total_seconds()
            intervals.append(interval)
        
        return sum(intervals) / len(intervals) if intervals else 0
    
    def save_results(self):
        """Sauvegarde les résultats dans un fichier JSON"""
        results = {
            'analysis_timestamp': datetime.datetime.now().isoformat(),
            'duration_minutes': 3,
            'total_logs': self.log_count,
            'drift_corrections': self.drift_corrections,
            'ntp_syncs': self.ntp_syncs,
            'time_drift_events': self.time_drift_events,
            'system_events': self.system_events
        }
        
        filename = f"drift_analysis_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        try:
            with open(filename, 'w', encoding='utf-8') as f:
                json.dump(results, f, indent=2, ensure_ascii=False)
            print(f"\n💾 Résultats sauvegardés dans: {filename}")
        except Exception as e:
            print(f"❌ Erreur lors de la sauvegarde: {e}")

def main():
    print("🔬 ANALYSEUR DE DÉRIVE TEMPORELLE ESP32")
    print("=" * 50)
    
    analyzer = DriftLogAnalyzer()
    
    if not analyzer.connect():
        print("❌ Impossible de se connecter au port série")
        return
    
    try:
        analyzer.analyze_logs(duration_minutes=3)
    except KeyboardInterrupt:
        print("\n⏹️ Arrêt demandé par l'utilisateur")
    finally:
        if analyzer.serial_conn:
            analyzer.serial_conn.close()
            print("🔌 Connexion série fermée")

if __name__ == "__main__":
    main()
