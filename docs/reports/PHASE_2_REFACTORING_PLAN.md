# PHASE 2 - REFACTORING automatism.cpp

**Date début**: 2025-10-11  
**Durée estimée**: 3-5 jours  
**Objectif**: Diviser 3421 lignes en 5 modules logiques

---

## 📊 ÉTAT ACTUEL

### Fichiers Analysés
- `src/automatism.cpp`: **3421 lignes** 🔴
- `include/automatism.h`: **278 lignes**
- **47 méthodes** publiques identifiées
- **Tout dans 1 fichier** = Maintenance impossible

### Responsabilités Mélangées

**Automatism fait TOUT**:
1. 🐟 Logique nourrissage (matin/midi/soir, servos)
2. 💧 Gestion pompes (aqua/réserve, refill, flood)
3. 🌡️ Contrôle température (chauffage ON/OFF)
4. 💡 Gestion lumière
5. 🌊 Détection marées (sommeil anticipé)
6. 💾 Persistance NVS (snapshot actionneurs, config)
7. 🌐 Sync serveur distant (POST/GET, retry, backoff)
8. 📧 Emails (flood alerts, feeding notifs, tank pump)
9. 🖥️ Affichage OLED (updateDisplay, countdown)
10. 😴 Logique sommeil (adaptatif, activité, force wakeup)
11. 🔄 WebSocket broadcasts (feedback utilisateur)

**Verdict**: **DIEU OBJET** (God Object anti-pattern)

---

## 🎯 STRUCTURE CIBLE

### Architecture Proposée

```
src/automatism/
├── automatism_core.h              # Orchestration principale
├── automatism_core.cpp            # (~800 lignes)
│
├── automatism_actuators.h         # Contrôle hardware
├── automatism_actuators.cpp       # (~600 lignes)
│
├── automatism_feeding.h           # Nourrissage
├── automatism_feeding.cpp         # (~500 lignes)
│
├── automatism_network.h           # Communication serveur
├── automatism_network.cpp         # (~700 lignes)
│
├── automatism_persistence.h       # NVS save/load
├── automatism_persistence.cpp     # (~300 lignes)
│
├── automatism_sleep.h             # Logique sommeil
└── automatism_sleep.cpp           # (~500 lignes)
```

**Total**: 6 modules au lieu de 5 (sleep séparé car complexe)

---

## 📋 CATÉGORISATION DES 47 MÉTHODES

### Module 1: PERSISTENCE (NVS) - 3 méthodes

**Responsabilité**: Sauvegarde/chargement état en NVS

```cpp
// automatism_persistence.h
class AutomatismPersistence {
public:
    static void saveActuatorSnapshot(bool aqua, bool heater, bool light);
    static bool loadActuatorSnapshot(bool& aqua, bool& heater, bool& light);
    static void clearActuatorSnapshot();
};
```

**Méthodes à extraire**:
1. `saveActuatorSnapshotToNVS()` - ligne 47
2. `loadActuatorSnapshotFromNVS()` - ligne 59
3. `clearActuatorSnapshotInNVS()` - ligne 71

**Dépendances**: Preferences (NVS)

---

### Module 2: ACTUATORS (Pompes, Chauffage, Lumière) - 10 méthodes

**Responsabilité**: Contrôle manuel actionneurs avec sync WebSocket

```cpp
// automatism_actuators.h
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
    void syncToWebSocket();  // Broadcast immédiat
};
```

**Méthodes à extraire**:
1. `startAquaPumpManualLocal()` - ligne 78
2. `stopAquaPumpManualLocal()` - ligne 124
3. `startHeaterManualLocal()` - ligne 171
4. `stopHeaterManualLocal()` - ligne 209
5. `startLightManualLocal()` - ligne 247
6. `stopLightManualLocal()` - ligne 286
7. `toggleEmailNotifications()` - ligne 325
8. `toggleForceWakeup()` - ligne 341
9. `startTankPumpManual()` - ligne 2522
10. `stopTankPumpManual()` - ligne 2594

