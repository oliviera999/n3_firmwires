# PLAN D'ACTION IMMÉDIAT - ESP32 FFP5CS v11.03

> ℹ️ **Mise à jour (nov. 2025)** : Les mentions aux namespaces `CompatibilityAliases` / `CompatibilityUtils` appartiennent à l’ancienne architecture. Utiliser désormais directement les namespaces natifs (`ProjectConfig`, `EmailConfig`, `ActuatorConfig`, etc.).

**Date**: 2025-10-10  
**Objectif**: Actions concrètes et priorisées pour améliorer le projet  
**Durée estimée PHASE 1**: 1 heure (quick wins)

---

## ⚡ QUICK WINS - À FAIRE AUJOURD'HUI (1 heure)

### ✅ Checklist Phase 1: Nettoyage Critique

#### 1. Nettoyage filesystem (2 minutes)

```powershell
# NOTE: Le dossier unused/ est CONSERVÉ (contient code de référence)

# Supprimer logs temporaires
Remove-Item "*.log" -Force -ErrorAction SilentlyContinue
Remove-Item "*.log.err" -Force -ErrorAction SilentlyContinue
Remove-Item "monitor_*.log" -Force -ErrorAction SilentlyContinue

# Supprimer zips temporaires
Remove-Item "ffp3.zip" -Force -ErrorAction SilentlyContinue
Remove-Item "ffp3 (2).zip" -Force -ErrorAction SilentlyContinue

# Vérifier état
git status
```

**Gain**: Repo propre (logs et zips temporaires supprimés)

---

#### 2. Correction bug web_server.cpp (2 minutes)

**Fichier**: `src/web_server.cpp`  
**Lignes**: 33-38

**AVANT**:
```cpp
WebServerManager::WebServerManager(SystemSensors& sensors, SystemActuators& acts)
    : _sensors(sensors), _acts(acts), _diag(nullptr) {
  #ifndef DISABLE_ASYNC_WEBSERVER
  _server = new AsyncWebServer(80);
  // OPTIMISATION: Configuration serveur pour meilleure réactivité
  _server = new AsyncWebServer(80);  // ❌ DUPLIQUÉ !
  // OPTIMISATION: Configuration serveur pour meilleure réactivité
  // Note: setTimeout() n'est pas disponible dans cette version d'AsyncWebServer
  #endif
}
```

**APRÈS**:
```cpp
WebServerManager::WebServerManager(SystemSensors& sensors, SystemActuators& acts)
    : _sensors(sensors), _acts(acts), _diag(nullptr) {
  #ifndef DISABLE_ASYNC_WEBSERVER
  _server = new AsyncWebServer(80);
  // Note: setTimeout() n'est pas disponible dans cette version d'AsyncWebServer
  #endif
}
```

**Action**: Supprimer lignes 35-37 (duplication + commentaires inutiles)

---

#### 3. Suppression code mort (2 minutes)

**Fichier**: `src/power.cpp`  
**Lignes**: 14-16

**AVANT**:
```cpp
// Simple exécution immédiate – si l'assert LWIP réapparaît, il faudra activer
// LWIP_TCPIP_CORE_LOCKING dans le menuconfig et remplacer cette implémentation
// par un appel tcpip_api_call, mais pour l'instant on garde quelque chose qui
// compile partout.
static inline void tcpip_safe_call(std::function<void()> fn) {
  fn();  // ❌ Ne fait rien d'utile
}
```

**APRÈS**:
```cpp
// Supprimé - fonction inutile remplacée par appels directs
```

**Action**:
1. Supprimer définition fonction (lignes 14-16)
2. Rechercher usages: `power.cpp:72` et remplacer par appel direct
```cpp
// AVANT:
tcpip_safe_call([](){ WiFi.disconnect(); });

// APRÈS:
WiFi.disconnect();
```

---

#### 4. Configuration .gitignore (5 minutes)

**Fichier**: `.gitignore`

**AJOUTER**:
```gitignore
# Logs temporaires
*.log
*.log.err
monitor_*.log
monitor_*.log.err

# Fichiers temporaires compilation
.pio/
.vscode/
compile_commands.json

# Archives temporaires
*.zip
!lib/*.zip

# Fichiers Windows
desktop.ini
Thumbs.db

# Fichiers macOS
.DS_Store

# Backups éditeurs
*~
*.swp
*.swo
```

