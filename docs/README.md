# Documentation ESP32 FFP5CS v11.03

## 📖 Navigation Rapide

### Pour Commencer
- **Getting Started** - Installation et premier démarrage du système
- **Architecture Overview** - Vue d'ensemble technique du projet
- **Configuration Guide** - Paramétrage et personnalisation

### 🏗️ Architecture Technique

#### Systèmes Principaux
- **FreeRTOS Tasks** - 4 tâches principales (sensor, web, automation, display)
  - Priorités et distribution sur 2 cores
  - Stack sizing et watchdog management
  
- **Memory Management** - Stratégie mémoire
  - Pools (email, JSON)
  - Caches (sensors, rules, pump stats)
  - PSRAM utilization (ESP32-S3)

- **Network Stack** - Communication réseau
  - WiFi multi-SSID avec reconnexion automatique
  - WebSocket temps réel + fallback polling
  - OTA updates (firmware + filesystem)
  - Serveur distant sync

- **Sensor System** - Acquisition données
  - DHT22 (température/humidité air)
  - DS18B20 (température eau)
  - HC-SR04 (niveaux d'eau: aquarium, potager, réserve)
  - Validation multi-niveaux et filtrage médiane

### 🛠️ Guides Pratiques

#### Configuration
- **WiFi Setup** - Configuration multi-réseaux dans `secrets.h`
- **Email Notifications** - SMTP Gmail, digest périodique
- **Sleep Management** - Light sleep adaptatif, modem sleep
- **Sensor Calibration** - Calibration capteurs ultrasoniques

#### Développement
- **Build Environments** - 10 configurations (wroom/s3, prod/test/dev)
- **Flash & Upload** - Procédures PlatformIO
- **Monitoring 90s** - Procédure post-déploiement obligatoire
- **OTA Updates** - Mise à jour sans câble

#### Troubleshooting
- **Common Issues** - Problèmes fréquents et solutions
- **Log Analysis** - Interpréter les logs série
- **Crash Debugging** - Guru Meditation, panic, watchdog timeout
- **Network Issues** - WiFi reconnection, WebSocket timeout

### 🔧 Référence API

#### Endpoints HTTP
```
GET  /                  - Interface web principale (SPA)
GET  /api/status        - État système complet (JSON)
GET  /api/sensors       - Lecture capteurs
GET  /api/actuators     - État actionneurs
POST /api/feed          - Nourrissage manuel
POST /api/config        - Modification configuration
GET  /api/metrics       - Métriques performance
```

#### WebSocket Protocol
```javascript
// Connection
ws://[IP]:81/ws  ou  ws://[IP]:80/ws

// Messages
{type: 'subscribe'}        // S'abonner aux mises à jour
{type: 'sensors', data: {}} // Update capteurs
{type: 'actuators', data: {}} // Update actionneurs
```

#### Composants Principaux
- **Automatism** - Logique métier (3400+ lignes, candidat refactoring)
- **SystemSensors** - Orchestration capteurs
- **SystemActuators** - Contrôle pompes, relais, servo
- **PowerManager** - Sleep, watchdog, NTP
- **WebServerManager** - Serveur HTTP/WebSocket

### 📊 Diagnostics & Monitoring

#### Métriques Disponibles
- **Heap Memory** - Free heap, min heap, fragmentation
- **Task Stats** - Stack HWM par tâche FreeRTOS
- **Network** - RSSI WiFi, reconnections, requests OK/KO
- **Sensors** - Reads total, failed, cache hits/misses
- **Performance** - CPU usage, loop time, latency

#### Scripts de Monitoring
```powershell
# Monitoring 90 secondes (obligatoire après déploiement)
./tools/monitoring/monitor_wroom_test.ps1

# Diagnostic complet
./tools/monitoring/diagnose_wroom_test.ps1

# Analyse logs
./tools/monitoring/analyze_logs.ps1
```

### 📝 Historique & Changelog

#### Versions Récentes
- **v11.03** (2025-10-10) - Déplacement bouton email notifications
- **v11.02** (2025-10-10) - Correction finale timezone UTC+1
- **v11.01** (2025-10-10) - Simplification timezone avec configTime()
- **v11.00** (2025-10-10) - Fix absolu fuseau horaire (double sécurité)
- **v10.99** (2025-10-10) - Correction time_drift_monitor timezone
- **v10.95-98** (2025-10-10) - Série de fixes timezone OLED

Voir [VERSION.md](../VERSION.md) pour l'historique complet depuis v10.20.

#### Migrations
- **v10.20 → v11.x** - Phase 2/3 optimisations (caches, pools, PSRAM)
- **Config Migration** - project_config.h centralisé (1063 lignes)

### 📚 Documentation Détaillée

#### Guides Existants (Racine & docs/)
Le projet contient 80+ fichiers de documentation. Les principaux :

**Fixes & Corrections**:
- `*_FIX.md` - Corrections de bugs spécifiques
- `WIFI_RECONNECTION_FIX_V*.md` - Évolutions reconnexion WiFi
- `PANIC_*.md` - Résolution crashes et guru meditation

**Guides Techniques**:
- `*_GUIDE.md` - Guides d'utilisation et configuration
- `SLEEP_*.md` - Configuration et optimisation sommeil
- `OTA_*.md` - Mises à jour OTA (6 fichiers différents)

**Rapports d'Analyse**:
- `RAPPORT_*.md` - Rapports techniques détaillés
- `ANALYSE_*.md` - Analyses de conformité et performance
- `TEST_*.md` - Procédures et résultats de tests

**Note**: Une consolidation de la documentation est prévue (voir ANALYSE_EXHAUSTIVE_PROJET_ESP32_FFP5CS.md).

### 🔍 Structure du Projet

```
ffp5cs/
├── src/              # Sources C++ (29 fichiers)
│   ├── app.cpp                  # Point d'entrée, setup/loop
│   ├── automatism.cpp           # Logique métier (3400+ lignes)
│   ├── sensors.cpp              # Capteurs de base
│   ├── system_sensors.cpp       # Orchestration capteurs
│   ├── actuators.cpp            # Actionneurs
│   ├── system_actuators.cpp     # Orchestration actionneurs
│   ├── power.cpp                # Sleep, watchdog, NTP
│   ├── wifi_manager.cpp         # Gestion WiFi
│   ├── web_server.cpp           # Serveur HTTP/WebSocket
│   ├── web_client.cpp           # Client HTTP (serveur distant)
│   ├── mailer.cpp               # Envoi emails
│   ├── ota_manager.cpp          # Mises à jour OTA
│   └── ...                      # 17 autres fichiers
│
├── include/          # Headers (35 fichiers)
│   ├── project_config.h         # Configuration centralisée (1063 lignes)
│   ├── pins.h                   # Pinout ESP32-WROOM/S3
│   ├── secrets.h                # Credentials (non versionné)
│   └── ...
│
├── data/             # Interface web (LittleFS)
│   ├── index.html               # SPA principale
│   ├── pages/
│   │   ├── controles.html       # Contrôle manuel
│   │   ├── reglages.html        # Configuration
│   │   └── wifi.html            # Config WiFi
│   ├── shared/
│   │   ├── common.js            # Fonctions communes
│   │   ├── websocket.js         # Gestion WebSocket
│   │   └── common.css           # Styles
│   └── assets/                  # Bibliothèques (uPlot)
│
├── tools/            # Scripts utilitaires
│   ├── testing/      # Scripts de test PowerShell
│   ├── monitoring/   # Scripts diagnostic et monitoring
│   └── deployment/   # Scripts de déploiement
│
├── docs/             # Documentation (vous êtes ici)
│   ├── README.md                # Ce fichier
│   ├── guides/                  # Guides pratiques
│   ├── architecture/            # Documentation technique
│   ├── api/                     # Référence API
│   └── archive/                 # Docs obsolètes
│
├── unused/           # Code temporaire/expérimental
│   ├── automatism_optimized.cpp # Version optimisée (non utilisée)
│   ├── psram_usage_example.cpp  # Exemple PSRAM
│   └── ffp10.52/                # Archive version 10.52 complète
│
├── platformio.ini    # Configuration PlatformIO (10 environnements)
├── VERSION.md        # Changelog détaillé v10.20 → v11.03
└── README.md         # README principal du projet
```

### 🚀 Quick Start

#### Prérequis
- PlatformIO Core ou PlatformIO IDE (VS Code)
- ESP32-WROOM-32 (4MB Flash) ou ESP32-S3 (8MB Flash)
- Python 3.x (pour scripts de build)
- Câble USB pour flash initial

#### Installation Rapide

```bash
# 1. Cloner le projet
git clone <repo-url>
cd ffp5cs

# 2. Configurer credentials
cp include/secrets.h.example include/secrets.h
# Éditer secrets.h : WiFi SSID/passwords, email credentials

# 3. Choisir environnement et compiler
pio run -e wroom-test    # ESP32-WROOM mode test
# pio run -e s3-test     # ESP32-S3 mode test
# pio run -e wroom-prod  # ESP32-WROOM production

# 4. Uploader firmware + filesystem
pio run -e wroom-test -t upload
pio run -e wroom-test -t uploadfs

# 5. Monitor série (115200 baud)
pio device monitor

# 6. OBLIGATOIRE: Monitoring 90 secondes post-déploiement
# (voir procédure dans .cursorrules)
```

#### Premier Test

1. **Vérifier logs série** - Rechercher "WiFi connected", IP affichée
2. **Accéder interface web** - http://[IP-ESP32]
3. **Tester WebSocket** - Les valeurs doivent se mettre à jour en temps réel
4. **Test nourrissage** - Bouton "Nourrir" dans page Contrôles
5. **Vérifier OLED** - Écran doit afficher valeurs capteurs

### 🐛 Issues Connues

#### Problèmes en Cours
- **automatism.cpp trop large** (3421 lignes) - Refactoring prévu
- **Documentation fragmentée** (80+ .md) - Consolidation en cours
- **Timezone fixes multiples** (9 versions v10.95-11.03) - Stabilisé en v11.03

#### Limitations Actuelles
- **PSRAM** - Optimizer quasi-vide, gains non mesurés
- **Stack reduction** - 25% réduit sans benchmark (risque overflow)
- **Optimizations** - Caches sans métriques (prétention -70% non validée)

Voir [ANALYSE_EXHAUSTIVE_PROJET_ESP32_FFP5CS.md](../ANALYSE_EXHAUSTIVE_PROJET_ESP32_FFP5CS.md) pour analyse détaillée.

### 📞 Support & Contribution

#### Obtenir de l'Aide
- **Issues GitHub** - Reporter bugs et demandes features
- **Documentation** - Consulter guides et troubleshooting
- **Logs Série** - Toujours fournir logs complets (monitoring 90s)

#### Contribuer
1. Fork le projet
2. Créer une branche feature (`git checkout -b feature/ma-feature`)
3. **Incrémenter version** dans `project_config.h` (OBLIGATOIRE)
4. Commit avec message descriptif incluant version
5. **Monitoring 90s** après test (OBLIGATOIRE)
6. Push et créer Pull Request

#### Règles de Développement
Voir [.cursorrules](../.cursorrules) pour :
- Conventions de code (K&R, 2 espaces, 100 chars/ligne)
- Gestion mémoire (éviter String, préférer char[])
- Watchdog management (reset réguliers)
- Procédure monitoring post-déploiement

### 📈 Roadmap

#### Court Terme (v11.x)
- [ ] Refactoring automatism.cpp en modules (3-5 jours)
- [ ] Consolidation documentation (2-3 jours)
- [ ] Benchmark optimisations (valider -70%, -60%)
- [ ] Simplification project_config.h (1063 → 500 lignes)

#### Moyen Terme (v12.x)
- [ ] Métriques performance continues (/api/metrics)
- [ ] Tests automatisés (monitoring 90s scriptés)
- [ ] NVS encryption (sécurité credentials)
- [ ] Dashboard monitoring web

#### Long Terme
- [ ] CI/CD pipeline
- [ ] Multi-board support (ESP32-C3, ESP32-C6)
- [ ] LoRa/LoRaWAN integration
- [ ] Mobile app (Flutter/React Native)

Voir [ACTION_PLAN_IMMEDIAT.md](../ACTION_PLAN_IMMEDIAT.md) pour détails.

---

## 📄 Documents Importants

- **[VERSION.md](../VERSION.md)** - Changelog complet v10.20 → v11.03
- **[README.md](../README.md)** - README principal du projet
- **[.cursorrules](../.cursorrules)** - Règles de développement (IMPORTANT)
- **[ANALYSE_EXHAUSTIVE_PROJET_ESP32_FFP5CS.md](../ANALYSE_EXHAUSTIVE_PROJET_ESP32_FFP5CS.md)** - Analyse technique détaillée (1000+ lignes)
- **[RESUME_EXECUTIF_ANALYSE.md](../RESUME_EXECUTIF_ANALYSE.md)** - Résumé analyse (note 6.5/10)
- **[ACTION_PLAN_IMMEDIAT.md](../ACTION_PLAN_IMMEDIAT.md)** - Plan d'action 8 phases

---

**Dernière mise à jour**: 2025-10-10  
**Version système**: 11.03  
**Mainteneur**: Équipe FFP5CS

**Note**: Cette documentation est un point de départ. Consulter les fichiers .md spécifiques pour détails techniques approfondis.


