#include "automatism_network.h"
#include "automatism.h"
#include "project_config.h"
#include "esp_task_wdt.h"
#include "pins.h"
#include <WiFi.h>

// ============================================================================
// Module: AutomatismNetwork
// Responsabilité: Communication avec serveur distant
// ============================================================================

AutomatismNetwork::AutomatismNetwork(WebClient& web, ConfigManager& cfg)
    : _web(web)
    , _config(cfg)
    , _emailAddress("")
    , _emailEnabled(false)
    , _freqWakeSec(600)  // 10 min par défaut
    , _limFlood(5)
    , _aqThresholdCm(30)
    , _tankThresholdCm(30)
    , _heaterThresholdC(24.0f)
    , _lastSend(0)
    , _lastRemoteFetch(0)
    , _serverOk(false)
    , _sendState(0)
    , _recvState(0)
    , _prevPumpTank(false)
    , _prevPumpAqua(false)
    , _prevBouffeMatin(false)
    , _prevBouffeMidi(false)
    , _prevBouffeSoir(false)
{
    Serial.println(F("[Network] Module initialisé"));
}

// ============================================================================
// COMMUNICATION SERVEUR - Méthodes Simples
// ============================================================================

bool AutomatismNetwork::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
    bool ok = _web.fetchRemoteState(doc);
    if (ok && doc.size() > 0) {
        String jsonStr;
        serializeJson(doc, jsonStr);
        _config.saveRemoteVars(jsonStr);
        _serverOk = true;
        _recvState = 1;  // OK
        Serial.println(F("[Network] État distant récupéré avec succès"));
    } else {
        _serverOk = false;
        _recvState = -1;  // Erreur
        Serial.println(F("[Network] Échec récupération état distant"));
    }
    _lastRemoteFetch = millis();
    return ok;
}

void AutomatismNetwork::applyConfigFromJson(const ArduinoJson::JsonDocument& doc) {
    // Lambda helper pour assignation conditionnelle
    auto assignIfPresent = [&doc](const char* key, auto& var) {
        if (!doc.containsKey(key)) return;
        using T = std::decay_t<decltype(var)>;
        T v = doc[key].as<T>();
        if constexpr (std::is_arithmetic_v<T>) {
            // Ignorer 0 pour éviter d'écraser par défaut
            if (v == 0) return;
        }
        var = v;
    };
    
    // Application de la configuration
    assignIfPresent("emailAddress", _emailAddress);
    assignIfPresent("emailNotif", _emailEnabled);
    assignIfPresent("FreqWakeUp", _freqWakeSec);
    assignIfPresent("limFlood", _limFlood);
    assignIfPresent("aqThreshold", _aqThresholdCm);
    assignIfPresent("tankThreshold", _tankThresholdCm);
    assignIfPresent("chauffageThreshold", _heaterThresholdC);
    
    Serial.println(F("[Network] Configuration appliquée depuis JSON"));
}

// ============================================================================
// SEND FULL UPDATE
// NOTE: Cette méthode nécessite BEAUCOUP de variables d'Automatism
// Pour l'instant, elle reste dans automatism.cpp et sera déléguée plus tard
// quand on aura refactorisé les variables membres
// ============================================================================