---

#### 5. Organisation scripts (10 minutes)

```powershell
# Créer structure
New-Item -ItemType Directory -Force -Path "tools/testing"
New-Item -ItemType Directory -Force -Path "tools/monitoring"
New-Item -ItemType Directory -Force -Path "tools/deployment"

# Déplacer scripts test
Move-Item "test_*.ps1" "tools/testing/"

# Déplacer scripts diagnostic
Move-Item "diagnose_*.ps1" "tools/monitoring/"
Move-Item "monitor_*.ps1" "tools/monitoring/"

# Déplacer scripts sync
Move-Item "sync_*.ps1" "tools/deployment/"
Move-Item "restart_*.ps1" "tools/deployment/"
```

---

#### 6. Documentation navigation (30 minutes)

**Fichier**: `docs/README.md` (NOUVEAU)

```markdown
# Documentation ESP32 FFP5CS v11.03

## Navigation Rapide

### 📖 Pour Commencer
- [Getting Started](guides/getting_started.md) - Installation et configuration
- [Architecture Overview](architecture/overview.md) - Vue d'ensemble du système
- [Configuration Guide](guides/configuration.md) - Paramétrage projet

### 🏗️ Architecture
- [FreeRTOS Tasks](architecture/freertos_tasks.md) - Tâches et priorités
- [Memory Management](architecture/memory_management.md) - Gestion mémoire
- [Network Architecture](architecture/network.md) - WiFi, WebSocket, OTA
- [Sensor System](architecture/sensors.md) - Capteurs et validation

### 🛠️ Guides Pratiques
- [Troubleshooting](guides/troubleshooting.md) - Résolution problèmes
- [OTA Updates](guides/ota_updates.md) - Mises à jour firmware
- [Sleep Management](guides/sleep_management.md) - Gestion sommeil
- [WiFi Configuration](guides/wifi_config.md) - Configuration WiFi

### 🔧 Référence API
- [Sensors API](api/sensors.md) - Interface capteurs
- [Actuators API](api/actuators.md) - Interface actionneurs
- [Web Server API](api/web_server.md) - Endpoints HTTP
- [WebSocket Protocol](api/websocket.md) - Protocole temps réel

### 📊 Diagnostics
- [Monitoring Guide](diagnostics/monitoring.md) - Surveillance système
- [Log Analysis](diagnostics/logs.md) - Analyse logs
- [Performance Metrics](diagnostics/performance.md) - Métriques

### 📝 Historique
- [Changelog](changelog/VERSION.md) - Versions 10.20 à 11.03
- [Migration Guides](changelog/migrations/) - Guides migration
- [Known Issues](changelog/known_issues.md) - Problèmes connus

### 📚 Archive
- [Version 10.x Docs](archive/v10/) - Documentation versions antérieures

---

## 🚀 Démarrage Rapide

### Prérequis
- PlatformIO Core ou IDE
- ESP32-WROOM-32 ou ESP32-S3
- Python 3.x (pour scripts)

### Installation
```bash
# Clone
git clone <repo>

# Configuration
cp include/secrets.h.example include/secrets.h
# Éditer secrets.h avec vos credentials

# Build
pio run -e wroom-test

# Upload
pio run -e wroom-test -t upload

# Monitor
pio device monitor
```

### Post-Déploiement
```bash
# Monitoring automatique 90s
./tools/monitoring/monitor_wroom_test.ps1
```

---

## 🔍 Structure du Projet

```
ffp5cs/
├── src/              # Sources C++
├── include/          # Headers
├── data/             # Interface web (LittleFS)
├── tools/            # Scripts utilitaires
│   ├── testing/      # Scripts de test
│   ├── monitoring/   # Scripts diagnostic
│   └── deployment/   # Scripts déploiement
├── docs/             # Documentation
│   ├── architecture/ # Design système
│   ├── guides/       # Guides pratiques
│   ├── api/          # Référence API
│   ├── diagnostics/  # Monitoring
│   ├── changelog/    # Historique versions
│   └── archive/      # Docs obsolètes
└── unused/           # Code temporaire (ne pas utiliser)
```

---

## 📞 Support

- **Issues**: GitHub Issues
- **Questions**: Voir guides troubleshooting
- **Updates**: Voir changelog

---

**Dernière mise à jour**: 2025-10-10  
**Version**: 11.03
```

