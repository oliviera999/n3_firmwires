#!/usr/bin/env python3
"""
Calcule le MD5 d'un fichier firmware pour l'OTA
"""

import hashlib
import sys
import os

def calculate_md5(filepath):
    """Calcule le MD5 d'un fichier"""
    md5_hash = hashlib.md5()
    try:
        with open(filepath, "rb") as f:
            # Lecture par chunks pour les gros fichiers
            for chunk in iter(lambda: f.read(4096), b""):
                md5_hash.update(chunk)
        return md5_hash.hexdigest()
    except FileNotFoundError:
        print(f"❌ Fichier non trouvé: {filepath}")
        return None
    except Exception as e:
        print(f"❌ Erreur: {e}")
        return None

def main():
    if len(sys.argv) < 2:
        # Par défaut, chercher le firmware dans le dossier courant
        firmware_files = [f for f in os.listdir('.') if f.endswith('.bin')]
        if firmware_files:
            print("📦 Fichiers firmware trouvés:")
            for f in firmware_files:
                size = os.path.getsize(f)
                md5 = calculate_md5(f)
                if md5:
                    print(f"\n📁 {f}")
                    print(f"   Taille: {size} bytes")
                    print(f"   MD5: {md5}")
        else:
            print("Usage: python calculate_md5.py <firmware.bin>")
    else:
        filepath = sys.argv[1]
        if os.path.exists(filepath):
            size = os.path.getsize(filepath)
            md5 = calculate_md5(filepath)
            if md5:
                print(f"📁 Fichier: {filepath}")
                print(f"📊 Taille: {size} bytes")
                print(f"🔐 MD5: {md5}")
                print(f"\n📋 Pour metadata.json:")
                print(f'  "size": {size},')
                print(f'  "md5": "{md5}",')
        else:
            print(f"❌ Fichier non trouvé: {filepath}")

if __name__ == "__main__":
    main()
