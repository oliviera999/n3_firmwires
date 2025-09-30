# Configuration de l'environnement esp32dev-prod-test

## ✅ Environnement créé avec succès

L'environnement `esp32dev-prod-test` a été créé selon vos spécifications avec les modifications suivantes :

### 🔧 **Configuration de base**
- **Nom** : `esp32dev-prod-test`
- **Base** : Identique à `esp32dev-prod` (mêmes optimisations de production)
- **Ajouté** à la liste des environnements par défaut dans `platformio.ini`

### 🌡️ **Capteur DHT11**
- **Modification** : `src/sensors.cpp` - Le constructeur `AirSensor` utilise maintenant `DHT11` au lieu de `DHT22` pour l'environnement `esp32dev-prod-test`
- **Plages de validation** : Ajoutées dans `include/constants.h` avec des constantes spécifiques :
  - **DHT11** : 5-60°C, 5-100% humidité
  - **DHT22** : -40-80°C, 0-100% humidité
- **Fichiers mis à jour** : `system_sensors.cpp`, `automatism.cpp`, `web_client.cpp` pour utiliser les nouvelles constantes

### 🌐 **URLs configurées**
- **Modification** : `include/constants.h` - Système de sélection conditionnelle des URLs
- **URLs équivalentes** (pour `esp32dev-prod-test`) :
  - `post-ffp3-data2.php`
  - `ffp3-outputs-action2.php`
  - `metadata2.json`
- **URLs collées** (pour `esp32dev-prod` et autres) :
  - `post-ffp3-data.php`
  - `ffp3-outputs-action.php`
  - `metadata.json`

### ✅ **Compilation réussie**
- Tous les fichiers compilent sans erreur
- Le flag `ENVIRONMENT_PROD_TEST` est correctement défini
- Les includes nécessaires ont été ajoutés

## 🎯 **Utilisation**

Pour utiliser le nouvel environnement :
```bash
pio run -e esp32dev-prod-test
pio upload -e esp32dev-prod-test
```

## 📋 **Résumé de la configuration**

| Environnement | Capteur DHT | URLs | Plages de validation |
|---------------|-------------|------|---------------------|
| `esp32dev-prod-test` | **DHT11** | **Équivalentes** (data2.php, action2.php, metadata2.json) | 5-60°C, 5-100% |
| `esp32dev-prod` | **DHT22** | **Collées** (data.php, action.php, metadata.json) | -40-80°C, 0-100% |

L'environnement `esp32dev-prod-test` est maintenant prêt et fonctionnel avec :
- **DHT11** au lieu de DHT22
- **URLs équivalentes** au lieu des URLs collées
- **Mêmes optimisations** que l'environnement de production