**Action**: Créer ce fichier qui servira de point d'entrée documentation

---

### 🎉 Résultat PHASE 1

Après ces 6 actions (~45 minutes):

✅ **Logs temporaires nettoyés**: Fichiers .log et .log.err supprimés  
✅ **Bug corrigé**: Double instanciation AsyncWebServer dans web_server.cpp  
✅ **Code mort supprimé**: tcpip_safe_call() remplacé par appels directs  
✅ **Gitignore amélioré**: Ignore .log.err et monitor_analysis_*.log  
✅ **Navigation documentée**: docs/README.md créé (point d'entrée)  
⏸️ **Scripts organisation**: À faire manuellement si souhaité (mv vers tools/)

**Note**: Le dossier `unused/` est conservé (contient code de référence historique)

**Prochaine étape**: PHASE 2 (Refactoring automatism.cpp)

---

## 📅 PHASE 2 - Refactoring automatism.cpp (3-5 jours)

### Objectif
Diviser `src/automatism.cpp` (3421 lignes) en 5 modules logiques

### Étape 1: Préparation (1h)

#### 1.1 Créer structure
```bash
mkdir -p src/automatism
touch src/automatism/automatism_core.h
touch src/automatism/automatism_core.cpp
touch src/automatism/automatism_network.h
touch src/automatism/automatism_network.cpp
touch src/automatism/automatism_actuators.h
touch src/automatism/automatism_actuators.cpp
touch src/automatism/automatism_feeding.h
touch src/automatism/automatism_feeding.cpp
touch src/automatism/automatism_persistence.h
touch src/automatism/automatism_persistence.cpp
```

#### 1.2 Analyser dépendances actuelles
```bash
# Lister tous les includes de automatism.cpp
grep "^#include" src/automatism.cpp

# Lister toutes les méthodes publiques
grep "^.*Automatism::" src/automatism.cpp | cut -d'(' -f1
```

### Étape 2: Extraction modules (2 jours)

#### Module 1: automatism_persistence.cpp (4h)
**Responsabilité**: Gestion NVS (save/load état)

**Fonctions à extraire**:
- `saveActuatorSnapshotToNVS()`
- `loadActuatorSnapshotFromNVS()`
- `clearActuatorSnapshotInNVS()`
- Logique sauvegarde config

**Classe**:
```cpp
// automatism_persistence.h
class AutomatismPersistence {
public:
    static void saveActuatorSnapshot(bool aqua, bool heater, bool light);
    static bool loadActuatorSnapshot(bool& aqua, bool& heater, bool& light);
    static void clearActuatorSnapshot();
    
    static void saveRemoteConfig(const String& json);
    static bool loadRemoteConfig(String& json);
};
```

---

#### Module 2: automatism_network.cpp (8h)
**Responsabilité**: Communication serveur distant

**Fonctions à extraire**:
- `sendFullUpdate()`
- `fetchRemoteState()`
- Toutes les tâches xTaskCreate pour sync
- Logique retry/backoff

**Classe**:
```cpp
// automatism_network.h
class AutomatismNetwork {
public:
    AutomatismNetwork(WebClient& web);
    
    bool sendFullUpdate(const SensorReadings& r, const String& extraParams = "");
    bool fetchRemoteState(JsonDocument& doc);
    
    // Sync asynchrone
    void syncAquaPumpState(bool isOn);
    void syncTankPumpState(bool isOn);
    
private:
    WebClient& _web;
    TaskHandle_t _syncTaskHandle;
    
    static void syncTask(void* param);
};
```

---

#### Module 3: automatism_actuators.cpp (6h)
**Responsabilité**: Contrôle pompes, relais, servo

**Fonctions à extraire**:
- `startAquaPumpManualLocal()`
- `stopAquaPumpManualLocal()`
- `startTankPumpManualLocal()`
- `stopTankPumpManualLocal()`
- Logique gestion relais/servo

**Classe**:
```cpp
// automatism_actuators.h
class AutomatismActuators {
public:
    AutomatismActuators(SystemActuators& acts, RealtimeWebSocket& ws);
    
    // Pompe aquarium
    void startAquaPumpManual();
    void stopAquaPumpManual();
    bool isAquaPumpRunning() const;
    
    // Pompe réserve
    void startTankPumpManual();
    void stopTankPumpManual();
    bool isTankPumpRunning() const;
    
    // Chauffage
    void setHeaterState(bool on);
    
    // Lumière
    void setLightState(bool on);
    
private:
    SystemActuators& _acts;
    RealtimeWebSocket& _ws;
    
    void syncStateToWebSocket();
};
```

---

#### Module 4: automatism_feeding.cpp (8h)
**Responsabilité**: Logique nourrissage automatique

**Fonctions à extraire**:
- Logique calcul heures nourrissage
- Gestion flags bouffe (matin/midi/soir)
- Distribution automatique
- Vérifications conditions

**Classe**:
```cpp
// automatism_feeding.h
class AutomatismFeeding {
public:
    AutomatismFeeding(SystemActuators& acts, ConfigManager& cfg);
    
    void checkFeedingSchedule(int currentHour, int currentMinute, int dayOfYear);
    bool feedManual(const String& type); // "gros" ou "petits"
    
    struct FeedingStatus {
        bool morningDone;
        bool noonDone;
        bool eveningDone;
        int lastFeedDay;
    };
    
    FeedingStatus getStatus() const;
    
private:
    SystemActuators& _acts;
    ConfigManager& _config;
    
    bool shouldFeedNow(int hour, int minute) const;
    void performFeeding(bool isLarge);
};
```

---

#### Module 5: automatism_core.cpp (reste)
**Responsabilité**: Orchestration et logique métier principale

**Garde**:
- `update()` - loop principale
- `begin()` - initialisation
- Logique marées
- Détection conditions (niveaux, température)
- Coordination modules

**Classe**:
```cpp
// automatism_core.h
class Automatism {
public:
    Automatism(SystemSensors& s, SystemActuators& a, WebClient& w, 
               DisplayView& o, PowerManager& p, Mailer& m, ConfigManager& c);
    
    void begin();
    void update(const SensorReadings& r);
    void updateDisplay();
    
    // Accesseurs modules
    AutomatismFeeding& feeding() { return _feeding; }
    AutomatismActuators& actuators() { return _actuators; }
    AutomatismNetwork& network() { return _network; }
    
private:
    // Références composants
    SystemSensors& _sensors;
    SystemActuators& _acts;
    WebClient& _web;
    DisplayView& _oled;
    PowerManager& _power;
    Mailer& _mailer;
    ConfigManager& _config;
    
    // Modules
    AutomatismFeeding _feeding;
    AutomatismActuators _actuators;
    AutomatismNetwork _network;
    
    // Logique interne
    void checkTideConditions(const SensorReadings& r);
    void checkTemperature(const SensorReadings& r);
    void checkWaterLevels(const SensorReadings& r);
};
```

### Étape 3: Tests (1 jour)

#### 3.1 Checklist validation
```markdown
- [ ] Compile sans erreurs
- [ ] Taille binaire similaire (±5%)
- [ ] Heap usage similaire
- [ ] Tests manuels:
    - [ ] Nourrissage manuel
    - [ ] Pompes aqua/réserve
    - [ ] Sync serveur
    - [ ] WebSocket updates
    - [ ] Sauvegarde NVS
    - [ ] Monitoring 90s
```

#### 3.2 Script test automatique
```powershell
# tools/testing/test_refactoring.ps1
Write-Host "Test post-refactoring automatism"

# Compile
pio run -e wroom-test

# Upload
pio run -e wroom-test -t upload

# Monitor 90s
Start-Sleep -Seconds 90

# Vérifier logs
$logs = Get-Content "monitor.log"
$errors = $logs | Select-String "ERROR|PANIC|Guru"

if ($errors.Count -gt 0) {
    Write-Host "❌ Erreurs détectées:" -ForegroundColor Red
    $errors
    exit 1
} else {
    Write-Host "✅ Tests passés" -ForegroundColor Green
    exit 0
}
```

### Étape 4: Documentation (4h)

#### 4.1 Créer docs/architecture/automatism.md
```markdown
# Architecture Automatism

## Vue d'ensemble

Le système automatism est divisé en 5 modules:

1. **Core**: Orchestration principale
2. **Network**: Communication serveur
3. **Actuators**: Contrôle hardware
4. **Feeding**: Logique nourrissage
5. **Persistence**: Sauvegarde état

## Diagramme

[Core] <---> [Actuators]
   |            |
   v            v
[Network]   [Feeding]
   |            |
   v            v
[Persistence] [NVS]

## Détails modules

### Core
- Fichier: `src/automatism/automatism_core.cpp`
- Responsabilité: Orchestration
- Dépendances: Tous les autres modules

### Network
- Fichier: `src/automatism/automatism_network.cpp`
- Responsabilité: Sync serveur distant
- API: sendFullUpdate(), fetchRemoteState()

... etc
```

---

## 🔄 PHASE 3 - Configuration (2-3 jours)

### Objectif
Simplifier `project_config.h` (1063 lignes → ~500 lignes)

### Actions

#### 1. Fusionner namespaces redondants (4h)
```cpp
// AVANT: 2 namespaces
namespace TimingConfig { ... }
namespace TimingConfigExtended { ... }

// APRÈS: 1 namespace
namespace TimingConfig {
    // Constantes principales
    // Constantes étendues (bien organisées)
}
```

#### 2. Supprimer compatibilité obsolète (2h)
```cpp
// SUPPRIMER: project_config.h lignes 637-732
> *Section historique (legacy)*: `namespace CompatibilityAliases { ... }` (supprimé en v11.118)
namespace Config { ... }  // Rétro-compatibilité
```

#### 3. Diviser en fichiers (1 jour)
```
config/
├── core_config.h        (version, board, features)
├── sensor_config.h      (validation, timeouts capteurs)
├── network_config.h     (wifi, web, ota)
├── power_config.h       (sleep, watchdog)
└── timing_config.h      (intervalles, délais)
```

#### 4. Standardiser unités (4h)
```cpp
// AVANT: Mix ms/sec
TimingConfig::SENSOR_READ_INTERVAL_MS = 4000     // ms
SleepConfig::MIN_INACTIVITY_DELAY_SEC = 300      // sec

// APRÈS: Tout en ms avec suffix
TimingConfig::SENSOR_READ_INTERVAL_MS = 4000
TimingConfig::MIN_INACTIVITY_DELAY_MS = 300000

// OU tout en sec avec suffix
TimingConfig::SENSOR_READ_INTERVAL_SEC = 4
TimingConfig::MIN_INACTIVITY_DELAY_SEC = 300
```

---

## 📊 Suivi Progression

### Tableau de bord

| Phase | Statut | Durée réelle | Commentaires |
|-------|--------|--------------|--------------|
| **Phase 1: Nettoyage** | ⬜ TODO | - | Quick wins 1h |
| **Phase 2: Refactoring** | ⬜ TODO | - | automatism.cpp |
| **Phase 3: Configuration** | ⬜ TODO | - | project_config.h |
| **Phase 4: Environments** | ⬜ TODO | - | platformio.ini |
| **Phase 5: Metrics** | ⬜ TODO | - | Monitoring |
| **Phase 6: Validation** | ⬜ TODO | - | Benchmarks |
| **Phase 7: Docs** | ⬜ TODO | - | Structure docs/ |
| **Phase 8: Sécurité** | ⬜ TODO | - | NVS encrypt, etc |

**Légende**: ⬜ TODO | 🟡 EN COURS | ✅ TERMINÉ | ❌ BLOQUÉ

---

## 📝 Notes

### Après chaque phase
1. Commit avec message descriptif
2. Tag version si pertinent
3. Monitoring 90s + analyse logs
4. Mise à jour ce document (statut, durée)
5. Documentation changements

### En cas de problème
1. Rollback git immédiat
2. Analyse logs détaillée
3. Documentation issue
4. Fix ou report phase suivante

---

**Document créé**: 2025-10-10  
**Dernière mise à jour**: 2025-10-10  
**Responsable**: Équipe FFP5CS

**Référence**: Voir ANALYSE_EXHAUSTIVE_PROJET_ESP32_FFP5CS.md pour détails complets

