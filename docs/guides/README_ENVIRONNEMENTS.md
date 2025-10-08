# Configuration des Environnements Test et Production

Ce projet permet de switcher facilement entre un environnement de test et un environnement de production selon le type de compilation.

## 🏗️ Environnements Disponibles

### Environnements de Test
- **`esp32-s3-test`** : ESP32-S3 en mode test
- **`esp32dev-test`** : ESP32-WROOM en mode test

### Environnements de Production
- **`esp32-s3-prod`** : ESP32-S3 en mode production
- **`esp32dev-prod`** : ESP32-WROOM en mode production

### Environnements de Compatibilité
- **`esp32-s3-devkitc-1-n16r8v`** : Configuration originale ESP32-S3
- **`esp32dev`** : Configuration originale ESP32-WROOM

## 🚀 Utilisation

### Compilation pour l'environnement de TEST

```bash
# Pour ESP32-S3 en mode test
pio run -e esp32-s3-test

# Pour ESP32-WROOM en mode test
pio run -e esp32dev-test
```

### Compilation pour l'environnement de PRODUCTION

```bash
# Pour ESP32-S3 en mode production
pio run -e esp32-s3-prod

# Pour ESP32-WROOM en mode production
pio run -e esp32dev-prod
```

### Upload vers la carte

```bash
# Upload en mode test
pio run -e esp32-s3-test -t upload

# Upload en mode production
pio run -e esp32-s3-prod -t upload
```

## ⚙️ Différences entre les Environnements

### Mode TEST
- **Debug activé** : `DEBUG_MODE=1`
- **Logging verbeux** : `VERBOSE_LOGGING=1`
- **Niveau de log** : `LOG_DEBUG`
- **Serveur de test** : `https://test-api.votre-domaine.com:8080`
- **Endpoints** : `/api/test`, `/ws/test`
- **MQTT** : `test-mqtt.votre-domaine.com:1883`
- **Topics MQTT** : `test/device/`
- **Type de build** : `debug`

### Mode PRODUCTION
- **Debug désactivé** : `DEBUG_MODE=0`
- **Logging minimal** : `VERBOSE_LOGGING=0`
- **Niveau de log** : `LOG_WARN` ou `LOG_ERROR`
- **Serveur de production** : `https://api.votre-domaine.com:443`
- **Endpoints** : `/api/v1`, `/ws`
- **MQTT** : `mqtt.votre-domaine.com:8883`
- **Topics MQTT** : `prod/device/`
- **Type de build** : `release`

## 🔧 Configuration

### Variables d'Environnement Définies

#### Mode TEST
```cpp
#define ENVIRONMENT TEST
#define TEST_SERVER_URL "https://test-api.votre-domaine.com"
#define TEST_SERVER_PORT 8080
#define DEBUG_MODE 1
#define VERBOSE_LOGGING 1
```

#### Mode PRODUCTION
```cpp
#define ENVIRONMENT PRODUCTION
#define PROD_SERVER_URL "https://api.votre-domaine.com"
#define PROD_SERVER_PORT 443
#define DEBUG_MODE 0
#define VERBOSE_LOGGING 0
```

### Utilisation dans le Code

```cpp
#include "config.h"

void setup() {
    // Affichage des informations d'environnement
    DEBUG_PRINTF("Environnement: %s\n", getEnvironmentName());
    DEBUG_PRINTF("Serveur: %s\n", getServerUrl().c_str());
    
    // Utilisation conditionnelle selon l'environnement
    #ifdef ENVIRONMENT_TEST
        // Code spécifique au test
        DEBUG_PRINTLN("Mode test activé");
    #elif defined(ENVIRONMENT_PRODUCTION)
        // Code spécifique à la production
        DEBUG_PRINTLN("Mode production activé");
    #endif
}
```

## 📝 Macros de Debug

### Macros Disponibles
- `DEBUG_PRINT(x)` : Affichage en mode debug
- `DEBUG_PRINTLN(x)` : Affichage avec retour à la ligne en mode debug
- `DEBUG_PRINTF(fmt, ...)` : Affichage formaté en mode debug
- `VERBOSE_PRINT(x)` : Affichage en mode verbose
- `VERBOSE_PRINTLN(x)` : Affichage avec retour à la ligne en mode verbose
- `VERBOSE_PRINTF(fmt, ...)` : Affichage formaté en mode verbose

### Exemple d'Utilisation
```cpp
void sendData() {
    DEBUG_PRINTLN("Envoi de données...");
    VERBOSE_PRINTF("Données: %s\n", data.c_str());
    
    // En mode production, seuls les messages DEBUG_PRINT* s'affichent
    // En mode test, tous les messages s'affichent
}
```

## 🔄 Personnalisation

### Modification des URLs de Serveur

Éditez le fichier `platformio.ini` et modifiez les variables :

```ini
[env:esp32-s3-test]
build_flags = 
    # ... autres flags ...
    -DTEST_SERVER_URL="https://votre-serveur-test.com"
    -DTEST_SERVER_PORT=8080

[env:esp32-s3-prod]
build_flags = 
    # ... autres flags ...
    -DPROD_SERVER_URL="https://votre-serveur-prod.com"
    -DPROD_SERVER_PORT=443
```

### Ajout de Nouvelles Variables

1. Ajoutez les variables dans `platformio.ini` :
```ini
build_flags = 
    # ... autres flags ...
    -DMA_VARIABLE_TEST="valeur_test"
    -DMA_VARIABLE_PROD="valeur_prod"
```

2. Utilisez-les dans `config.h` :
```cpp
#ifdef ENVIRONMENT_TEST
    #define MA_VARIABLE MA_VARIABLE_TEST
#elif defined(ENVIRONMENT_PRODUCTION)
    #define MA_VARIABLE MA_VARIABLE_PROD
#endif
```

## 🚨 Bonnes Pratiques

1. **Toujours utiliser les macros de debug** au lieu de `Serial.print()` directement
2. **Tester en mode test** avant de déployer en production
3. **Vérifier les URLs** et ports avant chaque déploiement
4. **Utiliser des certificats SSL** en production
5. **Sauvegarder les configurations** sensibles dans des variables d'environnement

## 🔍 Vérification

Pour vérifier l'environnement actuel, regardez les logs au démarrage :

```
=== CONFIGURATION ENVIRONNEMENT ===
Environnement: TEST
Mode Debug: ACTIVÉ
Serveur URL: https://test-api.votre-domaine.com:8080
API Endpoint: https://test-api.votre-domaine.com:8080/api/test
==================================
```

## 📋 Checklist de Déploiement

### Avant le Déploiement en Production
- [ ] Tester en mode test
- [ ] Vérifier les URLs de production
- [ ] Vérifier les certificats SSL
- [ ] Désactiver le mode debug
- [ ] Vérifier les timeouts et intervalles
- [ ] Tester la reconnexion WiFi
- [ ] Vérifier la gestion des erreurs 