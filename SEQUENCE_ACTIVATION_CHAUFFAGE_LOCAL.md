# 🔥 Séquence d'Activation Chauffage - Interface Web Locale

**Version**: 11.35  
**Date**: 14 Octobre 2025  
**Type**: Documentation technique  

---

## 📋 Vue d'Ensemble

**Durée totale**: ~100-500 ms (activation immédiate + sync arrière-plan)  
**Priorité**: LOCALE (serveur distant bloqué 5s)  
**Architecture**: Multi-couches avec délégation

---

## 🔄 Séquence Complète

### PHASE 1: Réception Requête Web (0-10 ms)

#### Étape 1.1: Interface Web → ESP32
```javascript
// Frontend (data/www/*.html)
fetch('/control?relay=heater')
  .then(response => response.text())
  .then(result => console.log("Heater: " + result));
```

#### Étape 1.2: ESP32 Reçoit Requête HTTP
```cpp
// Fichier: src/web_server.cpp, ligne 535
AsyncWebServer → endpoint "/control"

if (req->hasParam("relay")) {
    String rel = req->getParam("relay")->value();
    // rel = "heater"
    
    Serial.printf("[Web] 🔌 Relay control: %s\n", rel.c_str());
    // Log: [Web] 🔌 Relay control: heater
}
```

---

### PHASE 2: Traitement Commande (10-50 ms)

#### Étape 2.1: Vérification État Actuel
```cpp
// Fichier: src/web_server.cpp, lignes 603-618
else if (rel == "heater") {
    bool newState;
    
    // Vérifier état actuel
    if (_acts.isHeaterOn()) {
        // Chauffage déjà ON → l'éteindre
        Serial.println("[Web] 🔥 Stopping heater...");
        autoCtrl.stopHeaterManualLocal();
        newState = false;
        resp = "HEATER OFF";
    }
    else {
        // Chauffage OFF → l'allumer 🔥
        Serial.println("[Web] 🔥 Starting heater...");
        autoCtrl.startHeaterManualLocal();  // ← APPEL PRINCIPAL
        newState = true;
        resp = "HEATER ON";
        Serial.println("[Web] ✅ Heater started");
    }
}
```

**Logs produits**:
```
[Web] 🔥 Starting heater...
[Web] ✅ Heater started
```

#### Étape 2.2: Délégation vers Automatism
```cpp
// Fichier: src/automatism.cpp, lignes 82-86
void Automatism::startHeaterManualLocal() {
    // Délégation au module Actuators
    AutomatismActuators actuators(_acts, _config);
    actuators.startHeaterManual();  // ← DÉLÉGATION
}
```

#### Étape 2.3: Traitement par AutomatismActuators
```cpp
// Fichier: src/automatism/automatism_actuators.cpp, lignes 172-187
void AutomatismActuators::startHeaterManual() {
    Serial.println(F("[Actuators] 🔥 Activation manuelle chauffage (local)..."));
    
    // 1. ACTIVATION IMMÉDIATE GPIO
    _acts.startHeater();  // ← ACTIVATION MATÉRIELLE
    
    // 2. SAUVEGARDE NVS IMMÉDIATE
    AutomatismPersistence::saveCurrentActuatorState("heater", true);
    
    // 3. WEBSOCKET IMMÉDIAT (feedback UI)
    realtimeWebSocket.broadcastNow();
    
    // 4. SYNCHRONISATION SERVEUR (arrière-plan)
    syncActuatorStateAsync("sync_heater_start", "etatHeat=1",
                          _syncHeaterStartHandle, "chauffage activé");
}
```

**Logs produits**:
```
[Actuators] 🔥 Activation manuelle chauffage (local)...
```

---

### PHASE 3: Activation GPIO Matérielle (50-60 ms)

#### Étape 3.1: Activation GPIO via SystemActuators
```cpp
// Fichier: src/system_actuators.cpp, ligne 58
void SystemActuators::startHeater() {
    heater.on();  // ← Appel HeaterController
    LOG(LOG_INFO, "Heater ON");
    EventLog::add("Heater ON");
}
```

