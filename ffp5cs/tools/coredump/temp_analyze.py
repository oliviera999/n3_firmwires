#!/usr/bin/env python3
"""Analyse le coredump extrait - recherche des noms de tâches"""

import struct

with open('coredumps/coredump_extracted.elf', 'rb') as f:
    elf = f.read()

# Chercher tous les strings ASCII qui ressemblent à des noms de tâches
print('=== RECHERCHE DE NOMS DE TÂCHES DANS LE COREDUMP ===')
print()

# Noms de tâches connus dans le projet
known_tasks = [b'loopTask', b'autoTask', b'mailTask', b'sensorTask', b'async_tcp', 
               b'IDLE', b'tiT', b'wifi', b'sys_evt', b'esp_timer', b'ipc', b'Tmr']

for task in known_tasks:
    pos = 0
    occurrences = []
    while True:
        pos = elf.find(task, pos)
        if pos == -1:
            break
        occurrences.append(pos)
        pos += 1
    if occurrences:
        print(f'  Trouvé "{task.decode()}" aux offsets: {occurrences[:5]}{"..." if len(occurrences) > 5 else ""}')

print()

# En-tête ELF
e_phoff = struct.unpack('<I', elf[28:32])[0]
e_phnum = struct.unpack('<H', elf[44:46])[0]
e_phentsize = struct.unpack('<H', elf[42:44])[0]

print('=' * 60)
print('📊 ANALYSE DU COREDUMP ESP32')
print('=' * 60)
print()