bool AutomatismNetwork::sendFullUpdate(
    const SensorReadings& readings,
    SystemActuators& acts,
    int diffMaree,
    uint8_t feedMorning, uint8_t feedNoon, uint8_t feedEvening,
    uint16_t feedBigDur, uint16_t feedSmallDur,
    const String& bouffePetits, const String& bouffeGros,
    bool forceWakeUp, uint16_t freqWakeSec,
    uint32_t refillDurationSec,
    const char* extraPairs
) {
    // Reset watchdog au début
    esp_task_wdt_reset();
    
    // VALIDATION MESURES
    float tempWater = readings.tempWater;
    float tempAir = readings.tempAir;
    float humidity = readings.humidity;
    
    // Validation températures
    if (isnan(tempWater) || tempWater <= 0.0f || tempWater >= 60.0f) {
        Serial.printf("[Network] Temp eau invalide: %.1f°C, force NaN\n", tempWater);
        tempWater = NAN;
    }
    
    if (isnan(tempAir) || tempAir <= SensorConfig::AirSensor::TEMP_MIN || 
        tempAir >= SensorConfig::AirSensor::TEMP_MAX) {
        Serial.printf("[Network] Temp air invalide: %.1f°C, force NaN\n", tempAir);
        tempAir = NAN;
    }
    
    // Validation humidité
    if (isnan(humidity) || humidity < SensorConfig::AirSensor::HUMIDITY_MIN || 
        humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
        Serial.printf("[Network] Humidité invalide: %.1f%%, force NaN\n", humidity);
        humidity = NAN;
    }
    
    // Vérification mémoire
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < BufferConfig::LOW_MEMORY_THRESHOLD_BYTES) {
        Serial.printf("[Network] Mémoire faible (%u bytes), report envoi\n", freeHeap);
        return false;
    }
    
    // Construction payload
    char data[1024];
    snprintf(data, sizeof(data),
        "api_key=%s&sensor=%s&version=%s&TempAir=%.1f&Humidite=%.1f&TempEau=%.1f&"
        "EauPotager=%u&EauAquarium=%u&EauReserve=%u&diffMaree=%d&Luminosite=%u&"
        "etatPompeAqua=%d&etatPompeTank=%d&etatHeat=%d&etatUV=%d&"
        "bouffeMatin=%u&bouffeMidi=%u&bouffeSoir=%u&tempsGros=%u&tempsPetits=%u&"
        "aqThreshold=%u&tankThreshold=%u&chauffageThreshold=%.1f&"
        "tempsRemplissageSec=%u&limFlood=%u&WakeUp=%d&FreqWakeUp=%u&"
        "bouffePetits=%s&bouffeGros=%s&mail=%s&mailNotif=%s",
        Config::API_KEY, Config::SENSOR, Config::VERSION,
        tempAir, humidity, tempWater,
        readings.wlPota, readings.wlAqua, readings.wlTank, diffMaree, readings.luminosite,
        acts.isAquaPumpRunning(), acts.isTankPumpRunning(), acts.isHeaterOn(), acts.isLightOn(),
        feedMorning, feedNoon, feedEvening, feedBigDur, feedSmallDur,
        _aqThresholdCm, _tankThresholdCm, _heaterThresholdC,
        refillDurationSec, _limFlood,
        forceWakeUp ? 1 : 0, freqWakeSec,
        bouffePetits.c_str(), bouffeGros.c_str(),
        _emailAddress.c_str(), _emailEnabled ? "checked" : "");
    
    esp_task_wdt_reset();
    
    String payload = String(data);
    if (extraPairs && extraPairs[0] != '\0') {
        payload += "&";
        payload += extraPairs;
    }
    
    // Ajout resetMode=0 si absent
    if (payload.indexOf("resetMode=") == -1) {
        payload += "&resetMode=0";
    }
    
    esp_task_wdt_reset();
    
    bool success = _web.postRaw(payload, false);
    
    if (success) {
        _sendState = 1;  // OK
        Serial.println(F("[Network] sendFullUpdate SUCCESS"));
    } else {
        _sendState = -1;  // Erreur
        Serial.println(F("[Network] sendFullUpdate FAILED"));
    }
    
    _lastSend = millis();
    return success;
}

// ============================================================================
// HELPERS PRIVÉS (isTrue, isFalse)
// ============================================================================

bool AutomatismNetwork::isTrue(ArduinoJson::JsonVariantConst v) {
    if (v.is<bool>()) return v.as<bool>();
    if (v.is<int>()) return v.as<int>() == 1;
    if (v.is<const char*>()) {
        const char* p = v.as<const char*>();
        if (!p) return false;
        String s = String(p);
        s.toLowerCase();
        s.trim();
        return s == "1" || s == "true" || s == "on" || s == "checked";
    }
    return false;
}

