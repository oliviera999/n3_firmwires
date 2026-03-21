#!/usr/bin/env python3
"""
Script pour analyser un core dump ESP32 et extraire la stack trace

Utilise esp-coredump.py (ESP-IDF) ou des outils alternatifs pour analyser le fichier ELF.
"""

import subprocess
import sys
import os
import argparse
import json
from pathlib import Path
from datetime import datetime

def find_esp_coredump():
    """Trouve l'exécutable esp-coredump.py"""
    # Essayer plusieurs emplacements possibles
    possible_paths = [
        "esp-coredump.py",
        "esp-coredump",
        os.path.expanduser("~/.espressif/tools/esp-coredump/esp-coredump.py"),
    ]
    
    # Vérifier aussi dans le PATH
    try:
        result = subprocess.run(["where" if sys.platform == "win32" else "which", "esp-coredump.py"], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()
    except:
        pass
    
    for path in possible_paths:
        if os.path.exists(path):
            return path
    
    return None

def _pio_redirect_root():
    flag = os.environ.get("N3_PIO_BUILD_REDIRECT", "").strip().lower()
    if flag in ("0", "false", "no", "off"):
        return None
    custom = os.environ.get("N3_PIO_BUILD_ROOT", "").strip()
    if custom:
        return Path(os.path.expandvars(custom)).expanduser()
    if os.name == "nt":
        return Path(r"C:\pio-builds")
    return None


def _project_slug(project_dir):
    return "-".join(Path(project_dir).name.split())


def find_elf_file(project_root=None):
    """Trouve le fichier ELF du firmware compilé"""
    if project_root is None:
        project_root = Path.cwd()
    project_root = Path(project_root).resolve()

    build_dirs = list(project_root.glob(".pio/build/*/firmware.elf"))
    if build_dirs:
        return str(build_dirs[0])

    root = _pio_redirect_root()
    if root and root.is_dir():
        slug = _project_slug(project_root)
        env_dir = root / slug
        if env_dir.is_dir():
            elves = list(env_dir.glob("*/firmware.elf"))
            if elves:
                elves.sort(key=lambda p: p.stat().st_mtime, reverse=True)
                return str(elves[0])

    return None

def analyze_with_esp_coredump(coredump_file, elf_file=None, output_file=None):
    """Analyse le core dump avec esp-coredump.py"""
    esp_coredump = find_esp_coredump()
    if not esp_coredump:
        print("⚠️  esp-coredump.py non trouvé!")
        print("   Installez ESP-IDF ou utilisez l'option --simple")
        return False
    
    if not elf_file:
        elf_file = find_elf_file()
        if not elf_file:
            print("⚠️  Fichier ELF du firmware non trouvé!")
            print("   Compilez d'abord le projet avec: pio run")
            print("   Ou spécifiez le fichier ELF avec --elf")
            return False
    
    print(f"📊 Analyse du core dump avec esp-coredump.py...")
    print(f"   Core dump: {coredump_file}")
    print(f"   Firmware ELF: {elf_file}")
    
    # Commande esp-coredump pour analyser
    cmd = [
        sys.executable, esp_coredump,
        "info_corefile",
        "--core", coredump_file,
        "--core-format", "elf",
        "--core-elf", coredump_file,
    ]
    
    if elf_file:
        cmd.extend(["--prog", elf_file])
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        
        if output_file:
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(result.stdout)
            print(f"✅ Rapport sauvegardé: {output_file}")
        else:
            print("\n" + "=" * 60)
            print(result.stdout)
            print("=" * 60)
        
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Erreur lors de l'analyse:")
        print(f"   {e.stderr}")
        if e.stdout:
            print(f"   Sortie: {e.stdout}")
        return False

def analyze_simple(coredump_file, output_file=None):
    """Analyse simple du core dump (sans esp-coredump.py)"""
    print("📊 Analyse simple du core dump...")
    
    report = []
    report.append("=" * 60)
    report.append("📋 Rapport d'Analyse Core Dump (Simple)")
    report.append("=" * 60)
    report.append(f"Fichier: {coredump_file}")
    report.append(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    report.append("")
    
    # Vérifier la taille
    file_size = os.path.getsize(coredump_file)
    report.append(f"Taille du fichier: {file_size} bytes ({file_size / 1024:.2f} KB)")
    
    # Lire les premiers bytes pour vérifier le format
    with open(coredump_file, 'rb') as f:
        magic = f.read(4)
        if magic == b'\x7fELF':
            report.append("Format: ELF valide ✅")
            
            # Lire l'en-tête ELF de base
            f.seek(16)
            e_type = int.from_bytes(f.read(2), 'little')
            e_machine = int.from_bytes(f.read(2), 'little')
            
            report.append(f"Type ELF: {e_type}")
            report.append(f"Machine: {e_machine} (0x{e_machine:X})")
            
            if e_machine == 0xF3:  # RISC-V
                report.append("Architecture: RISC-V")
            elif e_machine == 0x5E:  # Xtensa
                report.append("Architecture: Xtensa (ESP32)")
        else:
            report.append(f"Format: Inconnu (magic: {magic.hex()})")
    
    # Vérifier si le fichier est vide (tous 0xFF)
    with open(coredump_file, 'rb') as f:
        data = f.read(1024)
        if all(b == 0xFF for b in data):
            report.append("")
            report.append("⚠️  ATTENTION: Partition vide (tous les bytes sont 0xFF)")
            report.append("   Cela signifie qu'aucun crash n'a été enregistré.")
    
    report.append("")
    report.append("=" * 60)
    report.append("💡 Pour une analyse complète avec stack trace:")
    report.append("   1. Installez ESP-IDF")
    report.append("   2. Utilisez: esp-coredump.py info_corefile --core <fichier> --prog <firmware.elf>")
    report.append("=" * 60)
    
    report_text = "\n".join(report)
    
    if output_file:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(report_text)
        print(f"✅ Rapport sauvegardé: {output_file}")
    else:
        print("\n" + report_text)
    
    return True

def main():
    parser = argparse.ArgumentParser(
        description="Analyse un core dump ESP32 et extrait la stack trace"
    )
    parser.add_argument(
        "coredump_file",
        help="Fichier core dump ELF à analyser"
    )
    parser.add_argument(
        "--elf", "-e",
        default=None,
        help="Fichier ELF du firmware (cherché automatiquement si non fourni)"
    )
    parser.add_argument(
        "--output", "-o",
        default=None,
        help="Fichier de sortie pour le rapport (défaut: affichage console)"
    )
    parser.add_argument(
        "--simple", "-s",
        action="store_true",
        help="Utiliser l'analyse simple (sans esp-coredump.py)"
    )
    
    args = parser.parse_args()
    
    # Vérifier que le fichier existe
    if not os.path.exists(args.coredump_file):
        print(f"❌ Erreur: Fichier non trouvé: {args.coredump_file}")
        sys.exit(1)
    
    print("=" * 60)
    print("🔍 Analyse Core Dump ESP32")
    print("=" * 60)
    
    # Générer le nom de fichier de sortie si non fourni
    if args.output is None and not args.simple:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        base_name = Path(args.coredump_file).stem
        args.output = f"{base_name}_analysis_{timestamp}.txt"
    
    # Analyser
    if args.simple:
        success = analyze_simple(args.coredump_file, args.output)
    else:
        success = analyze_with_esp_coredump(args.coredump_file, args.elf, args.output)
    
    if not success:
        print("\n⚠️  Analyse avec esp-coredump.py échouée, tentative d'analyse simple...")
        analyze_simple(args.coredump_file, args.output)
    
    print("=" * 60)

if __name__ == "__main__":
    main()
