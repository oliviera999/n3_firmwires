# PHASE 2 - GUIDE COMPLET DES MODULES

**Date**: 2025-10-11  
**Pour**: Refactoring automatism.cpp modules 2-6  
**Durée estimée**: 2-4 jours

---

## MODULE 2: ACTUATORS (4-6 heures)

### 🎯 Objectif

Extraire 10 méthodes de contrôle manuel actionneurs avec factorisation du code dupliqué

### 📋 Méthodes à Extraire

**Pompe Aquarium** (2):
1. `startAquaPumpManualLocal()` - ligne 63
2. `stopAquaPumpManualLocal()` - ligne 109

**Chauffage** (2):
3. `startHeaterManualLocal()` - ligne 156
4. `stopHeaterManualLocal()` - ligne 194

**Lumière** (2):
5. `startLightManualLocal()` - ligne 247
6. `stopLightManualLocal()` - ligne 286

**Pompe Réserve** (2):
7. `startTankPumpManual()` - ligne 2522
8. `stopTankPumpManual()` - ligne 2594

**Configuration** (2):
9. `toggleEmailNotifications()` - ligne 325
10. `toggleForceWakeup()` - ligne 341

### 🔍 Analyse Code Dupliqué

**Pattern répété 8 fois** (start/stop x 4 actionneurs):

```cpp
void startXxxManualLocal() {
    // 1. Action immédiate
    _acts.startXxx();
    _lastXxxManualOrigin = ManualOrigin::LOCAL_SERVER;
    
    // 2. WebSocket
    realtimeWebSocket.broadcastNow();
    
    // 3. Tâche async sync (13 lignes identiques !)
    if (WiFi.status() == WL_CONNECTED) {
        static TaskHandle_t handle = nullptr;
        if (handle != nullptr && eTaskGetState(handle) != eDeleted) return;
        
        xTaskCreate([](void* param) {
            vTaskDelay(pdMS_TO_TICKS(50));
            SensorReadings r = autoCtrl._sensors.read();
            bool ok = autoCtrl.sendFullUpdate(r, "etatXxx=1");
            *taskHandle = nullptr;
            vTaskDelete(NULL);
        }, "sync_xxx", 4096, &handle, 1, NULL);
    }
}
```

**Code dupliqué**: ~400 lignes (10 méthodes x ~40 lignes)  
**Après factorisation**: ~150 lignes (helper + 10 wrappers courts)  
**Gain**: -250 lignes

### ✅ Solution: Fonction Helper

```cpp
// Dans automatism_actuators.cpp
namespace {
    // Helper pour sync async avec serveur distant
    void syncActuatorStateAsync(
        const char* taskName,
        const char* extraParams,
        TaskHandle_t& taskHandle,
        const char* actionDesc
    ) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("[Actuators] ⚠️ Pas de WiFi - %s localement uniquement\n", actionDesc);
            return;
        }
        
        Serial.println(F("[Actuators] 🌐 Synchronisation serveur en arrière-plan..."));
        
        // Vérifier si tâche déjà en cours
        if (taskHandle != nullptr && eTaskGetState(taskHandle) != eDeleted) {
            Serial.printf("[Actuators] ⚠️ Tâche sync %s déjà en cours - skip\n", taskName);
            return;
        }
        
        // Paramètres pour la tâche
        struct SyncParams {
            const char* extraParams;
            const char* actionDesc;
            TaskHandle_t* handle;
        };
        
        SyncParams* params = new SyncParams{extraParams, actionDesc, &taskHandle};
        
        BaseType_t result = xTaskCreate([](void* param) {
            SyncParams* p = (SyncParams*)param;
            
            vTaskDelay(pdMS_TO_TICKS(50));
            extern Automatism autoCtrl;
            SensorReadings freshReadings = autoCtrl._sensors.read();
            bool success = autoCtrl.sendFullUpdate(freshReadings, p->extraParams);
            
            if (success) {
                Serial.printf("[Actuators] ✅ Sync serveur OK - %s\n", p->actionDesc);
            } else {
                Serial.printf("[Actuators] ⚠️ Sync serveur échec - %s\n", p->actionDesc);
            }
            
            *(p->handle) = nullptr;
            delete p;
            vTaskDelete(NULL);
        }, taskName, 4096, params, 1, &taskHandle);
        
        if (result != pdPASS) {
            Serial.printf("[Actuators] ❌ Échec création tâche %s\n", taskName);
            delete params;
            taskHandle = nullptr;
        }
    }
}
```