bool AutomatismNetwork::isFalse(ArduinoJson::JsonVariantConst v) {
    if (v.is<bool>()) return !v.as<bool>();
    if (v.is<int>()) return v.as<int>() == 0;
    if (v.is<const char*>()) {
        const char* p = v.as<const char*>();
        if (!p) return false;
        String s = String(p);
        s.toLowerCase();
        s.trim();
        return s == "0" || s == "false" || s == "off" || s == "unchecked";
    }
    return false;
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 1: pollRemoteState()
// Polling serveur + cache + état UI
// ============================================================================

bool AutomatismNetwork::pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis, Automatism& autoCtrl) {
    // Vérification intervalle
    if ((uint32_t)(currentMillis - _lastRemoteFetch) < REMOTE_FETCH_INTERVAL_MS) {
        return false;
    }
    _lastRemoteFetch = currentMillis;
    
    // Vérification WiFi
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    esp_task_wdt_reset();
    
    // Indique téléchargement en cours (flèche vide)
    _recvState = 0;
    
    // Mise à jour affichage OLED avec état marée
    // NOTE: Nécessite accès à _disp, _sensors, mailBlinkUntil depuis autoCtrl
    // Simplifié ici - l'affichage reste géré par automatism.cpp
    
    // Tentative récupération depuis serveur
    bool okRecv = _web.fetchRemoteState(doc);
    bool remoteSuccess = okRecv;
    
    if (!okRecv) {
        // Fallback: tentative rechargement depuis la flash
        String cachedJson;
        if (_config.loadRemoteVars(cachedJson)) {
            DeserializationError err = deserializeJson(doc, cachedJson);
            if (!err) {
                okRecv = true;
                Serial.println(F("[Network] Variables distantes chargées depuis sauvegarde (offline)"));
            }
        }
    }
    
    // Mise à jour états
    _recvState = (remoteSuccess && doc.size() > 0) ? 1 : -1;
    _serverOk = remoteSuccess;
    
    if (!okRecv) {
        return false;
    }
    
    // Sauvegarde dernière version reçue dans flash
    String jsonToSave;
    serializeJson(doc, jsonToSave);
    _config.saveRemoteVars(jsonToSave);
    
    esp_task_wdt_reset();
    
    return true;
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 2: handleResetCommand()
// Gestion commandes reset distant
// ============================================================================

bool AutomatismNetwork::handleResetCommand(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
    bool needReset = false;
    const char* resetKey = nullptr;
    
    // Détection clés reset
    if (doc.containsKey("reset") && doc["reset"].as<int>() == 1) {
        needReset = true;
        resetKey = "reset";
    }
    if (doc.containsKey("resetMode") && doc["resetMode"].as<int>() == 1) {
        needReset = true;
        resetKey = "resetMode"; // Priorité à resetMode si présent
    }
    
    if (!needReset) {
        return false;
    }
    
    Serial.println(F("[Network] Reset distant demandé"));
    
    // Email notification si activé
    if (_emailEnabled) {
        // NOTE: Nécessite accès au Mailer
        // Pour l'instant, déléguer via autoCtrl
        autoCtrl.armMailBlink();
    }
    
    // Acquittement serveur (reset flag à 0)
    if (resetKey) {
        char override[64];
        snprintf(override, sizeof(override), "%s=0", resetKey);
        SensorReadings curR = autoCtrl.readSensors();
        autoCtrl.sendFullUpdate(curR, override);
    }
    
    // Délai pour laisser requête HTTP se terminer
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Sauvegarde paramètres critiques NVS
    // NOTE: Ces méthodes sont dans Automatism, on les appelle via autoCtrl
    // Pour simplifier, on laisse Automatism gérer ça
    
    // Reset ESP
    ESP.restart();
    
    return true; // Jamais atteint mais pour cohérence
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 3: parseRemoteConfig()
// Parse et applique configuration distante
// ============================================================================

void AutomatismNetwork::parseRemoteConfig(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
    // Application variables basiques
    assignIfPresent(doc, "aqThreshold", _aqThresholdCm);
    assignIfPresent(doc, "tankThreshold", _tankThresholdCm);
    assignIfPresent(doc, "limFlood", _limFlood);
    
    // Durées de remplissage
    if (doc.containsKey("tempsRemplissageSec")) {
        // NOTE: refillDurationMs est dans Automatism, on devrait ajouter un setter
        // Pour l'instant, on log juste
        int refillSec = doc["tempsRemplissageSec"].as<int>();
        Serial.printf("[Network] tempsRemplissageSec reçu: %d (non appliqué - TODO setter)\n", refillSec);
    }
    
    // Threshold chauffage
    if (doc.containsKey("chauffageThreshold")) {
        _heaterThresholdC = doc["chauffageThreshold"].as<float>();
    }
    
    // Email configuration
    if (doc.containsKey("mail")) {
        const char* m = doc["mail"].as<const char*>();
        _emailAddress = m ? String(m) : String();
    }
    
    if (doc.containsKey("mailNotif")) {
        auto v = doc["mailNotif"];
        if (v.is<bool>()) {
            _emailEnabled = v.as<bool>();
        } else if (v.is<int>()) {
            _emailEnabled = (v.as<int>() == 1);
        } else if (v.is<const char*>()) {
            const char* p = v.as<const char*>();
            if (p && p[0]) {
                String s = String(p);
                _emailEnabled = (s == "checked" || s == "1" || s == "true" || s == "on");
            }
        }
    }
    
    // FreqWakeUp
    if (doc.containsKey("FreqWakeUp")) {
        int fv = doc["FreqWakeUp"].as<int>();
        if (fv > 0) _freqWakeSec = fv;
    }
    
    Serial.println(F("[Network] Configuration distante appliquée"));
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 4: handleRemoteFeedingCommands()
// Gestion nourrissage manuel distant
// ============================================================================

void AutomatismNetwork::handleRemoteFeedingCommands(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
    // Commande "bouffePetits"
    if (doc.containsKey("bouffePetits")) {
        auto var = doc["bouffePetits"];
        if (isTrue(var)) {
            autoCtrl.manualFeedSmall();
            
            if (_emailEnabled) {
                String message = autoCtrl.createFeedingMessage(
                    "Bouffe manuelle - Petits poissons",
                    autoCtrl.getFeedBigDur(),
                    autoCtrl.getFeedSmallDur()
                );
                // NOTE: Mailer nécessaire - autoCtrl doit le gérer
            }
            
            autoCtrl.armMailBlink();
            autoCtrl.sendFullUpdate(autoCtrl.readSensors(), nullptr);
        }
    }
    
    // Commande "bouffeGros"
    if (doc.containsKey("bouffeGros")) {
        auto var = doc["bouffeGros"];
        if (isTrue(var)) {
            autoCtrl.manualFeedBig();
            
            if (_emailEnabled) {
                String message = autoCtrl.createFeedingMessage(
                    "Bouffe manuelle - Gros poissons",
                    autoCtrl.getFeedBigDur(),
                    autoCtrl.getFeedSmallDur()
                );
                // NOTE: Mailer nécessaire
            }
            
            autoCtrl.armMailBlink();
            autoCtrl.sendFullUpdate(autoCtrl.readSensors());
        }
    }
}

// ============================================================================
// REMOTE STATE MANAGEMENT - MÉTHODE 5: handleRemoteActuators()
// Gestion actionneurs + GPIO dynamiques
// NOTE: Cette méthode est TRÈS complexe (250 lignes) et nécessite de nombreux
// accès aux membres privés d'Automatism. Pour Phase 2.11, on la laisse simplifiée
// et on la complètera dans une phase ultérieure quand on aura refactorisé
// les variables membres dans des modules dédiés.
// ============================================================================

void AutomatismNetwork::handleRemoteActuators(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl) {
    // NOTE: Cette méthode nécessite énormément d'accès aux membres d'Automatism:
    // - _acts (SystemActuators)
    // - Variables: _lastAquaManualOrigin, _manualTankOverride, tankPumpLocked, etc.
    // - Méthodes: startAquaPumpManualLocal, stopTankPumpManual, etc.
    //
    // Pour Phase 2.11, on implémente une version SIMPLIFIÉE qui gère:
    // - Commandes light (lightCmd prioritaire)
    // - GPIO génériques basiques
    //
    // La version COMPLÈTE avec gestion pompes/heater/GPIO 0-116 sera implémentée
    // dans Phase 2.12 après avoir ajouté les getters/setters nécessaires.
    
    // Commande lumière (lightCmd prioritaire)
    if (doc.containsKey("lightCmd")) {
        auto v = doc["lightCmd"];
        if (isTrue(v)) {
            autoCtrl.startLightManualLocal();
            Serial.println(F("[Network] Lumière ON (commande explicite lightCmd)"));
        } else if (isFalse(v)) {
            autoCtrl.stopLightManualLocal();
            Serial.println(F("[Network] Lumière OFF (commande explicite lightCmd)"));
        }
    } else if (doc.containsKey("light")) {
        // État distant: applique ON et OFF
        auto v = doc["light"];
        if (isTrue(v)) {
            autoCtrl.startLightManualLocal();
            Serial.println(F("[Network] Lumière ON (état distant)"));
        } else if (isFalse(v)) {
            autoCtrl.stopLightManualLocal();
            Serial.println(F("[Network] Lumière OFF (état distant)"));
        }
    }
    
    Serial.println(F("[Network] handleRemoteActuators() - Version simplifiée Phase 2.11"));
    Serial.println(F("[Network] GPIO dynamiques 0-116 seront implémentés en Phase 2.12"));
}

// ============================================================================
// CHECK CRITICAL CHANGES
// NOTE: Méthode ÉNORME (285 lignes) pour détecter changements critiques
// ============================================================================

void AutomatismNetwork::checkCriticalChanges(const SensorReadings& readings) {
    // TODO: Méthode de 285 lignes à implémenter
    // Détecte changements actionneurs, bouffe, etc.
    
    // Pour l'instant, reste dans automatism.cpp
    // Sera migrée dans Phase 2.7
    
    Serial.println(F("[Network] checkCriticalChanges() - Implémentation temporaire"));
}

