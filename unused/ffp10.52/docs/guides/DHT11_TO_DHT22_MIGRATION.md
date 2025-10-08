# Migration DHT11 vers DHT22

## Changements effectués

### 1. Code source principal
- **Fichier** : `src/sensors.cpp`
- **Ligne 421** : Changement de `DHT11` vers `DHT22` dans le constructeur `AirSensor`

### 2. Plages de validation ajustées
- **Température** : De `5.0°C - 60.0°C` vers `-40.0°C - 80.0°C`
  - DHT22 : Plage étendue avec précision ±0.5°C
  - DHT11 : Plage limitée avec précision ±2°C
- **Humidité** : De `5.0% - 100.0%` vers `0.0% - 100.0%`
  - DHT22 : Plage complète 0-100% avec précision ±2-5%
  - DHT11 : Plage limitée avec précision ±5%

### 3. Documentation mise à jour
- **README.md** : "DHT11 (air)" → "DHT22 (air)"
- **WATCHDOG_TIMEOUT_FIX.md** : "DHT11" → "DHT22"
- **SENSOR_RECOVERY_SYSTEM.md** : "DHT11/DHT22" → "DHT22"

## Avantages du DHT22

### Précision améliorée
- **Température** : ±0.5°C (vs ±2°C pour DHT11)
- **Humidité** : ±2-5% (vs ±5% pour DHT11)

### Plage de mesure étendue
- **Température** : -40°C à +80°C (vs -40°C à +80°C mais moins précis)
- **Humidité** : 0-100% (vs 20-90% pour DHT11)

### Compatibilité
- Même bibliothèque Adafruit DHT sensor library
- Même interface de programmation
- Même pin de connexion

## Notes importantes

1. **Câblage** : Le DHT22 utilise le même câblage que le DHT11
2. **Timing** : Les délais de lecture restent identiques
3. **Bibliothèque** : Aucun changement de dépendance nécessaire
4. **Validation** : Les plages de validation ont été ajustées pour exploiter la meilleure précision du DHT22

## Test recommandé

Après la migration, il est recommandé de :
1. Compiler et flasher le code
2. Vérifier les lectures de température et d'humidité
3. Comparer avec un autre capteur de référence si disponible
4. Vérifier la stabilité des mesures sur plusieurs heures 