**Dépendances**: SystemActuators, RealtimeWebSocket, ConfigManager

---

### Module 3: FEEDING (Nourrissage) - 6 méthodes

**Responsabilité**: Logique nourrissage automatique et manuel

```cpp
// automatism_feeding.h
class AutomatismFeeding {
public:
    AutomatismFeeding(SystemActuators& acts, ConfigManager& cfg, Mailer& mail);
    
    void handleFeedingSchedule(int hour, int minute, int dayOfYear);
    void manualFeedSmall();
    void manualFeedBig();
    String createFeedingMessage(const char* type, uint16_t bigDur, uint16_t smallDur);
    
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
    Mailer& _mailer;
    
    // Configuration
    uint8_t feedMorning = 8;
    uint8_t feedNoon = 12;
    uint8_t feedEvening = 19;
    uint16_t feedBigDur = 10;
    uint16_t feedSmallDur = 10;
    
    void performFeedingCycle(bool isLarge);
    void traceFeedingEvent();
    void traceFeedingEventSelective(bool small, bool big);
    void saveFeedingState();
    void checkNewDay();
};
```

**Méthodes à extraire**:
1. `handleFeeding()` - ligne 1189
2. `manualFeedSmall()` - ligne 2021
3. `manualFeedBig()` - ligne 2065
4. `createFeedingMessage()` - ligne 3198
5. `traceFeedingEvent()` - ligne 2638
6. `traceFeedingEventSelective()` - ligne 2654
7. `checkNewDay()` - ligne 874
8. `saveFeedingState()` - ligne 897

**Dépendances**: SystemActuators, ConfigManager, Mailer

---

### Module 4: NETWORK (Sync Serveur) - 5 méthodes

**Responsabilité**: Communication avec serveur distant

```cpp
// automatism_network.h
class AutomatismNetwork {
public:
    AutomatismNetwork(WebClient& web);
    
    bool sendFullUpdate(const SensorReadings& r, const char* extraParams = nullptr);
    bool fetchRemoteState(JsonDocument& doc);
    void applyConfigFromJson(const JsonDocument& doc);
    void handleRemoteState();  // Polling périodique
    void checkCriticalChanges();  // Détection changements config
    
private:
    WebClient& _web;
    unsigned long _lastRemoteFetch = 0;
    unsigned long _lastSend = 0;
    
    const unsigned long remoteFetchInterval = 4000;
    const unsigned long sendInterval = 120000;
    
    // Variables remote
    uint16_t limFlood;
    uint16_t aqThresholdCm;
    uint16_t tankThresholdCm;
    float heaterThresholdC;
    // etc.
    
    void syncAquaPumpState(bool isOn);
    void syncTankPumpState(bool isOn);
    void syncHeaterState(bool isOn);
    void syncLightState(bool isOn);
};
```

**Méthodes à extraire**:
1. `fetchRemoteState()` - ligne 2109
2. `applyConfigFromJson()` - ligne 2119
3. `sendFullUpdate()` - ligne 2444
4. `handleRemoteState()` - ligne 1476
5. `checkCriticalChanges()` - ligne 2159

**Dépendances**: WebClient, ArduinoJson

---

### Module 5: SLEEP (Gestion Sommeil) - 8 méthodes

**Responsabilité**: Logique sommeil adaptatif et marées