#### Étape 3.2: Contrôle GPIO Physique
```cpp
// Fichier: include/actuators.h, lignes 19-28
class HeaterController {
public:
    HeaterController(int gpio) : _gpio(gpio) {
        pinMode(_gpio, OUTPUT);
        off();
    }
    
    void on() {
        digitalWrite(_gpio, HIGH);  // ← ACTIVATION GPIO RÉELLE
        _state = true;
        LOG(LOG_INFO, "Heater GPIO%d ON", _gpio);
    }

private:
    int _gpio;    // GPIO 2 (Pins::RADIATEURS)
    bool _state;
};
```

**Logs produits**:
```
[09:50:XX][INFO] Heater GPIO2 ON
[09:50:XX][INFO] Heater ON
```

**Action physique**: 
- ⚡ GPIO 2 passe à HIGH (3.3V)
- 🔌 Relais s'active
- 🔥 Chauffage démarre physiquement

---

### PHASE 4: Sauvegarde NVS Priorité Locale (60-80 ms)

#### Étape 4.1: Sauvegarde État Actuel
```cpp
// Fichier: src/automatism/automatism_persistence.cpp, lignes 59-71
void AutomatismPersistence::saveCurrentActuatorState(const char* actuator, bool state) {
    Preferences prefs;
    prefs.begin("actState", false);  // Namespace "actState"
    
    prefs.putBool(actuator, state);  // "heater" = true
    prefs.putUInt("lastLocal", millis());  // Timestamp action locale
    
    prefs.end();
    
    Serial.printf("[Persistence] État %s=%s sauvegardé en NVS (priorité locale)\n",
                  actuator, state ? "ON" : "OFF");
}
```

**Logs produits**:
```
[Persistence] État heater=ON sauvegardé en NVS (priorité locale)
```

**NVS écrit**:
- Namespace: `actState`
- Clé: `heater` = `true`
- Clé: `lastLocal` = `millis()` (ex: 60000)

#### Étape 4.2: Marquer Sync En Attente
```cpp
// Fichier: src/web_server.cpp, lignes 620-622
// PRIORITÉ LOCALE (v11.32): Sauvegarde NVS + pending sync
AutomatismPersistence::saveCurrentActuatorState("heater", newState);
AutomatismPersistence::markPendingSync("heater", newState);
```

```cpp
// Fichier: src/automatism/automatism_persistence.cpp, lignes 103-141
void AutomatismPersistence::markPendingSync(const char* actuator, bool state) {
    Preferences prefs;
    prefs.begin("pendingSync", false);  // Namespace "pendingSync"
    
    // Sauvegarder l'état
    prefs.putBool(actuator, state);  // "heater" = true
    
    // Ajouter à la liste des items pending
    uint8_t count = prefs.getUChar("count", 0);
    
    // Si pas déjà dans la liste
    if (!alreadyPending) {
        char key[16];
        snprintf(key, sizeof(key), "item_%u", count);
        prefs.putString(key, actuator);  // "item_0" = "heater"
        count++;
        prefs.putUChar("count", count);  // count = 1
    }
    
    prefs.putUInt("lastSync", millis());  // Timestamp sync
    prefs.end();
    
    Serial.printf("[Persistence] ⏳ Pending sync marqué: %s=%s (total: %u)\n",
                  actuator, state ? "ON" : "OFF", count);
}
```

**Logs produits**:
```
[Persistence] ⏳ Pending sync marqué: heater=ON (total: 1)
```

**NVS écrit**:
- Namespace: `pendingSync`
- Clé: `heater` = `true`
- Clé: `count` = `1`
- Clé: `item_0` = `"heater"`
- Clé: `lastSync` = `millis()`

---

### PHASE 5: Feedback Temps Réel WebSocket (80-100 ms)

#### Étape 5.1: Broadcast Temps Réel
```cpp
// Fichier: src/automatism/automatism_actuators.cpp, ligne 182
realtimeWebSocket.broadcastNow();
```

**Action**:
- 📡 Envoie JSON complet à tous les clients WebSocket connectés
- 🔄 Interface web se met à jour instantanément
- 🎨 Bouton chauffage change d'état visuellement

**Données envoyées** (JSON):
```json
{
  "tempAir": 28.0,
  "humidity": 61.0,
  "tempWater": 28.3,
  "wlPota": 209,
  "wlAqua": 169,
  "wlTank": 209,
  "luminosite": 957,
  "pumpAqua": false,
  "pumpTank": false,
  "heater": true,     ← NOUVEAU ÉTAT
  "light": true,
  "diffMaree": 40
}
```

#### Étape 5.2: Confirmation Spécifique Action
```cpp
// Fichier: src/web_server.cpp, ligne 634
realtimeWebSocket.sendActionConfirm("heater", resp);
```