# Trouver le segment NOTE (type 4)
for i in range(e_phnum):
    ph_offset = e_phoff + i * e_phentsize
    p_type = struct.unpack('<I', elf[ph_offset:ph_offset+4])[0]
    
    if p_type == 4:  # PT_NOTE
        p_offset = struct.unpack('<I', elf[ph_offset+4:ph_offset+8])[0]
        p_filesz = struct.unpack('<I', elf[ph_offset+16:ph_offset+20])[0]
        
        note_data = elf[p_offset:p_offset+p_filesz]
        print(f'Segment NOTE: offset=0x{p_offset:X}, taille={p_filesz} bytes')
        print()
        
        # Parser les notes (format: namesz, descsz, type, name, desc)
        pos = 0
        note_idx = 0
        tasks_info = []
        crashed_task = None
        
        print('=== LISTE DES NOTES ===')
        while pos < len(note_data) - 12:
            namesz = struct.unpack('<I', note_data[pos:pos+4])[0]
            descsz = struct.unpack('<I', note_data[pos+4:pos+8])[0]
            note_type = struct.unpack('<I', note_data[pos+8:pos+12])[0]
            
            if namesz == 0 and descsz == 0:
                break
            
            # Nom (arrondi à 4 bytes)
            name_padded = (namesz + 3) & ~3
            name_start = pos + 12
            name_raw = note_data[name_start:name_start+namesz]
            name = name_raw.rstrip(b'\x00').decode('utf-8', errors='replace')
            
            # Description
            desc_start = name_start + name_padded
            desc_padded = (descsz + 3) & ~3
            desc = note_data[desc_start:desc_start+descsz]
            
            # Afficher toutes les notes
            print(f'  [{note_idx:2}] name="{name}" type={note_type} size={descsz}')
            
            # ESP_CORE_DUMP_INFO contient les infos du crash
            if 'INFO' in name or 'CORE' in name:
                print(f'       Data: {desc[:min(48, descsz)].hex()}')
                
                # Chercher les registres PC/excvaddr dans la description
                if descsz >= 8:
                    for j in range(0, min(descsz-4, 64), 4):
                        val = struct.unpack('<I', desc[j:j+4])[0]
                        # Les adresses de code sont typiquement 0x400xxxxx (IRAM) ou 0x3Fxxxxxx (DRAM)
                        if 0x40000000 <= val <= 0x40400000:
                            print(f'       Potentiel PC/addr à offset {j}: 0x{val:08X}')
            
            # Collecter les infos de tâches
            if 'EXTRA_INFO' in name and descsz > 0:
                # ESP_CORE_DUMP_EXTRA_INFO contient des infos sur l'exception
                print(f'=== {name} (Exception Info) ===')
                if descsz >= 16:
                    # Format: crash_tcb, crash_pc, exception_cause, etc.
                    exc_cause = struct.unpack('<I', desc[0:4])[0] if descsz >= 4 else 0
                    exc_vaddr = struct.unpack('<I', desc[4:8])[0] if descsz >= 8 else 0
                    exc_pc = struct.unpack('<I', desc[8:12])[0] if descsz >= 12 else 0
                    
                    print(f'  Exception cause: {exc_cause}')
                    print(f'  Exception vaddr: 0x{exc_vaddr:08X}')
                    print(f'  PC at crash: 0x{exc_pc:08X}')
                    
                    # Decoder la cause d'exception Xtensa
                    exc_causes = {
                        0: 'IllegalInstruction',
                        2: 'InstructionFetchError',
                        3: 'LoadStoreError', 
                        4: 'Level1Interrupt',
                        5: 'Alloca',
                        6: 'IntegerDivideByZero',
                        9: 'LoadStoreAlignment',
                        12: 'InstrPIF_DataError',
                        13: 'LoadStorePIF_DataError',
                        14: 'InstrPIF_AddrError',
                        15: 'LoadStorePIF_AddrError',
                        16: 'InstTLBMiss',
                        17: 'InstTLBMultiHit',
                        18: 'InstFetchPrivilege',
                        20: 'InstFetchProhibited',
                        24: 'LoadStoreTLBMiss',
                        25: 'LoadStoreTLBMultiHit',
                        26: 'LoadStorePrivilege',
                        28: 'LoadProhibited',
                        29: 'StoreProhibited',
                    }
                    if exc_cause in exc_causes:
                        print(f'  → Cause: {exc_causes[exc_cause]}')
                print()
            
            elif note_type == 1 and descsz >= 4:  # NT_PRSTATUS - process status
                # Extraire les registres Xtensa
                if descsz >= 72:  # Taille minimale pour les registres
                    # Les registres commencent généralement après l'en-tête prstatus
                    # PC est souvent à offset 0 ou dans les premiers mots
                    pass
            
            # Chercher les noms de tâches dans les notes
            if b'Task' in name_raw or descsz > 100:
                # Chercher une chaîne de caractères ASCII qui ressemble à un nom de tâche
                for j in range(0, min(64, descsz)):
                    try:
                        potential_name = desc[j:j+16]
                        if all(32 <= b < 127 or b == 0 for b in potential_name[:16] if b != 0):
                            decoded = potential_name.split(b'\x00')[0].decode('utf-8', errors='ignore')
                            if len(decoded) >= 3 and decoded.isalnum() or '_' in decoded:
                                tasks_info.append(decoded)
                                break
                    except:
                        pass
            
            note_idx += 1
            pos = desc_start + desc_padded
        
        print(f'Total: {note_idx} notes dans le segment')
        
        if tasks_info:
            print()
            print('=== TÂCHES DÉTECTÉES ===')
            for t in set(tasks_info[:10]):
                print(f'  - {t}')
        
        break

# Analyser les segments LOAD pour trouver des indices
print()
print('=== SEGMENTS MÉMOIRE (LOAD) ===')
for i in range(e_phnum):
    ph_offset = e_phoff + i * e_phentsize
    p_type = struct.unpack('<I', elf[ph_offset:ph_offset+4])[0]
    
    if p_type == 1:  # PT_LOAD
        p_offset = struct.unpack('<I', elf[ph_offset+4:ph_offset+8])[0]
        p_vaddr = struct.unpack('<I', elf[ph_offset+8:ph_offset+12])[0]
        p_filesz = struct.unpack('<I', elf[ph_offset+16:ph_offset+20])[0]
        
        # Classifier la région mémoire
        if 0x3FF00000 <= p_vaddr < 0x40000000:
            region = 'DRAM/Heap'
        elif 0x40000000 <= p_vaddr < 0x40400000:
            region = 'IRAM'
        elif p_vaddr >= 0x40400000:
            region = 'Flash'
        else:
            region = 'Other'
        
        print(f'  0x{p_vaddr:08X} - 0x{p_vaddr+p_filesz:08X} ({p_filesz:6} bytes) [{region}]')

print()
print('=' * 60)
print('💡 Pour une analyse complète avec symboles, utilisez:')
print('   xtensa-esp32-elf-gdb .pio/build/wroom-test/firmware.elf')
print('   (gdb) target remote :1234')
print('   ou avec esp_coredump si les versions correspondent')
print('=' * 60)
