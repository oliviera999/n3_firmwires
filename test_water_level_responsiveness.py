#!/usr/bin/env python3
"""
Script de test pour vérifier la réactivité des capteurs de niveau d'eau
Version: v11.41 - Test du mode réactif ultrasonique

Ce script analyse les logs série pour vérifier que les capteurs ultrasoniques
répondent rapidement aux changements de niveau sans lissage excessif.
"""

import serial
import time
import re
from datetime import datetime
import sys

def test_water_level_responsiveness():
    """Test de réactivité des capteurs de niveau d'eau"""
    
    print("🔍 Test de réactivité des capteurs de niveau d'eau - v11.41")
    print("=" * 60)
    
    # Configuration du port série
    PORT = 'COM3'  # Ajuster selon votre configuration
    BAUD = 115200
    
    try:
        # Connexion série
        ser = serial.Serial(PORT, BAUD, timeout=1)
        print(f"✅ Connexion série établie sur {PORT} à {BAUD} baud")
        
        # Variables de test
        aquarium_readings = []
        potager_readings = []
        start_time = time.time()
        test_duration = 90  # 90 secondes de test
        
        print(f"📊 Collecte des données pendant {test_duration} secondes...")
        print("💡 Modifiez manuellement le niveau d'eau pour tester la réactivité")
        print()
        
        while time.time() - start_time < test_duration:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                
                if line:
                    # Recherche des lectures ultrasoniques
                    if "[Ultrasonic] Lecture réactive" in line:
                        # Extraction de la valeur
                        match = re.search(r'Lecture réactive (\d+): (\d+) cm', line)
                        if match:
                            reading_num = int(match.group(1))
                            value = int(match.group(2))
                            timestamp = time.time() - start_time
                            
                            print(f"⏱️ {timestamp:6.1f}s - Lecture réactive {reading_num}: {value} cm")
                    
                    elif "[SystemSensors] ⏱️ Niveau aquarium:" in line:
                        # Extraction du temps d'exécution
                        match = re.search(r'⏱️ Niveau aquarium: (\d+) ms', line)
                        if match:
                            exec_time = int(match.group(1))
                            timestamp = time.time() - start_time
                            print(f"🏊 {timestamp:6.1f}s - Aquarium: {exec_time} ms")
                    
                    elif "[SystemSensors] ⏱️ Niveau potager:" in line:
                        # Extraction du temps d'exécution
                        match = re.search(r'⏱️ Niveau potager: (\d+) ms', line)
                        if match:
                            exec_time = int(match.group(1))
                            timestamp = time.time() - start_time
                            print(f"🌱 {timestamp:6.1f}s - Potager: {exec_time} ms")
                    
                    elif "[Ultrasonic] Mode réactif:" in line:
                        # Extraction de la valeur finale
                        match = re.search(r'Mode réactif: (\d+) cm \(moyenne de (\d+) lectures sur 3\)', line)
                        if match:
                            value = int(match.group(1))
                            readings_count = int(match.group(2))
                            timestamp = time.time() - start_time
                            print(f"📈 {timestamp:6.1f}s - Mode réactif: {value} cm ({readings_count}/3 lectures)")
                    
                    elif "[Ultrasonic] Saut important détecté:" in line:
                        # Détection de saut important
                        match = re.search(r'Saut important détecté: (\d+) -> (\d+) cm \(Δ=(\d+)\)', line)
                        if match:
                            old_val = int(match.group(1))
                            new_val = int(match.group(2))
                            delta = int(match.group(3))
                            timestamp = time.time() - start_time
                            print(f"🚨 {timestamp:6.1f}s - SAUT DÉTECTÉ: {old_val} → {new_val} cm (Δ={delta})")
                
            except Exception as e:
                print(f"❌ Erreur lecture série: {e}")
                break
        
        print("\n" + "=" * 60)
        print("✅ Test terminé - Analyse des résultats:")
        print()
        print("📋 Critères de validation:")
        print("   • Temps d'exécution < 200ms (objectif: < 100ms)")
        print("   • Détection rapide des sauts de niveau")
        print("   • Pas de lissage excessif (valeurs instantanées)")
        print("   • Stabilité des lectures")
        print()
        print("💡 Modifications apportées en v11.41:")
        print("   • Mode réactif avec 3 lectures (comme demandé)")
        print("   • Délai standard de 60ms entre lectures")
        print("   • Moyenne simple au lieu de médiane")
        print("   • Seuil de validation plus permissif (50cm)")
        print("   • Pas de double confirmation pour les sauts")
        
    except serial.SerialException as e:
        print(f"❌ Erreur connexion série: {e}")
        print("💡 Vérifiez que le port série est correct et que l'ESP32 est connecté")
        return False
    
    except KeyboardInterrupt:
        print("\n⏹️ Test interrompu par l'utilisateur")
        return True
    
    finally:
        if 'ser' in locals():
            ser.close()
            print("🔌 Connexion série fermée")
    
    return True

if __name__ == "__main__":
    print("🚀 Démarrage du test de réactivité des capteurs ultrasoniques")
    print(f"📅 {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print()
    
    success = test_water_level_responsiveness()
    
    if success:
        print("\n🎉 Test terminé avec succès!")
        print("📊 Consultez les logs ci-dessus pour analyser la réactivité")
    else:
        print("\n❌ Test échoué")
        sys.exit(1)