**Action**:
- ✅ Envoie confirmation spécifique pour éviter timeout
- 📨 Message: `{type: "action_confirm", action: "heater", result: "HEATER ON"}`

---

### PHASE 6: Synchronisation Serveur Distant (100-500 ms)

#### Étape 6.1: Tentative Envoi Immédiat
```cpp
// Fichier: src/web_server.cpp, lignes 624-631
// Envoi immédiat au serveur distant
bool syncSuccess = autoCtrl.sendFullUpdate(_sensors.read());

if (syncSuccess) {
    AutomatismPersistence::clearPendingSync("heater");
    Serial.println("[Web] ✅ Heater synced to server");
} else {
    Serial.println("[Web] ⏳ Heater sync pending (will retry)");
}
```

**Cas 1: Succès Immédiat** ✅
```
[Web] ✅ Heater synced to server
[Persistence] ✅ Pending sync effacé: heater (reste: 0)
```

**Cas 2: Échec → Sync Arrière-Plan** ⏳
```
[Web] ⏳ Heater sync pending (will retry)
[Actuators] 🌐 Synchronisation serveur en arrière-plan...
```

#### Étape 6.2: Sync Asynchrone (si nécessaire)
```cpp
// Fichier: src/automatism/automatism_actuators.cpp, lignes 26-79
void AutomatismActuators::syncActuatorStateAsync(...) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[Actuators] ⚠️ Pas de WiFi - %s localement uniquement\n", actionDesc);
        return;
    }
    
    // Créer tâche FreeRTOS dédiée
    BaseType_t result = xTaskCreate([](void* param) {
        SyncParams* p = (SyncParams*)param;
        
        vTaskDelay(pdMS_TO_TICKS(50));  // Petit délai
        
        // Lecture capteurs fraîche
        extern Automatism autoCtrl;
        SensorReadings freshReadings = autoCtrl.readSensors();
        
        // Envoi complet avec état chauffage
        bool success = autoCtrl.sendFullUpdate(freshReadings, p->extraParams);
        //                                      extraParams = "etatHeat=1"
        
        if (success) {
            Serial.printf("[Actuators] ✅ Sync serveur OK - %s\n", p->actionDesc);
        } else {
            Serial.printf("[Actuators] ⚠️ Sync serveur échec - %s\n", p->actionDesc);
        }
        
        *(p->handle) = nullptr;
        delete p;
        vTaskDelete(NULL);  // Tâche se supprime elle-même
    }, taskName, 4096, params, 1, &taskHandle);
}
```

**Logs produits**:
```
[Actuators] 🌐 Synchronisation serveur en arrière-plan...
[Actuators] ✅ Sync serveur OK - chauffage activé
```

#### Étape 6.3: Construction Payload HTTP
```cpp
// Fichier: src/automatism/automatism_network.cpp, lignes 151-169
bool AutomatismNetwork::sendFullUpdate(...) {
    char data[1024];
    snprintf(data, sizeof(data),
        "api_key=%s&sensor=%s&version=%s&"
        "TempAir=%.1f&Humidite=%.1f&TempEau=%.1f&"
        "EauPotager=%u&EauAquarium=%u&EauReserve=%u&"
        "diffMaree=%d&Luminosite=%u&"
        "etatPompeAqua=%d&etatPompeTank=%d&"
        "etatHeat=%d&"           // ← État chauffage
        "etatUV=%d&"
        "bouffeMatin=%u&bouffeMidi=%u&bouffeSoir=%u&"
        "tempsGros=%u&tempsPetits=%u&"
        "aqThreshold=%u&tankThreshold=%u&chauffageThreshold=%.1f&"
        "tempsRemplissageSec=%u&limFlood=%u&"
        "WakeUp=%d&FreqWakeUp=%u&"
        "bouffePetits=%s&bouffeGros=%s&mail=%s&mailNotif=%s",
        Config::API_KEY, Config::SENSOR, Config::VERSION,
        tempAir, humidity, tempWater,
        readings.wlPota, readings.wlAqua, readings.wlTank,
        diffMaree, readings.luminosite,
        acts.isAquaPumpRunning(), 
        acts.isTankPumpRunning(),
        acts.isHeaterOn(),       // ← TRUE maintenant
        acts.isLightOn(),
        feedMorning, feedNoon, feedEvening,
        feedBigDur, feedSmallDur,
        _aqThresholdCm, _tankThresholdCm, _heaterThresholdC,
        refillDurationSec, _limFlood,
        forceWakeUp ? 1 : 0, freqWakeSec,
        bouffePetits.c_str(), bouffeGros.c_str(),
        _emailAddress.c_str(), _emailEnabled ? "checked" : ""
    );
    
    // Ajouter paramètre extra si fourni
    String payload = String(data);
    if (extraPairs && extraPairs[0] != '\0') {
        payload += "&";
        payload += extraPairs;  // "etatHeat=1" (redondant mais explicite)
    }
    
    // Envoi POST
    bool success = _web.postRaw(payload, false);
    return success;
}
```

