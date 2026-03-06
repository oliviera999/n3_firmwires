#!/usr/bin/env python3
"""
Outil intégré pour extraire et analyser les core dumps ESP32

Combine l'extraction depuis la flash et l'analyse en une seule commande.
"""

import subprocess
import sys
import os
import argparse
from pathlib import Path
from datetime import datetime

# Importer les fonctions des autres scripts
sys.path.insert(0, str(Path(__file__).parent))
from extract_coredump import extract_coredump, verify_coredump, find_esptool
from analyze_coredump import analyze_with_esp_coredump, analyze_simple, find_elf_file

def main():
    parser = argparse.ArgumentParser(
        description="Extrait et analyse le core dump depuis l'ESP32 en une seule commande",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exemples:
  # Extraction et analyse complète
  python coredump_tool.py --port COM3

  # Extraction seulement
  python coredump_tool.py --port COM3 --extract-only

  # Analyse d'un fichier existant
  python coredump_tool.py --analyze-only coredump_20260113_120000.elf
        """
    )
    
    parser.add_argument(
        "--port", "-p",
        default="COM3",
        help="Port série de l'ESP32 (défaut: COM3)"
    )
    parser.add_argument(
        "--offset", "-o",
        type=lambda x: int(x, 0),
        default=0x3F0000,
        help="Offset de la partition coredump (défaut: 0x3F0000)"
    )
    parser.add_argument(
        "--size", "-s",
        type=lambda x: int(x, 0),
        default=0x10000,
        help="Taille de la partition coredump (défaut: 0x10000)"
    )
    parser.add_argument(
        "--env",
        choices=["test", "prod"],
        default="prod",
        help="Environnement (test ou prod)"
    )
    parser.add_argument(
        "--extract-only",
        action="store_true",
        help="Extraire seulement, sans analyser"
    )
    parser.add_argument(
        "--analyze-only",
        metavar="FILE",
        help="Analyser seulement un fichier existant (sans extraction)"
    )
    parser.add_argument(
        "--output-dir", "-d",
        default="coredumps",
        help="Dossier pour sauvegarder les fichiers (défaut: coredumps/)"
    )
    parser.add_argument(
        "--keep-raw",
        action="store_true",
        help="Conserver le fichier brut après analyse"
    )
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("🔧 Outil Core Dump ESP32 - Extraction et Analyse")
    print("=" * 60)
    
    # Créer le dossier de sortie
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Mode analyse seulement
    if args.analyze_only:
        coredump_file = args.analyze_only
        if not os.path.exists(coredump_file):
            print(f"❌ Erreur: Fichier non trouvé: {coredump_file}")
            sys.exit(1)
        
        print(f"\n📊 Analyse du fichier: {coredump_file}")
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_file = output_dir / f"analysis_{timestamp}.txt"
        
        if analyze_with_esp_coredump(coredump_file, None, str(report_file)):
            print(f"✅ Analyse terminée: {report_file}")
        else:
            print("\n⚠️  Analyse avec esp-coredump.py échouée, utilisation de l'analyse simple...")
            analyze_simple(coredump_file, str(report_file))
        
        sys.exit(0)
    
    # Mode extraction (avec ou sans analyse)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    coredump_file = output_dir / f"coredump_{timestamp}.elf"
    
    print(f"\n📡 Étape 1: Extraction depuis la flash...")
    if not extract_coredump(str(args.port), args.offset, args.size, str(coredump_file)):
        print("❌ Extraction échouée!")
        sys.exit(1)
    
    # Vérifier le fichier
    print("\n🔍 Vérification du fichier...")
    is_valid, message = verify_coredump(str(coredump_file))
    print(f"{'✅' if is_valid else '⚠️ '} {message}")
    
    if not is_valid:
        print("\n⚠️  Le core dump semble vide ou invalide.")
        print("   Cela peut signifier qu'aucun crash n'a eu lieu.")
        if not args.extract_only:
            response = input("\nVoulez-vous quand même tenter l'analyse? (o/N): ")
            if response.lower() != 'o':
                sys.exit(0)
    
    # Mode extraction seulement
    if args.extract_only:
        print(f"\n✅ Core dump extrait: {coredump_file}")
        print(f"\n💡 Pour analyser plus tard:")
        print(f"   python tools/coredump/coredump_tool.py --analyze-only {coredump_file}")
        sys.exit(0)
    
    # Étape 2: Analyse
    print(f"\n📊 Étape 2: Analyse du core dump...")
    report_file = output_dir / f"analysis_{timestamp}.txt"
    
    # Chercher le fichier ELF du firmware
    elf_file = find_elf_file()
    if elf_file:
        print(f"   Firmware ELF trouvé: {elf_file}")
    else:
        print("   ⚠️  Fichier ELF du firmware non trouvé")
        print("   Compilez le projet avec: pio run")
    
    if analyze_with_esp_coredump(str(coredump_file), elf_file, str(report_file)):
        print(f"✅ Analyse terminée: {report_file}")
    else:
        print("\n⚠️  Analyse avec esp-coredump.py échouée, utilisation de l'analyse simple...")
        analyze_simple(str(coredump_file), str(report_file))
    
    # Nettoyer le fichier brut si demandé
    if not args.keep_raw:
        print(f"\n🗑️  Suppression du fichier brut: {coredump_file}")
        try:
            os.remove(coredump_file)
        except:
            print("   ⚠️  Impossible de supprimer le fichier")
    
    print("\n" + "=" * 60)
    print("✅ Processus terminé!")
    print(f"   Rapport: {report_file}")
    if args.keep_raw:
        print(f"   Core dump: {coredump_file}")
    print("=" * 60)

if __name__ == "__main__":
    main()
