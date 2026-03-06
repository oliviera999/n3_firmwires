#!/usr/bin/env python3
"""
Extrait la partie ELF pure d'un core dump ESP-IDF

Les core dumps ESP-IDF peuvent avoir un en-tête avant l'ELF.
Ce script trouve l'ELF et l'extrait.
"""

import sys
import os

def find_elf_offset(data):
    """Trouve l'offset de la signature ELF dans les données"""
    # Chercher la signature ELF (0x7F 'E' 'L' 'F')
    elf_magic = b'\x7fELF'
    
    # Chercher à partir de l'offset 0x18 (où on sait qu'il y a souvent l'ELF)
    for offset in range(0, min(len(data) - 4, 0x1000), 4):
        if data[offset:offset+4] == elf_magic:
            return offset
    
    return None

def extract_elf(input_file, output_file=None):
    """Extrait la partie ELF d'un core dump"""
    if not os.path.exists(input_file):
        print(f"❌ Fichier non trouvé: {input_file}")
        return False
    
    with open(input_file, 'rb') as f:
        data = f.read()
    
    print(f"📄 Fichier: {input_file}")
    print(f"📊 Taille: {len(data)} bytes")
    
    # Trouver l'offset ELF
    elf_offset = find_elf_offset(data)
    
    if elf_offset is None:
        print("❌ Signature ELF non trouvée dans le fichier")
        return False
    
    print(f"✅ Signature ELF trouvée à l'offset: 0x{elf_offset:X}")
    
    # Extraire la partie ELF
    elf_data = data[elf_offset:]
    
    # Générer le nom de sortie si non fourni
    if output_file is None:
        base_name = os.path.splitext(input_file)[0]
        output_file = f"{base_name}_extracted.elf"
    
    # Sauvegarder
    with open(output_file, 'wb') as f:
        f.write(elf_data)
    
    print(f"✅ ELF extrait: {output_file} ({len(elf_data)} bytes)")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python extract_elf_from_coredump.py <coredump_file> [output_file]")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    if extract_elf(input_file, output_file):
        print(f"\n💡 Pour analyser:")
        print(f"   python -m esp_coredump info_corefile --core {output_file} --core-format elf .pio/build/wroom-test/firmware.elf")
    else:
        sys.exit(1)
