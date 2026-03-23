# 🐟 FFP5CS ESP32 Aquaponie Controller

**Système de contrôle automatisé pour aquaponie avec ESP32**

[![Version](https://img.shields.io/badge/version-13.26-blue.svg)](VERSION.md)
[![ESP32](https://img.shields.io/badge/ESP32-WROOM%20%7C%20S3-green.svg)](platformio.ini)
[![Framework](https://img.shields.io/badge/framework-Arduino-orange.svg)](platformio.ini)
[![License](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)

---

## 🚀 Démarrage rapide

### 📖 Documentation
👉 **[📚 Documentation technique](docs/README.md)** — structure, config, rapports

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
├── data/                  # Interface web (index.html, pages/, assets/, sw.js)
├── docs/                  # Documentation
│   ├── README.md         # Vue d'ensemble
│   ├── technical/        # Références techniques (seuils ESP32 vs serveur)
│   ├── reports/          # Rapports (conformité, NVS, corrections, etc.)
│   └── references        # Référence emails (32 types)
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

### 🔄 Améliorations Récentes (v13.26)
- [x] Audit mémoire wroom-beta (bornage sécurité tableaux WiFi)
- [x] Suppression des `String` temporaires dans les scans WiFi (API ESP-IDF)
- [x] Helper SSID AP sans `String` (`esp_wifi_get_config`)
- [x] Vérification FreeRTOS stack depth sur toolchain actuelle

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

### 📚 Documentation
- **[Documentation](docs/README.md)** — structure, compilation, principes
- **[Seuils ESP32 / serveur](docs/technical/SEUILS_SERVEUR_ESP32.md)** — différences volontaires
- **[Rapports](docs/reports/)** — conformité, NVS, corrections, origine problèmes

---

## 🐛 Résolution de problèmes

### 🔍 Diagnostics courants
1. **Problème WiFi** → Logs série 115200 baud, vérifier RSSI et reconnexion
2. **Erreurs mémoire** → [Rapport origine problèmes](docs/reports/monitoring/reports/ANALYSE_ORIGINE_PROBLEMES_CRITIQUES.md) (DHT22, heap, watchdog)
3. **OTA échoué** → Vérifier WiFi, quota firmware, `pio run -e wroom-test -t upload`
4. **Capteurs instables** → [Résumé corrections](docs/reports/corrections/RESUME_CORRECTIONS_APPLIQUEES.md) (DHT22, queue, timeouts)

### 📊 Monitoring en temps réel
- **Interface web**: `http://ffp3-XXXX.local`
- **Logs série**: 115200 baud
- **WebSocket**: Mise à jour temps réel

---

## 📈 Historique des versions

### v11.190 (2026-02) - Points à surveiller
- HTTP/TLS 5s, NVS remove idempotent, servo pin valide

### v11.124 (2026-01-10) - Audit général - Corrections prioritaires
- ✅ **Correction incohérence version** : Unifié config.h et app.cpp sur v11.124
- ✅ **Optimisation boot** : Delay debug conditionnel (1s en debug, 0s en production)
- ✅ **Code propre** : Utilisation de `ProjectConfig::VERSION` au lieu de hardcode

### v11.120 (2026-01-10) - Stabilisation watchdog & stack automation
- ✅ **Stack overflow corrigé** : `automationTask` stack augmenté 4096 → 8192 bytes
- ✅ **Watchdog timeout corrigé** : IDLE tasks non surveillées (évite faux positifs)
- ✅ **Core dump désactivé** : Économie flash en production

### v11.119 (2026-01-07) - Refactorisation Majeure
- ✅ **Nettoyage Code Mort** : Suppression `JsonPool`, `PSRAMOptimizer`.
- ✅ **Configuration Unifiée** : Migration complète vers `include/config.h`.
- ✅ **Modernisation JSON** : Passage à `ArduinoJson 7` (`JsonDocument`).
- ✅ **Monitoring Simplifié** : Réduction drastique de `TimeDriftMonitor` et `TaskMonitor`.

[Voir toutes les versions](VERSION.md)

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
- **[Documentation](docs/README.md)** — vue d'ensemble, structure, rapports
- **[Technique](docs/technical/)** — seuils ESP32 vs serveur
- **[Rapports](docs/reports/)** — conformité, NVS, corrections, monitoring

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

*Dernière mise à jour: 2026-03-23 - Version 13.26*
