#!/usr/bin/env python3
"""
Diagnostic pour le problème de gestion du chauffage insensible aux commandes BDD distante
"""

import json
import re
from pathlib import Path

def safe_read_file(file_path):
    """Lit un fichier en gérant les problèmes d'encodage"""
    try:
        return file_path.read_text(encoding='utf-8')
    except UnicodeDecodeError:
        try:
            return file_path.read_text(encoding='latin-1')
        except:
            return file_path.read_bytes().decode('utf-8', errors='ignore')

def analyze_heating_issue():
    """Analyse le problème de chauffage"""
    
    print("=== DIAGNOSTIC CHAUFFAGE - COMMANDES DISTANTES ===\n")
    
    # 1. Vérification des pins
    print("1. CONFIGURATION DES PINS:")
    pins_file = Path("include/pins.h")
    if pins_file.exists():
        content = safe_read_file(pins_file)
        radiator_match = re.search(r'RADIATEURS\s*=\s*(\d+)', content)
        if radiator_match:
            radiator_pin = radiator_match.group(1)
            print(f"   - GPIO RADIATEURS: {radiator_pin}")
        else:
            print("   ❌ GPIO RADIATEURS non trouvé")
    else:
        print("   ❌ Fichier pins.h non trouvé")
    
    # 2. Vérification du traitement des commandes
    print("\n2. TRAITEMENT DES COMMANDES DISTANTES:")
    automatism_file = Path("src/automatism.cpp")
    if automatism_file.exists():
        content = safe_read_file(automatism_file)
        
        # Recherche du traitement GPIO RADIATEURS
        radiator_handling = re.search(r'else if \(id == Pins::RADIATEURS\)\s*\{[^}]*\}', content, re.DOTALL)
        if radiator_handling:
            print("   ✅ Traitement GPIO RADIATEURS trouvé")
            print(f"   Code: {radiator_handling.group(0).strip()}")
        else:
            print("   ❌ Traitement GPIO RADIATEURS non trouvé")
        
        # Vérification de la fonction handleRemoteCommands
        if "handleRemoteCommands" in content:
            print("   ✅ Fonction handleRemoteCommands présente")
        else:
            print("   ❌ Fonction handleRemoteCommands manquante")
    
    # 3. Vérification de la récupération des états distants
    print("\n3. RÉCUPÉRATION DES ÉTATS DISTANTS:")
    web_client_file = Path("src/web_client.cpp")
    if web_client_file.exists():
        content = safe_read_file(web_client_file)
        if "fetchRemoteState" in content:
            print("   ✅ Fonction fetchRemoteState présente")
        else:
            print("   ❌ Fonction fetchRemoteState manquante")
    
    # 4. Vérification des URLs de configuration
    print("\n4. CONFIGURATION DES URLs:")
    constants_file = Path("include/constants.h")
    if constants_file.exists():
        content = safe_read_file(constants_file)
        server_output_match = re.search(r'SERVER_OUTPUT\s*=\s*"([^"]+)"', content)
        if server_output_match:
            print(f"   ✅ SERVER_OUTPUT: {server_output_match.group(1)}")
        else:
            print("   ❌ SERVER_OUTPUT non configuré")
    
    # 5. Recommandations
    print("\n5. RECOMMANDATIONS:")
    print("   a) Vérifier que la BDD distante envoie bien les commandes sur le GPIO 13 (ESP32-S3)")
    print("   b) Ajouter des logs de debug dans handleRemoteCommands()")
    print("   c) Vérifier la structure JSON retournée par fetchRemoteState()")
    print("   d) Tester avec une commande manuelle via l'interface web locale")
    
    # 6. Points de vérification critiques
    print("\n6. POINTS CRITIQUES À VÉRIFIER:")
    print("   - La BDD distante doit envoyer: {\"13\": 1} pour allumer le chauffage")
    print("   - La BDD distante doit envoyer: {\"13\": 0} pour éteindre le chauffage")
    print("   - Le GPIO 13 doit être configuré en sortie")
    print("   - Les commandes automatiques ne doivent pas interférer")

def generate_debug_code():
    """Génère du code de debug pour diagnostiquer le problème"""
    
    print("\n=== CODE DE DEBUG À AJOUTER ===\n")
    
    debug_code = '''
// Dans automatism.cpp, fonction handleRemoteCommands(), ajouter:

// Debug des commandes distantes
Serial.printf("[DEBUG] Commande distante reçue: %s\\n", key);
Serial.printf("[DEBUG] Valeur: %s\\n", kv.value().as<String>().c_str());

// Spécifiquement pour le chauffage
if (id == Pins::RADIATEURS) {
    Serial.printf("[DEBUG] Commande chauffage: GPIO %d = %s\\n", id, valBool ? "ON" : "OFF");
    if (valBool) {
        Serial.println("[DEBUG] Démarrage chauffage...");
        _acts.startHeater();
        Serial.println("[DEBUG] Chauffage démarré");
    } else {
        Serial.println("[DEBUG] Arrêt chauffage...");
        _acts.stopHeater();
        Serial.println("[DEBUG] Chauffage arrêté");
    }
}

// Dans web_client.cpp, fonction fetchRemoteState(), ajouter:
Serial.printf("[DEBUG] Réponse serveur: %s\\n", resp.c_str());
'''
    
    print(debug_code)

if __name__ == "__main__":
    analyze_heating_issue()
    generate_debug_code() 