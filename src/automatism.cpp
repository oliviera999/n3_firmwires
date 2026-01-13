#include "automatism.h"
#include <Arduino.h>
#include <WiFi.h>
#include "esp_task_wdt.h"
#include "task_monitor.h"

// ============================================================================
// Automatism: Chef d'orchestre
// Responsabilité: Coordonner les modules spécialisés
// ============================================================================

Automatism::Automatism(SystemSensors& sensors, SystemActuators& acts, WebClient& web, 
                       DisplayView& disp, PowerManager& power, Mailer& mail, ConfigManager& config)
    : _sensors(sensors)
    , _acts(acts)
    , _web(web)
    , _disp(disp)
    , _power(power)
    , _mailer(mail)
    , _config(config)
    , _feeding(acts, config)
    , _feedingSchedule(acts, config, mail, power)
    , _network(web, config)
    , _sleep(power, config)
    , _refillController(*this)
    , _alertController(*this)
    , _displayController(disp, power)
{
    // Initialisation des états par défaut
    tankPumpLocked = false;
    forceWakeUp = false;
    _pumpStartMs = 0;
}

void Automatism::begin() {
    Serial.println(F("[Auto] Démarrage Automatism..."));
    
    // Initialisation des sous-modules
    _sleep.begin();
    _displayController.begin();
    _network.begin(); // Initialisation DataQueue après LittleFS
    
    // Restauration état persistant
  restorePersistentForceWakeup();
  restoreActuatorState();
    restoreRemoteConfigFromCache();
    
    Serial.println(F("[Auto] Initialisation terminée"));
}

void Automatism::update() {
    // Collecte des capteurs
    SensorReadings readings = _sensors.read();
    update(readings);
}

void Automatism::update(const SensorReadings& r) {
    _lastReadings = r;
    unsigned long now = millis();

    // ========================================
    // PRIORITÉ ABSOLUE : NOURRISSAGE ET REMPLISSAGE
    // ========================================
    
    // 1. Nourrissage automatique (PRIORITÉ 1 - temps critiques)
    handleFeeding();
    
    // 2. Gestion affichage (délégué)
    AutomatismRuntimeContext ctx;
    ctx.readings = r;
    ctx.nowMs = now;
    _displayController.updateDisplay(ctx, _acts, *this);

    // 3. Gestion réseau (polling commandes)
    JsonDocument doc;
    if (_network.pollRemoteState(doc, now)) {
        // Appliquer commandes reçues
        _network.handleRemoteFeedingCommands(doc, *this);
    }

    // 4. Gestion logique métier via contrôleurs
    _refillController.process(ctx);
    _alertController.process(ctx);
    
    // 5. Gestion sommeil (délégué)
    _sleep.process(r, _acts, *this);
}

void Automatism::updateDisplay() {
  SensorReadings r = _sensors.read();
    unsigned long now = millis();
    AutomatismRuntimeContext ctx;
    ctx.readings = r;
    ctx.nowMs = now;
    _displayController.updateDisplay(ctx, _acts, *this);
}

uint32_t Automatism::getRecommendedDisplayIntervalMs() {
  return _displayController.getRecommendedDisplayIntervalMs(millis());
}

// ============================================================================
// ACTIONS MANUELLES (Exposées pour le serveur Web)
// ============================================================================

void Automatism::manualFeedSmall() {
    Serial.println(F("[Auto] Nourrissage manuel PETITS déclenché"));
    Serial.printf("[Auto] Durée configurée: %u s\n", tempsPetits);
    // CORRECTION: Utiliser directement les durées de Automatism (synchronisées avec serveur)
    _acts.feedSmallFish(tempsPetits);
    _manualFeedingActive = true;
    _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD; // Début animation
    _feedingPhaseEnd = millis() + (tempsPetits * 1000);
    _currentFeedingType = "Petits"; // Pour l'affichage OLED
}

void Automatism::manualFeedBig() {
    Serial.println(F("[Auto] Nourrissage manuel GROS déclenché"));
    Serial.printf("[Auto] Durée configurée: %u s\n", tempsGros);
    // CORRECTION: Utiliser directement les durées de Automatism (synchronisées avec serveur)
    _acts.feedBigFish(tempsGros);
    _manualFeedingActive = true;
    _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
    _feedingPhaseEnd = millis() + (tempsGros * 1000);
    _currentFeedingType = "Gros"; // Pour l'affichage OLED
}

void Automatism::toggleEmailNotifications() {
    mailNotif = !mailNotif;
    _network.setEmailEnabled(mailNotif);
    // Persistance NVS via ConfigManager si nécessaire
}

