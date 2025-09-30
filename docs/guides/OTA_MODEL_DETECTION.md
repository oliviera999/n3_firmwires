# Système OTA avec Détection Automatique du Modèle ESP32

## Vue d'ensemble

Ce système remplace l'ancien système ElegantOTA par un gestionnaire OTA personnalisé qui détecte automatiquement le modèle d'ESP32 et configure les dossiers OTA appropriés.

## Fonctionnalités

### 🔍 Détection Automatique du Modèle
- **ESP32-S3** : Détecté via `BOARD_S3` ou `CHIP_ESP32S3`
- **ESP32-WROOM** : Détecté via `BOARD_ESP32` ou `CHIP_ESP32`
- **ESP32-C3** : Détecté via `CHIP_ESP32C3`
- **Autres modèles** : Détection automatique via `esp_chip_info()`

### 📁 Gestion des Dossiers Spécifiques
Chaque modèle utilise son propre dossier OTA :
```
/ota/
├── esp32-s3/
│   ├── firmware.bin
│   ├── version.json
│   └── manifest.json
├── esp32-wroom/
│   ├── firmware.bin
│   ├── version.json
│   └── manifest.json
└── esp32-c3/
    ├── firmware.bin
    ├── version.json
    └── manifest.json
```

### 🌐 Endpoints OTA
- `/ota/status` : Informations sur le modèle et la configuration OTA
- `/ota/check` : Vérification de mise à jour disponible
- `/ota/update` : Déclenchement d'une mise à jour OTA
- `/update` : Endpoint de compatibilité (redirection vers info)

## Configuration

### 1. Fichier de Configuration (`include/ota_config.h`)

```cpp
namespace OTAConfig {
    // URLs de base pour chaque modèle
    const char* ESP32_S3_URL = "https://votre-serveur.com/ota/esp32-s3/";
    const char* ESP32_WROOM_URL = "https://votre-serveur.com/ota/esp32-wroom/";
    const char* ESP32_C3_URL = "https://votre-serveur.com/ota/esp32-c3/";
    
    // Dossiers spécifiques par modèle
    const char* ESP32_S3_FOLDER = "/esp32-s3/";
    const char* ESP32_WROOM_FOLDER = "/esp32-wroom/";
    const char* ESP32_C3_FOLDER = "/esp32-c3/";
    
    // Configuration technique
    const int HTTP_TIMEOUT = 30000;
    const size_t MAX_FIRMWARE_SIZE = 16777216; // 16MB
}
```

### 2. Configuration PlatformIO

Les flags de compilation définissent le modèle :
```ini
[env:esp32-s3-devkitc-1-n16r8v]
build_flags = -DBOARD_S3

[env:esp32dev]
build_flags = -DBOARD_ESP32
```

## Utilisation

### Initialisation Automatique

Le système s'initialise automatiquement dans `WebServerManager::begin()` :

```cpp
// Initialisation du gestionnaire OTA avec détection automatique du modèle
_otaManager = new OTAManager(config);
if (_otaManager->begin()) {
    Serial.println("[WebServer] Gestionnaire OTA initialisé avec succès");
    
    // Configuration des callbacks OTA
    _otaManager->onStart([]() {
        Serial.println("[OTAManager] Début de la mise à jour");
    });
    
    _otaManager->onEnd([](bool success) {
        if (success) {
            config.setOtaUpdateFlag(true);
        }
    });
}
```

### API REST

#### Vérification du Statut
```bash
GET /ota/status
```
Réponse :
```json
{
    "model": "ESP32-S3",
    "folder": "/esp32-s3/",
    "baseUrl": "https://votre-serveur.com/ota/esp32-s3/",
    "available": true
}
```

#### Vérification de Mise à Jour
```bash
GET /ota/check
```
Réponse :
```json
{
    "update": true
}
```

#### Déclenchement de Mise à Jour
```bash
POST /ota/update
Content-Type: application/x-www-form-urlencoded

server_url=https://serveur-alternatif.com/ota/esp32-s3/
```
Réponse :
```json
{
    "success": true
}
```

