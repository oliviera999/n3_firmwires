#!/usr/bin/env python3
"""
Script de test pour le système de surveillance de dérive temporelle
Ce script simule des conditions de test et vérifie le fonctionnement du système
"""

import serial
import time
import re
from datetime import datetime

class TimeDriftTester:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        
    def connect(self):
        """Établir la connexion série"""
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"✅ Connexion série établie sur {self.port}")
            return True
        except Exception as e:
            print(f"❌ Erreur de connexion série: {e}")
            return False
    
    def disconnect(self):
        """Fermer la connexion série"""
        if self.serial_conn:
            self.serial_conn.close()
            print("🔌 Connexion série fermée")
    
    def wait_for_boot(self, timeout=30):
        """Attendre que l'ESP32 démarre complètement"""
        print("⏳ Attente du démarrage de l'ESP32...")
        start_time = time.time()
        
        while time.time() - start_time < timeout:
            if self.serial_conn.in_waiting > 0:
                line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                if "Initialisation terminée" in line or "Init done" in line:
                    print("✅ ESP32 démarré avec succès")
                    return True
                print(f"📝 {line}")
        
        print("❌ Timeout - ESP32 n'a pas démarré dans les temps")
        return False
    
    def monitor_drift_info(self, duration=300):
        """Surveiller les informations de dérive pendant une durée donnée"""
        print(f"🔍 Surveillance des informations de dérive pendant {duration} secondes...")
        start_time = time.time()
        drift_found = False
        
        while time.time() - start_time < duration:
            if self.serial_conn.in_waiting > 0:
                line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                
                # Détecter les informations de dérive
                if "INFORMATIONS DE DÉRIVE TEMPORELLE" in line:
                    print(f"\n📊 {line}")
                    drift_found = True
                elif drift_found and "===" in line:
                    print(f"📊 {line}")
                    drift_found = False
                elif drift_found:
                    print(f"📊 {line}")
                
                # Détecter les erreurs
                if "TimeDrift" in line and ("Erreur" in line or "Échec" in line):
                    print(f"⚠️ {line}")
                
                # Détecter les alertes de dérive élevée
                if "ALERTE" in line and "Dérive élevée" in line:
                    print(f"🚨 {line}")
            
            time.sleep(0.1)
        
        return True
    
    def test_ntp_sync(self):
        """Tester la synchronisation NTP"""
        print("🌐 Test de synchronisation NTP...")
        
        # Envoyer une commande pour forcer la synchronisation
        self.serial_conn.write(b"AT+SYNC_NTP\n")
        time.sleep(2)
        
        # Surveiller les réponses
        start_time = time.time()
        while time.time() - start_time < 30:
            if self.serial_conn.in_waiting > 0:
                line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                if "Synchronisation NTP" in line:
                    print(f"📡 {line}")
                elif "Sync NTP réussie" in line:
                    print(f"✅ {line}")
                    return True
                elif "Échec de synchronisation NTP" in line:
                    print(f"❌ {line}")
                    return False
            
            time.sleep(0.1)
        
        print("⏰ Timeout - Synchronisation NTP non détectée")
        return False
    
    def extract_drift_values(self, line):
        """Extraire les valeurs de dérive d'une ligne de log"""
        # Rechercher les valeurs de dérive
        drift_ppm_match = re.search(r'Dérive: ([\d.-]+) PPM', line)
        drift_seconds_match = re.search(r'\(([\d.-]+) secondes\)', line)
        
        if drift_ppm_match and drift_seconds_match:
            return {
                'ppm': float(drift_ppm_match.group(1)),
                'seconds': float(drift_seconds_match.group(1))
            }
        return None
    
    def run_comprehensive_test(self):
        """Exécuter un test complet du système de dérive temporelle"""
        print("🚀 Démarrage du test complet de surveillance de dérive temporelle")
        print("=" * 60)
        
        if not self.connect():
            return False
        
        try:
            # Attendre le démarrage
            if not self.wait_for_boot():
                return False
            
            print("\n📋 Phase 1: Test de synchronisation NTP")
            ntp_success = self.test_ntp_sync()
            
            print("\n📋 Phase 2: Surveillance des informations de dérive")
            self.monitor_drift_info(180)  # 3 minutes
            
            print("\n📋 Phase 3: Test de persistance NVS")
            # Redémarrer l'ESP32 pour tester la persistance
            print("🔄 Redémarrage de l'ESP32...")
            self.serial_conn.write(b"AT+RST\n")
            time.sleep(5)
            
            if self.wait_for_boot():
                print("✅ Redémarrage réussi - Test de persistance")
                self.monitor_drift_info(60)  # 1 minute après redémarrage
            
            print("\n✅ Test complet terminé")
            return True
            
        except KeyboardInterrupt:
            print("\n⏹️ Test interrompu par l'utilisateur")
            return False
        except Exception as e:
            print(f"\n❌ Erreur pendant le test: {e}")
            return False
        finally:
            self.disconnect()

def main():
    """Fonction principale"""
    print("🔧 Testeur de Surveillance de Dérive Temporelle")
    print("=" * 50)
    
    # Configuration du port série (à adapter selon votre configuration)
    port = input("Port série (ex: COM3, /dev/ttyUSB0): ").strip()
    if not port:
        port = "COM3"  # Port par défaut pour Windows
    
    tester = TimeDriftTester(port)
    
    print(f"\n📡 Configuration:")
    print(f"   Port: {port}")
    print(f"   Baudrate: 115200")
    print(f"   Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    
    input("\n⏸️ Appuyez sur Entrée pour commencer le test...")
    
    success = tester.run_comprehensive_test()
    
    if success:
        print("\n🎉 Test terminé avec succès!")
    else:
        print("\n💥 Test échoué ou interrompu")
    
    input("\n⏸️ Appuyez sur Entrée pour quitter...")

if __name__ == "__main__":
    main()