```cpp
// automatism_sleep.h
class AutomatismSleep {
public:
    AutomatismSleep(PowerManager& power, ConfigManager& cfg);
    
    void handleAutoSleep(const SensorReadings& r);
    bool shouldEnterSleepEarly(const SensorReadings& r);
    void handleMaree(const SensorReadings& r);
    
    // Détection activité
    bool hasSignificantActivity();
    void updateActivityTimestamp();
    void notifyWebActivity();
    
    // Calculs adaptatifs
    uint32_t calculateAdaptiveSleepDelay();
    bool isNightTime();
    bool hasRecentErrors();
    
    // Validation système
    bool verifySystemStateAfterWakeup();
    bool validateSystemStateBeforeSleep();
    void detectSleepAnomalies();
    
private:
    PowerManager& _power;
    ConfigManager& _config;
    
    unsigned long _lastWakeMs = 0;
    unsigned long _lastActivityMs = 0;
    unsigned long _lastWebActivityMs = 0;
    bool _hasRecentErrors = false;
    uint8_t _consecutiveWakeupFailures = 0;
    
    struct SleepConfig {
        uint32_t minSleepTime;
        uint32_t maxSleepTime;
        uint32_t normalSleepTime;
        // etc.
    } _sleepConfig;
};
```

**Méthodes à extraire**:
1. `handleAutoSleep()` - ligne 2754
2. `shouldEnterSleepEarly()` - ligne 2691
3. `handleMaree()` - ligne 1177
4. `hasSignificantActivity()` - ligne 3221
5. `updateActivityTimestamp()` - ligne 3232
6. `notifyLocalWebActivity()` - ligne 3243
7. `logActivity()` - ligne 3238
8. `calculateAdaptiveSleepDelay()` - ligne 3255
9. `isNightTime()` - ligne 3288
10. `hasRecentErrors()` - ligne 3301
11. `verifySystemStateAfterWakeup()` - ligne 3305
12. `detectSleepAnomalies()` - ligne 3349
13. `validateSystemStateBeforeSleep()` - ligne 3371

**Dépendances**: PowerManager, ConfigManager

---

### Module 6: CORE (Orchestration) - Reste

**Responsabilité**: Update loop principale et coordination modules

```cpp
// automatism_core.h (devient automatism.h)
class Automatism {
public:
    Automatism(SystemSensors& s, SystemActuators& a, WebClient& w, 
               DisplayView& d, PowerManager& p, Mailer& m, ConfigManager& c);
    
    void begin();
    void update();  // Update sans paramètres (lecture interne)
    void update(const SensorReadings& r);  // Update avec readings fournis
    void updateDisplay();
    uint32_t getRecommendedDisplayIntervalMs();
    
    // Accesseurs modules
    AutomatismFeeding& feeding() { return _feeding; }
    AutomatismActuators& actuators() { return _actuators; }
    AutomatismNetwork& network() { return _network; }
    AutomatismSleep& sleep() { return _sleep; }
    
    // Getters état
    bool isPumpAquaLocked() const;
    bool isTankPumpLocked() const;
    bool isEmailEnabled() const;
    bool getForceWakeUp() const;
    const String& getEmailAddress() const;
    uint16_t getFeedBigDur() const;
    uint16_t getFeedSmallDur() const;
    time_t getCurrentTime();
    String getCurrentTimeString();
    uint32_t getPumpStartTime() const;
    
    // Actions publiques (déléguées aux modules)
    void manualFeedSmall() { _feeding.manualFeedSmall(); }
    void manualFeedBig() { _feeding.manualFeedBig(); }
    void startAquaPumpManualLocal() { _actuators.startAquaPumpManual(); }
    void stopAquaPumpManualLocal() { _actuators.stopAquaPumpManual(); }
    // etc.
    
private:
    // Références composants
    SystemSensors& _sensors;
    SystemActuators& _acts;
    WebClient& _web;
    DisplayView& _disp;
    PowerManager& _power;
    Mailer& _mailer;
    ConfigManager& _config;
    
    // Modules
    AutomatismFeeding _feeding;
    AutomatismActuators _actuators;
    AutomatismNetwork _network;
    AutomatismSleep _sleep;
    AutomatismPersistence _persistence;  // Static methods
    
    // Logique orchestration
    void handleRefill(const SensorReadings& r);
    void handleAlerts(const SensorReadings& r);
    
    // État central
    SensorReadings _lastReadings;
    bool serverOk;
    int8_t sendState, recvState;
    // etc.
};
```