### 📝 Implémentation

**automatism_actuators.h**:
```cpp
#pragma once
#include <Arduino.h>
#include "system_actuators.h"
#include "config_manager.h"

class AutomatismActuators {
public:
    AutomatismActuators(SystemActuators& acts, ConfigManager& cfg);
    
    // Pompe aquarium
    void startAquaPumpManual();
    void stopAquaPumpManual();
    
    // Pompe réserve
    void startTankPumpManual();
    void stopTankPumpManual();
    
    // Chauffage
    void startHeaterManual();
    void stopHeaterManual();
    
    // Lumière
    void startLightManual();
    void stopLightManual();
    
    // Configuration
    void toggleEmailNotifications();
    void toggleForceWakeup();
    
private:
    SystemActuators& _acts;
    ConfigManager& _config;
    
    // Task handles pour sync async
    TaskHandle_t _syncAquaStartHandle = nullptr;
    TaskHandle_t _syncAquaStopHandle = nullptr;
    TaskHandle_t _syncTankStartHandle = nullptr;
    TaskHandle_t _syncTankStopHandle = nullptr;
    TaskHandle_t _syncHeaterStartHandle = nullptr;
    TaskHandle_t _syncHeaterStopHandle = nullptr;
    TaskHandle_t _syncLightStartHandle = nullptr;
    TaskHandle_t _syncLightStopHandle = nullptr;
};
```

**Wrapper simplifié** (après factorisation):
```cpp
void AutomatismActuators::startAquaPumpManual() {
    Serial.println(F("[Actuators] 🐠 Démarrage pompe aquarium..."));
    
    _acts.startAquaPump();
    realtimeWebSocket.broadcastNow();
    syncActuatorStateAsync("sync_aqua_start", "etatPompeAqua=1", 
                          _syncAquaStartHandle, "pompe aqua ON");
}
```

**Réduction**: 45 lignes → 7 lignes par méthode !

---

## MODULE 3: FEEDING (6 heures)

### 🎯 Objectif

Extraire logique nourrissage (automatique + manuel)

### 📋 Méthodes à Extraire (8)

1. `handleFeeding()` - ligne 1189 (~180 lignes !)
2. `manualFeedSmall()` - ligne 2021 (~44 lignes)
3. `manualFeedBig()` - ligne 2065 (~44 lignes)
4. `createFeedingMessage()` - ligne 3198 (~23 lignes)
5. `traceFeedingEvent()` - ligne 2638 (~16 lignes)
6. `traceFeedingEventSelective()` - ligne 2654 (~37 lignes)
7. `checkNewDay()` - ligne 874 (~23 lignes)
8. `saveFeedingState()` - ligne 897 (~11 lignes)

**Total**: ~378 lignes à extraire

### 🔍 Variables à Migrer

```cpp
// Horaires feeding
uint8_t feedMorning = 8;
uint8_t feedNoon = 12;
uint8_t feedEvening = 19;

// Durées
uint16_t feedBigDur = 10;
uint16_t feedSmallDur = 10;

// Phases feeding
enum class FeedingPhase { NONE, FEEDING_FORWARD, FEEDING_BACKWARD };
FeedingPhase _currentFeedingPhase;
unsigned long _feedingPhaseEnd;
unsigned long _feedingTotalEnd;

// État feeding
String bouffePetits{"0"};
String bouffeGros{"0"};
int lastFeedDay = -1;
```

### 📝 Structure