void Automatism::toggleForceWakeup() {
    forceWakeUp = !forceWakeUp;
    _sleep.setForceWakeUp(forceWakeUp);
    Serial.printf("[Auto] ForceWakeUp basculé: %s\n", forceWakeUp ? "ON" : "OFF");
    // Persistance NVS
    g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, "forceWakeUp", forceWakeUp);
}

void Automatism::triggerResetMode() {
    Serial.println(F("[Auto] Reset Mode activé -> Redémarrage"));
    ESP.restart();
}

// ============================================================================
// COMMUNICATION RÉSEAU (Proxy vers AutomatismNetwork)
// ============================================================================

bool Automatism::sendFullUpdate(const SensorReadings& readings, const char* extraPairs) {
    // Délégation complète à AutomatismSync
    // On passe *this pour l'accès aux getters (durées, seuils, etc.)
    return _network.sendFullUpdate(readings, _acts, *this, extraPairs);
}

bool Automatism::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
    return _network.fetchRemoteState(doc);
}

void Automatism::applyConfigFromJson(const ArduinoJson::JsonDocument& doc) {
    _network.applyConfigFromJson(doc);
    
    // Synchroniser les variables depuis AutomatismNetwork vers Automatism
    // (car applyConfigFromJson dans Network n'a pas accès à Automatism)
    aqThresholdCm = _network.getAqThresholdCm();
    tankThresholdCm = _network.getTankThresholdCm();
    limFlood = _network.getLimFlood();
    heaterThresholdC = _network.getHeaterThresholdC();
    freqWakeSec = _network.getFreqWakeSec();
    
    // Appliquer les autres variables directement depuis le JSON
    auto parseIntValue = [](ArduinoJson::JsonVariantConst value) -> int {
        if (value.is<int>()) return value.as<int>();
        if (value.is<float>()) return static_cast<int>(value.as<float>());
        if (value.is<const char*>()) return atoi(value.as<const char*>());
        return value.as<int>();
    };
    
    // 113: Durée remplissage
    if (doc.containsKey("tempsRemplissageSec") || doc.containsKey("refillDuration") || doc.containsKey("113")) {
        auto v = doc.containsKey("tempsRemplissageSec") ? doc["tempsRemplissageSec"] : 
                 (doc.containsKey("refillDuration") ? doc["refillDuration"] : doc["113"]);
        int refillSec = parseIntValue(v);
        if (refillSec > 0 && refillSec <= 600) {
            refillDurationMs = static_cast<uint32_t>(refillSec) * 1000UL;
            Serial.printf("[Auto] ✅ Durée remplissage mise à jour: %d s\n", refillSec);
        }
    }
    
    // 111: Durée nourrissage gros
    if (doc.containsKey("tempsGros") || doc.containsKey("111")) {
        auto v = doc.containsKey("tempsGros") ? doc["tempsGros"] : doc["111"];
        int value = parseIntValue(v);
        if (value > 0 && value <= 300) {
            tempsGros = static_cast<uint16_t>(value);
        }
    }
    
    // 112: Durée nourrissage petits
    if (doc.containsKey("tempsPetits") || doc.containsKey("112")) {
        auto v = doc.containsKey("tempsPetits") ? doc["tempsPetits"] : doc["112"];
        int value = parseIntValue(v);
        if (value > 0 && value <= 300) {
            tempsPetits = static_cast<uint16_t>(value);
        }
    }
    
    // 105: Heure nourrissage matin
    if (doc.containsKey("bouffeMatin") || doc.containsKey("bm") || doc.containsKey("105")) {
        auto v = doc.containsKey("bouffeMatin") ? doc["bouffeMatin"] : 
                 (doc.containsKey("bm") ? doc["bm"] : doc["105"]);
        int value = parseIntValue(v);
        if (value >= 0 && value <= 23) {
            bouffeMatin = static_cast<uint8_t>(value);
        }
    }
    
    // 106: Heure nourrissage midi
    if (doc.containsKey("bouffeMidi") || doc.containsKey("bmi") || doc.containsKey("106")) {
        auto v = doc.containsKey("bouffeMidi") ? doc["bouffeMidi"] : 
                 (doc.containsKey("bmi") ? doc["bmi"] : doc["106"]);
        int value = parseIntValue(v);
        if (value >= 0 && value <= 23) {
            bouffeMidi = static_cast<uint8_t>(value);
        }
    }
    
    // 107: Heure nourrissage soir
    if (doc.containsKey("bouffeSoir") || doc.containsKey("bs") || doc.containsKey("107")) {
        auto v = doc.containsKey("bouffeSoir") ? doc["bouffeSoir"] : 
                 (doc.containsKey("bs") ? doc["bs"] : doc["107"]);
        int value = parseIntValue(v);
        if (value >= 0 && value <= 23) {
            bouffeSoir = static_cast<uint8_t>(value);
        }
    }
}

