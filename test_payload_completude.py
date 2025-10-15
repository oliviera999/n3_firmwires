#!/usr/bin/env python3
"""
Script de validation de la complétude des données POST
Capture et analyse les payloads envoyés par l'ESP32
"""

import serial
import time
import re
from datetime import datetime

PORT = 'COM6'  # USB-SERIAL CH340
BAUDRATE = 115200
DURATION = 90  # secondes

# Liste complète des champs attendus (32 champs)
EXPECTED_FIELDS = [
    'api_key', 'sensor', 'version', 
    'TempAir', 'Humidite', 'TempEau',
    'EauPotager', 'EauAquarium', 'EauReserve', 
    'diffMaree', 'Luminosite',
    'etatPompeAqua', 'etatPompeTank', 'etatHeat', 'etatUV',
    'bouffeMatin', 'bouffeMidi', 'bouffeSoir', 
    'tempsGros', 'tempsPetits',
    'aqThreshold', 'tankThreshold', 'chauffageThreshold',
    'tempsRemplissageSec', 'limFlood', 
    'WakeUp', 'FreqWakeUp',
    'bouffePetits', 'bouffeGros', 
    'mail', 'mailNotif',
    'resetMode'
]

def analyze_payload(payload):
    """Analyse un payload et vérifie la présence de tous les champs"""
    print("\n" + "="*80)
    print("ANALYSE DU PAYLOAD CAPTURÉ")
    print("="*80)
    
    # Extraction des champs présents
    fields_found = re.findall(r'(\w+)=', payload)
    fields_found_unique = list(dict.fromkeys(fields_found))  # Préserver l'ordre
    
    print(f"\n📊 Statistiques:")
    print(f"  - Champs attendus: {len(EXPECTED_FIELDS)}")
    print(f"  - Champs trouvés: {len(fields_found_unique)}")
    print(f"  - Taille payload: {len(payload)} octets")
    
    # Vérification champ par champ
    missing = []
    present = []
    
    print(f"\n✅ Champs présents ({len(fields_found_unique)}):")
    for field in EXPECTED_FIELDS:
        if field in fields_found_unique:
            present.append(field)
            # Extraire la valeur
            match = re.search(rf'{field}=([^&]*)', payload)
            value = match.group(1) if match else "?"
            print(f"  ✓ {field:25s} = {value}")
        else:
            missing.append(field)
    
    if missing:
        print(f"\n❌ Champs manquants ({len(missing)}):")
        for field in missing:
            print(f"  ✗ {field}")
    
    # Champs supplémentaires (non attendus)
    extra = [f for f in fields_found_unique if f not in EXPECTED_FIELDS]
    if extra:
        print(f"\n➕ Champs supplémentaires ({len(extra)}):")
        for field in extra:
            match = re.search(rf'{field}=([^&]*)', payload)
            value = match.group(1) if match else "?"
            print(f"  + {field:25s} = {value}")
    
    # Verdict final
    print("\n" + "="*80)
    if len(missing) == 0:
        print("✅ VERDICT: PAYLOAD COMPLET - Tous les champs sont présents!")
    else:
        print(f"⚠️ VERDICT: PAYLOAD INCOMPLET - {len(missing)} champs manquants")
    print("="*80 + "\n")
    
    return len(missing) == 0

def main():
    print("="*80)
    print("VALIDATION COMPLÉTUDE DONNÉES ESP32 - POST-REFACTORING v11.08")
    print("="*80)
    print(f"Port: {PORT}")
    print(f"Baudrate: {BAUDRATE}")
    print(f"Durée: {DURATION} secondes")
    print(f"Champs attendus: {len(EXPECTED_FIELDS)}")
    print("="*80)
    
    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        print(f"\n✓ Port série {PORT} ouvert\n")
        
        start_time = time.time()
        payload_found = None
        
        while time.time() - start_time < DURATION:
            if ser.in_waiting > 0:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    # Afficher la ligne
                    elapsed = time.time() - start_time
                    print(f"[{elapsed:6.1f}s] {line}")
                    
                    # Détecter le payload POST
                    if 'payload:' in line.lower():
                        # Extraire le payload après "payload:"
                        match = re.search(r'payload:\s*(.+)', line, re.IGNORECASE)
                        if match:
                            payload_found = match.group(1)
                            print("\n" + ">"*80)
                            print(">>> PAYLOAD POST DÉTECTÉ <<<")
                            print(">"*80)
                    elif 'api_key=' in line:
                        # Le payload est sur cette ligne complète
                        payload_found = line
                        print("\n" + ">"*80)
                        print(">>> PAYLOAD POST DÉTECTÉ <<<")
                        print(">"*80)
                        
                except UnicodeDecodeError:
                    pass
            
            time.sleep(0.05)
        
        ser.close()
        
        print("\n" + "="*80)
        print(f"MONITORING TERMINÉ APRÈS {DURATION} SECONDES")
        print("="*80)
        
        if payload_found:
            analyze_payload(payload_found)
        else:
            print("\n⚠️ AUCUN PAYLOAD POST CAPTURÉ DURANT CETTE PÉRIODE")
            print("Note: Augmenter la durée ou vérifier la fréquence d'envoi")
        
    except serial.SerialException as e:
        print(f"\n❌ ERREUR: Impossible d'ouvrir le port {PORT}")
        print(f"Détails: {e}")
        print("\nVérifier:")
        print("  1. L'ESP32 est bien connecté")
        print("  2. Le port COM est correct")
        print("  3. Aucun autre programme n'utilise le port")
    except KeyboardInterrupt:
        print("\n\n⚠️ Monitoring interrompu par l'utilisateur")
    except Exception as e:
        print(f"\n❌ ERREUR INATTENDUE: {e}")
        import traceback
        traceback.print_exc()

if __name__ == '__main__':
    main()

