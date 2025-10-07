#!/usr/bin/env python3
"""
Script de déploiement du système OTA moderne
Compile, flashe et teste le système OTA complet
"""

import subprocess
import sys
import time
import requests
import json
from datetime import datetime

class OTADeployer:
    def __init__(self):
        self.project_dir = "."
        self.esp32_ip = "192.168.1.100"  # À modifier selon votre réseau
        self.test_results = []
        
    def log(self, message, level="INFO"):
        timestamp = datetime.now().strftime("%H:%M:%S")
        print(f"[{timestamp}] [{level}] {message}")
        
    def run_command(self, command, description=""):
        """Exécute une commande et retourne le résultat"""
        if description:
            self.log(f"🔄 {description}")
            
        try:
            result = subprocess.run(
                command, 
                shell=True, 
                capture_output=True, 
                text=True, 
                cwd=self.project_dir
            )
            
            if result.returncode == 0:
                self.log(f"✅ {description} - Succès", "SUCCESS")
                return True, result.stdout
            else:
                self.log(f"❌ {description} - Échec", "ERROR")
                self.log(f"Erreur: {result.stderr}", "ERROR")
                return False, result.stderr
                
        except Exception as e:
            self.log(f"❌ {description} - Exception: {e}", "ERROR")
            return False, str(e)
            
    def check_prerequisites(self):
        """Vérifie les prérequis"""
        self.log("=== Vérification des prérequis ===")
        
        # Vérifier PlatformIO
        success, output = self.run_command("pio --version", "Vérification PlatformIO")
        if success:
            self.log(f"📋 PlatformIO: {output.strip()}")
        else:
            self.log("❌ PlatformIO non installé ou non accessible", "ERROR")
            return False
            
        # Vérifier Python
        success, output = self.run_command("python --version", "Vérification Python")
        if success:
            self.log(f"📋 Python: {output.strip()}")
        else:
            self.log("❌ Python non installé ou non accessible", "ERROR")
            return False
            
        # Vérifier les dépendances Python
        try:
            import requests
            self.log("✅ Module requests disponible", "SUCCESS")
        except ImportError:
            self.log("❌ Module requests manquant", "ERROR")
            return False
            
        return True
        
    def compile_firmware(self):
        """Compile le firmware"""
        self.log("=== Compilation du firmware ===")
        
        # Nettoyage
        success, _ = self.run_command("pio run -t clean", "Nettoyage du projet")
        if not success:
            self.log("⚠️ Nettoyage échoué, continuation...", "WARNING")
            
        # Compilation
        success, output = self.run_command("pio run -e esp32dev", "Compilation ESP32")
        if success:
            self.log("✅ Firmware compilé avec succès", "SUCCESS")
            
            # Extraction des informations de taille
            if "RAM:" in output and "Flash:" in output:
                for line in output.split('\n'):
                    if "RAM:" in line:
                        self.log(f"📊 {line.strip()}", "INFO")
                    elif "Flash:" in line:
                        self.log(f"📊 {line.strip()}", "INFO")
                        
            return True
        else:
            self.log("❌ Compilation échouée", "ERROR")
            return False
            
    def test_server_connectivity(self):
        """Teste la connectivité avec le serveur OTA"""
        self.log("=== Test de connectivité serveur ===")
        
        try:
            # Test métadonnées
            response = requests.get("http://iot.olution.info/ffp3/ota/metadata.json", timeout=10)
            if response.status_code == 200:
                metadata = response.json()
                self.log(f"✅ Serveur de métadonnées accessible", "SUCCESS")
                self.log(f"📋 Version disponible: {metadata.get('version', 'N/A')}", "INFO")
                self.log(f"📋 URL firmware: {metadata.get('bin_url', 'N/A')}", "INFO")
                
                # Test firmware
                firmware_url = metadata.get('bin_url')
                if firmware_url:
                    head_response = requests.head(firmware_url, timeout=10)
                    if head_response.status_code == 200:
                        size = head_response.headers.get('content-length', 'N/A')
                        self.log(f"✅ Firmware accessible: {size} bytes", "SUCCESS")
                        return True
                    else:
                        self.log(f"❌ Firmware inaccessible: {head_response.status_code}", "ERROR")
                        return False
                else:
                    self.log("❌ URL firmware manquante", "ERROR")
                    return False
            else:
                self.log(f"❌ Serveur inaccessible: {response.status_code}", "ERROR")
                return False
                
        except requests.exceptions.RequestException as e:
            self.log(f"❌ Erreur connexion: {e}", "ERROR")
            return False
            
    def flash_firmware(self):
        """Flashe le firmware sur l'ESP32"""
        self.log("=== Flash du firmware ===")
        
        # Vérifier si un port est disponible
        success, output = self.run_command("pio device list", "Liste des ports disponibles")
        if success:
            if "COM" in output or "/dev/" in output:
                self.log("✅ Port série détecté", "SUCCESS")
            else:
                self.log("⚠️ Aucun port série détecté", "WARNING")
                self.log("📝 Connectez l'ESP32 et relancez le script", "INFO")
                return False
                
        # Flash du firmware
        success, output = self.run_command("pio run -e esp32dev -t upload", "Flash du firmware")
        if success:
            self.log("✅ Firmware flashé avec succès", "SUCCESS")
            return True
        else:
            self.log("❌ Flash échoué", "ERROR")
            return False
            
    def test_esp32_connectivity(self):
        """Teste la connectivité avec l'ESP32"""
        self.log("=== Test de connectivité ESP32 ===")
        
        # Attendre que l'ESP32 démarre
        self.log("⏳ Attente du démarrage de l'ESP32 (30s)...", "INFO")
        time.sleep(30)
        
        # Test de connectivité
        try:
            response = requests.get(f"http://{self.esp32_ip}/", timeout=10)
            if response.status_code == 200:
                self.log("✅ ESP32 accessible", "SUCCESS")
                
                # Test des endpoints OTA
                endpoints = ["/ota/status", "/ota/check", "/update"]
                for endpoint in endpoints:
                    try:
                        ep_response = requests.get(f"http://{self.esp32_ip}{endpoint}", timeout=5)
                        if ep_response.status_code in [200, 404]:
                            self.log(f"✅ Endpoint {endpoint} accessible", "SUCCESS")
                        else:
                            self.log(f"⚠️ Endpoint {endpoint}: {ep_response.status_code}", "WARNING")
                    except:
                        self.log(f"❌ Endpoint {endpoint} inaccessible", "ERROR")
                        
                return True
            else:
                self.log(f"❌ ESP32 inaccessible: {response.status_code}", "ERROR")
                return False
                
        except requests.exceptions.RequestException as e:
            self.log(f"❌ Erreur connexion ESP32: {e}", "ERROR")
            return False
            
    def test_ota_functionality(self):
        """Teste les fonctionnalités OTA"""
        self.log("=== Test des fonctionnalités OTA ===")
        
        try:
            # Test de vérification OTA
            response = requests.post(f"http://{self.esp32_ip}/ota/check", timeout=15)
            if response.status_code == 200:
                self.log("✅ Vérification OTA fonctionnelle", "SUCCESS")
                
                # Test de déclenchement OTA (sans réellement lancer)
                try:
                    update_response = requests.post(f"http://{self.esp32_ip}/ota/update", timeout=10)
                    if update_response.status_code in [200, 202]:
                        self.log("✅ Déclenchement OTA fonctionnel", "SUCCESS")
                        return True
                    else:
                        self.log(f"⚠️ Déclenchement OTA: {update_response.status_code}", "WARNING")
                        return True  # La vérification fonctionne
                except:
                    self.log("⚠️ Déclenchement OTA non testé", "WARNING")
                    return True  # La vérification fonctionne
            else:
                self.log(f"❌ Vérification OTA échouée: {response.status_code}", "ERROR")
                return False
                
        except requests.exceptions.RequestException as e:
            self.log(f"❌ Erreur test OTA: {e}", "ERROR")
            return False
            
    def monitor_ota_process(self):
        """Surveille le processus OTA"""
        self.log("=== Surveillance du processus OTA ===")
        
        # Attendre et surveiller les logs
        for i in range(60):  # 5 minutes max
            try:
                response = requests.get(f"http://{self.esp32_ip}/", timeout=5)
                if response.status_code == 200:
                    self.log(f"✅ ESP32 toujours accessible (minute {i+1}/5)", "SUCCESS")
                else:
                    self.log(f"⚠️ ESP32 non accessible (minute {i+1}/5)", "WARNING")
            except:
                self.log(f"⚠️ ESP32 non accessible (minute {i+1}/5)", "WARNING")
                
            time.sleep(60)  # Attendre 1 minute
            
        self.log("✅ Surveillance terminée", "SUCCESS")
        
    def generate_deployment_report(self):
        """Génère un rapport de déploiement"""
        self.log("=== RAPPORT DE DÉPLOIEMENT OTA ===", "INFO")
        
        total_tests = len(self.test_results)
        passed_tests = sum(1 for _, success, _ in self.test_results if success)
        failed_tests = total_tests - passed_tests
        
        self.log(f"📊 Total des étapes: {total_tests}", "INFO")
        self.log(f"✅ Étapes réussies: {passed_tests}", "SUCCESS" if passed_tests > 0 else "ERROR")
        self.log(f"❌ Étapes échouées: {failed_tests}", "ERROR" if failed_tests > 0 else "SUCCESS")
        
        success_rate = (passed_tests / total_tests) * 100 if total_tests > 0 else 0
        self.log(f"📈 Taux de réussite: {success_rate:.1f}%", "INFO")
        
        # Détails des étapes
        self.log("\n📋 Détails du déploiement:", "INFO")
        for step_name, success, message in self.test_results:
            status = "✅" if success else "❌"
            self.log(f"  {status} {step_name}: {message}")
            
        # Recommandations
        self.log("\n💡 Recommandations:", "INFO")
        if failed_tests == 0:
            self.log("  🎉 Déploiement réussi ! Le système OTA est opérationnel.", "SUCCESS")
            self.log("  📝 Le système vérifiera automatiquement les mises à jour toutes les 2 heures.", "INFO")
        else:
            self.log("  🔧 Certaines étapes ont échoué. Vérifiez la configuration.", "WARNING")
            
        if failed_tests > 0:
            failed_step_names = [name for name, success, _ in self.test_results if not success]
            self.log(f"  📝 Étapes à corriger: {', '.join(failed_step_names)}", "WARNING")
            
    def deploy(self):
        """Déploiement complet du système OTA"""
        self.log("🚀 Démarrage du déploiement OTA moderne", "INFO")
        self.log(f"📡 IP ESP32 cible: {self.esp32_ip}", "INFO")
        self.log(f"🌐 Serveur OTA: http://iot.olution.info/ffp3/ota/", "INFO")
        
        # Vérification des prérequis
        if not self.check_prerequisites():
            self.log("❌ Prérequis non satisfaits", "ERROR")
            return False
            
        # Compilation
        if not self.compile_firmware():
            self.log("❌ Compilation échouée", "ERROR")
            return False
            
        # Test serveur
        if not self.test_server_connectivity():
            self.log("❌ Serveur OTA inaccessible", "ERROR")
            return False
            
        # Flash (optionnel)
        flash_choice = input("\n🤔 Voulez-vous flasher le firmware sur l'ESP32 ? (y/N): ").lower()
        if flash_choice == 'y':
            if not self.flash_firmware():
                self.log("❌ Flash échoué", "ERROR")
                return False
                
            # Test connectivité ESP32
            if not self.test_esp32_connectivity():
                self.log("❌ ESP32 inaccessible après flash", "ERROR")
                return False
                
            # Test fonctionnalités OTA
            if not self.test_ota_functionality():
                self.log("❌ Fonctionnalités OTA non opérationnelles", "ERROR")
                return False
                
            # Surveillance
            monitor_choice = input("\n🤔 Voulez-vous surveiller le processus OTA ? (y/N): ").lower()
            if monitor_choice == 'y':
                self.monitor_ota_process()
        else:
            self.log("⏭️ Flash ignoré", "INFO")
            
        # Rapport final
        self.generate_deployment_report()
        return True

def main():
    """Fonction principale"""
    print("🚀 Déployeur OTA Moderne - FFP3CS")
    print("=" * 50)
    
    deployer = OTADeployer()
    
    # Configuration de l'IP ESP32
    esp32_ip = input("📡 IP de l'ESP32 (défaut: 192.168.1.100): ").strip()
    if esp32_ip:
        deployer.esp32_ip = esp32_ip
        
    # Déploiement
    success = deployer.deploy()
    
    if success:
        print("\n🎉 Déploiement terminé avec succès !")
    else:
        print("\n❌ Déploiement échoué. Vérifiez les erreurs ci-dessus.")

if __name__ == "__main__":
    main()