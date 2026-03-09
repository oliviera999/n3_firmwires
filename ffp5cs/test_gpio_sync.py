#!/usr/bin/env python3
"""
Test GPIO Parsing Unifié - Version 11.68
Teste le nouveau système de parsing GPIO simplifié
"""

import json
import os
import requests
import sys
import time

# Configuration
SERVER_URL = os.environ.get("FFP3_SERVER_URL", "http://localhost/ffp3")
API_KEY = os.environ.get("FFP3_API_KEY", "")
if not API_KEY:
    print("Erreur : variable d'environnement FFP3_API_KEY non définie")
    sys.exit(1)

def test_server_outputs_format():
    """Test 1: Vérifier que le serveur retourne uniquement GPIO numériques"""
    print("=== Test 1: Format serveur GPIO numériques uniquement ===")
    
    try:
        response = requests.get(f"{SERVER_URL}/api/outputs/state", timeout=10)
        if response.status_code == 200:
            data = response.json()
            print(f"✓ Serveur répond: {len(data)} GPIO")
            
            # Vérifier format GPIO numérique uniquement
            numeric_keys = 0
            semantic_keys = 0
            
            for key in data.keys():
                if key.isdigit():
                    numeric_keys += 1
                    print(f"  GPIO {key}: {data[key]}")
                else:
                    semantic_keys += 1
                    print(f"  ⚠️ Clé sémantique trouvée: {key}")
            
            if semantic_keys == 0:
                print("✓ Format correct: GPIO numériques uniquement")
                return True
            else:
                print(f"❌ Format incorrect: {semantic_keys} clés sémantiques trouvées")
                return False
                
        else:
            print(f"❌ Erreur serveur: {response.status_code}")
            return False
            
    except Exception as e:
        print(f"❌ Erreur connexion: {e}")
        return False

def test_post_partial_update():
    """Test 2: POST partiel pour changement GPIO"""
    print("\n=== Test 2: POST partiel GPIO ===")
    
    # Test changement GPIO 16 (pompe aquarium)
    payload = {
        "api_key": API_KEY,
        "sensor": "esp32-wroom",
        "version": "11.68",
        "16": "1"  # GPIO 16 = pompe aquarium ON
    }
    
    try:
        response = requests.post(f"{SERVER_URL}/post-data", data=payload, timeout=10)
        if response.status_code == 200:
            print("✓ POST partiel accepté")
            
            # Vérifier que le changement est pris en compte
            time.sleep(1)
            state_response = requests.get(f"{SERVER_URL}/api/outputs/state", timeout=10)
            if state_response.status_code == 200:
                state_data = state_response.json()
                if state_data.get("16") == 1:
                    print("✓ Changement GPIO 16 confirmé")
                    return True
                else:
                    print(f"❌ GPIO 16 non mis à jour: {state_data.get('16')}")
                    return False
            else:
                print("❌ Impossible de vérifier l'état")
                return False
        else:
            print(f"❌ Erreur POST: {response.status_code}")
            return False
            
    except Exception as e:
        print(f"❌ Erreur POST: {e}")
        return False

def test_gpio_mapping():
    """Test 3: Vérifier mapping GPIO connu"""
    print("\n=== Test 3: Mapping GPIO connu ===")
    
    # GPIO connus selon gpio_mapping.h
    known_gpios = {
        16: "Pompe aquarium",
        18: "Pompe réservoir", 
        15: "Lumière",
        2: "Chauffage",
        108: "Nourrir petits",
        109: "Nourrir gros",
        100: "Email",
        101: "Notif email",
        102: "Seuil aquarium",
        103: "Seuil réservoir",
        104: "Seuil chauffage",
        105: "Heure matin",
        106: "Heure midi", 
        107: "Heure soir",
        110: "Reset ESP32",
        111: "Durée gros",
        112: "Durée petits",
        113: "Durée remplissage",
        114: "Limite inondation",
        115: "Forcer réveil",
        116: "Fréq réveil"
    }
    
    try:
        response = requests.get(f"{SERVER_URL}/api/outputs/state", timeout=10)
        if response.status_code == 200:
            data = response.json()
            
            found_gpios = 0
            for gpio, description in known_gpios.items():
                if str(gpio) in data:
                    found_gpios += 1
                    print(f"  ✓ GPIO {gpio}: {description} = {data[str(gpio)]}")
                else:
                    print(f"  ⚠️ GPIO {gpio}: {description} - non trouvé")
            
            print(f"\n✓ {found_gpios}/{len(known_gpios)} GPIO connus trouvés")
            return found_gpios >= len(known_gpios) * 0.8  # 80% minimum
            
        else:
            print(f"❌ Erreur serveur: {response.status_code}")
            return False
            
    except Exception as e:
        print(f"❌ Erreur: {e}")
        return False

def test_bidirectional_sync():
    """Test 4: Synchronisation bidirectionnelle"""
    print("\n=== Test 4: Synchronisation bidirectionnelle ===")
    
    # 1. Changer GPIO 15 (lumière) via POST
    print("1. Changement GPIO 15 (lumière) via POST...")
    payload = {
        "api_key": API_KEY,
        "sensor": "esp32-wroom", 
        "version": "11.68",
        "15": "1"
    }
    
    try:
        response = requests.post(f"{SERVER_URL}/post-data", data=payload, timeout=10)
        if response.status_code != 200:
            print(f"❌ Échec POST: {response.status_code}")
            return False
            
        # 2. Vérifier changement via GET
        print("2. Vérification changement via GET...")
        time.sleep(1)
        state_response = requests.get(f"{SERVER_URL}/api/outputs/state", timeout=10)
        if state_response.status_code == 200:
            state_data = state_response.json()
            if state_data.get("15") == 1:
                print("✓ Synchronisation bidirectionnelle OK")
                return True
            else:
                print(f"❌ GPIO 15 non synchronisé: {state_data.get('15')}")
                return False
        else:
            print("❌ Impossible de vérifier l'état")
            return False
            
    except Exception as e:
        print(f"❌ Erreur: {e}")
        return False

def main():
    """Exécution des tests"""
    print("🧪 Test GPIO Parsing Unifié - Version 11.68")
    print("=" * 50)
    
    tests = [
        test_server_outputs_format,
        test_post_partial_update, 
        test_gpio_mapping,
        test_bidirectional_sync
    ]
    
    passed = 0
    total = len(tests)
    
    for test in tests:
        try:
            if test():
                passed += 1
        except Exception as e:
            print(f"❌ Erreur dans le test: {e}")
    
    print("\n" + "=" * 50)
    print(f"📊 Résultats: {passed}/{total} tests réussis")
    
    if passed == total:
        print("🎉 Tous les tests sont passés !")
        print("✅ Le système GPIO Parsing Unifié fonctionne correctement")
    else:
        print("⚠️ Certains tests ont échoué")
        print("🔧 Vérifiez la configuration du serveur et l'ESP32")
    
    return passed == total

if __name__ == "__main__":
    main()
