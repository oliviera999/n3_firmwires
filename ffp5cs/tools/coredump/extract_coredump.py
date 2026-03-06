#!/usr/bin/env python3
"""
Script pour extraire le core dump depuis la partition flash de l'ESP32

Utilise esptool.py pour lire la partition coredump et sauvegarder le fichier ELF.
"""

import subprocess
import sys
import os
import argparse
from datetime import datetime
from pathlib import Path

# Permettre l'import de serial_utils (tools/monitor) pour libération du port
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "monitor"))

# Offsets de partition pour ESP32-WROOM 4MB
COREDUMP_OFFSET_TEST = 0x3F0000  # partitions_esp32_wroom_test_coredump.csv
COREDUMP_OFFSET_PROD = 0x3F0000  # partitions_esp32_wroom_ota_coredump.csv
COREDUMP_SIZE = 0x10000  # 64 KB

def find_esptool():
    """Trouve l'exécutable esptool.py"""
    # Essayer d'abord python -m esptool (module installé via pip)
    try:
        result = subprocess.run([sys.executable, "-m", "esptool", "--help"], 
                              capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            return "module"  # Indicateur spécial pour utiliser python -m esptool
    except:
        pass
    
    # Essayer plusieurs emplacements possibles
    possible_paths = [
        "esptool.py",
        "esptool",
        os.path.expanduser("~/.platformio/penv/Scripts/esptool.py"),
        os.path.expanduser("~/.platformio/penv/bin/esptool.py"),
    ]
    
    # Vérifier aussi dans le PATH
    try:
        result = subprocess.run(["where" if sys.platform == "win32" else "which", "esptool.py"], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()
    except:
        pass
    
    for path in possible_paths:
        if os.path.exists(path):
            return path
    
    return None

def extract_coredump(port, offset, size, output_file):
    """Extrait le core dump depuis la flash"""
    try:
        from serial_utils import release_com_port_windows
        if release_com_port_windows(port):
            import time
            time.sleep(2)
    except Exception:
        pass

    esptool = find_esptool()
    if not esptool:
        print("❌ Erreur: esptool.py non trouvé!")
        print("   Installez-le avec: pip install esptool")
        return False
    
    print(f"📡 Extraction du core dump depuis la flash...")
    print(f"   Port: {port}")
    print(f"   Offset: 0x{offset:X} ({offset} bytes)")
    print(f"   Taille: 0x{size:X} ({size} bytes)")
    print(f"   Fichier de sortie: {output_file}")
    
    # Commande esptool pour lire la partition
    if esptool == "module":
        # Utiliser python -m esptool
        cmd = [
            sys.executable, "-m", "esptool",
            "--port", port,
            "--baud", "921600",
            "read_flash",
            hex(offset),
            hex(size),
            output_file
        ]
    else:
        # Utiliser esptool directement
        cmd = [
            sys.executable, esptool,
            "--port", port,
            "--baud", "921600",
            "read_flash",
            hex(offset),
            hex(size),
            output_file
        ]
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        print("✅ Core dump extrait avec succès!")
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Erreur lors de l'extraction:")
        print(f"   {e.stderr}")
        return False

def verify_coredump(file_path):
    """Vérifie que le fichier contient un core dump valide"""
    if not os.path.exists(file_path):
        return False, "Fichier non trouvé"
    
    file_size = os.path.getsize(file_path)
    if file_size == 0:
        return False, "Fichier vide"
    
    # Vérifier la signature ELF (magic bytes: 0x7F 'E' 'L' 'F')
    with open(file_path, 'rb') as f:
        magic = f.read(4)
        if magic == b'\x7fELF':
            return True, f"Fichier ELF valide ({file_size} bytes)"
        elif all(b == 0xFF for b in magic):
            return False, "Partition vide (tous les bytes sont 0xFF)"
        else:
            return False, f"Format inconnu (magic: {magic.hex()})"

def main():
    parser = argparse.ArgumentParser(
        description="Extrait le core dump depuis la partition flash de l'ESP32"
    )
    parser.add_argument(
        "--port", "-p",
        default="COM3",
        help="Port série de l'ESP32 (défaut: COM3)"
    )
    parser.add_argument(
        "--offset", "-o",
        type=lambda x: int(x, 0),  # Supporte hex (0x...) et décimal
        default=COREDUMP_OFFSET_PROD,
        help=f"Offset de la partition coredump (défaut: 0x{COREDUMP_OFFSET_PROD:X})"
    )
    parser.add_argument(
        "--size", "-s",
        type=lambda x: int(x, 0),
        default=COREDUMP_SIZE,
        help=f"Taille de la partition coredump (défaut: 0x{COREDUMP_SIZE:X})"
    )
    parser.add_argument(
        "--output", "-f",
        default=None,
        help="Fichier de sortie (défaut: coredump_YYYYMMDD_HHMMSS.elf)"
    )
    parser.add_argument(
        "--env",
        choices=["test", "prod"],
        default="prod",
        help="Environnement (test ou prod) pour utiliser l'offset par défaut"
    )
    
    args = parser.parse_args()
    
    # Déterminer l'offset selon l'environnement
    if args.offset == COREDUMP_OFFSET_PROD and args.env == "test":
        args.offset = COREDUMP_OFFSET_TEST
    
    # Générer le nom de fichier si non fourni
    if args.output is None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output = f"coredump_{timestamp}.elf"
    
    # Créer le dossier de sortie si nécessaire
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    print("=" * 60)
    print("🔍 Extraction Core Dump ESP32")
    print("=" * 60)
    
    # Extraire le core dump
    if not extract_coredump(args.port, args.offset, args.size, args.output):
        sys.exit(1)
    
    # Vérifier le fichier extrait
    print("\n🔍 Vérification du fichier...")
    is_valid, message = verify_coredump(args.output)
    
    if is_valid:
        print(f"✅ {message}")
        print(f"\n📄 Core dump sauvegardé: {os.path.abspath(args.output)}")
        print(f"\n💡 Pour analyser le core dump, utilisez:")
        print(f"   python tools/coredump/analyze_coredump.py {args.output}")
    else:
        print(f"⚠️  {message}")
        print(f"\n💡 Le fichier a été sauvegardé mais peut être invalide ou vide.")
        print(f"   Cela peut signifier qu'aucun crash n'a eu lieu depuis le dernier boot.")
    
    print("=" * 60)

if __name__ == "__main__":
    main()
