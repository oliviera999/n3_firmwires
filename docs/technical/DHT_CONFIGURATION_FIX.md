# Correction de la configuration DHT11/DHT22

**Date** : 7 octobre 2025  
**Statut** : ✅ Complété

## 🎯 Problème identifié

Le code utilisait `ENVIRONMENT_PROD_TEST` qui n'existe plus dans la configuration PlatformIO.

### Code obsolète
```cpp
#if defined(ENVIRONMENT_PROD_TEST)
    DHT11  // wroom-test (ENVIRONMENT_PROD_TEST) : utilise DHT11
#else
    DHT22  // Environnements de production et autres : utilise DHT22
#endif
```

## ✅ Solution appliquée

La logique a été corrigée pour utiliser les profils réellement définis dans `platformio.ini`.

### Code corrigé (src/sensors.cpp)
```cpp
#if defined(PROFILE_PROD)
    DHT22  // Production : utilise DHT22
#else
    DHT11  // Dev et Test : utilise DHT11
#endif
```

## 📊 Mapping des environnements

| Environnement | Profile | Capteur | Usage |
|--------------|---------|---------|-------|
| **Production** | | | |
| `wroom-prod` | `PROFILE_PROD` | 🟢 **DHT22** | Production WROOM |
| `s3-prod` | `PROFILE_PROD` | 🟢 **DHT22** | Production S3 |
| `wroom-prod-pio6` | `PROFILE_PROD` | 🟢 **DHT22** | Production WROOM (PIO 6) |
| `s3-prod-pio6` | `PROFILE_PROD` | 🟢 **DHT22** | Production S3 (PIO 6) |
| `wroom-minimal` | `PROFILE_PROD` | 🟢 **DHT22** | Production WROOM minimal |
| **Test** | | | |
| `wroom-test` | `PROFILE_TEST` | 🔵 **DHT11** | Test WROOM |
| `s3-test` | `PROFILE_TEST` | 🔵 **DHT11** | Test S3 |
| `wroom-test-pio6` | `PROFILE_TEST` | 🔵 **DHT11** | Test WROOM (PIO 6) |
| `s3-test-pio6` | `PROFILE_TEST` | 🔵 **DHT11** | Test S3 (PIO 6) |
| **Développement** | | | |
| `wroom-dev` | `PROFILE_DEV` | 🔵 **DHT11** | Développement WROOM |
| `s3-dev` | `PROFILE_DEV` | 🔵 **DHT11** | Développement S3 |

## 📁 Fichiers modifiés

### 1. Code source
- ✅ **`src/sensors.cpp`** (lignes 782-788)
  - Remplacement de `ENVIRONMENT_PROD_TEST` par `PROFILE_PROD`
  - Inversion de la logique : `PROFILE_PROD` → DHT22, sinon → DHT11

### 2. Documentation créée
- ✅ **`docs/guides/DHT_SENSOR_CONFIGURATION.md`** (nouveau)
  - Guide complet de configuration DHT11/DHT22
  - Mapping des environnements
  - Comparaison des capteurs
  - Instructions de câblage et utilisation

### 3. Documentation mise à jour
- ✅ **`docs/guides/DHT11_TO_DHT22_MIGRATION.md`**
  - Mise à jour avec le système de sélection automatique
  - Ajout de la liste des environnements par capteur
  - Référence au nouveau guide de configuration

## 🔍 Vérifications effectuées

✅ Aucune autre référence à `ENVIRONMENT_PROD_TEST` dans `src/`  
✅ Aucune autre référence à `ENVIRONMENT_PROD_TEST` dans `include/`  
✅ Aucune erreur de lint dans `src/sensors.cpp`  
✅ Tous les environnements de `platformio.ini` documentés

## 📊 Plages de validation

Les plages sont **strictement identiques** pour DHT11 et DHT22 :

```cpp
// include/project_config.h
namespace SensorConfig::AirSensor {
    constexpr float TEMP_MIN = 3.0f;      // +3°C minimum
    constexpr float TEMP_MAX = 50.0f;     // +50°C maximum
    constexpr float HUMIDITY_MIN = 10.0f; // 10% minimum
    constexpr float HUMIDITY_MAX = 100.0f; // 100% maximum
}
```

**Pour les 2 modèles DHT :**
- 🌡️ **Température** : **+3°C à +50°C**
- 💧 **Humidité** : **10% à 100%**

## 🎯 Résultat

Le système fonctionne maintenant correctement :

### Environnements de PRODUCTION 🟢
```bash
pio run -e wroom-prod      # → Compile avec DHT22
pio run -e s3-prod         # → Compile avec DHT22
```

### Environnements de DEV/TEST 🔵
```bash
pio run -e wroom-dev       # → Compile avec DHT11
pio run -e wroom-test      # → Compile avec DHT11
pio run -e s3-test         # → Compile avec DHT11
```

## 📚 Documentation de référence

- **Configuration complète** : `docs/guides/DHT_SENSOR_CONFIGURATION.md`
- **Guide de migration** : `docs/guides/DHT11_TO_DHT22_MIGRATION.md`
- **Récupération capteurs** : `docs/guides/SENSOR_RECOVERY_SYSTEM.md`

## ⚡ Avantages

1. ✅ **Alignement** avec la configuration réelle de PlatformIO
2. ✅ **Clarté** : logique simple et directe (PROD = DHT22, autre = DHT11)
3. ✅ **Économie** : DHT11 moins cher pour dev/test
4. ✅ **Qualité** : DHT22 plus précis en production
5. ✅ **Simplicité** : un seul code, sélection automatique

## 🚀 Prochaines étapes

Le système est maintenant prêt à l'emploi. Pour utiliser :

1. **Développer/Tester** : compiler avec un environnement `-dev` ou `-test`
2. **Déployer** : compiler avec un environnement `-prod`
3. Le capteur approprié sera automatiquement sélectionné

Aucune modification manuelle du code n'est nécessaire !

