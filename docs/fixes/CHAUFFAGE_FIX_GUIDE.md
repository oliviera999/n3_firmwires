# Guide de résolution - Problème de chauffage insensible aux commandes distantes

## 🔍 Diagnostic effectué

### Problème identifié
Le chauffage (GPIO 13) ne répond pas aux commandes de la base de données distante car **l'entrée GPIO 13 est absente de la BDD**.

### État actuel
- ✅ GPIO 13 configuré correctement dans `pins.h`
- ✅ Code de traitement des commandes présent dans `automatism.cpp`
- ✅ Fonction `fetchRemoteState()` opérationnelle
- ❌ **GPIO 13 absent de la BDD distante**

## 🛠️ Solutions appliquées

### 1. Ajout de logs de debug
**Fichiers modifiés :**
- `src/automatism.cpp` : Logs des commandes GPIO reçues
- `src/web_client.cpp` : Log de la réponse serveur complète

**Code ajouté :**
```cpp
// Debug des commandes GPIO reçues
Serial.printf("[DEBUG] Commande GPIO reçue: %s = %s (id=%d, valBool=%s)\n", 
             key, kv.value().as<String>().c_str(), id, valBool ? "true" : "false");

// Debug spécifique chauffage
if (id == Pins::RADIATEURS) {
    Serial.printf("[DEBUG] Commande chauffage: GPIO %d = %s\n", id, valBool ? "ON" : "OFF");
    // ... logs détaillés
}
```

### 2. Correction de la base de données distante
**Script créé :** `fix_heating_database.py`

**Action requise :** Exécuter le script pour ajouter l'entrée GPIO 13 manquante.

## 📋 Étapes de résolution

### Étape 1 : Corriger la BDD distante
```bash
python fix_heating_database.py
```

### Étape 2 : Compiler et téléverser le firmware
```bash
pio run -t upload
```

### Étape 3 : Tester les commandes
1. **Via l'interface web locale** : `http://[IP_ESP32]/heater`
2. **Via la BDD distante** : Utiliser l'interface web distante
3. **Vérifier les logs série** pour voir les commandes reçues

### Étape 4 : Vérification
Les logs série doivent afficher :
```
[DEBUG] Commande GPIO reçue: 13 = 1 (id=13, valBool=true)
[DEBUG] Commande chauffage: GPIO 13 = ON
[DEBUG] Démarrage chauffage...
[DEBUG] Chauffage démarré
```

## 🔧 Structure attendue de la BDD

La BDD distante doit contenir :
```json
{
  "13": "0",  // État du chauffage (0=OFF, 1=ON)
  "16": "1",  // Pompe aquarium
  "18": "0",  // Pompe tank
  "15": "0",  // Lumière
  // ... autres GPIOs
}
```

## 🚨 Points critiques

### 1. Mapping GPIO
- **ESP32-S3** : GPIO 13 = `Pins::RADIATEURS`
- **ESP32** : GPIO 2 = `Pins::RADIATEURS`

### 2. Format des commandes
- **Allumage** : `{"13": "1"}` ou `{"13": 1}`
- **Extinction** : `{"13": "0"}` ou `{"13": 0}`

### 3. Interférences possibles
- **Commande automatique** : Peut interférer avec les commandes manuelles
- **Seuil de température** : `heaterThresholdC` (actuellement 17.0°C)

## 📊 Tests de validation

### Test 1 : Commande locale
```bash
curl "http://[IP_ESP32]/heater"
```

### Test 2 : Commande distante
```bash
python test_heating_commands.py
```

### Test 3 : Vérification BDD
```bash
curl "http://iot.olution.info/ffp3/ffp3control/ffp3-outputs-action2.php?action=outputs_state&board=1"
```

## 🔄 Rollback si nécessaire

Si les modifications causent des problèmes :

1. **Supprimer les logs de debug** :
   - Retirer les `Serial.printf` ajoutés dans `automatism.cpp`
   - Retirer le log dans `web_client.cpp`

2. **Restaurer la BDD** :
   - Utiliser l'interface web pour remettre les paramètres d'origine

## 📞 Support

En cas de problème persistant :
1. Vérifier les logs série pour les erreurs
2. Contrôler la connectivité WiFi
3. Vérifier l'état de la BDD distante
4. Tester avec une commande locale d'abord

---
*Guide créé le $(date)*
*Version firmware : 5.7* 