**Payload HTTP complet**:
```
api_key=fdGTMoptd5CD2ert3
&sensor=esp32-wroom
&version=11.35
&TempAir=28.0
&Humidite=61.0
&TempEau=28.3
&EauPotager=209
&EauAquarium=169
&EauReserve=209
&diffMaree=40
&Luminosite=957
&etatPompeAqua=0
&etatPompeTank=0
&etatHeat=1         ← État chauffage ON
&etatUV=1
&bouffeMatin=8
&bouffeMidi=12
&bouffeSoir=19
&tempsGros=2
&tempsPetits=2
&aqThreshold=18
&tankThreshold=80
&chauffageThreshold=18.0
&tempsRemplissageSec=5
&limFlood=8
&WakeUp=0
&FreqWakeUp=6
&bouffePetits=0
&bouffeGros=0
&mail=oliv.arn.lau@gmail.com
&mailNotif=
&etatHeat=1         ← Extra param (redondant mais explicite)
&resetMode=0
```

#### Étape 6.4: Envoi HTTP POST
```cpp
// Fichier: src/web_client.cpp, lignes 85-154
bool WebClient::postRaw(const String& payload, bool block) {
    HTTPClient http;
    
    // URL: http://iot.olution.info/ffp3/post-data-test
    http.begin(String(Config::SERVER_POST_DATA));
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);  // 30s
    
    // Désactiver modem sleep pour stabilité
    WiFi.setSleep(false);
    
    Serial.printf("[HTTP] → %s (%u bytes)\n", Config::SERVER_POST_DATA, payload.length());
    Serial.printf("[HTTP] payload: %s\n", payload.c_str());
    
    // Envoi POST avec retry automatique (jusqu'à 3 tentatives)
    int code = http.POST(payload);
    
    Serial.printf("[HTTP] ← code %d, %d bytes\n", code, http.getSize());
    
    http.end();
    
    return (code >= 200 && code < 400);
}
```

**Logs produits**:
```
[HTTP] → http://iot.olution.info/ffp3/post-data-test (489 bytes)
[HTTP] payload: api_key=...&etatHeat=1&...
[HTTP] 🌐 Using HTTP (attempt 1/3)
[HTTP] Sending POST... (timeout: 30000 ms)
[HTTP] ← code 200, 35 bytes
[HTTP] === DÉBUT RÉPONSE ===
Données enregistrées avec succès
[HTTP] === FIN RÉPONSE ===
```

---

### PHASE 7: Confirmation & Nettoyage (400-500 ms)

#### Étape 7.1: Effacement Pending Sync
```cpp
// Fichier: src/web_server.cpp, lignes 626-628
if (syncSuccess) {
    AutomatismPersistence::clearPendingSync("heater");
    Serial.println("[Web] ✅ Heater synced to server");
}
```

```cpp
// Fichier: src/automatism/automatism_persistence.cpp, lignes 178-212
void AutomatismPersistence::clearPendingSync(const char* actuator) {
    Preferences prefs;
    prefs.begin("pendingSync", false);
    
    // Supprimer l'état
    prefs.remove(actuator);  // Remove "heater"
    
    // Retirer de la liste
    uint8_t count = prefs.getUChar("count", 0);
    uint8_t newCount = 0;
    
    // Renuméroter les items restants
    for (uint8_t i = 0; i < count; i++) {
        char oldKey[16];
        snprintf(oldKey, sizeof(oldKey), "item_%u", i);
        String item = prefs.getString(oldKey, "");
        
        if (item != String(actuator) && item.length() > 0) {
            if (newCount != i) {
                char newKey[16];
                snprintf(newKey, sizeof(newKey), "item_%u", newCount);
                prefs.putString(newKey, item);
            }
            newCount++;
        } else {
            prefs.remove(oldKey);
        }
    }
    
    prefs.putUChar("count", newCount);
    prefs.end();
    
    Serial.printf("[Persistence] ✅ Pending sync effacé: %s (reste: %u)\n", 
                  actuator, newCount);
}
```