**Méthodes gardées dans core**:
1. `begin()` - ligne 430
2. `update()` - ligne 614
3. `update(const SensorReadings&)` - ligne 782
4. `updateDisplay()` - ligne 619
5. `getRecommendedDisplayIntervalMs()` - ligne 775
6. `handleRefill()` - ligne 908
7. `handleAlerts()` - ligne 1369
8. `triggerResetMode()` - ligne 388

**Dépendances**: Tous les modules

---

## 📝 PLAN D'IMPLÉMENTATION

### Étape 1: Préparation (1 heure)

#### 1.1 Créer structure répertoires
```bash
mkdir -p src/automatism
```

#### 1.2 Créer fichiers modules (vides)
```bash
# Module Persistence
touch src/automatism/automatism_persistence.h
touch src/automatism/automatism_persistence.cpp

# Module Actuators
touch src/automatism/automatism_actuators.h
touch src/automatism/automatism_actuators.cpp

# Module Feeding
touch src/automatism/automatism_feeding.h
touch src/automatism/automatism_feeding.cpp

# Module Network
touch src/automatism/automatism_network.h
touch src/automatism/automatism_network.cpp

# Module Sleep
touch src/automatism/automatism_sleep.h
touch src/automatism/automatism_sleep.cpp

# Core reste automatism.h/cpp
```

#### 1.3 Backup fichiers originaux
```bash
cp src/automatism.cpp src/automatism.cpp.backup
cp include/automatism.h include/automatism.h.backup
```

---

### Étape 2: Module PERSISTENCE (2 heures)

**Ordre**: Commencer par le plus simple

#### 2.1 Créer header
```cpp
// src/automatism/automatism_persistence.h
#pragma once
#include <Arduino.h>
#include <Preferences.h>

class AutomatismPersistence {
public:
    // Snapshot actionneurs pour sleep/wake
    static void saveActuatorSnapshot(bool pumpAqua, bool heater, bool light);
    static bool loadActuatorSnapshot(bool& pumpAqua, bool& heater, bool& light);
    static void clearActuatorSnapshot();
};
```

#### 2.2 Implémenter .cpp
- Copier méthodes lignes 47-76 de automatism.cpp
- Adapter namespace (Automatism:: → AutomatismPersistence::)
- Tester compilation

#### 2.3 Mettre à jour automatism.cpp
- Remplacer implémentations par:
```cpp
void Automatism::saveActuatorSnapshotToNVS(bool a, bool h, bool l) {
    AutomatismPersistence::saveActuatorSnapshot(a, h, l);
}
```

---

### Étape 3: Module ACTUATORS (6 heures)

**Plus complexe**: Gère WebSocket, tâches async, retry logic

#### 3.1 Créer header avec structure complète

#### 3.2 Extraire 10 méthodes une par une
- startAquaPumpManualLocal
- stopAquaPumpManualLocal
- startTankPumpManual
- stopTankPumpManual
- startHeaterManualLocal
- stopHeaterManualLocal
- startLightManualLocal
- stopLightManualLocal
- toggleEmailNotifications
- toggleForceWakeup

#### 3.3 Factoriser code dupliqué
**Observation**: Patterns similaires dans start/stop pompes

```cpp
// Pattern dupliqué (lignes 78-122 et 124-171)
void start/stopXxxManualLocal() {
    // 1. Action immédiate
    _acts.startXxx();
    
    // 2. WebSocket immédiat
    realtimeWebSocket.broadcastNow();
    
    // 3. Sync serveur async
    xTaskCreate([](void* param) {
        // ... 13 lignes similaires
    }, "sync_xxx", 4096, &handle, 1, NULL);
}
```

