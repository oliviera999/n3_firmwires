#include "automatism_network.h"
#include "automatism.h"
#include "automatism_persistence.h"
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
    , _dataQueue(100)  // AUGMENTÉ de 50 à 100 entrées (v11.50)
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
    Serial.println(F("[Network] Module initialisé (constructeur)"));
    
    // NOTE (v11.32): DataQueue::begin() NE PEUT PAS être appelé ici
    // car LittleFS n'est pas encore monté (constructeur global)
    // L'initialisation sera faite dans begin() après LittleFS.begin()
}

bool AutomatismNetwork::begin() {
    // Initialiser la queue de données (après montage LittleFS)
    if (_dataQueue.begin()) {
        Serial.printf("[Network] ✓ DataQueue initialisée (%u entrées en attente)\n", _dataQueue.size());
        return true;
    } else {
        Serial.println(F("[Network] ⚠️ Échec initialisation DataQueue"));
        return false;
    }
}

// v11.70: normalizeServerKeys() supprimé - standardisation des clés JSON
// Le serveur et l'ESP32 utilisent maintenant les mêmes noms de clés

// ============================================================================
// COMMUNICATION SERVEUR - Méthodes Simples
// ============================================================================

bool AutomatismNetwork::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
    bool ok = _web.fetchRemoteState(doc);
    if (ok && doc.size() > 0) {
        // v11.70: Pas de normalisation - clés déjà standardisées
        String jsonStr;
        serializeJson(doc, jsonStr);
        _config.saveRemoteVars(jsonStr);
        _serverOk = true;
        _recvState = 1;  // OK
        Serial.println(F("[Network] État distant récupéré avec succès (clés standardisées)"));
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
    // Accepter emailEnabled (bool/int/string) et mailNotif (compat server)
    if (doc.containsKey("emailEnabled")) {
        auto v = doc["emailEnabled"];
        if (v.is<bool>()) _emailEnabled = v.as<bool>();
        else if (v.is<int>()) _emailEnabled = (v.as<int>() == 1);
        else if (v.is<const char*>()) {
            const char* p = v.as<const char*>();
            if (p && p[0]) {
                String s = String(p); s.toLowerCase(); s.trim();
                _emailEnabled = (s == "1" || s == "true" || s == "on" || s == "checked" || s == "yes");
            }
        }
    }
    if (doc.containsKey("emailNotif")) {
        auto v = doc["emailNotif"];
        if (v.is<bool>()) _emailEnabled = v.as<bool>();
        else if (v.is<int>()) _emailEnabled = (v.as<int>() == 1);
        else if (v.is<const char*>()) {
            const char* p = v.as<const char*>();
            if (p && p[0]) {
                String s = String(p); s.toLowerCase(); s.trim();
                _emailEnabled = (s == "1" || s == "true" || s == "on" || s == "checked" || s == "yes");
            }
        }
    }
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
    
    // v11.70: Construction payload optimisée avec buffer statique pour éviter fragmentation mémoire
    char payloadBuffer[1024]; // Buffer unique pour tout le payload
    int len = snprintf(payloadBuffer, sizeof(payloadBuffer),
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
    
    // Vérification de dépassement de buffer
    if (len >= sizeof(payloadBuffer)) {
        Serial.printf("[Network] ⚠️ Payload tronqué (limite: %u bytes)\n", sizeof(payloadBuffer) - 1);
        payloadBuffer[sizeof(payloadBuffer) - 1] = '\0';
    }
    
    if (extraPairs && extraPairs[0] != '\0') {
        strncat(payloadBuffer, "&", sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
        strncat(payloadBuffer, extraPairs, sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
    }
    
    // === PROTECTION CRITIQUE: resetMode=0 TOUJOURS PRÉSENT ===
    // S'assurer que resetMode=0 est toujours envoyé pour éviter les resets non désirés
    if (strstr(payloadBuffer, "resetMode=") == nullptr) {
        strncat(payloadBuffer, "&resetMode=0", sizeof(payloadBuffer) - strlen(payloadBuffer) - 1);
        Serial.println(F("[Network] 🔒 resetMode=0 ajouté automatiquement pour sécurité"));
    } else {
        // Vérifier que resetMode n'est pas à 1 (sauf cas spécifique de trigger)
        if (strstr(payloadBuffer, "resetMode=1") != nullptr) {
            Serial.println(F("[Network] ⚠️ resetMode=1 détecté dans payload"));
        } else {
            Serial.println(F("[Network] ✅ resetMode=0 confirmé dans payload"));
        }
    }
    
    bool success = _web.postRaw(payloadBuffer);
    if (!_config.isRemoteSendEnabled()) {
        Serial.println(F("[Network] ⛔ Envoi distant désactivé (config) - SKIP"));
        return false;
    }
    
    if (success) {
        _sendState = 1;  // OK
        Serial.println(F("[Network] sendFullUpdate SUCCESS"));
        
        // v11.70: REJEU DE QUEUE LIMITÉ pour équilibrer stabilité et fonctionnalité
        // Replay maximum 5 entrées pour éviter l'épuisement mémoire
        if (_dataQueue.size() > 0) {
            uint16_t replayed = replayQueuedData();
            if (replayed > 0) {
                Serial.printf("[Network] ✓ %u données rejouées avec succès\n", replayed);
            } else {
                Serial.println(F("[Network] ⚠️ Échec rejeu queue - données conservées"));
            }
        }
    } else {
        _sendState = -1;  // Erreur
        Serial.println(F("[Network] sendFullUpdate FAILED"));
        
        // v11.70: QUEUE ACTIVÉE avec limite pour équilibrer stabilité et fonctionnalité
        // Ajouter à la queue seulement si elle n'est pas pleine (limite mémoire)
        if (_dataQueue.size() < 20) { // Limite à 20 entrées max
            // Utilisation directe du buffer char[] pour éviter conversion String
            _dataQueue.push(String(payloadBuffer));
            Serial.printf("[Network] 📥 Donnée ajoutée à la queue (%u entrées)\n", _dataQueue.size());
        } else {
            Serial.println(F("[Network] ⚠️ Queue pleine - donnée perdue (limite mémoire)"));
        }
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
    // Flag de réception distante
    if (!_config.isRemoteRecvEnabled()) {
        Serial.println(F("[Network] ⛔ Réception distante désactivée (config)"));
        return false;
    }
    // Vérification intervalle
    if ((uint32_t)(currentMillis - _lastRemoteFetch) < REMOTE_FETCH_INTERVAL_MS) {
        return false;
    }
    _lastRemoteFetch = currentMillis;
    
    // Vérification WiFi
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    // Indique téléchargement en cours (flèche vide)
    _recvState = 0;
    
    // Mise à jour affichage OLED avec état marée
    // NOTE: Nécessite accès à _disp, _sensors, mailBlinkUntil depuis autoCtrl
    // Simplifié ici - l'affichage reste géré par automatism.cpp
    
    // Tentative récupération depuis serveur
    bool okRecv = _web.fetchRemoteState(doc);
    bool remoteSuccess = okRecv;
    
    // v11.72: Fallback - aplatir éventuel wrapper { "outputs": { ... } }
    if (okRecv && doc.containsKey("outputs") && doc["outputs"].is<JsonObject>()) {
        Serial.println(F("[Network] Wrapper 'outputs' détecté - aplatissement"));
        ArduinoJson::DynamicJsonDocument tmp(BufferConfig::JSON_DOCUMENT_SIZE);
        tmp.set(doc["outputs"].as<ArduinoJson::JsonObjectConst>());
        doc.clear();
        for (auto kv : tmp.as<ArduinoJson::JsonObjectConst>()) {
            doc[kv.key().c_str()] = kv.value();
        }
    }
    
    // === LOGS DÉTAILLÉS v11.07 - DIAGNOSTIC JSON ===
    if (okRecv && doc.size() > 0) {
        Serial.println(F("[Network] === JSON REÇU DU SERVEUR ==="));
        String jsonDebug;
        serializeJsonPretty(doc, jsonDebug);
        // Afficher premiers 1000 caractères pour éviter overflow
        Serial.println(jsonDebug.substring(0, min((int)jsonDebug.length(), 1000)));
        if (jsonDebug.length() > 1000) {
            Serial.printf("[Network] ... (tronqué, %u bytes restants)\n", jsonDebug.length() - 1000);
        }
        Serial.println(F("[Network] === FIN JSON ==="));
    } else if (!okRecv) {
        Serial.println(F("[Network] ⚠️ Échec fetchRemoteState() - aucune donnée reçue"));
    }
    
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
    
    // v11.74: Sauvegarde + diagnostics clés critiques
    String jsonToSave;
    serializeJson(doc, jsonToSave);
    _config.saveRemoteVars(jsonToSave);
    // Diagnostics: clés critiques présentes ?
    const char* crit[] = {"16","18","2","15","108","109","110","115"};
    for (size_t i=0;i<sizeof(crit)/sizeof(crit[0]);++i){
        const char* k = crit[i];
        bool has = doc.containsKey(k);
        Serial.printf("[Network] Key %s: %s\n", k, has?"OK":"MISSING");
    }
    
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
    
    // === PROTECTION ANTI-BOUCLE INFINIE ===
    // Vérifier si un reset a déjà été effectué récemment
    Preferences prefs;
    prefs.begin("reset", false);
    uint32_t lastResetTime = prefs.getULong("lastReset", 0);
    uint32_t currentTime = millis();
    
    // Si reset dans les 30 dernières secondes, ignorer pour éviter la boucle
    if (lastResetTime > 0 && (currentTime - lastResetTime) < 30000) {
        Serial.printf("[Network] ⚠️ Reset ignoré - déjà effectué il y a %lu ms\n", 
                      currentTime - lastResetTime);
        prefs.end();
        return false;
    }
    
    // Marquer le reset en cours
    prefs.putULong("lastReset", currentTime);
    prefs.end();
    
    // Email notification si activé
    if (_emailEnabled) {
        // NOTE: Nécessite accès au Mailer
        // Pour l'instant, déléguer via autoCtrl
        autoCtrl.armMailBlink();
    }
    
    // === ACQUITTEMENT ROBUSTE AVEC RETRY ET CONFIRMATION ===
    bool ackSuccess = false;
    if (resetKey) {
        char override[64];
        snprintf(override, sizeof(override), "%s=0", resetKey);
        SensorReadings curR = autoCtrl.readSensors();
        
        // Tentative d'acquittement avec retry (5 tentatives pour plus de robustesse)
        for (int attempt = 1; attempt <= 5; attempt++) {
            Serial.printf("[Network] Tentative acquittement %d/5...\n", attempt);
            ackSuccess = autoCtrl.sendFullUpdate(curR, override);
            if (ackSuccess) {
                Serial.println(F("[Network] ✅ Acquittement réussi"));
                
                // v11.70: Attendre confirmation serveur avant restart
                Serial.println(F("[Network] ⏳ Attente confirmation serveur (5 secondes)..."));
                vTaskDelay(pdMS_TO_TICKS(5000));
                
                // Vérifier si resetMode est toujours à 1 côté serveur (boucle infinie)
                ArduinoJson::DynamicJsonDocument confirmDoc(BufferConfig::JSON_DOCUMENT_SIZE);
                bool confirmOk = _web.fetchRemoteState(confirmDoc);
                if (confirmOk && confirmDoc.containsKey("110")) {
                    int resetValue = confirmDoc["110"].as<int>();
                    if (resetValue == 1) {
                        Serial.println(F("[Network] ⚠️ ResetMode toujours à 1 côté serveur - risque boucle infinie"));
                        Serial.println(F("[Network] 🔄 Redémarrage forcé malgré le risque"));
                    } else {
                        Serial.println(F("[Network] ✅ Confirmation: resetMode=0 reçu côté serveur"));
                    }
                } else {
                    Serial.println(F("[Network] ⚠️ Impossible de confirmer reset côté serveur - redémarrage forcé"));
                }
                break;
            }
            if (attempt < 5) {
                Serial.printf("[Network] ⚠️ Échec tentative %d, retry dans 1s\n", attempt);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        
        if (!ackSuccess) {
            Serial.println(F("[Network] ❌ Échec acquittement après 5 tentatives"));
            Serial.println(F("[Network] ⚠️ Redémarrage sans confirmation (risque de boucle infinie)"));
        }
    }
    
    // Sauvegarde paramètres critiques NVS
    // NOTE: Ces méthodes sont dans Automatism, on les appelle via autoCtrl
    // Pour simplifier, on laisse Automatism gérer ça
    
    Serial.println(F("[Network] 🔄 Redémarrage ESP32..."));
    
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
        float newThreshold = doc["chauffageThreshold"].as<float>();
        if (newThreshold > 0.0f) { // Protection contre valeurs invalides
            _heaterThresholdC = newThreshold;
            Serial.printf("[Network] 🔥 Seuil chauffage mis à jour dans Network: %.1f°C\n", _heaterThresholdC);
        }
    }
    
    // Email configuration
    if (doc.containsKey("mail")) {
        const char* m = doc["mail"].as<const char*>();
        _emailAddress = m ? String(m) : String();
    }
    
    if (doc.containsKey("emailEnabled")) {
        auto v = doc["emailEnabled"];
        if (v.is<bool>()) _emailEnabled = v.as<bool>();
        else if (v.is<int>()) _emailEnabled = (v.as<int>() == 1);
        else if (v.is<const char*>()) {
            const char* p = v.as<const char*>();
            if (p && p[0]) {
                String s = String(p); s.toLowerCase(); s.trim();
                _emailEnabled = (s == "1" || s == "true" || s == "on" || s == "checked" || s == "yes");
            }
        }
    }
    if (doc.containsKey("mailNotif")) {
        auto v = doc["mailNotif"];
        if (v.is<bool>()) _emailEnabled = v.as<bool>();
        else if (v.is<int>()) _emailEnabled = (v.as<int>() == 1);
        else if (v.is<const char*>()) {
            const char* p = v.as<const char*>();
            if (p && p[0]) {
                String s = String(p); s.toLowerCase(); s.trim();
                _emailEnabled = (s == "1" || s == "true" || s == "on" || s == "checked" || s == "yes");
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
    // Commande "bouffePetits" (ou GPIO 108 en fallback)
    bool triggerSmall = false;
    if (doc.containsKey("bouffePetits")) {
        triggerSmall = isTrue(doc["bouffePetits"]);
    } else if (doc.containsKey("108")) {
        // Fallback: chercher GPIO 108 si "bouffePetits" absent
        triggerSmall = isTrue(doc["108"]);
        Serial.println(F("[Network] 🔧 Utilisation GPIO 108 pour nourrissage petits (fallback)"));
    }
    
    if (triggerSmall) {
        Serial.println(F("[Network] 🐟 Commande nourrissage PETITS reçue du serveur distant"));
        
        // Exécution de la commande
        autoCtrl.manualFeedSmall();
        
        // v11.31: ACK immédiat + log
        sendCommandAck("bouffePetits", "executed");
        logRemoteCommandExecution("fd_small", true);
        
        if (_emailEnabled) {
            String message = autoCtrl.createFeedingMessage(
                "Bouffe manuelle - Petits poissons",
                autoCtrl.getFeedBigDur(),
                autoCtrl.getFeedSmallDur()
            );
            // NOTE: Mailer nécessaire - autoCtrl doit le gérer
        }
        
        autoCtrl.armMailBlink();
        // v11.66: Reset géré par callback de completion dans automatism_feeding.cpp
        // autoCtrl.sendFullUpdate(autoCtrl.readSensors(), "bouffePetits=0&108=0");  // SUPPRIMÉ
    }
    
    // Commande "bouffeGros" (ou GPIO 109 en fallback)
    bool triggerBig = false;
    if (doc.containsKey("bouffeGros")) {
        triggerBig = isTrue(doc["bouffeGros"]);
    } else if (doc.containsKey("109")) {
        // Fallback: chercher GPIO 109 si "bouffeGros" absent
        triggerBig = isTrue(doc["109"]);
        Serial.println(F("[Network] 🔧 Utilisation GPIO 109 pour nourrissage gros (fallback)"));
    }
    
    if (triggerBig) {
        Serial.println(F("[Network] 🐟 Commande nourrissage GROS reçue du serveur distant"));
        
        // Exécution de la commande
        autoCtrl.manualFeedBig();
        
        // v11.31: ACK immédiat + log
        sendCommandAck("bouffeGros", "executed");
        logRemoteCommandExecution("fd_large", true);
        
        if (_emailEnabled) {
            String message = autoCtrl.createFeedingMessage(
                "Bouffe manuelle - Gros poissons",
                autoCtrl.getFeedBigDur(),
                autoCtrl.getFeedSmallDur()
            );
            // NOTE: Mailer nécessaire
        }
        
        autoCtrl.armMailBlink();
        // v11.66: Reset géré par callback de completion dans automatism_feeding.cpp
        // autoCtrl.sendFullUpdate(autoCtrl.readSensors(), "bouffeGros=0&109=0");  // SUPPRIMÉ
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
    // v11.69: DÉSACTIVÉ - Utilisation exclusive de GPIOParser pour éviter conflits
    // Cette méthode créait une double gestion des commandes et bloquait les commandes distantes
    // pendant 5 secondes après toute action locale, empêchant le contrôle à distance.
    Serial.println(F("[Network] handleRemoteActuators DÉSACTIVÉ (v11.69) - GPIOParser utilisé"));
    return; // Désactivation complète
    
    // NOTE: Cette méthode gère les actionneurs par NOM (light, heat, pump_aqua, etc.)
    // Le code dans automatism.cpp gère les GPIO par NUMÉRO (fallback rétrocompatibilité)
    // Cela permet de supporter les deux formats: noms ET numéros
    
    // ========================================
    // PRIORITÉ LOCALE (v11.38): Bloquer serveur distant pendant 5s après action locale
    // FIX v11.38: Bloquer AUSSI si action récente (même si sync réussie)
    // ========================================
    const uint32_t LOCAL_PRIORITY_TIMEOUT_MS = 5000; // 5 secondes de priorité locale
    
    // PRIORITÉ 1: Bloquer si action locale récente (MÊME si sync déjà réussie)
    // Cela évite que le serveur distant n'écrase l'état pendant la fenêtre de 5 secondes
    if (AutomatismPersistence::hasRecentLocalAction(LOCAL_PRIORITY_TIMEOUT_MS)) {
        uint32_t lastLocal = AutomatismPersistence::getLastLocalActionTime();
        uint32_t elapsed = millis() - lastLocal;
        Serial.printf("[Network] ⚠️ PRIORITÉ LOCALE ACTIVE - Commandes distantes bloquées (%lu ms écoulées)\n", elapsed);
        return; // Ignorer TOUTES les commandes distantes
    }
    
    // PRIORITÉ 2: Bloquer si sync en attente (backup si sync lente)
    if (AutomatismPersistence::hasPendingSync()) {
        uint32_t pendingCount = AutomatismPersistence::getPendingSyncCount();
        uint32_t lastPending = AutomatismPersistence::getLastPendingSyncTime();
        uint32_t elapsed = millis() - lastPending;
        Serial.printf("[Network] ⏳ PRIORITÉ LOCALE - Sync en attente (%u items, %lu ms écoulées)\n", 
                      pendingCount, elapsed);
        return; // Ignorer les commandes distantes tant que sync pending
    }
    
    // === LOGS DÉTAILLÉS POUR DIAGNOSTIC ===
    Serial.println(F("[Network] === ACTIONNEURS REÇUS DU SERVEUR ==="));
    for (auto kv : doc.as<ArduinoJson::JsonObjectConst>()) {
        const char* key = kv.key().c_str();
        // N'afficher que les clés d'actionneurs connues
        if (strcmp(key, "light") == 0 || strcmp(key, "lightCmd") == 0 ||
            strcmp(key, "heat") == 0 || strcmp(key, "heatCmd") == 0 ||
            strcmp(key, "pump_aqua") == 0 || strcmp(key, "pump_tank") == 0) {
            Serial.printf("[Network] Clé '%s' = %s\n", key, kv.value().as<String>().c_str());
        }
    }
    Serial.println(F("[Network] === FIN ACTIONNEURS ==="));
    
    // ========================================
    // LUMIÈRE (lightCmd prioritaire sur light)
    // ========================================
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
        auto v = doc["light"];
        if (isTrue(v)) {
            autoCtrl.startLightManualLocal();
            Serial.println(F("[Network] Lumière ON (état distant)"));
        } else if (isFalse(v)) {
            autoCtrl.stopLightManualLocal();
            Serial.println(F("[Network] Lumière OFF (état distant)"));
        }
    }
    
    // ========================================
    // CHAUFFAGE (heatCmd prioritaire sur heat)
    // ========================================
    if (doc.containsKey("heatCmd")) {
        auto v = doc["heatCmd"];
        if (isTrue(v)) {
            autoCtrl.startHeaterManualLocal();
            Serial.println(F("[Network] Chauffage ON (commande explicite heatCmd)"));
        } else if (isFalse(v)) {
            autoCtrl.stopHeaterManualLocal();
            Serial.println(F("[Network] Chauffage OFF (commande explicite heatCmd)"));
        }
    } else if (doc.containsKey("heat")) {
        auto v = doc["heat"];
        if (isTrue(v)) {
            autoCtrl.startHeaterManualLocal();
            Serial.println(F("[Network] Chauffage ON (état distant)"));
        } else if (isFalse(v)) {
            autoCtrl.stopHeaterManualLocal();
            Serial.println(F("[Network] Chauffage OFF (état distant)"));
        }
    }
    
    // ========================================
    // POMPE AQUARIUM (pump_aquaCmd prioritaire sur pump_aqua)
    // ========================================
    if (doc.containsKey("pump_aquaCmd")) {
        auto v = doc["pump_aquaCmd"];
        if (isTrue(v)) {
            autoCtrl.startAquaPumpManualLocal();
            Serial.println(F("[Network] Pompe aquarium ON (commande explicite pump_aquaCmd)"));
        } else if (isFalse(v)) {
            autoCtrl.stopAquaPumpManualLocal();
            Serial.println(F("[Network] Pompe aquarium OFF (commande explicite pump_aquaCmd)"));
        }
    } else if (doc.containsKey("pump_aqua")) {
        auto v = doc["pump_aqua"];
        if (isTrue(v)) {
            autoCtrl.startAquaPumpManualLocal();
            Serial.println(F("[Network] Pompe aquarium ON (état distant)"));
        } else if (isFalse(v)) {
            autoCtrl.stopAquaPumpManualLocal();
            Serial.println(F("[Network] Pompe aquarium OFF (état distant)"));
        }
    }
    
    // ========================================
    // POMPE RÉSERVOIR (pump_tankCmd prioritaire sur pump_tank) - v11.31: ACK + logs
    // ========================================
    if (doc.containsKey("pump_tankCmd")) {
        auto v = doc["pump_tankCmd"];
        if (isTrue(v)) {
            Serial.println(F("[Network] 💧 Commande pompe TANK ON reçue du serveur distant"));
            autoCtrl.startTankPumpManual();
            sendCommandAck("pump_tank", "on");
            logRemoteCommandExecution("ptank_on", true);
            
            // v11.59: Reset GPIO virtuals to prevent infinite loops
            SensorReadings readings = autoCtrl.readSensors();
            autoCtrl.sendFullUpdate(readings, "pump_tankCmd=0&pump_tank=0");
            Serial.println(F("[Network] GPIO virtuals reset: pump_tankCmd=0&pump_tank=0"));
        } else if (isFalse(v)) {
            Serial.println(F("[Network] 💧 Commande pompe TANK OFF reçue du serveur distant"));
            autoCtrl.stopTankPumpManual();
            sendCommandAck("pump_tank", "off");
            logRemoteCommandExecution("ptank_off", true);
            
            // v11.59: Reset GPIO virtuals to prevent infinite loops
            SensorReadings readings = autoCtrl.readSensors();
            autoCtrl.sendFullUpdate(readings, "pump_tankCmd=0&pump_tank=0");
            Serial.println(F("[Network] GPIO virtuals reset: pump_tankCmd=0&pump_tank=0"));
        }
    } else if (doc.containsKey("pump_tank")) {
        auto v = doc["pump_tank"];
        if (isTrue(v)) {
            // Protection: Ne redémarrer QUE si pompe arrêtée
            if (!autoCtrl.isTankPumpRunning()) {
                autoCtrl.startTankPumpManual();
                sendCommandAck("pump_tank", "on");
                logRemoteCommandExecution("ptank_on", true);
                Serial.println(F("[Network] Pompe réservoir démarrée (commande distante)"));
                
                // v11.66: Reset GPIO virtuals to prevent infinite loops
                SensorReadings readings = autoCtrl.readSensors();
                autoCtrl.sendFullUpdate(readings, "pump_tankCmd=0&pump_tank=0");
                Serial.println(F("[Network] GPIO virtuals reset: pump_tankCmd=0&pump_tank=0"));
            } else {
                Serial.println(F("[Network] ⚠️ Commande pump_tank=1 IGNORÉE - cycle déjà en cours"));
            }
        } else if (isFalse(v)) {
            // Arrêt seulement si effectivement en cours
            if (autoCtrl.isTankPumpRunning()) {
                autoCtrl.stopTankPumpManual();
                sendCommandAck("pump_tank", "off");
                logRemoteCommandExecution("ptank_off", true);
                Serial.println(F("[Network] Pompe réservoir arrêtée (commande distante)"));
                
                // v11.66: Reset GPIO virtuals to prevent infinite loops
                SensorReadings readings = autoCtrl.readSensors();
                autoCtrl.sendFullUpdate(readings, "pump_tankCmd=0&pump_tank=0");
                Serial.println(F("[Network] GPIO virtuals reset: pump_tankCmd=0&pump_tank=0"));
            } else {
                Serial.println(F("[Network] ⚠️ Commande pump_tank=0 IGNORÉE - pompe déjà arrêtée"));
            }
        }
    }
    
    Serial.println(F("[Network] handleRemoteActuators() - v11.07 COMPLET (light, heat, pumps)"));
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
// PERSISTANCE ET ACK - v11.31
// ============================================================================

uint16_t AutomatismNetwork::replayQueuedData() {
    uint16_t sent = 0;
    const uint16_t MAX_REPLAYS_PER_CYCLE = 5;  // v11.70: LIMITÉ à 5 pour stabilité
    
    while (_dataQueue.size() > 0 && sent < MAX_REPLAYS_PER_CYCLE) {
        // Lire sans supprimer
        String payload = _dataQueue.peek();
        if (payload.length() == 0) {
            Serial.println(F("[Network] ⚠️ Empty payload in queue, skipping"));
            _dataQueue.pop();
            continue;
        }
        
        // Tentative envoi
        Serial.printf("[Network] Replaying queued payload (%u bytes)...\n", payload.length());
        if (_web.postRaw(payload)) {
            // Succès : supprimer de la queue
            _dataQueue.pop();
            sent++;
            Serial.printf("[Network] ✓ Replayed payload %u/%u\n", sent, MAX_REPLAYS_PER_CYCLE);
            
            // Note: Watchdog géré par la tâche appelante (automationTask)
            // Ne pas appeler esp_task_wdt_reset() ici (erreur "task not found")
        } else {
            // Échec : arrêter pour ne pas vider la batterie
            Serial.println(F("[Network] ✗ Replay failed, stopping"));
            break;
        }
    }
    
    if (sent > 0) {
        Serial.printf("[Network] Replay summary: %u sent, %u remaining\n", sent, _dataQueue.size());
    }
    
    return sent;
}

bool AutomatismNetwork::sendCommandAck(const char* command, const char* status) {
    // Payload minimal pour ACK
    char ackPayload[256];
    snprintf(ackPayload, sizeof(ackPayload),
             "api_key=%s&sensor=%s&ack_command=%s&ack_status=%s&ack_timestamp=%lu",
             Config::API_KEY, Config::SENSOR, command, status, millis());
    
    // Envoi non-bloquant vers endpoint principal
    // NOTE: Le serveur peut ignorer les champs ack_* si endpoint non prévu
    // C'est acceptable car l'envoi périodique contient l'état complet
    String response;
    bool ok = _web.postRaw(String(ackPayload));
    
    if (ok) {
        Serial.printf("[Network] ✓ ACK sent: %s=%s\n", command, status);
    } else {
        Serial.printf("[Network] ✗ ACK failed: %s=%s (non-bloquant)\n", command, status);
    }
    
    return ok;
}

void AutomatismNetwork::logRemoteCommandExecution(const char* command, bool success) {
    Preferences prefs;
    prefs.begin("cmdLog", false);
    
    // Compteurs globaux
    uint32_t totalCmds = prefs.getUInt("total", 0) + 1;
    uint32_t successCmds = prefs.getUInt("success", 0) + (success ? 1 : 0);
    
    prefs.putUInt("total", totalCmds);
    prefs.putUInt("success", successCmds);
    
    // Compteurs par commande (limité à 10 commandes max pour économiser NVS)
    String key = String("cmd_") + command;
    uint32_t cmdTotal = prefs.getUInt(key.c_str(), 0) + 1;
    prefs.putUInt(key.c_str(), cmdTotal);
    
    prefs.end();
    
    // Log avec statistiques
    float successRate = (totalCmds > 0) ? ((float)successCmds / totalCmds * 100.0f) : 0.0f;
    Serial.printf("[Network] Command '%s': %s (Global: %.1f%% success, Total: %lu, This cmd: %lu times)\n", 
                  command, success ? "✓ OK" : "✗ FAILED", successRate, totalCmds, cmdTotal);
}