```cpp
// automatism_feeding.h
class AutomatismFeeding {
public:
    AutomatismFeeding(SystemActuators& acts, ConfigManager& cfg, Mailer& mail);
    
    void handleSchedule(int hour, int minute, int dayOfYear);
    void feedSmallManual();
    void feedBigManual();
    
    String createMessage(const char* type, uint16_t bigDur, uint16_t smallDur);
    
    // Configuration
    void setSchedule(uint8_t morning, uint8_t noon, uint8_t evening);
    void setDurations(uint16_t bigDur, uint16_t smallDur);
    
    struct Status {
        bool morningDone;
        bool noonDone;
        bool eveningDone;
        int lastDay;
    };
    Status getStatus() const;
    
private:
    SystemActuators& _acts;
    ConfigManager& _config;
    Mailer& _mailer;
    
    // Configuration
    uint8_t _feedMorning;
    uint8_t _feedNoon;
    uint8_t _feedEvening;
    uint16_t _feedBigDur;
    uint16_t _feedSmallDur;
    
    // État
    enum class Phase { NONE, FORWARD, BACKWARD };
    Phase _phase;
    unsigned long _phaseEnd;
    unsigned long _totalEnd;
    
    void performFeedingCycle(bool isLarge);
    void traceFeedingEvent();
    void traceFeedingEventSelective(bool small, bool big);
    void saveFeedingState();
    void checkNewDay();
    bool shouldFeedNow(int hour, int minute) const;
};
```

### ⚠️ Attention

**handleFeeding()** est très long (180 lignes, 1189-1369).  
Diviser en sous-méthodes:
- `checkFeedingTime()` - Vérifier si c'est l'heure
- `performFeeding()` - Exécuter nourrissage
- `updateFeedingPhase()` - Gérer phases servo

---

## MODULE 4: NETWORK (8 heures)

### 🎯 Objectif

Extraire toute la communication avec serveur distant

### 📋 Méthodes à Extraire (5)

1. `fetchRemoteState()` - ligne 2109 (~10 lignes)
2. `applyConfigFromJson()` - ligne 2119 (~40 lignes)
3. `sendFullUpdate()` - ligne 2444 (~78 lignes) - CRITIQUE
4. `handleRemoteState()` - ligne 1476 (~545 lignes !) - ÉNORME
5. `checkCriticalChanges()` - ligne 2159 (~285 lignes !)

**Total**: ~958 lignes ! (Le plus gros module)

### 🔍 Variables Remote à Migrer

```cpp
// Configuration serveur
String emailAddress;
bool emailEnabled;
uint16_t freqWakeSec;

// Seuils
uint16_t limFlood;
uint16_t aqThresholdCm;
uint16_t tankThresholdCm;
float heaterThresholdC;

// Timing
unsigned long lastSend;
unsigned long _lastRemoteFetch;
const unsigned long sendInterval = 120000;
const unsigned long remoteFetchInterval = 4000;

// État
bool serverOk;
int8_t sendState, recvState;
```

### 📝 Structure

```cpp
// automatism_network.h
class AutomatismNetwork {
public:
    AutomatismNetwork(WebClient& web, ConfigManager& cfg);
    
    // Communication serveur
    bool fetchRemoteState(JsonDocument& doc);
    void applyConfig(const JsonDocument& doc);
    bool sendFullUpdate(const SensorReadings& r, const char* extra = nullptr);
    
    // Polling périodique
    void handleRemotePoll();  // Était handleRemoteState()
    
    // Détection changements
    void checkCriticalChanges();
    
    // Getters configuration
    const String& getEmailAddress() const { return _emailAddress; }
    bool isEmailEnabled() const { return _emailEnabled; }
    uint16_t getFreqWakeSec() const { return _freqWakeSec; }
    uint16_t getLimFlood() const { return _limFlood; }
    // etc.
    
    // Setters
    void setEmailEnabled(bool enabled);
    
private:
    WebClient& _web;
    ConfigManager& _config;
    
    // Configuration remote
    String _emailAddress;
    bool _emailEnabled;
    uint16_t _freqWakeSec;
    uint16_t _limFlood;
    uint16_t _aqThresholdCm;
    uint16_t _tankThresholdCm;
    float _heaterThresholdC;
    
    // Timing
    unsigned long _lastSend;
    unsigned long _lastFetch;
    
    // État
    bool _serverOk;
    int8_t _sendState;
    int8_t _recvState;
    
    // État précédent pour détection changements
    bool _prevPumpTank;
    bool _prevPumpAqua;
    bool _prevBouffeMatin;
    bool _prevBouffeMidi;
    bool _prevBouffeSoir;
};
```

