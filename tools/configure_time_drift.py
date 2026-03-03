#!/usr/bin/env python3
"""
Script de configuration pour le système de surveillance de dérive temporelle
Permet de configurer les paramètres du moniteur via l'interface série
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "monitor"))

import serial
import time
import json
from datetime import datetime

from serial_utils import open_serial_with_release

class TimeDriftConfigurator:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        
    def connect(self):
        """Établir la connexion série"""
        try:
            self.serial_conn = open_serial_with_release(self.port, self.baudrate, timeout=1)
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
    
    def send_command(self, command, timeout=5):
        """Envoyer une commande et attendre la réponse"""
        if not self.serial_conn:
            return None
        
        print(f"📤 Envoi: {command}")
        self.serial_conn.write(f"{command}\n".encode())
        
        start_time = time.time()
        response = ""
        
        while time.time() - start_time < timeout:
            if self.serial_conn.in_waiting > 0:
                line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    response += line + "\n"
                    print(f"📥 Réponse: {line}")
            
            time.sleep(0.1)
        
        return response
    
    def get_current_config(self):
        """Récupérer la configuration actuelle"""
        print("\n📋 Récupération de la configuration actuelle...")
        
        # Commande pour récupérer la configuration (à implémenter dans le firmware)
        response = self.send_command("AT+GET_DRIFT_CONFIG")
        
        if response and "TimeDrift" in response:
            print("✅ Configuration récupérée")
            return response
        else:
            print("❌ Impossible de récupérer la configuration")
            return None
    
    def set_sync_interval(self, interval_minutes):
        """Configurer l'intervalle de synchronisation NTP"""
        print(f"\n⏰ Configuration de l'intervalle de sync: {interval_minutes} minutes")
        
        interval_ms = interval_minutes * 60 * 1000
        response = self.send_command(f"AT+SET_SYNC_INTERVAL={interval_ms}")
        
        if response and "OK" in response:
            print(f"✅ Intervalle de sync configuré: {interval_minutes} minutes")
            return True
        else:
            print(f"❌ Échec de configuration de l'intervalle")
            return False
    
    def set_drift_threshold(self, threshold_ppm):
        """Configurer le seuil d'alerte de dérive"""
        print(f"\n⚠️ Configuration du seuil d'alerte: {threshold_ppm} PPM")
        
        response = self.send_command(f"AT+SET_DRIFT_THRESHOLD={threshold_ppm}")
        
        if response and "OK" in response:
            print(f"✅ Seuil d'alerte configuré: {threshold_ppm} PPM")
            return True
        else:
            print(f"❌ Échec de configuration du seuil")
            return False
    
    def force_ntp_sync(self):
        """Forcer une synchronisation NTP"""
        print("\n🌐 Forçage de la synchronisation NTP...")
        
        response = self.send_command("AT+FORCE_NTP_SYNC", timeout=30)
        
        if response and "Sync NTP réussie" in response:
            print("✅ Synchronisation NTP réussie")
            return True
        else:
            print("❌ Échec de la synchronisation NTP")
            return False
    
    def get_drift_status(self):
        """Récupérer le statut actuel de la dérive"""
        print("\n📊 Récupération du statut de dérive...")
        
        response = self.send_command("AT+GET_DRIFT_STATUS")
        
        if response and "TimeDrift" in response:
            print("✅ Statut de dérive récupéré")
            return response
        else:
            print("❌ Impossible de récupérer le statut")
            return None
    
    def reset_drift_data(self):
        """Réinitialiser les données de dérive"""
        print("\n🔄 Réinitialisation des données de dérive...")
        
        response = self.send_command("AT+RESET_DRIFT_DATA")
        
        if response and "OK" in response:
            print("✅ Données de dérive réinitialisées")
            return True
        else:
            print("❌ Échec de la réinitialisation")
            return False
    
    def interactive_config(self):
        """Configuration interactive"""
        print("\n🔧 Configuration Interactive du Moniteur de Dérive Temporelle")
        print("=" * 60)
        
        while True:
            print("\n📋 Options disponibles:")
            print("1. Afficher la configuration actuelle")
            print("2. Configurer l'intervalle de synchronisation NTP")
            print("3. Configurer le seuil d'alerte de dérive")
            print("4. Forcer une synchronisation NTP")
            print("5. Afficher le statut de dérive")
            print("6. Réinitialiser les données de dérive")
            print("7. Quitter")
            
            choice = input("\n🎯 Votre choix (1-7): ").strip()
            
            if choice == "1":
                self.get_current_config()
            elif choice == "2":
                try:
                    interval = int(input("Intervalle de sync (minutes): "))
                    if interval < 5:
                        print("❌ Intervalle minimum: 5 minutes")
                    else:
                        self.set_sync_interval(interval)
                except ValueError:
                    print("❌ Valeur invalide")
            elif choice == "3":
                try:
                    threshold = float(input("Seuil d'alerte (PPM): "))
                    if threshold < 1:
                        print("❌ Seuil minimum: 1 PPM")
                    else:
                        self.set_drift_threshold(threshold)
                except ValueError:
                    print("❌ Valeur invalide")
            elif choice == "4":
                self.force_ntp_sync()
            elif choice == "5":
                self.get_drift_status()
            elif choice == "6":
                confirm = input("⚠️ Confirmer la réinitialisation? (oui/non): ")
                if confirm.lower() in ['oui', 'o', 'yes', 'y']:
                    self.reset_drift_data()
                else:
                    print("❌ Réinitialisation annulée")
            elif choice == "7":
                print("👋 Au revoir!")
                break
            else:
                print("❌ Choix invalide")
    
    def load_config_from_file(self, filename):
        """Charger la configuration depuis un fichier JSON"""
        try:
            with open(filename, 'r') as f:
                config = json.load(f)
            
            print(f"📁 Chargement de la configuration depuis {filename}")
            
            if 'sync_interval_minutes' in config:
                self.set_sync_interval(config['sync_interval_minutes'])
            
            if 'drift_threshold_ppm' in config:
                self.set_drift_threshold(config['drift_threshold_ppm'])
            
            print("✅ Configuration chargée avec succès")
            return True
            
        except FileNotFoundError:
            print(f"❌ Fichier {filename} non trouvé")
            return False
        except json.JSONDecodeError:
            print(f"❌ Erreur de format JSON dans {filename}")
            return False
        except Exception as e:
            print(f"❌ Erreur lors du chargement: {e}")
            return False
    
    def save_config_to_file(self, filename):
        """Sauvegarder la configuration actuelle dans un fichier JSON"""
        try:
            config = {
                'timestamp': datetime.now().isoformat(),
                'sync_interval_minutes': 60,  # Valeur par défaut
                'drift_threshold_ppm': 100.0,  # Valeur par défaut
                'description': 'Configuration du moniteur de dérive temporelle'
            }
            
            with open(filename, 'w') as f:
                json.dump(config, f, indent=2)
            
            print(f"💾 Configuration sauvegardée dans {filename}")
            return True
            
        except Exception as e:
            print(f"❌ Erreur lors de la sauvegarde: {e}")
            return False

def main():
    """Fonction principale"""
    print("⚙️ Configurateur de Surveillance de Dérive Temporelle")
    print("=" * 55)
    
    # Configuration du port série
    port = input("Port série (ex: COM3, /dev/ttyUSB0): ").strip()
    if not port:
        port = "COM3"  # Port par défaut pour Windows
    
    configurator = TimeDriftConfigurator(port)
    
    if not configurator.connect():
        return
    
    try:
        configurator.interactive_config()
    except KeyboardInterrupt:
        print("\n⏹️ Configuration interrompue par l'utilisateur")
    except Exception as e:
        print(f"\n❌ Erreur pendant la configuration: {e}")
    finally:
        configurator.disconnect()

if __name__ == "__main__":
    main()
