#!/usr/bin/env python3
"""
Script pour reset complet de la mémoire flash de l'ESP32
Options disponibles:
1. Reset complet (flash + NVS + OTA)
2. Reset partiel (NVS seulement)
3. Reset OTA seulement
4. Reset du système de fichiers
"""

import subprocess
import sys
import os
import time

def run_command(command, description, check=True):
    """Exécute une commande et affiche le résultat"""
    print(f"\n=== {description} ===")
    print(f"Commande: {command}")
    
    try:
        result = subprocess.run(command, shell=True, check=check, 
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

def get_available_ports():
    """Récupère la liste des ports disponibles"""
    try:
        result = subprocess.run("pio device list", shell=True, 
                              capture_output=True, text=True)
        if result.returncode == 0:
            print("📋 Ports disponibles:")
            print(result.stdout)
            return result.stdout
    except Exception as e:
        print(f"❌ Erreur lors de la détection des ports: {e}")
    return None

def reset_complete():
    """Reset complet de la mémoire flash"""
    print("\n🔥 RESET COMPLET DE LA MÉMOIRE FLASH")
    print("=" * 50)
    
    # 1. Erase flash complet
    print("1️⃣  Effacement complet de la flash...")
    if not run_command("pio run -t erase", "Effacement de la flash"):
        print("⚠️  Erase échoué, tentative avec esptool...")
        run_command("esptool.py --chip esp32s3 erase_flash", "Effacement avec esptool", check=False)
    
    # 2. Reset NVS
    print("2️⃣  Reset de la NVS...")
    run_command("pio run -t erase", "Reset NVS", check=False)
    
    # 3. Upload du firmware
    print("3️⃣  Upload du nouveau firmware...")
    if not run_command("pio run -t upload", "Upload du firmware"):
        print("❌ Upload échoué!")
        return False
    
    print("✅ Reset complet terminé!")

def reset_nvs_only():
    """Reset de la NVS seulement"""
    print("\n🗂️  RESET DE LA NVS SEULEMENT")
    print("=" * 50)
    
    # Utiliser esptool pour effacer seulement la partition NVS
    print("🗂️  Effacement de la partition NVS...")
    if not run_command("esptool.py --chip esp32s3 erase_region 0x9000 0x6000", 
                      "Effacement partition NVS", check=False):
        print("⚠️  Erase NVS échoué, tentative alternative...")
        run_command("pio run -t erase", "Reset complet comme fallback", check=False)
    
    print("✅ Reset NVS terminé!")

def reset_ota_only():
    """Reset des données OTA seulement"""
    print("\n🔄 RESET DES DONNÉES OTA")
    print("=" * 50)
    
    # Effacer la partition OTA
    print("🔄 Effacement des données OTA...")
    run_command("esptool.py --chip esp32s3 erase_region 0x150000 0x100000", 
               "Effacement partition OTA", check=False)
    
    print("✅ Reset OTA terminé!")

def reset_filesystem():
    """Reset du système de fichiers"""
    print("\n📁 RESET DU SYSTÈME DE FICHIERS")
    print("=" * 50)
    
    # Upload d'un système de fichiers vide
    print("📁 Upload d'un système de fichiers vide...")
    if not run_command("pio run -t uploadfs", "Upload système de fichiers"):
        print("❌ Upload filesystem échoué!")
        return False
    
    print("✅ Reset filesystem terminé!")

def main():
    print("🚀 Script de Reset Flash ESP32 - FFP3 Project")
    print("=" * 50)
    
    # Vérifier que nous sommes dans le bon répertoire
    if not os.path.exists("platformio.ini"):
        print("❌ Erreur: platformio.ini non trouvé. Assurez-vous d'être dans le répertoire du projet.")
        sys.exit(1)
    
    # Afficher les ports disponibles
    get_available_ports()
    
    print("\n🔌 Assurez-vous que l'ESP32 est connecté via USB")
    print("\nChoisissez le type de reset:")
    print("1. 🔥 Reset complet (flash + NVS + OTA)")
    print("2. 🗂️  Reset NVS seulement")
    print("3. 🔄 Reset OTA seulement")
    print("4. 📁 Reset système de fichiers")
    print("5. 🔍 Voir les ports disponibles")
    print("0. ❌ Quitter")
    
    while True:
        try:
            choice = input("\nVotre choix (0-5): ").strip()
            
            if choice == "0":
                print("👋 Au revoir!")
                break
            elif choice == "1":
                print("\n⚠️  ATTENTION: Ceci va effacer TOUTE la mémoire flash!")
                confirm = input("Êtes-vous sûr? (oui/non): ").strip().lower()
                if confirm in ["oui", "o", "yes", "y"]:
                    reset_complete()
                else:
                    print("❌ Opération annulée")
            elif choice == "2":
                reset_nvs_only()
            elif choice == "3":
                reset_ota_only()
            elif choice == "4":
                reset_filesystem()
            elif choice == "5":
                get_available_ports()
            else:
                print("❌ Choix invalide, veuillez réessayer")
                
        except KeyboardInterrupt:
            print("\n\n👋 Opération annulée par l'utilisateur")
            break
        except Exception as e:
            print(f"❌ Erreur: {e}")

if __name__ == "__main__":
    main() 