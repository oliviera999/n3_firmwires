#!/usr/bin/env python3
"""
Script pour analyser un crash qui s'est produit vers 11h15
Recherche dans tous les fichiers de log disponibles
"""

import os
import re
from datetime import datetime
from pathlib import Path

def search_crash_in_file(filepath, search_time="11:1"):
    """Recherche un crash dans un fichier autour de 11h15"""
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
        
        crash_patterns = [
            r"rst:0x",
            r"Reset reason",
            r"reset reason",
            r"BOOT FFP5CS",
            r"Guru Meditation",
            r"Panic",
            r"PANIC",
            r"Brownout",
            r"Core dump",
            r"stack overflow",
            r"watchdog.*timeout",
            r"WDT.*timeout"
        ]
        
        found_crashes = []
        context_lines = []
        
        for i, line in enumerate(lines):
            # Chercher les lignes autour de 11h15
            if search_time in line or (i > 0 and search_time in lines[i-1]) or (i < len(lines)-1 and search_time in lines[i+1]):
                # Vérifier si c'est un pattern de crash
                for pattern in crash_patterns:
                    if re.search(pattern, line, re.IGNORECASE):
                        found_crashes.append({
                            'line_num': i+1,
                            'line': line.strip(),
                            'pattern': pattern,
                            'context': lines[max(0, i-5):min(len(lines), i+6)]
                        })
                        break
        
        return found_crashes
    except Exception as e:
        print(f"Erreur lecture {filepath}: {e}")
        return []

def analyze_all_logs():
    """Analyse tous les fichiers de log disponibles"""
    project_root = Path(__file__).parent
    log_files = []
    
    # Chercher tous les fichiers de log
    patterns = [
        "*.log",
        "*.txt",
        "monitor_*.log",
        "monitor_*.txt",
        "esp32_*.log",
        "esp32_*.txt"
    ]
    
    for pattern in patterns:
        log_files.extend(project_root.glob(pattern))
        log_files.extend((project_root / "pythonserial").glob(pattern))
    
    print("=" * 60)
    print("ANALYSE DU CRASH VERS 11H15")
    print("=" * 60)
    print(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"Fichiers analysés: {len(log_files)}")
    print("=" * 60)
    print()
    
    all_crashes = []
    
    for log_file in log_files:
        if log_file.is_file() and log_file.stat().st_size > 0:
            print(f"Analyse de: {log_file.name}")
            crashes = search_crash_in_file(log_file, "11:1")
            if crashes:
                print(f"  ✓ {len(crashes)} crash(es) trouvé(s) !")
                all_crashes.extend([(log_file.name, crash) for crash in crashes])
            else:
                print(f"  - Aucun crash trouvé")
    
    print()
    print("=" * 60)
    
    if all_crashes:
        print(f"TOTAL: {len(all_crashes)} crash(es) détecté(s) vers 11h15")
        print("=" * 60)
        print()
        
        for file_name, crash_info in all_crashes:
            print(f"Fichier: {file_name}")
            print(f"Ligne: {crash_info['line_num']}")
            print(f"Pattern: {crash_info['pattern']}")
            print(f"Ligne complète:")
            print(f"  {crash_info['line']}")
            print()
            print("Contexte (10 lignes):")
            for ctx_line in crash_info['context']:
                marker = ">>> " if ctx_line.strip() == crash_info['line'] else "    "
                print(f"{marker}{ctx_line.rstrip()}")
            print()
            print("-" * 60)
            print()
    else:
        print("AUCUN CRASH TROUVÉ VERS 11H15")
        print("=" * 60)
        print()
        print("Recherche dans tous les fichiers pour d'autres crashes...")
        
        # Recherche générale de crashes
        general_crashes = []
        for log_file in log_files:
            if log_file.is_file() and log_file.stat().st_size > 0:
                crashes = search_crash_in_file(log_file, "")
                if crashes:
                    general_crashes.extend([(log_file.name, crash) for crash in crashes])
        
        if general_crashes:
            print(f"Trouvé {len(general_crashes)} crash(es) dans les logs (toutes heures confondues)")
            print("Derniers crashes détectés:")
            for file_name, crash_info in general_crashes[-5:]:
                print(f"  - {file_name}: ligne {crash_info['line_num']} - {crash_info['pattern']}")
        else:
            print("Aucun crash détecté dans les fichiers de log analysés")

if __name__ == "__main__":
    analyze_all_logs()