### ⚠️ ATTENTION handleRemoteState()

**545 lignes** dans 1 méthode ! (ligne 1476-2021)

**Diviser en**:
- `pollRemoteState()` - Lecture état serveur
- `parseRemoteConfig()` - Parsing JSON
- `applyRemoteActuators()` - Application actionneurs distants
- `handleRemoteCommands()` - Commandes spéciales

**Exemple structure**:
```cpp
void AutomatismNetwork::handleRemotePoll() {
    if (!shouldPoll()) return;
    
    JsonDocument doc;
    if (!fetchRemoteState(doc)) return;
    
    parseAndApplyConfig(doc);
    applyRemoteActuators(doc);
    handleRemoteCommands(doc);
}
```

---

## MODULE 5: SLEEP (8 heures)

### 🎯 Objectif

Isoler toute la logique sommeil adaptatif et marées

### 📋 Méthodes à Extraire (13)

**Principales**:
1. `handleAutoSleep()` - ligne 2754 (**443 lignes !!**)
2. `shouldEnterSleepEarly()` - ligne 2691 (~63 lignes)
3. `handleMaree()` - ligne 1177 (~12 lignes)

**Détection activité**:
4. `hasSignificantActivity()` - ligne 3221 (~11 lignes)
5. `updateActivityTimestamp()` - ligne 3232 (~6 lignes)
6. `notifyLocalWebActivity()` - ligne 3243 (~12 lignes)
7. `logActivity()` - ligne 3238 (~5 lignes)

**Calculs adaptatifs**:
8. `calculateAdaptiveSleepDelay()` - ligne 3255 (~33 lignes)
9. `isNightTime()` - ligne 3288 (~13 lignes)
10. `hasRecentErrors()` - ligne 3301 (~4 lignes)

**Validation système**:
11. `verifySystemStateAfterWakeup()` - ligne 3305 (~44 lignes)
12. `detectSleepAnomalies()` - ligne 3349 (~22 lignes)
13. `validateSystemStateBeforeSleep()` - ligne 3371 (~30 lignes)

**Total**: ~698 lignes

### 🔍 Variables Sleep à Migrer

```cpp
// Timing wake/sleep
unsigned long _lastWakeMs;
unsigned long _lastActivityMs;
unsigned long _lastSensorActivityMs;
unsigned long _lastWebActivityMs;

// État
bool _forceWakeFromWeb;
bool _hasRecentErrors;
uint8_t _consecutiveWakeupFailures;
int16_t tideTriggerCm;

// Config sleep adaptatif
struct SleepConfig {
    uint32_t minSleepTime;
    uint32_t maxSleepTime;
    uint32_t normalSleepTime;
    uint32_t errorSleepTime;
    uint32_t nightSleepTime;
    bool adaptiveSleep;
} _sleepConfig;
```

### ⚠️ CRITIQUE handleAutoSleep()

**443 LIGNES** dans 1 méthode ! (ligne 2754-3197)

C'est ÉNORME. **OBLIGATOIRE de diviser** en :

1. `checkSleepConditions()` - Vérifier conditions (validations)
2. `prepareSleep()` - Sauvegardes NVS, snapshot actionneurs
3. `executeSleep()` - Appel PowerManager.goToLightSleep()
4. `handleWakeup()` - Restauration état, vérifications
5. `handleWakeupFailure()` - Gestion échecs

