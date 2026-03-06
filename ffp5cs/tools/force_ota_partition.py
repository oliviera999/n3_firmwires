#!/usr/bin/env python3
"""
Force OTA Partition Switch Tool
Efface la partition otadata pour forcer l'ESP32 à démarrer sur app0
Utile quand une OTA ne bascule pas correctement
"""

import sys
import subprocess
import os

def erase_otadata():
    """Efface la partition otadata pour forcer le boot sur app0"""
    print("🔧 Force OTA Partition Switch Tool")
    print("=" * 50)
    
    # Détection du port COM
    print("📡 Recherche des ports COM...")
    result = subprocess.run(["python", "-m", "serial.tools.list_ports"], 
                          capture_output=True, text=True)
    
    if "COM" not in result.stdout:
        print("❌ Aucun port COM trouvé")
        return False
    
    # Extraction du premier port COM
    lines = result.stdout.split('\n')
    com_port = None
    for line in lines:
        if "COM" in line:
            com_port = line.split()[0]
            break
    
    if not com_port:
        print("❌ Impossible de détecter le port COM")
        return False
    
    print(f"✅ Port détecté: {com_port}")
    
    # Commande pour effacer otadata (offset 0xE000, taille 0x2000)
    cmd = [
        "python", "-m", "esptool",
        "--chip", "esp32s3",
        "--port", com_port,
        "--baud", "921600",
        "erase_region", "0xE000", "0x2000"
    ]
    
    print("\n📝 Effacement de la partition otadata...")
    print(f"   Offset: 0xE000")
    print(f"   Taille: 0x2000 (8KB)")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            print("✅ Partition otadata effacée avec succès!")
            print("\n⚠️  IMPORTANT:")
            print("   - L'ESP32 démarrera maintenant sur app0 (partition par défaut)")
            print("   - La prochaine OTA sera installée sur app1")
            print("   - Le système OTA reprendra son cycle normal")
            return True
        else:
            print(f"❌ Erreur: {result.stderr}")
            return False
            
    except Exception as e:
        print(f"❌ Exception: {e}")
        return False

def main():
    print("\n⚠️  ATTENTION: Cette opération va forcer l'ESP32 à redémarrer sur app0")
    response = input("Continuer? (o/n): ")
    
    if response.lower() != 'o':
        print("❌ Opération annulée")
        return
    
    if erase_otadata():
        print("\n✅ Opération terminée avec succès!")
        print("🔄 Redémarrez l'ESP32 pour appliquer les changements")
    else:
        print("\n❌ Échec de l'opération")

if __name__ == "__main__":
    main()