**Logs produits**:
```
[Web] ✅ Heater synced to server
[Persistence] ✅ Pending sync effacé: heater (reste: 0)
```

#### Étape 7.2: Réponse HTTP au Client
```cpp
// Fichier: src/web_server.cpp, lignes 670-680
if (resp == "") resp = "OK";

Serial.printf("[Web] 📤 Sending response: %s\n", resp.c_str());

// Réponse avec headers anti-cache
AsyncWebServerResponse* response = req->beginResponse(200, "text/plain", resp);
response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
response->addHeader("Pragma", "no-cache");
response->addHeader("Expires", "0");
req->send(response);
```

**Logs produits**:
```
[Web] 📤 Sending response: HEATER ON
```

**Réponse HTTP**:
```
HTTP/1.1 200 OK
Content-Type: text/plain
Cache-Control: no-cache, no-store, must-revalidate
Pragma: no-cache
Expires: 0

HEATER ON
```

---

## 🛡️ Protection Priorité Locale (5 secondes)

### Blocage Commandes Distantes

Pendant 5 secondes après l'action locale, **toute commande du serveur distant est IGNORÉE** :

```cpp
// Fichier: src/automatism/automatism_network.cpp, lignes 498-518
void AutomatismNetwork::handleRemoteActuators(...) {
    const uint32_t LOCAL_PRIORITY_TIMEOUT_MS = 5000;  // 5 secondes
    
    // Vérifier si sync en attente
    if (AutomatismPersistence::hasPendingSync()) {
        uint32_t pendingCount = AutomatismPersistence::getPendingSyncCount();
        uint32_t elapsed = millis() - AutomatismPersistence::getLastPendingSyncTime();
        
        Serial.printf("[Network] ⏳ PRIORITÉ LOCALE - Sync en attente (%u items, %lu ms écoulées)\n", 
                      pendingCount, elapsed);
        return;  // ← IGNORE commandes distantes
    }
    
    // OU vérifier si action locale récente
    if (AutomatismPersistence::hasRecentLocalAction(LOCAL_PRIORITY_TIMEOUT_MS)) {
        uint32_t elapsed = millis() - AutomatismPersistence::getLastLocalActionTime();
        
        Serial.printf("[Network] ⚠️ PRIORITÉ LOCALE ACTIVE - Action récente (%lu ms écoulées)\n", elapsed);
        return;  // ← IGNORE commandes distantes
    }
    
    // Sinon, traiter normalement les commandes distantes
    // ...
}
```

**Logs produits** (si serveur distant tente commande):
```
[Network] ⏳ PRIORITÉ LOCALE - Sync en attente (1 items, 557 ms écoulées)
```

---

## 📊 Diagramme de Séquence Complet

