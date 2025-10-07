# Configuration des capteurs DHT11 et DHT22

## 📋 Vue d'ensemble

Le projet supporte deux modèles de capteurs DHT pour mesurer la température et l'humidité de l'air :
- **DHT11** : utilisé pour le développement et les tests
- **DHT22** : utilisé pour la production

## 🔧 Sélection automatique selon l'environnement

La sélection du capteur DHT se fait automatiquement à la compilation selon le profil :

```cpp
// src/sensors.cpp (lignes 782-788)
AirSensor::AirSensor() : _dht(Pins::DHT_PIN, 
#if defined(PROFILE_PROD)
                            DHT22  // Production : utilise DHT22
#else
                            DHT11  // Dev et Test : utilise DHT11
#endif
                            ),
```

### Mapping des environnements

| Environnement PlatformIO | Profile | Capteur DHT | Usage |
|--------------------------|---------|-------------|-------|
| `wroom-prod` | `PROFILE_PROD` | **DHT22** | Production WROOM |
| `s3-prod` | `PROFILE_PROD` | **DHT22** | Production S3 |
| `wroom-prod-pio6` | `PROFILE_PROD` | **DHT22** | Production WROOM (PIO 6) |
| `s3-prod-pio6` | `PROFILE_PROD` | **DHT22** | Production S3 (PIO 6) |
| `wroom-minimal` | `PROFILE_PROD` | **DHT22** | Production WROOM minimal |
| `wroom-test` | `PROFILE_TEST` | **DHT11** | Test WROOM |
| `s3-test` | `PROFILE_TEST` | **DHT11** | Test S3 |
| `wroom-test-pio6` | `PROFILE_TEST` | **DHT11** | Test WROOM (PIO 6) |
| `s3-test-pio6` | `PROFILE_TEST` | **DHT11** | Test S3 (PIO 6) |
| `wroom-dev` | `PROFILE_DEV` | **DHT11** | Développement WROOM |
| `s3-dev` | `PROFILE_DEV` | **DHT11** | Développement S3 |

## 📊 Comparaison des capteurs

### DHT11 (Dev/Test)
- **Précision température** : ±2°C
- **Précision humidité** : ±5%
- **Coût** : Économique
- **Usage** : Tests et développement

### DHT22 (Production)
- **Précision température** : ±0.5°C
- **Précision humidité** : ±2-5%
- **Coût** : Plus cher
- **Usage** : Environnements de production

### Plages de validation (identiques pour les 2 modèles)
Les plages configurées dans le projet sont **unifiées et identiques** pour DHT11 et DHT22 :
- **Température** : **+3°C à +50°C**
- **Humidité** : **10% à 100%**

## ⚙️ Configuration matérielle

### Pins utilisés

| Carte | GPIO | Définition |
|-------|------|------------|
| ESP32-S3 | GPIO 7 | `Pins::DHT_PIN` |
| ESP32-WROOM | GPIO 27 | `Pins::DHT_PIN` |

### Câblage identique

Les deux capteurs utilisent le **même câblage** :
- Pin 1 : VCC (3.3V ou 5V)
- Pin 2 : DATA (connecté au GPIO DHT_PIN avec résistance pull-up 10kΩ)
- Pin 3 : NC (non connecté)
- Pin 4 : GND

## 🔄 Paramètres de lecture (identiques pour les deux)

Le code gère les deux capteurs de **manière équivalente** :

### Timing
- **Intervalle minimum entre lectures** : 2500ms
- **Délai de stabilisation initial** : 100ms
- **Délai entre lectures multiples** : 100ms

### Validation
Les plages de validation sont **strictement identiques** pour les deux modèles :

```cpp
// include/project_config.h
namespace SensorConfig::AirSensor {
    constexpr float TEMP_MIN = 3.0f;      // +3°C minimum
    constexpr float TEMP_MAX = 50.0f;     // +50°C maximum
    constexpr float HUMIDITY_MIN = 10.0f; // 10% minimum
    constexpr float HUMIDITY_MAX = 100.0f; // 100% maximum
}
```

> 📌 **Note** : Ces plages sont **unifiées pour les deux capteurs** selon les spécifications du projet. DHT11 et DHT22 sont traités de manière identique dans le code.

### Filtrage
- **Filtre EMA** : coefficient 0.3
- **Historique glissant** : 5 valeurs
- **Seuil température** : écart max 3.0°C entre mesures
- **Seuil humidité** : écart max 10.0% entre mesures

### Récupération d'erreurs
- **Tentatives max** : 3
- **Délai entre tentatives** : 300ms
- **Délai après reset** : 1000ms

## 🔌 Bibliothèque utilisée

```ini
adafruit/DHT sensor library@^1.4.6
```

Cette bibliothèque supporte :
- ✅ DHT11
- ✅ DHT22 (AM2302)
- ✅ DHT21 (AM2301)

## 🚀 Comment changer de capteur

### Remplacement physique
1. Débrancher l'alimentation
2. Remplacer le capteur DHT11 par DHT22 (ou vice-versa)
3. Le câblage reste identique
4. Rebrancher l'alimentation

### Recompilation
Le code s'adapte automatiquement selon le profil :
- Pour **développer/tester** : compiler avec un environnement `-dev` ou `-test`
- Pour **déployer en production** : compiler avec un environnement `-prod`

### Exemple
```bash
# Pour tester avec DHT11
pio run -e wroom-test

# Pour produire avec DHT22
pio run -e wroom-prod
```

## ⚡ Avantages de cette architecture

1. **Simplicité** : Un seul code pour les deux capteurs
2. **Flexibilité** : Changement automatique selon l'environnement
3. **Économie** : DHT11 moins cher pour le développement
4. **Qualité** : DHT22 plus précis en production
5. **Compatibilité** : Même interface, même câblage

## 📝 Notes importantes

1. Les **plages de validation sont unifiées** pour éviter les surprises lors du passage de test à production
2. Le **timing est identique** pour les deux capteurs (optimisé pour le DHT22 qui est plus lent)
3. La **bibliothèque Adafruit** gère automatiquement les différences de protocole entre DHT11 et DHT22
4. Aucune modification de code n'est nécessaire pour passer de l'un à l'autre

## 🔍 Vérification du capteur utilisé

Pour vérifier quel capteur est compilé, regardez les logs de compilation :
- Les `build_flags` contiennent `-DPROFILE_PROD` → DHT22
- Sinon (`PROFILE_DEV` ou `PROFILE_TEST`) → DHT11

Ou consultez les logs série au démarrage :
```
[AirSensor] Capteur détecté et initialisé
```

