#!/usr/bin/env python3
"""
Compare deux fichiers core dump pour identifier les différences
"""

import struct
import sys
import os

def analyze_header(data, name):
    """Analyse l'en-tête d'un core dump"""
    print(f"\n=== {name} ===")
    if len(data) < 64:
        print("Fichier trop court")
        return
    
    magic = data[0:4]
    version = struct.unpack('<I', data[4:8])[0] if len(data) >= 8 else 0
    elf_sig = data[24:28] if len(data) >= 28 else b''
    
    print(f"Magic (0x00): {magic.hex()}")
    print(f"Version (0x04): {version}")
    print(f"ELF @0x18: {elf_sig.hex()} {'✅' if elf_sig == b'\\x7fELF' else '❌'}")
    
    # Afficher les premiers bytes
    print("Premiers 64 bytes:")
    for i in range(0, min(64, len(data)), 16):
        hex_str = ' '.join(f'{b:02x}' for b in data[i:i+16])
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
        print(f"  {i:04X}: {hex_str:<48} {ascii_str}")

def compare_files(file1, file2):
    """Compare deux fichiers core dump"""
    with open(file1, 'rb') as f:
        data1 = f.read()
    with open(file2, 'rb') as f:
        data2 = f.read()
    
    print("=" * 60)
    print("📊 COMPARAISON DE CORE DUMPS")
    print("=" * 60)
    print(f"Fichier 1: {file1} ({len(data1)} bytes)")
    print(f"Fichier 2: {file2} ({len(data2)} bytes)")
    
    # Analyser les en-têtes
    analyze_header(data1, "FICHIER 1")
    analyze_header(data2, "FICHIER 2")
    
    # Compter les différences
    min_len = min(len(data1), len(data2))
    diff_count = sum(1 for i in range(min_len) if data1[i] != data2[i])
    identical = diff_count == 0
    
    print(f"\n=== RÉSULTAT ===")
    print(f"Bytes différents: {diff_count} / {min_len}")
    print(f"Identiques: {'✅ Oui' if identical else '❌ Non'}")
    
    # Trouver les premières différences
    if not identical:
        print("\n=== PREMIÈRES DIFFÉRENCES ===")
        count = 0
        for i in range(min_len):
            if data1[i] != data2[i] and count < 10:
                print(f"Offset 0x{i:04X}: 0x{data1[i]:02X} != 0x{data2[i]:02X}")
                count += 1
    
    # Vérifier si l'un est vide (tous 0xFF)
    is_empty1 = all(b == 0xFF for b in data1[:1024])
    is_empty2 = all(b == 0xFF for b in data2[:1024])
    
    print(f"\n=== ÉTAT ===")
    print(f"Fichier 1 vide: {'Oui' if is_empty1 else 'Non'}")
    print(f"Fichier 2 vide: {'Oui' if is_empty2 else 'Non'}")
    
    # Vérifier les formats ESP-IDF
    magic1 = data1[0:4] if len(data1) >= 4 else b''
    magic2 = data2[0:4] if len(data2) >= 4 else b''
    
    esp_magic = b'\xa4\x7d\x00\x00'
    print(f"\nFichier 1 format ESP-IDF: {'✅' if magic1 == esp_magic else '❌'}")
    print(f"Fichier 2 format ESP-IDF: {'✅' if magic2 == esp_magic else '❌'}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python compare_coredumps.py <file1> <file2>")
        sys.exit(1)
    
    compare_files(sys.argv[1], sys.argv[2])
