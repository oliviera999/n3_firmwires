#!/usr/bin/env python3
"""
Script de monitoring en temps réel pour l'ESP32 et l'OTA
Surveille les logs série et teste la connectivité
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import serial
import requests
import time
import threading
from datetime import datetime

from serial_utils import open_serial_with_release

class OTAMonitor:
    def __init__(self, port="COM6", baudrate=115200, esp32_ip="192.168.1.100"):
        self.port = port
        self.baudrate = baudrate
        self.esp32_ip = esp32_ip
        self.serial_conn = None
        self.monitoring = False
        
    def log(self, message, level="INFO"):
        timestamp = datetime.now().strftime("%H:%M:%S")
        print(f"[{timestamp}] [{level}] {message}")
        
    def connect_serial(self):
        """Connexion au port série"""
        try:
            self.serial_conn = open_serial_with_release(
                port=self.port,
                baudrate=self.baudrate,
                timeout=1
            )
            self.log(f"✅ Connexion série établie sur {self.port}", "SUCCESS")
            return True
        except Exception as e:
            self.log(f"❌ Erreur connexion série: {e}", "ERROR")
            return False
            
    def monitor_serial(self):
        """Monitoring des logs série"""
        if not self.serial_conn:
            return
            
        self.log("🔍 Début du monitoring série...", "INFO")
        
        while self.monitoring:
            try:
                if self.serial_conn.in_waiting:
                    line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        # Filtrer les logs OTA
                        if "[OTA]" in line:
                            self.log(f"📡 OTA: {line}", "OTA")
                        elif "WiFi" in line or "wifi" in line:
                            self.log(f"📶 WiFi: {line}", "WIFI")
                        elif "IP" in line:
                            self.log(f"🌐 IP: {line}", "NETWORK")
                        elif "ERROR" in line or "error" in line:
                            self.log(f"❌ ERREUR: {line}", "ERROR")
                        else:
                            print(f"    {line}")
            except Exception as e:
                self.log(f"❌ Erreur lecture série: {e}", "ERROR")
                break
                
    def test_connectivity(self):
        """Test de connectivité ESP32"""
        try:
            response = requests.get(f"http://{self.esp32_ip}/", timeout=5)
            if response.status_code == 200:
                self.log(f"✅ ESP32 accessible sur {self.esp32_ip}", "SUCCESS")
                return True
            else:
                self.log(f"⚠️ ESP32 répond avec code {response.status_code}", "WARNING")
                return False
        except requests.exceptions.RequestException as e:
            self.log(f"❌ ESP32 inaccessible: {e}", "ERROR")
            return False
            
    def test_ota_endpoints(self):
        """Test des endpoints OTA"""
        if not self.test_connectivity():
            return False
            
        endpoints = [
            ("/ota/status", "Statut OTA"),
            ("/ota/check", "Vérification OTA"),
            ("/update", "Page OTA (POST /api/ota)")
        ]
        
        for endpoint, description in endpoints:
            try:
                response = requests.get(f"http://{self.esp32_ip}{endpoint}", timeout=5)
                if response.status_code in [200, 404]:
                    self.log(f"✅ {description}: {response.status_code}", "SUCCESS")
                else:
                    self.log(f"⚠️ {description}: {response.status_code}", "WARNING")
            except:
                self.log(f"❌ {description}: inaccessible", "ERROR")
                
    def check_ota_server(self):
        """Vérification du serveur OTA"""
        try:
            response = requests.get("http://iot.olution.info/ffp3/ota/metadata.json", timeout=10)
            if response.status_code == 200:
                metadata = response.json()
                self.log(f"✅ Serveur OTA: Version {metadata.get('version', 'N/A')} disponible", "SUCCESS")
                return metadata
            else:
                self.log(f"❌ Serveur OTA: HTTP {response.status_code}", "ERROR")
                return None
        except Exception as e:
            self.log(f"❌ Serveur OTA: {e}", "ERROR")
            return None
            
    def start_monitoring(self):
        """Démarrage du monitoring complet"""
        self.log("🚀 Démarrage du monitoring OTA en temps réel", "INFO")
        self.log(f"📡 Port série: {self.port}", "INFO")
        self.log(f"🌐 IP ESP32: {self.esp32_ip}", "INFO")
        
        # Connexion série
        if not self.connect_serial():
            self.log("❌ Impossible de se connecter au port série", "ERROR")
            return
            
        self.monitoring = True
        
        # Thread pour le monitoring série
        serial_thread = threading.Thread(target=self.monitor_serial)
        serial_thread.daemon = True
        serial_thread.start()
        
        # Monitoring principal
        try:
            while self.monitoring:
                # Test de connectivité toutes les 30 secondes
                self.test_connectivity()
                
                # Test serveur OTA toutes les 60 secondes
                if time.time() % 60 < 30:  # Toutes les minutes
                    self.check_ota_server()
                    
                # Test endpoints toutes les 2 minutes
                if time.time() % 120 < 30:  # Toutes les 2 minutes
                    self.test_ota_endpoints()
                    
                time.sleep(30)  # Attendre 30 secondes
                
        except KeyboardInterrupt:
            self.log("🛑 Arrêt du monitoring demandé", "INFO")
        finally:
            self.stop_monitoring()
            
    def stop_monitoring(self):
        """Arrêt du monitoring"""
        self.monitoring = False
        if self.serial_conn:
            self.serial_conn.close()
        self.log("✅ Monitoring arrêté", "INFO")

def main():
    print("🔧 Moniteur OTA Temps Réel - FFP3CS")
    print("=" * 50)
    
    # Configuration
    port = "COM6"  # Port série de l'ESP32
    esp32_ip = "192.168.1.100"  # IP de l'ESP32
    
    monitor = OTAMonitor(port=port, esp32_ip=esp32_ip)
    
    try:
        monitor.start_monitoring()
    except KeyboardInterrupt:
        print("\n🛑 Arrêt du programme")
    except Exception as e:
        print(f"❌ Erreur: {e}")

if __name__ == "__main__":
    main()