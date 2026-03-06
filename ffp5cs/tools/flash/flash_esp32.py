#!/usr/bin/env python3
"""
Script pour flasher l'ESP32-WROOM avec la configuration FFP3
"""

import subprocess
import sys
import os

def run_command(command, description):
    """Exécute une commande et affiche le résultat"""
    print(f"\n=== {description} ===")
    print(f"Commande: {command}")
    
    try:
        result = subprocess.run(command, shell=True, check=True, 
                              capture_output=True, text=True)
        print("✅ Succès!")
        if result.stdout:
            print("Sortie:", result.stdout)
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Erreur: {e}")
        if e.stdout:
            print("Sortie:", e.stdout)
        if e.stderr:
            print("Erreur:", e.stderr)
        return False

def main():
    print("🚀 Script de flash pour ESP32-WROOM - FFP3 Project")
    print("=" * 50)
    
    # Vérifier que nous sommes dans le bon répertoire
    if not os.path.exists("platformio.ini"):
        print("❌ Erreur: platformio.ini non trouvé. Assurez-vous d'être dans le répertoire du projet.")
        sys.exit(1)
    
    # 1. Nettoyer le projet
    if not run_command("pio run -t clean", "Nettoyage du projet"):
        print("⚠️  Nettoyage échoué, continuation...")
    
    # 2. Compiler pour ESP32-WROOM
    if not run_command("pio run -e esp32-wroom", "Compilation pour ESP32-WROOM"):
        print("❌ Compilation échouée!")
        sys.exit(1)
    
    # 3. Flasher l'ESP32-WROOM
    print("\n🔌 Connectez l'ESP32-WROOM via USB et appuyez sur Entrée pour continuer...")
    input()
    
    if not run_command("pio run -e esp32-wroom -t upload", "Flash de l'ESP32-WROOM"):
        print("❌ Flash échoué!")
        sys.exit(1)
    
    # 4. Ouvrir le moniteur série
    print("\n📺 Ouverture du moniteur série...")
    print("Appuyez sur Ctrl+C pour arrêter le moniteur")
    
    try:
        subprocess.run("pio device monitor", shell=True)
    except KeyboardInterrupt:
        print("\n👋 Moniteur arrêté par l'utilisateur")
    
    print("\n✅ Flash terminé avec succès!")

if __name__ == "__main__":
    main() 