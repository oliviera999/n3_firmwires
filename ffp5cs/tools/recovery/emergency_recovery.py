#!/usr/bin/env python3
"""
Emergency Recovery - Récupération d'urgence de l'ESP32 brické
FFP3CS - Emergency Recovery
"""

import subprocess
import time
import os
import sys
from pathlib import Path
from datetime import datetime

# Permettre l'import de serial_utils (tools/monitor)
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "monitor"))

def emergency_recovery():
    """Récupération d'urgence de l'ESP32"""
    
    print("🚨 === RÉCUPÉRATION D'URGENCE ESP32 ===")
    print(f"Début: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    
    try:
        # Étape 1: Nettoyage complet
        print("\n=== ÉTAPE 1: Nettoyage Complet ===")
        
        print("  Nettoyage des builds...")
        result = subprocess.run(
            ["pio", "run", "-e", "esp32dev", "--target", "clean"],
            capture_output=True, text=True, timeout=60
        )
        print("  ✓ Clean terminé")
        
        # Étape 2: Recompilation avec partition standard
        print("\n=== ÉTAPE 2: Recompilation avec Partition Standard ===")
        
        # Temporairement utiliser la partition standard
        print("  Modification temporaire de platformio.ini...")
        
        # Lire le fichier actuel
        with open("platformio.ini", 'r') as f:
            content = f.read()
        
        # Sauvegarder la version actuelle
        with open("platformio.ini.backup", 'w') as f:
            f.write(content)
        
        # Remplacer temporairement par la partition standard
        content = content.replace(
            "board_build.partitions = partitions_esp32_wroom_ota.csv",
            "board_build.partitions = partitions_esp32_wroom.csv"
        )
        
        with open("platformio.ini", 'w') as f:
            f.write(content)
        
        print("  ✓ Partition standard temporairement activée")
        
        # Étape 3: Compilation
        print("  Compilation en cours...")
        result = subprocess.run(
            ["pio", "run", "-e", "esp32dev"],
            capture_output=True, text=True, timeout=300
        )
        
        if result.returncode == 0:
            print("  ✓ Compilation réussie")
        else:
            print(f"  ✗ Erreur compilation: {result.stderr}")
            return False
        
        # Étape 4: Flash d'urgence
        print("\n=== ÉTAPE 3: Flash d'Urgence ===")
        
        print("  Flash en cours (peut prendre plusieurs minutes)...")
        result = subprocess.run(
            ["pio", "run", "-e", "esp32dev", "--target", "upload"],
            capture_output=True, text=True, timeout=300
        )
        
        if result.returncode == 0:
            print("  ✓ Flash d'urgence réussi")
        else:
            print(f"  ✗ Erreur flash: {result.stderr}")
            
            # Essayer avec esptool directement
            print("  Tentative avec esptool...")
            result = subprocess.run([
                "esptool.py", "--chip", "esp32", "--port", "COM6", 
                "--baud", "115200", "--before", "default_reset", 
                "--after", "hard_reset", "write_flash", 
                "0x1000", ".pio/build/esp32dev/bootloader.bin",
                "0x8000", ".pio/build/esp32dev/partitions.bin",
                "0x10000", ".pio/build/esp32dev/firmware.bin"
            ], capture_output=True, text=True, timeout=300)
            
            if result.returncode == 0:
                print("  ✓ Flash esptool réussi")
            else:
                print(f"  ✗ Erreur esptool: {result.stderr}")
                return False
        
        # Étape 5: Restauration de la configuration
        print("\n=== ÉTAPE 4: Restauration Configuration ===")
        
        # Restaurer la configuration OTA
        with open("platformio.ini.backup", 'r') as f:
            content = f.read()
        
        with open("platformio.ini", 'w') as f:
            f.write(content)
        
        print("  ✓ Configuration OTA restaurée")
        
        # Étape 6: Test de récupération
        print("\n=== ÉTAPE 5: Test de Récupération ===")
        
        print("  Attente du redémarrage (30s)...")
        time.sleep(30)
        
        # Test de connexion série
        try:
            from serial_utils import open_serial_with_release
            ser = open_serial_with_release('COM6', 115200, timeout=5)
            time.sleep(5)
            
            # Lire quelques lignes pour vérifier
            startup_detected = False
            for _ in range(10):
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"    Log: {line}")
                        if "Démarrage FFP3CS" in line:
                            startup_detected = True
                            print("    ✅ Démarrage normal détecté!")
                            break
                        elif "abort()" in line or "not bootable" in line:
                            print("    ❌ Problème persistant")
                            break
                time.sleep(1)
            
            ser.close()
            
            if startup_detected:
                print("  ✅ Récupération réussie!")
                return True
            else:
                print("  ❌ Récupération échouée")
                return False
                
        except Exception as e:
            print(f"  ✗ Erreur test récupération: {e}")
            return False
        
    except Exception as e:
        print(f"✗ Erreur récupération: {e}")
        return False
    
    finally:
        print(f"\n=== Récupération terminée: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} ===")

def main():
    """Fonction principale"""
    print("🚨 ATTENTION: Récupération d'urgence de l'ESP32 brické")
    print("Cette opération va tenter de sauver l'ESP32...")
    
    success = emergency_recovery()
    
    if success:
        print("\n🎉 RÉCUPÉRATION RÉUSSIE!")
        print("L'ESP32 devrait maintenant fonctionner normalement.")
        print("Vous pouvez maintenant tester l'OTA avec la partition standard.")
    else:
        print("\n💥 RÉCUPÉRATION ÉCHOUÉE!")
        print("L'ESP32 pourrait nécessiter une intervention manuelle.")
        print("Considérez un flash manuel avec esptool.py")

if __name__ == "__main__":
    main() 