```
┌─────────┐     ┌──────────┐     ┌────────────┐     ┌─────────────┐     ┌──────┐     ┌─────────┐
│Interface│     │WebServer │     │ Automatism │     │  Actuators  │     │ GPIO │     │Serveur  │
│   Web   │     │  ESP32   │     │            │     │             │     │  2   │     │ Distant │
└────┬────┘     └─────┬────┘     └──────┬─────┘     └──────┬──────┘     └──┬───┘     └────┬────┘
     │                │                  │                   │                │              │
     │ 1. Click      │                  │                   │                │              │
     │  "Chauffage"  │                  │                   │                │              │
     ├──────────────>│                  │                   │                │              │
     │                │                  │                   │                │              │
     │                │ 2. /control?    │                   │                │              │
     │                │    relay=heater │                   │                │              │
     │                ├─────────────────┤                   │                │              │
     │                │                  │                   │                │              │
     │                │ 3. startHeaterManualLocal()         │                │              │
     │                ├─────────────────>│                   │                │              │
     │                │                  │                   │                │              │
     │                │                  │ 4. startHeaterManual()            │              │
     │                │                  ├──────────────────>│                │              │
     │                │                  │                   │                │              │
     │                │                  │                   │ 5. _acts.startHeater()       │
     │                │                  │                   ├───────────────>│              │
     │                │                  │                   │                │              │
     │                │                  │                   │     heater.on() │              │
     │                │                  │                   │     digitalWrite(2, HIGH)     │
     │                │                  │                   │                ├─> ⚡ GPIO ON │
     │                │                  │                   │                │              │
     │                │                  │                   │ 6. saveNVS(heater=ON)        │
     │                │                  │                   ├──> NVS         │              │
     │                │                  │                   │    actState    │              │
     │                │                  │                   │    heater=true │              │
     │                │                  │                   │                │              │
     │                │                  │ 7. WebSocket broadcast (feedback UI)              │
     │<─────────────────────────────────┴───────────────────┤                │              │
     │ {heater: true}                   │                   │                │              │
     │                │                  │                   │                │              │
     │                │ 8. markPendingSync(heater)          │                │              │
     │                ├─────────────────>│                   │                │              │
     │                │                  ├──> NVS            │                │              │
     │                │                  │    pendingSync    │                │              │
     │                │                  │    heater=true    │                │              │
     │                │                  │    count=1        │                │              │
     │                │                  │                   │                │              │
     │                │ 9. sendFullUpdate(etatHeat=1)       │                │              │
     │                ├─────────────────>│───────────────────┤                │              │
     │                │                  │                   │                │              │
     │                │                  │ 10. HTTP POST (async task)         │              │
     │                │                  │                   │                │─────────────>│
     │                │                  │                   │                │  POST data   │
     │                │                  │                   │                │  etatHeat=1  │
     │                │                  │                   │                │              │
     │                │                  │                   │                │<─────────────┤
     │                │                  │                   │                │  200 OK      │
     │                │                  │                   │                │              │
     │                │ 11. clearPendingSync(heater)        │                │              │
     │                ├─────────────────>│                   │                │              │
     │                │                  ├──> NVS            │                │              │
     │                │                  │    pendingSync    │                │              │
     │                │                  │    count=0        │                │              │
     │                │                  │                   │                │              │
     │                │ 12. Response HTTP│                   │                │              │
     │<───────────────┤ "HEATER ON"     │                   │                │              │
     │                │                  │                   │                │              │
     │ UI Update ✓    │                  │                   │                │              │
     │                │                  │                   │                │              │
```

---

## 📝 Logs Complets Produits

### Séquence Succès Complète

```log
[Web] 🔌 Relay control: heater
[Web] 🔥 Starting heater...
[Actuators] 🔥 Activation manuelle chauffage (local)...
[09:50:15][INFO] Heater GPIO2 ON
[09:50:15][INFO] Heater ON
[Persistence] État heater=ON sauvegardé en NVS (priorité locale)
[Persistence] ⏳ Pending sync marqué: heater=ON (total: 1)
[HTTP] → http://iot.olution.info/ffp3/post-data-test (489 bytes)
[HTTP] payload: api_key=...&etatHeat=1&...
[HTTP] 🌐 Using HTTP (attempt 1/3)
[HTTP] Sending POST... (timeout: 30000 ms)
[HTTP] ← code 200, 35 bytes
[HTTP] === DÉBUT RÉPONSE ===
Données enregistrées avec succès
[HTTP] === FIN RÉPONSE ===
[Network] sendFullUpdate SUCCESS
[Web] ✅ Heater synced to server
[Persistence] ✅ Pending sync effacé: heater (reste: 0)
[Web] ✅ Heater started
[Web] 📤 Sending response: HEATER ON
```

### Timeline des Actions

| Temps | Action | Log Principal |
|-------|--------|---------------|
| **T+0ms** | Requête HTTP reçue | `[Web] 🔌 Relay control: heater` |
| **T+10ms** | Début activation | `[Web] 🔥 Starting heater...` |
| **T+20ms** | Délégation Actuators | `[Actuators] 🔥 Activation manuelle...` |
| **T+50ms** | ⚡ **GPIO ON** | `[INFO] Heater GPIO2 ON` |
| **T+60ms** | Sauvegarde NVS | `[Persistence] État heater=ON sauvegardé` |
| **T+70ms** | Pending sync | `[Persistence] ⏳ Pending sync marqué` |
| **T+80ms** | WebSocket broadcast | (JSON envoyé aux clients) |
| **T+100ms** | HTTP POST start | `[HTTP] → post-data-test` |
| **T+400ms** | HTTP response | `[HTTP] ← code 200` |
| **T+420ms** | Clear pending | `[Persistence] ✅ Pending sync effacé` |
| **T+450ms** | Confirmation web | `[Web] ✅ Heater started` |
| **T+500ms** | Response client | `[Web] 📤 Sending response` |

