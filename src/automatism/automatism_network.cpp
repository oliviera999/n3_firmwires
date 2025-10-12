#include "automatism_network.h"
#include "project_config.h"
#include "esp_task_wdt.h"
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
// HANDLE REMOTE STATE
// NOTE: Méthode ÉNORME (545 lignes) qui sera divisée en sous-méthodes
// ============================================================================

void AutomatismNetwork::handleRemoteState(SystemSensors& sensors, SystemActuators& actuators) {
    // TODO: Méthode de 545 lignes à diviser en:
    // - pollRemoteState()
    // - parseRemoteConfig()
    // - applyRemoteActuators()
    // - handleRemoteCommands()
    
    // Pour l'instant, reste dans automatism.cpp
    // Sera migrée dans Phase 2.7
    
    Serial.println(F("[Network] handleRemoteState() - Implémentation temporaire"));
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

// ============================================================================
// HELPERS PRIVÉS
// ============================================================================

bool AutomatismNetwork::shouldPollRemote() const {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    unsigned long now = millis();
    bool fetchDue = (now - _lastRemoteFetch) >= REMOTE_FETCH_INTERVAL_MS;
    
    return fetchDue;
}