**Factorisation**: Créer méthode helper
```cpp
private:
    void syncActuatorStateAsync(const char* taskName, 
                                const char* extraParams,
                                TaskHandle_t& handle);
```

---

### Étape 4: Module FEEDING (6 heures)

#### 4.1 Analyser logique feeding
- Heures feeding (matin/midi/soir)
- Flags bouffe (bouffeMatin/Midi/SoirOk)
- Durées servos (bigDur, smallDur)
- Phases feeding (FORWARD/BACKWARD)

#### 4.2 Extraire 8 méthodes
- handleFeeding
- manualFeedSmall
- manualFeedBig
- createFeedingMessage
- traceFeedingEvent
- traceFeedingEventSelective
- checkNewDay
- saveFeedingState

#### 4.3 Migrer variables membres
- feedMorning, feedNoon, feedEvening
- feedBigDur, feedSmallDur
- FeedingPhase enum
- Flags nouriage

---

### Étape 5: Module NETWORK (8 heures)

**Le plus complexe**: Gère HTTP, JSON, retry, backoff

#### 5.1 Analyser dépendances
- WebClient (HTTP)
- ArduinoJson (parsing)
- Timing (intervals)

#### 5.2 Extraire 5 méthodes
- fetchRemoteState
- applyConfigFromJson
- sendFullUpdate
- handleRemoteState
- checkCriticalChanges

#### 5.3 Migrer variables remote
- limFlood, aqThresholdCm, tankThresholdCm, heaterThresholdC
- emailAddress, emailEnabled
- freqWakeSec
- serverOk, sendState, recvState

#### 5.4 Simplifier tâches async
Actuellement lignes 101-122, 148-171 - code dupliqué
Créer helper `syncStateAsync()`

---

### Étape 6: Module SLEEP (8 heures)

**Très complexe**: Logique adaptative, marées, activité

#### 6.1 Extraire 13 méthodes sleep
- handleAutoSleep (ligne 2754 - ÉNORME 443 lignes !)
- shouldEnterSleepEarly
- handleMaree
- hasSignificantActivity
- updateActivityTimestamp
- notifyLocalWebActivity
- logActivity
- calculateAdaptiveSleepDelay
- isNightTime
- hasRecentErrors
- verifySystemStateAfterWakeup
- detectSleepAnomalies
- validateSystemStateBeforeSleep

#### 6.2 Migrer variables sleep
- _lastWakeMs
- _lastActivityMs
- _lastWebActivityMs
- _hasRecentErrors
- _consecutiveWakeupFailures
- _sleepConfig struct

#### 6.3 Simplifier handleAutoSleep
**Problème**: 443 lignes dans 1 méthode !
Diviser en sous-méthodes:
- validateConditions()
- saveSystemState()
- performSleep()
- restoreSystemState()

---

### Étape 7: Module CORE Refactoré (4 heures)

#### 7.1 Nettoyer automatism.cpp
- Garder seulement orchestration
- begin(), update(), updateDisplay()
- handleRefill(), handleAlerts()

#### 7.2 Migrer vers composition
```cpp
// AVANT (tout dans Automatism)
void Automatism::manualFeedSmall() {
    // 44 lignes de code...
}

// APRÈS (délégation)
void Automatism::manualFeedSmall() {
    _feeding.manualFeedSmall();
}
```

#### 7.3 Réorganiser automatism.h
- Déclarer modules membres
- Méthodes publiques = délégations
- Variables privées = état orchestration

---

### Étape 8: Tests et Validation (1 jour)

#### 8.1 Compilation incrémentale
Après chaque module:
```bash
pio run -e wroom-test
```

#### 8.2 Tests fonctionnels
- [ ] Nourrissage manuel (small/big)
- [ ] Pompes (aqua/réserve start/stop)
- [ ] Chauffage/Lumière toggle
- [ ] Sync serveur distant
- [ ] WebSocket updates temps réel
- [ ] Sommeil adaptatif
- [ ] Marées détection
- [ ] Email notifications

