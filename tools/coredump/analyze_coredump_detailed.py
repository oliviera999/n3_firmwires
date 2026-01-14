#!/usr/bin/env python3
"""
Analyse détaillée d'un core dump ESP32
"""

import struct
import sys
import os

def analyze_coredump_file(filepath):
    """Analyse un fichier core dump"""
    if not os.path.exists(filepath):
        print(f"❌ Fichier non trouvé: {filepath}")
        return
    
    with open(filepath, 'rb') as f:
        data = f.read()
    
    print("=" * 60)
    print("📊 ANALYSE DÉTAILLÉE CORE DUMP")
    print("=" * 60)
    print(f"Fichier: {filepath}")
    print(f"Taille: {len(data)} bytes ({len(data)/1024:.2f} KB)")
    print()
    
    # Analyser l'en-tête ESP-IDF
    if len(data) >= 64:
        magic = data[0:4]
        version = struct.unpack('<I', data[4:8])[0] if len(data) >= 8 else 0
        elf_sig = data[24:28] if len(data) >= 28 else b''
        
        print("=== EN-TÊTE ESP-IDF ===")
        print(f"Magic (0x00-0x03): {magic.hex()}")
        print(f"Version (0x04-0x07): {version}")
        
        if elf_sig == b'\x7fELF':
            print(f"ELF signature (0x18): {elf_sig.hex()} ✅")
            
            # Analyser l'en-tête ELF
            if len(data) >= 52:
                e_machine = struct.unpack('<H', data[40:42])[0]
                e_entry = struct.unpack('<I', data[48:52])[0]
                
                print()
                print("=== EN-TÊTE ELF ===")
                print(f"Machine type (0x28): 0x{e_machine:04X}", end='')
                if e_machine == 0x5E:
                    print(" (Xtensa/ESP32)")
                else:
                    print()
                print(f"Entry point (0x30): 0x{e_entry:08X}")
        else:
            print(f"ELF signature (0x18): {elf_sig.hex()} ❌")
    
    # Vérifier si le fichier est vide (tous 0xFF)
    if all(b == 0xFF for b in data[:1024]):
        print()
        print("⚠️  ATTENTION: Fichier vide (tous les bytes sont 0xFF)")
        print("   Aucun crash n'a été enregistré.")
    elif magic == b'\xa4\x7d\x00\x00':
        print()
        print("✅ Format ESP-IDF Core Dump valide")
        print("   Le fichier contient un core dump avec en-tête ESP-IDF")
    else:
        print()
        print("⚠️  Format inconnu ou corrompu")
    
    print()
    print("=" * 60)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python analyze_coredump_detailed.py <coredump_file>")
        sys.exit(1)
    
    analyze_coredump_file(sys.argv[1])
