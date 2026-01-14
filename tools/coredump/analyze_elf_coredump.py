#!/usr/bin/env python3
"""
Analyse un fichier ELF core dump extrait
"""

import struct
import sys
import os

def analyze_elf_coredump(elf_file):
    """Analyse un fichier ELF core dump"""
    if not os.path.exists(elf_file):
        print(f"❌ Fichier non trouvé: {elf_file}")
        return
    
    with open(elf_file, 'rb') as f:
        data = f.read()
    
    print("=" * 60)
    print("📊 ANALYSE ELF CORE DUMP")
    print("=" * 60)
    print(f"Fichier: {elf_file}")
    print(f"Taille: {len(data)} bytes")
    print()
    
    if len(data) < 64:
        print("❌ Fichier trop court pour être un ELF valide")
        return
    
    # Lire l'en-tête ELF
    magic = data[0:4]
    if magic != b'\x7fELF':
        print(f"❌ Magic ELF invalide: {magic.hex()}")
        return
    
    print("✅ Format ELF valide")
    print()
    
    # Informations de base
    ei_class = data[4]  # 1=32-bit, 2=64-bit
    ei_data = data[5]   # 1=little-endian, 2=big-endian
    ei_version = data[6]
    ei_osabi = data[7]
    
    print("=== EN-TÊTE ELF ===")
    print(f"Class: {ei_class} ({'32-bit' if ei_class == 1 else '64-bit' if ei_class == 2 else 'Unknown'})")
    print(f"Data encoding: {ei_data} ({'little-endian' if ei_data == 1 else 'big-endian' if ei_data == 2 else 'Unknown'})")
    print(f"Version: {ei_version}")
    print(f"OS/ABI: {ei_osabi}")
    
    # Lire l'en-tête ELF principal (offset 0x10 pour 32-bit)
    if ei_class == 1:  # 32-bit
        e_type = struct.unpack('<H', data[16:18])[0]
        e_machine = struct.unpack('<H', data[18:20])[0]
        e_version = struct.unpack('<I', data[20:24])[0]
        e_entry = struct.unpack('<I', data[24:28])[0]
        e_phoff = struct.unpack('<I', data[28:32])[0]
        e_shoff = struct.unpack('<I', data[32:36])[0]
        e_flags = struct.unpack('<I', data[36:40])[0]
        e_ehsize = struct.unpack('<H', data[40:42])[0]
        e_phentsize = struct.unpack('<H', data[42:44])[0]
        e_phnum = struct.unpack('<H', data[44:46])[0]
        e_shentsize = struct.unpack('<H', data[46:48])[0]
        e_shnum = struct.unpack('<H', data[48:50])[0]
        e_shstrndx = struct.unpack('<H', data[50:52])[0]
        
        print()
        print("=== INFORMATIONS ELF ===")
        print(f"Type: {e_type} ({'ET_NONE' if e_type == 0 else 'ET_REL' if e_type == 1 else 'ET_EXEC' if e_type == 2 else 'ET_DYN' if e_type == 3 else 'ET_CORE' if e_type == 4 else 'Unknown'})")
        print(f"Machine: 0x{e_machine:04X}", end='')
        if e_machine == 0x5E:
            print(" (Xtensa/ESP32)")
        else:
            print()
        print(f"Version: {e_version}")
        print(f"Entry point: 0x{e_entry:08X}")
        print(f"Program header offset: 0x{e_phoff:08X}")
        print(f"Section header offset: 0x{e_shoff:08X}")
        print(f"Flags: 0x{e_flags:08X}")
        print(f"Program headers: {e_phnum}")
        print(f"Section headers: {e_shnum}")
        
        # Analyser les sections si disponibles
        if e_shoff > 0 and e_shnum > 0 and e_shoff < len(data):
            print()
            print("=== SECTIONS (premières 10) ===")
            sh_size = e_shentsize
            for i in range(min(e_shnum, 10)):
                sh_offset = e_shoff + (i * sh_size)
                if sh_offset + 40 <= len(data):
                    sh_name = struct.unpack('<I', data[sh_offset:sh_offset+4])[0]
                    sh_type = struct.unpack('<I', data[sh_offset+4:sh_offset+8])[0]
                    sh_addr = struct.unpack('<I', data[sh_offset+8:sh_offset+12])[0]
                    sh_offset_data = struct.unpack('<I', data[sh_offset+12:sh_offset+16])[0]
                    sh_size_data = struct.unpack('<I', data[sh_offset+16:sh_offset+20])[0]
                    
                    type_str = {0: 'SHT_NULL', 1: 'SHT_PROGBITS', 2: 'SHT_SYMTAB', 
                               3: 'SHT_STRTAB', 6: 'SHT_DYNSYM', 8: 'SHT_NOBITS'}.get(sh_type, f'0x{sh_type:X}')
                    print(f"  Section {i}: type={type_str}, addr=0x{sh_addr:08X}, size={sh_size_data}")
    
    print()
    print("=" * 60)
    print()
    print("💡 Pour une analyse complète avec stack trace, installez GDB:")
    print("   1. Téléchargez esp-gdb depuis: https://github.com/espressif/binutils-gdb/releases")
    print("   2. Ajoutez-le au PATH")
    print("   3. Relancez: python -m esp_coredump info_corefile --core <fichier> <firmware.elf>")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python analyze_elf_coredump.py <elf_file>")
        sys.exit(1)
    
    analyze_elf_coredump(sys.argv[1])