// ============================================================================
// HELPERS (Migration progressive)
// ============================================================================

size_t Automatism::createFeedingMessage(char* buffer, size_t bufferSize, const char* type,
                                        uint16_t bigDur, uint16_t smallDur) {
    if (!buffer || bufferSize == 0) return 0;
    
    // Informations système
    char sysInfo[128];
    Utils::getSystemInfo(sysInfo, sizeof(sysInfo));
    
    int n = snprintf(buffer, bufferSize,
        "%s\n\n"
        "Système: %s\n"
        "Heure: %s\n"
        "Durée Gros: %u s\n"
        "Durée Petits: %u s\n"
        "Mode: Manuel\n",
        type,
        sysInfo,
        _power.getCurrentTimeString().c_str(),
        bigDur, smallDur);
        
    return (n >= 0 && (size_t)n < bufferSize) ? n : 0;
}

void Automatism::logActivity(const char* activity) {
  _sleep.logActivity(activity);
}

void Automatism::notifyLocalWebActivity() {
  _sleep.notifyLocalWebActivity();
}

void Automatism::startTankPumpManual() {
    if (tankPumpLocked) {
        Serial.println(F("[Auto] Pompe réservoir verrouillée - commande ignorée"));
        return;
    }
    _acts.startTankPump();
    _lastPumpManual = true;
    _manualTankOverride = true;
}

void Automatism::stopTankPumpManual() {
    _acts.stopTankPump();
    _manualTankOverride = false;
}

// ... Autres méthodes manuelles déléguées à _acts ...
void Automatism::startAquaPumpManualLocal() { _acts.startAquaPump(); }
void Automatism::stopAquaPumpManualLocal() { _acts.stopAquaPump(); }
void Automatism::startHeaterManualLocal() { _acts.startHeater(); }
void Automatism::stopHeaterManualLocal() { _acts.stopHeater(); }
void Automatism::startLightManualLocal() { _acts.startLight(); }
void Automatism::stopLightManualLocal() { _acts.stopLight(); }

// Helpers Affichage
bool Automatism::isFeedingInManualMode() const { return _manualFeedingActive; }
bool Automatism::isRefillingInManualMode() const { return _manualTankOverride; }

// Méthodes privées d'initialisation (simplifiées)
void Automatism::restorePersistentForceWakeup() {
    bool saved = false;
    g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, "forceWakeUp", saved, false);
    forceWakeUp = saved;
    _sleep.setForceWakeUp(saved);
}

void Automatism::restoreActuatorState() {
    // Restauration état précédent après reboot (si pertinent)
    // Logique simplifiée
}

bool Automatism::restoreRemoteConfigFromCache() {
    // Chargement config depuis NVS
    String json;
    if (_config.loadRemoteVars(json)) {
        JsonDocument doc;
        deserializeJson(doc, json);
        
        // Charger l'email directement depuis NVS si disponible
        String emailFromNVS;
        if (g_nvsManager.loadString(NVS_NAMESPACES::CONFIG, "gpio_email", emailFromNVS, "") == NVSError::SUCCESS) {
            if (emailFromNVS.length() > 0) {
                strncpy(_emailAddress, emailFromNVS.c_str(), EmailConfig::MAX_EMAIL_LENGTH - 1);
                _emailAddress[EmailConfig::MAX_EMAIL_LENGTH - 1] = '\0';
            }
        }
        
        applyConfigFromJson(doc);
        return true;
    }
      return false;
}

// ============================================================================
// NOURRISSAGE AUTOMATIQUE
// ============================================================================

void Automatism::handleFeeding() {
    time_t now = _power.getCurrentEpoch();
    struct tm timeinfo;
    if (!localtime_r(&now, &timeinfo)) {
        Serial.println(F("[Auto] ❌ Erreur récupération heure pour nourrissage"));
        return;
    }
    
    int dayOfYear = timeinfo.tm_yday;
    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    
    _feedingSchedule.checkAndFeed(hour, minute, dayOfYear,
                                   bouffeMatin, bouffeMidi, bouffeSoir,
                                   tempsGros, tempsPetits,
                                   _emailAddress, mailNotif,
                                   [this]() { armMailBlink(); },
                                   [this](const char* type) { 
                                       _currentFeedingType = type;
                                       _manualFeedingActive = false;
                                       // Calculer la durée de la phase "Gros"
                                       const uint32_t bigCycleMs = (tempsGros + (tempsGros / 2U)) * 1000UL;
                                       const uint32_t delayMs = 2 * 1000UL;
                                       _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
                                       _feedingPhaseEnd = millis() + bigCycleMs + delayMs;
                                   });
}