**Structure proposée**:
```cpp
void AutomatismSleep::handleAutoSleep(const SensorReadings& r, 
                                      SystemActuators& acts,
                                      WebClient& web) {
    if (!shouldSleep()) return;
    
    if (!validateSystemState()) {
        Serial.println("[Sleep] Validation système échouée - pas de sommeil");
        return;
    }
    
    prepareSleep(acts);  // Snapshot actionneurs
    
    uint32_t sleepDuration = calculateSleepDuration();
    uint32_t actualSlept = _power.goToLightSleep(sleepDuration);
    
    handleWakeup(acts, actualSlept);
}
```

### 📝 Structure Module

```cpp
// automatism_sleep.h
class AutomatismSleep {
public:
    AutomatismSleep(PowerManager& power, ConfigManager& cfg);
    
    void handleAutoSleep(const SensorReadings& r, 
                        SystemActuators& acts, 
                        WebClient& web);
    bool shouldEnterSleepEarly(const SensorReadings& r);
    void handleMaree(const SensorReadings& r);
    
    // Activité
    bool hasSignificantActivity();
    void updateActivityTimestamp();
    void notifyWebActivity();
    void logActivity(const char* activity);
    
    // Calculs
    uint32_t calculateAdaptiveSleepDelay();
    bool isNightTime();
    bool hasRecentErrors();
    
    // Validation
    bool verifySystemStateAfterWakeup();
    void detectSleepAnomalies();
    bool validateSystemStateBeforeSleep();
    
    // Getters
    bool getForceWakeUp() const { return _forceWakeUp; }
    void setForceWakeUp(bool value) { _forceWakeUp = value; }
    
private:
    PowerManager& _power;
    ConfigManager& _config;
    
    unsigned long _lastWakeMs;
    unsigned long _lastActivityMs;
    unsigned long _lastWebActivityMs;
    bool _forceWakeFromWeb;
    bool _hasRecentErrors;
    uint8_t _consecutiveWakeupFailures;
    
    // Config adaptive
    struct SleepConfig {
        uint32_t minSleepTime;
        uint32_t maxSleepTime;
        uint32_t normalSleepTime;
        uint32_t errorSleepTime;
        uint32_t nightSleepTime;
        bool adaptiveSleep;
    } _sleepConfig;
    
    // Helpers privés pour diviser handleAutoSleep
    bool shouldSleep();
    bool validateSystemState();
    void prepareSleep(SystemActuators& acts);
    uint32_t calculateSleepDuration();
    void handleWakeup(SystemActuators& acts, uint32_t actualSlept);
    void handleWakeupFailure();
};
```

---

## MODULE 6: CORE (4 heures)

### 🎯 Objectif

Garder seulement orchestration et coordination

### 📋 Méthodes Conservées (8)

1. `begin()` - ligne 430 (~184 lignes) - Initialisation
2. `update()` - ligne 614 (~5 lignes) - Wrapper
3. `update(const SensorReadings&)` - ligne 782 (~92 lignes) - Loop principale
4. `updateDisplay()` - ligne 619 (~156 lignes) - Affichage OLED
5. `getRecommendedDisplayIntervalMs()` - ligne 775 (~7 lignes)
6. `handleRefill()` - ligne 908 (~269 lignes) - Logique refill aquarium
7. `handleAlerts()` - ligne 1369 (~107 lignes) - Alertes flood
8. `triggerResetMode()` - ligne 388 (~42 lignes) - Reset flags

**Total gardé**: ~862 lignes

### 📝 Structure Finale