## Structure du Serveur OTA

### Organisation Recommandée

```
serveur-web/
├── ota/
│   ├── esp32-s3/
│   │   ├── firmware.bin          # Firmware compilé pour ESP32-S3
│   │   ├── version.json          # Informations de version
│   │   └── manifest.json         # Manifeste des fichiers
│   ├── esp32-wroom/
│   │   ├── firmware.bin          # Firmware compilé pour ESP32-WROOM
│   │   ├── version.json
│   │   └── manifest.json
│   └── esp32-c3/
│       ├── firmware.bin          # Firmware compilé pour ESP32-C3
│       ├── version.json
│       └── manifest.json
```

### Format des Fichiers

#### version.json
```json
{
    "version": "1.2.3",
    "model": "ESP32-S3",
    "build_date": "2024-01-15T10:30:00Z",
    "size": 1048576,
    "checksum": "sha256:abc123...",
    "description": "Correction des bugs de chauffage"
}
```

#### manifest.json
```json
{
    "files": ["firmware.bin"],
    "total_size": 1048576,
    "required_space": 2097152,
    "compatibility": ["ESP32-S3"],
    "dependencies": []
}
```

## Sécurité

### Vérifications Intégrées
- **Compatibilité du modèle** : Vérification que le firmware correspond au modèle détecté
- **Taille du firmware** : Contrôle de la taille maximale autorisée
- **Checksum** : Vérification de l'intégrité du firmware (optionnel)
- **Timeout** : Protection contre les téléchargements infinis

### Recommandations
1. Utilisez HTTPS pour les serveurs OTA
2. Implémentez une authentification si nécessaire
3. Validez les checksums côté serveur et client
4. Limitez l'accès aux dossiers OTA

## Migration depuis ElegantOTA

### Changements Automatiques
- ✅ Remplacement automatique d'ElegantOTA
- ✅ Conservation des callbacks existants
- ✅ Endpoint `/update` maintenu pour compatibilité
- ✅ Gestion des emails de notification préservée

### Nouveautés
- 🔍 Détection automatique du modèle
- 📁 Dossiers spécifiques par modèle
- 🌐 API REST complète
- ⚙️ Configuration centralisée
- 🔒 Vérifications de sécurité renforcées

## Tests

### Script de Test Automatique
```bash
python test_ota_model_detection.py
```

Ce script vérifie :
- ✅ Détection du modèle lors de la compilation
- ✅ Configuration OTA pour chaque modèle
- ✅ Endpoints web fonctionnels
- ✅ Structure de serveur de test

### Tests Manuels

1. **Test de Détection** :
   ```bash
   pio run -e esp32-s3-devkitc-1-n16r8v -t upload
   # Vérifiez les logs : "Modèle détecté: ESP32-S3"
   ```

2. **Test des Endpoints** :
   ```bash
   curl http://ESP32_IP/ota/status
   curl http://ESP32_IP/ota/check
   ```

3. **Test de Mise à Jour** :
   ```bash
   curl -X POST http://ESP32_IP/ota/update
   ```

## Dépannage

### Problèmes Courants

#### 1. Modèle Non Détecté
**Symptôme** : "Modèle détecté: ESP32-UNKNOWN"
**Solution** : Vérifiez les flags de compilation dans `platformio.ini`

#### 2. Erreur de Connexion OTA
**Symptôme** : "Erreur HTTP: 404"
**Solution** : Vérifiez les URLs dans `include/ota_config.h`

#### 3. Firmware Incompatible
**Symptôme** : "Firmware incompatible avec le modèle"
**Solution** : Compilez le firmware pour le bon modèle

### Logs de Débogage

Activez les logs détaillés :
```cpp
// Dans platformio.ini
build_flags = -DLOG_LEVEL=LOG_DEBUG
```

## Support

Pour toute question ou problème :
1. Vérifiez les logs de compilation
2. Testez avec le script automatique
3. Consultez la documentation des endpoints
4. Vérifiez la configuration du serveur OTA 