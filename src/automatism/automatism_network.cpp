#include "automatism_network.h"
#include "project_config.h"
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

bool AutomatismNetwork::sendFullUpdate(const SensorReadings& readings, const char* extraPairs) {
    // TODO: Cette méthode nécessite accès à trop de variables d'Automatism
    // Pour l'instant, elle reste implémentée dans automatism.cpp
    // Sera migrée dans Phase 2.7 (après refactoring variables)
    
    Serial.println(F("[Network] sendFullUpdate() - Implémentation temporaire"));
    return false;  // Placeholder
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