```cpp
// include/automatism.h (refactoré)
#pragma once

#include "system_sensors.h"
#include "system_actuators.h"
#include "web_client.h"
#include "display_view.h"
#include "power.h"
#include "mailer.h"
#include "config_manager.h"
#include "automatism/automatism_persistence.h"
#include "automatism/automatism_actuators.h"
#include "automatism/automatism_feeding.h"
#include "automatism/automatism_network.h"
#include "automatism/automatism_sleep.h"
#include <ArduinoJson.h>

class Automatism {
public:
    Automatism(SystemSensors& s, SystemActuators& a, WebClient& w, 
               DisplayView& d, PowerManager& p, Mailer& m, ConfigManager& c);
    
    void begin();
    void update();
    void update(const SensorReadings& r);
    void updateDisplay();
    uint32_t getRecommendedDisplayIntervalMs();
    
    // === ACCESSEURS MODULES ===
    AutomatismFeeding& feeding() { return _feeding; }
    AutomatismActuators& actuators() { return _actuators; }
    AutomatismNetwork& network() { return _network; }
    AutomatismSleep& sleep() { return _sleep; }
    
    // === DÉLÉGATIONS PUBLIQUES ===
    // Feeding
    void manualFeedSmall() { _feeding.feedSmallManual(); }
    void manualFeedBig() { _feeding.feedBigManual(); }
    
    // Actuators
    void startAquaPumpManualLocal() { _actuators.startAquaPumpManual(); }
    void stopAquaPumpManualLocal() { _actuators.stopAquaPumpManual(); }
    void startTankPumpManual() { _actuators.startTankPumpManual(); }
    void stopTankPumpManual() { _actuators.stopTankPumpManual(); }
    void startHeaterManualLocal() { _actuators.startHeaterManual(); }
    void stopHeaterManualLocal() { _actuators.stopHeaterManual(); }
    void startLightManualLocal() { _actuators.startLightManual(); }
    void stopLightManualLocal() { _actuators.stopLightManual(); }
    void toggleEmailNotifications() { _actuators.toggleEmailNotifications(); }
    void toggleForceWakeup() { _actuators.toggleForceWakeup(); }
    
    // Network
    bool fetchRemoteState(JsonDocument& doc) { return _network.fetchRemoteState(doc); }
    bool sendFullUpdate(const SensorReadings& r, const char* extra = nullptr) {
        return _network.sendFullUpdate(r, extra);
    }
    
    // Sleep
    bool getForceWakeUp() const { return _sleep.getForceWakeUp(); }
    void notifyLocalWebActivity() { _sleep.notifyWebActivity(); }
    
    // Getters état (délégués)
    bool isPumpAquaLocked() const { return _config.getPompeAquaLocked(); }
    bool isTankPumpLocked() const { return _tankPumpLocked; }
    bool isEmailEnabled() const { return _network.isEmailEnabled(); }
    const String& getEmailAddress() const { return _network.getEmailAddress(); }
    uint16_t getFeedBigDur() const { return _feeding.getBigDuration(); }
    uint16_t getFeedSmallDur() const { return _feeding.getSmallDuration(); }
    time_t getCurrentTime() { return _power.getCurrentEpoch(); }
    String getCurrentTimeString() { return _power.getCurrentTimeString(); }
    uint32_t getPumpStartTime() const { return _pumpStartMs; }
    
    // Actions OLED
    void triggerMailBlink() { armMailBlink(); }
    void armMailBlink(uint32_t durationMs = 3000);
    void triggerResetMode();
    
private:
    // === RÉFÉRENCES COMPOSANTS ===
    SystemSensors& _sensors;
    SystemActuators& _acts;
    WebClient& _web;
    DisplayView& _disp;
    PowerManager& _power;
    Mailer& _mailer;
    ConfigManager& _config;
    
    // === MODULES (Composition) ===
    AutomatismFeeding _feeding;
    AutomatismActuators _actuators;
    AutomatismNetwork _network;
    AutomatismSleep _sleep;
    // AutomatismPersistence est statique (pas besoin d'instance)
    
    // === ÉTAT CENTRAL (Core uniquement) ===
    SensorReadings _lastReadings;
    bool _tankPumpLocked;
    unsigned long _mailBlinkUntil;
    unsigned long _countdownEnd;
    String _countdownLabel;
    uint32_t _pumpStartMs;
    bool _oledToggle;
    unsigned long _lastOled;
    unsigned long _lastScreenSwitch;
    
    // === LOGIQUE CORE ===
    void handleRefill(const SensorReadings& r);
    void handleAlerts(const SensorReadings& r);
    void renderDisplay();
    void updateCountdown();
};
```

**Réduction**: 3421 lignes → ~700 lignes de core