---

## 🎯 Points Clés Architecture

### 1. **Activation Immédiate** ⚡ (50ms)
- GPIO activé **AVANT** envoi serveur
- Feedback utilisateur instantané
- Pas de délai réseau

### 2. **Sauvegarde NVS** 💾 (60ms)
- Persistance immédiate
- Survit aux reboots
- Namespace `actState`

### 3. **Priorité Locale** 🛡️ (5s)
- Serveur distant bloqué 5 secondes
- Empêche conflits de commandes
- Garantit l'intention utilisateur local

### 4. **Sync Asynchrone** 🌐 (400ms)
- Tâche FreeRTOS dédiée
- N'affecte pas réactivité UI
- Retry automatique si échec

### 5. **Double Vérification** ✅
- État sauvegardé: NVS `actState`
- Pending sync: NVS `pendingSync`
- Si crash avant sync → retry au boot

---

## 🔄 Cas Particuliers

### Cas 1: Échec Envoi Serveur (WiFi down)

```
[Web] 🔥 Starting heater...
[Actuators] 🔥 Activation manuelle chauffage (local)...
[INFO] Heater GPIO2 ON  ← GPIO activé quand même ✓
[Persistence] État heater=ON sauvegardé
[Persistence] ⏳ Pending sync marqué
[Actuators] ⚠️ Pas de WiFi - chauffage activé localement uniquement
[Web] ⏳ Heater sync pending (will retry)
```

**Résultat**:
- ✅ Chauffage activé localement
- ✅ État sauvegardé NVS
- ⏳ Sync tenté au prochain cycle réseau

### Cas 2: Commande Redondante (déjà ON)

```
[Web] 🔌 Relay control: heater
[Web] 🔥 Stopping heater...  ← Car déjà ON
[Actuators] 🧊 Arrêt manuel chauffage (local)...
[INFO] Heater GPIO2 OFF
```

**Résultat**: Toggle automatique (ON → OFF)

### Cas 3: Serveur Distant Tente Commande (Priorité Locale Active)

```
[Network] ⏳ PRIORITÉ LOCALE - Sync en attente (1 items, 557 ms écoulées)
[DEBUG] Commande chauffage: GPIO 2 = OFF
[Auto] Chauffage GPIO commande IGNORÉE - état déjà ON (commande redondante)
```

**Résultat**: 
- ❌ Commande distante IGNORÉE
- ✅ État local préservé
- 🛡️ Priorité respectée

---

## 📊 Résumé

### Timing

| Phase | Durée | Bloquant |
|-------|-------|----------|
| **Réception web** | 0-10 ms | Oui |
| **Traitement** | 10-50 ms | Oui |
| **GPIO ON** | 50-60 ms | Oui |
| **NVS write** | 60-80 ms | Oui |
| **WebSocket** | 80-100 ms | Non |
| **HTTP POST** | 100-500 ms | **Non (async)** |
| **Total utilisateur** | **~100 ms** | ✅ Rapide |

### Sauvegarde

| Namespace NVS | Clé | Valeur | Rôle |
|---------------|-----|--------|------|
| `actState` | `heater` | `true` | État actuel |
| `actState` | `lastLocal` | `millis()` | Timestamp local |
| `pendingSync` | `heater` | `true` | À synchroniser |
| `pendingSync` | `count` | `1` | Nb items pending |
| `pendingSync` | `item_0` | `"heater"` | Liste items |
| `pendingSync` | `lastSync` | `millis()` | Timestamp pending |

### Architecture

```
Interface Web (HTTP)
    └─> WebServer (/control endpoint)
        └─> Automatism (startHeaterManualLocal)
            └─> AutomatismActuators (startHeaterManual)
                ├─> SystemActuators.startHeater() → GPIO ON ⚡
                ├─> AutomatismPersistence → NVS 💾
                ├─> RealtimeWebSocket → Feedback UI 📡
                └─> syncActuatorStateAsync → HTTP POST 🌐
                    └─> Serveur distant (async)
```

---

**Version**: 11.35  
**Type**: Documentation technique  
**Dernière MAJ**: 14 Octobre 2025


