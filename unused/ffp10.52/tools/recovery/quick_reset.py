#!/usr/bin/env python3
"""
Script rapide pour reset de l'ESP32
Usage: python quick_reset_esp32.py [option]
Options:
  --complete    Reset complet (flash + NVS + OTA)
  --nvs         Reset NVS seulement
  --ota         Reset OTA seulement
  --fs          Reset système de fichiers
  --help        Afficher cette aide
"""

import subprocess
import sys
import os

def run_command(command, description):
    """Exécute une commande et affiche le résultat"""
    print(f"🔄 {description}...")
    print(f"Commande: {command}")
    
    try:
        result = subprocess.run(command, shell=True, check=True, 
                              capture_output=True, text=True)
        print("✅ Succès!")
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Erreur: {e}")
        if e.stderr:
            print(f"Erreur: {e.stderr}")
        return False

def reset_complete():
    """Reset complet de la mémoire flash"""
    print("🔥 RESET COMPLET DE LA MÉMOIRE FLASH")
    
    # 1. Erase flash complet
    if not run_command("pio run -t erase", "Effacement de la flash"):
        print("⚠️  Tentative avec esptool...")
        run_command("esptool.py --chip esp32s3 erase_flash", "Effacement avec esptool")
    
    # 2. Upload du firmware
    if not run_command("pio run -t upload", "Upload du firmware"):
        print("❌ Upload échoué!")
        return False
    
    print("✅ Reset complet terminé!")

def reset_nvs():
    """Reset de la NVS seulement"""
    print("🗂️  RESET DE LA NVS")
    
    run_command("esptool.py --chip esp32s3 erase_region 0x9000 0x6000", 
               "Effacement partition NVS")
    print("✅ Reset NVS terminé!")

def reset_ota():
    """Reset des données OTA seulement"""
    print("🔄 RESET DES DONNÉES OTA")
    
    run_command("esptool.py --chip esp32s3 erase_region 0x150000 0x100000", 
               "Effacement partition OTA")
    print("✅ Reset OTA terminé!")

def reset_filesystem():
    """Reset du système de fichiers"""
    print("📁 RESET DU SYSTÈME DE FICHIERS")
    
    if not run_command("pio run -t uploadfs", "Upload système de fichiers"):
        print("❌ Upload filesystem échoué!")
        return False
    
    print("✅ Reset filesystem terminé!")

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    
    option = sys.argv[1].lower()
    
    # Vérifier que nous sommes dans le bon répertoire
    if not os.path.exists("platformio.ini"):
        print("❌ Erreur: platformio.ini non trouvé. Assurez-vous d'être dans le répertoire du projet.")
        sys.exit(1)
    
    if option == "--complete":
        reset_complete()
    elif option == "--nvs":
        reset_nvs()
    elif option == "--ota":
        reset_ota()
    elif option == "--fs":
        reset_filesystem()
    elif option == "--help":
        print(__doc__)
    else:
        print(f"❌ Option inconnue: {option}")
        print(__doc__)
        sys.exit(1)

if __name__ == "__main__":
    main() 