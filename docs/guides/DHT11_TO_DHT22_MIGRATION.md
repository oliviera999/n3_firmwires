# Migration DHT11 vers DHT22

## Système actuel (Sélection automatique)

Le système utilise maintenant une **sélection automatique** du capteur DHT selon le profil de compilation :

### 1. Code source principal
- **Fichier** : `src/sensors.cpp`
- **Lignes 782-788** : Sélection conditionnelle basée sur `PROFILE_PROD`
  - `PROFILE_PROD` → DHT22 (production)
  - `PROFILE_DEV` ou `PROFILE_TEST` → DHT11 (développement/test)

### 2. Plages de validation unifiées
Les plages de validation sont **identiques pour DHT11 et DHT22** dans le projet :
- **Température** : **+3°C à +50°C** (pour les deux capteurs)
- **Humidité** : **10% à 100%** (pour les deux capteurs)

Ces valeurs sont définies dans `include/project_config.h` et s'appliquent uniformément.

### 3. Documentation mise à jour
- **README.md** : "DHT11 (air)" → "DHT22 (air)"
- **WATCHDOG_TIMEOUT_FIX.md** : "DHT11" → "DHT22"
- **SENSOR_RECOVERY_SYSTEM.md** : "DHT11/DHT22" → "DHT22"

## Avantages du DHT22 en production

### Précision améliorée
- **Température** : ±0.5°C (vs ±2°C pour DHT11)
- **Humidité** : ±2-5% (vs ±5% pour DHT11)

### Note sur les plages
Le projet utilise des plages **unifiées** (+3°C à +50°C, 10-100%) identiques pour les deux capteurs, garantissant la compatibilité et la cohérence entre environnements dev/test et production.

### Compatibilité
- Même bibliothèque Adafruit DHT sensor library
- Même interface de programmation
- Même pin de connexion

## Notes importantes

1. **Câblage** : Le DHT22 utilise le même câblage que le DHT11
2. **Timing** : Les délais de lecture restent identiques
3. **Bibliothèque** : Aucun changement de dépendance nécessaire
4. **Validation** : Les plages de validation sont strictement identiques (+3°C à +50°C, 10-100%)

## Configuration des environnements

### Environnements utilisant DHT22 (Production)
- `wroom-prod`, `s3-prod`
- `wroom-prod-pio6`, `s3-prod-pio6`
- `wroom-minimal`

### Environnements utilisant DHT11 (Dev/Test)
- `wroom-dev`, `s3-dev`
- `wroom-test`, `s3-test`
- `wroom-test-pio6`, `s3-test-pio6`

## Test recommandé

Après la migration, il est recommandé de :
1. Compiler avec l'environnement souhaité (ex: `pio run -e wroom-prod`)
2. Flasher le code sur l'ESP32
3. Vérifier les lectures de température et d'humidité
4. Comparer avec un autre capteur de référence si disponible
5. Vérifier la stabilité des mesures sur plusieurs heures

## Documentation complémentaire

Voir `DHT_SENSOR_CONFIGURATION.md` pour plus de détails sur la configuration et l'utilisation des capteurs DHT. 