---

## 📊 PLAN DE DÉCOUPAGE

### Ordre Recommandé

1. ✅ **Persistence** (simple, sans dépendances) - FAIT
2. ⏳ **Actuators** (moyen, factorisation) - EN COURS
3. **Feeding** (moyen, logique métier)
4. **Network** (complexe, parsing JSON)
5. **Sleep** (très complexe, handleAutoSleep 443 lignes)
6. **Core** (refactoring final, délégations)

### Pourquoi cet ordre ?

- Simple → Complexe (apprendre progressivement)
- Indépendant → Dépendant
- Tests incrémentaux possibles

---

## 🧪 STRATÉGIE TESTS

### Après Chaque Module

```bash
# 1. Compilation
pio run -e wroom-test

# 2. Si succès, commit
git add src/automatism/
git commit -m "Phase 2.X: Module YYY extrait"

# 3. Flash (optionnel)
pio run -e wroom-test -t upload

# 4. Test rapide (1 min)
# - Vérifier module fonctionne
# - Exemple: Module Actuators → Test pompe ON/OFF
```

### Après Tous Modules

```bash
# 1. Flash complet
pio run -e wroom-test -t upload
pio run -e wroom-test -t uploadfs

# 2. Monitoring 90s OBLIGATOIRE
pio device monitor
# Observer 90 secondes

# 3. Tests fonctionnels complets
- [ ] Nourrissage manuel (small/big)
- [ ] Pompes aqua/réserve
- [ ] Chauffage/Lumière
- [ ] Sync serveur
- [ ] WebSocket temps réel
- [ ] Sommeil/réveil
- [ ] Marées
- [ ] Emails

# 4. Vérifier métriques
- Heap usage
- Flash size (doit être similaire)
- RAM usage (doit être similaire)
```

---

## ⚡ FACTORISATION CODE

### Pattern Identifié #1: Tâches Sync Async

**Répété 8 fois** avec légères variations.

**Solution**: Helper fonction (comme montré Module Actuators)

**Gain**: ~280 lignes (35 lignes x 8 occurrences)

### Pattern Identifié #2: Vérification Task Handle

```cpp
if (handle != nullptr && eTaskGetState(handle) != eDeleted) {
    Serial.println("Déjà en cours - skip");
    return;
}
```

**Répété 8 fois**

**Solution**: Helper
```cpp
inline bool isTaskRunning(TaskHandle_t handle) {
    return handle != nullptr && eTaskGetState(handle) != eDeleted;
}
```

---

## 📈 GAINS ATTENDUS

### Code

**Avant refactoring**:
- 1 fichier: 3421 lignes
- Maintenance: Impossible
- Lisibilité: 2/10
- Testabilité: 1/10

**Après refactoring**:
- 6 modules: ~700 lignes max chacun
- Maintenance: Facile
- Lisibilité: 8/10
- Testabilité: 8/10

### Métriques

- **Lignes code**: 3421 → ~3100 (factorisation: -321 lignes)
- **Fichiers**: 1 → 13 (6 modules x 2 + core)
- **Maintenabilité**: +300%
- **Temps debug**: -75% (isolation problèmes)
- **Onboarding**: -70% (responsabilités claires)

---

## 🎯 CHECKLIST PHASE 2 COMPLÈTE

- [x] ✅ Préparation (structure, backups)
- [x] ✅ Module 1: Persistence (3 méthodes)
- [ ] ⏳ Module 2: Actuators (10 méthodes)
- [ ] Module 3: Feeding (8 méthodes)
- [ ] Module 4: Network (5 méthodes)
- [ ] Module 5: Sleep (13 méthodes)
- [ ] Module 6: Core refactoré (orchestration)
- [ ] Tests fonctionnels complets
- [ ] Monitoring 90s
- [ ] Documentation
- [ ] Commit final Phase 2

**Progression**: 2/10 étapes (20%)

---

**Document**: Guide complet modules 2-6  
**Pour**: Implémentation Phase 2  
**Prochaine action**: Module Actuators (maintenant)


