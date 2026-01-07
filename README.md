# 🐟 FFP5CS ESP32 Aquaponie Controller

**Système de contrôle automatisé pour aquaponie avec ESP32**

[![Version](https://img.shields.io/badge/version-11.119-blue.svg)](VERSION.md)
[![ESP32](https://img.shields.io/badge/ESP32-WROOM%20%7C%20S3-green.svg)](platformio.ini)
[![Framework](https://img.shields.io/badge/framework-Arduino-orange.svg)](platformio.ini)
[![License](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)

---

## 🚀 Démarrage rapide

### 📖 Documentation organisée
👉 **[📚 Documentation complète](docs/README.md)** - 136 documents structurés

### ⚡ Installation rapide
1. **Cloner le projet**
   ```bash
   git clone <repository-url>
   cd ffp5cs
   ```

2. **Configuration PlatformIO**
   ```bash
   pio run -e wroom-test  # ESP32-WROOM (recommandé)
   # ou
   pio run -e s3-test     # ESP32-S3 (8MB)
   ```

3. **Flash initial**
   ```bash
   pio run -e wroom-test -t upload
   ```

4. **Monitoring**
   ```bash
   pio device monitor -e wroom-test
   ```

---

## 🎯 Fonctionnalités principales

### 🌊 Gestion aquaponie
- **Nourrissage automatique** programmable (matin/midi/soir)
- **Contrôle niveau d'eau** (aquarium, potager, réserve)
- **Remplissage automatique** depuis la réserve
- **Détection marées** pour mise en veille

### 🌡️ Monitoring environnement
- **Température air/eau** (DHT22, DS18B20)
- **Humidité** et **luminosité**
- **Alertes email** configurables
- **Interface web** temps réel

### ⚡ Optimisations système
- **Mode veille** intelligent (Light Sleep)
- **Reconnexion WiFi** automatique
- **Mise à jour OTA** (firmware + filesystem)
- **Monitoring mémoire** et performance

---

## 🏗️ Architecture

### 📁 Structure du projet
```
ffp5cs/
├── src/                    # Code source ESP32
│   ├── automatism/         # Modules spécialisés (feeding, sleep, etc.)
│   ├── app.cpp            # Point d'entrée principal (setup/loop)
│   ├── sensors.cpp        # Gestion capteurs bas niveau
│   ├── actuators.cpp      # Contrôle actionneurs bas niveau
│   └── web_server.cpp     # Interface web (AsyncWebServer)
├── include/                # En-têtes (Headers)
│   ├── config.h           # Configuration unifiée du projet
│   ├── automatism.h       # API de l'automate
│   └── web_server.h       # API du serveur web
├── data/www/              # Interface web (SPA, Assets)
├── docs/                  # Documentation organisée
│   ├── guides/           # Guides d'utilisation
│   ├── reports/          # Rapports et analyses
│   ├── technical/        # Corrections techniques
│   └── archives/         # Documents obsolètes
└── platformio.ini        # Configuration build
```

### 🔧 Environnements de build
- **`wroom-prod`** - Production ESP32-WROOM (optimisé)
- **`wroom-test`** - Test ESP32-WROOM (debug activé, logs série)
- **`s3-prod`** - Production ESP32-S3 (8MB)
- **`s3-test`** - Test ESP32-S3 (debug activé)

---

## 📊 État du projet

### ✅ Fonctionnalités stables
- [x] Nourrissage automatique
- [x] Monitoring capteurs
- [x] Interface web responsive
- [x] Mise à jour OTA
- [x] Mode veille optimisé
- [x] Reconnexion WiFi

### 🔄 Améliorations Récentes (v11.119)
- [x] Simplification capteurs (suppression "Optimizers" et méthodes obsolètes)
- [x] Configuration unifiée (`include/config.h`)
- [x] Modernisation JSON (`ArduinoJson 7`)
- [x] Monitoring allégé (`TimeDriftMonitor`, `TaskMonitor`)

### 📈 Métriques
- **Uptime**: 24/7 stable
- **Mémoire**: <80% utilisation
- **Latence WebSocket**: <100ms
- **Temps de boot**: ~15 secondes

---

## 🛠️ Développement

### 📋 Prérequis
- **PlatformIO** v6.0+
- **ESP32-WROOM-32** ou **ESP32-S3**
- **Arduino Framework**

### 🔨 Commandes utiles
```bash
# Build et upload
pio run -e wroom-test -t upload

# Monitoring série
pio device monitor -e wroom-test

# Nettoyage
pio run -e wroom-test -t clean

# Upload filesystem
pio run -e wroom-test -t uploadfs
```

### 📚 Guides de développement
- **[Configuration IDE](docs/guides/CURSOR_IDE_GUIDE.md)**
- **[Upload et OTA](docs/guides/UPLOAD_INSTRUCTIONS.md)**
- **[Monitoring système](docs/guides/SURVEILLANCE_MEMOIRE_GUIDE.md)**
- **[Gestionnaire WiFi](docs/guides/GESTIONNAIRE_WIFI_GUIDE.md)**

---

## 🐛 Résolution de problèmes

### 🔍 Diagnostics courants
1. **Problème WiFi** → [Gestionnaire WiFi](docs/guides/GESTIONNAIRE_WIFI_GUIDE.md)
2. **Erreurs mémoire** → [Surveillance mémoire](docs/guides/SURVEILLANCE_MEMOIRE_GUIDE.md)
3. **OTA échoué** → [Guide OTA](docs/guides/OTA_DIRECT_UPDATE_GUIDE.md)
4. **Capteurs instables** → [Corrections récentes](docs/technical/CORRECTIONS_NON_BLOQUANTES_V11.50.md)

### 📊 Monitoring en temps réel
- **Interface web**: `http://ffp3-XXXX.local`
- **Logs série**: 115200 baud
- **WebSocket**: Mise à jour temps réel

---

## 📈 Historique des versions

### v11.119 (2026-01-07) - Refactorisation Majeure
- ✅ **Nettoyage Code Mort** : Suppression `JsonPool`, `PSRAMOptimizer`.
- ✅ **Configuration Unifiée** : Migration complète vers `include/config.h`.
- ✅ **Modernisation JSON** : Passage à `ArduinoJson 7` (`JsonDocument`).
- ✅ **Monitoring Simplifié** : Réduction drastique de `TimeDriftMonitor` et `TaskMonitor`.

### v11.59 (2025-10-16) - Phase 1 Simplification
- ✅ **Consolidation platformio.ini** (7 → 4 environnements)
- ✅ **Organisation documentation** (136 fichiers structurés)

[Voir toutes les versions](docs/guides/VERSION.md)

---

## 🤝 Contribution

### 📝 Standards de développement
- **Version**: Incrémenter à chaque modification
- **Monitoring**: 90s après chaque déploiement
- **Tests**: Validation sur hardware réel
- **Documentation**: Mise à jour obligatoire

### 🔄 Workflow
1. **Modifier le code**
2. **Incrémenter la version** (config.h)
3. **Tester sur ESP32**
4. **Monitoring 90s + analyse logs**
5. **Documenter les changements**
6. **Commit avec numéro de version**

---

## 📞 Support

### 📚 Documentation
- **[Documentation complète](docs/README.md)** - 136 documents
- **[Guides d'utilisation](docs/guides/)** - 27 guides
- **[Résolution problèmes](docs/technical/)** - 31 solutions

### 🔍 Debugging
- **Logs série**: 115200 baud
- **Interface web**: Diagnostic en temps réel
- **WebSocket**: Monitoring live

---

## 📄 Licence

MIT License - Voir [LICENSE](LICENSE) pour plus de détails.

---

## 🙏 Remerciements

- **ESP32 Community** pour le support technique
- **PlatformIO** pour l'environnement de développement
- **Arduino Framework** pour la simplicité d'utilisation

---

*Dernière mise à jour: 2026-01-07 - Version 11.119*