#### 8.3 Tests mémoire
```bash
# Vérifier heap
pio device monitor

# Observer logs
[App] Heap libre: XXX bytes
```

#### 8.4 Monitoring 90s obligatoire
```bash
# Après flash complet
pio run -e wroom-test -t upload
pio run -e wroom-test -t uploadfs
pio device monitor
# Observer 90 secondes
```

---

### Étape 9: Documentation (4 heures)

#### 9.1 Créer docs/architecture/automatism.md
- Expliquer nouvelle architecture
- Diagramme modules
- Responsabilités chacun

#### 9.2 Mettre à jour docs/README.md
- Ajouter section refactoring
- Liens vers modules

#### 9.3 Créer PHASE_2_COMPLETE.md
- Ce qui a été fait
- Métriques avant/après
- Gains mesurés

---

## ⚠️ RISQUES ET MITIGATION

### Risque 1: Dépendances circulaires
**Problème**: Module A appelle Module B qui appelle Module A

**Mitigation**:
- Forward declarations
- Interfaces (abstract classes)
- Event system (callbacks)

### Risque 2: État partagé
**Problème**: Variables utilisées par plusieurs modules

**Mitigation**:
- Passer par référence (Core → Modules)
- Getters/Setters explicites
- Documentation claire ownership

### Risque 3: Régressions fonctionnelles
**Problème**: Logique cassée pendant refactoring

**Mitigation**:
- Commits fréquents (1 par module)
- Tests après chaque module
- Backup (automatism.cpp.backup)

### Risque 4: Overhead performance
**Problème**: Appels méthodes supplémentaires

**Mitigation**:
- Inline petites méthodes
- Pas de virtualité (pas de vtable)
- Mesurer avant/après

---

## 📊 ESTIMATION DÉTAILLÉE

### Par Module

| Module | Méthodes | Lignes estim. | Complexité | Durée |
|--------|----------|---------------|------------|-------|
| Persistence | 3 | ~100 | ⚙️ Simple | 2h |
| Actuators | 10 | ~600 | ⚙️⚙️ Moyen | 6h |
| Feeding | 8 | ~500 | ⚙️⚙️ Moyen | 6h |
| Network | 5 | ~700 | ⚙️⚙️⚙️ Complexe | 8h |
| Sleep | 13 | ~800 | ⚙️⚙️⚙️⚙️ Très complexe | 8h |
| Core | 8 | ~700 | ⚙️⚙️ Moyen | 4h |
| Tests | - | - | - | 8h |
| Docs | - | - | - | 4h |

**Total**: 46 heures = 5-6 jours (8h/jour)

### Par Jour

**Jour 1** (8h):
- Étape 1: Préparation (1h)
- Étape 2: Module Persistence (2h)
- Étape 3: Module Actuators (5h)

**Jour 2** (8h):
- Étape 3: Module Actuators fin (1h)
- Étape 4: Module Feeding (6h)
- Tests partiels (1h)

**Jour 3** (8h):
- Étape 5: Module Network (8h)

**Jour 4** (8h):
- Étape 6: Module Sleep (8h)

**Jour 5** (8h):
- Étape 7: Core refactoré (4h)
- Étape 8: Tests complets (4h)

**Jour 6** (optionnel, 4h):
- Étape 9: Documentation (4h)
- Polishing final

---

## 🚀 DÉMARRAGE PHASE 2

### Actions Immédiates (Maintenant)

**Je vais créer**:
1. Structure répertoires
2. Fichiers headers/cpp vides
3. Module Persistence (le plus simple en premier)
4. Tests compilation incrémentaux

**Prêt à démarrer** ?

---

**Document**: Plan détaillé Phase 2  
**Durée estimée**: 3-5 jours (24-40h)  
**Impact**: Maintenabilité +300%  
**Statut**: ⏳ PRÊT À DÉMARRER


