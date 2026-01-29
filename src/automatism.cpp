#include "automatism.h"
#include <Arduino.h>
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include "esp_task_wdt.h"
#include "task_monitor.h"
#include "gpio_parser.h"
#include "nvs_manager.h"
#include <cstring>

namespace {

static void formatDistanceAlert(char* buffer, size_t bufferSize, const char* prefix,
                                float distance, const char* suffix, float threshold) {
  snprintf(buffer, bufferSize, "%s%.1f cm (%s%.1f)", prefix, distance, suffix, threshold);
}

static void formatTemperatureAlert(char* buffer, size_t bufferSize, const char* prefix, float temp) {
  snprintf(buffer, bufferSize, "%s%.1f°C", prefix, temp);
}

}  // namespace

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
{
    // Initialisation des états par défaut
    tankPumpLocked = false;
    forceWakeUp = false;
    _pumpStartMs = 0;
}

void Automatism::begin() {
    Serial.println(F("[Auto] Démarrage Automatism..."));
    
    // v11.162: Timestamp de démarrage pour délayer les alertes non-critiques
    _startupMs = millis();
    Serial.printf("[Auto] Alertes non-critiques différées de %u secondes\n", STARTUP_ALERT_DELAY_MS / 1000);
    
    // Initialisation des sous-modules
    _sleep.begin();
    // Initialisation affichage (anciennement dans AutomatismDisplayController)
    _lastOled = 0;
    _lastScreenSwitch = 0;
    _splashStartTime = millis();
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
    unsigned long now = millis();
    _lastReadings = r;

    // v11.158: Refactorisation - extraction en sous-méthodes pour améliorer lisibilité
    updateFeedingAndDisplay(r, now);
    updateNetworkSync(r, now);
    updateBusinessLogic(r, now);
}

void Automatism::updateFeedingAndDisplay(const SensorReadings& r, uint32_t nowMs) {
    // ========================================
    // PRIORITÉ ABSOLUE : NOURRISSAGE ET REMPLISSAGE
    // ========================================
    
    // 1. Nourrissage automatique (PRIORITÉ 1 - temps critiques)
    handleFeeding();
    
    // 2. Gestion affichage (fusionné depuis AutomatismDisplayController)
    AutomatismRuntimeContext ctx;
    ctx.readings = r;
    ctx.nowMs = nowMs;
    updateDisplayInternal(ctx);

    // 3. Finaliser le cycle de nourrissage si terminé
    finalizeFeedingIfNeeded(nowMs);
}

void Automatism::updateNetworkSync(const SensorReadings& r, uint32_t nowMs) {
    // 4. Gestion réseau (polling commandes)
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    bool pollResult = _network.pollRemoteState(doc, nowMs);
    
    if (pollResult) {
        // 4.1 Parser et appliquer tous les GPIO (actionneurs + configs)
        // GPIOParser::parseAndApply appelle applyConfigFromJson() en interne
        // ce qui synchronise toutes les configurations (seuils, durées, heures)
        GPIOParser::parseAndApply(doc, *this);
        
        // 4.2 Appliquer commandes nourrissage distant
        _network.handleRemoteFeedingCommands(doc, *this);
    }
    
    // 4.3 Envoi périodique des données capteurs (toutes les 2 minutes)
    // Vérifie automatiquement si 2 minutes se sont écoulées depuis le dernier envoi
    _network.update(r, _acts, *this);
}

void Automatism::updateBusinessLogic(const SensorReadings& r, uint32_t nowMs) {
    // 5. Gestion logique métier (fusionné depuis les contrôleurs)
    AutomatismRuntimeContext ctx;
    ctx.readings = r;
    ctx.nowMs = nowMs;
    
    // Remplissage (fusionné depuis AutomatismRefillController)
    uint32_t startMs = millis();
    handleRefill(ctx);
    uint32_t duration = millis() - startMs;
    if (duration > 75) {
        Serial.printf("[Auto] ⚠️ Traitement remplissage long: %u ms\n", static_cast<unsigned>(duration));
    }
    esp_task_wdt_reset();
    
    // Alertes (fusionné depuis AutomatismAlertController)
    handleAlerts(ctx);
    
    // 6. Gestion sommeil (délégué)
    _sleep.process(r, _acts, *this);
}

void Automatism::updateDisplay() {
    SensorReadings r = _sensors.read();
    unsigned long now = millis();
    AutomatismRuntimeContext ctx;
    ctx.readings = r;
    ctx.nowMs = now;
    updateDisplayInternal(ctx);
}

int Automatism::computeDiffMaree(uint16_t currentAqua) {
    int diff = _sensors.diffMaree(currentAqua);
    _lastDiffMaree = diff;
    return diff;
}

uint32_t Automatism::getRecommendedDisplayIntervalMs() {
    return getRecommendedDisplayIntervalMsInternal(millis());
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

void Automatism::finalizeFeedingIfNeeded(uint32_t nowMs) {
    if (_feedingPhaseEnd == 0 || nowMs < _feedingPhaseEnd) {
        return;
    }

    _manualFeedingActive = false;
    _currentFeedingPhase = FeedingPhase::NONE;
    _feedingPhaseEnd = 0;
    _currentFeedingType = nullptr;

    if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
        SensorReadings curReadings = readSensors();
        sendFullUpdate(curReadings, "bouffePetits=0&108=0&bouffeGros=0&109=0");
        Serial.println(F("[Auto] ✅ Variables nourrissage réinitialisées (locales + distantes)"));
    } else {
        Serial.println(F("[Auto] ✅ Variables nourrissage réinitialisées (locales uniquement)"));
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
    
    // Uptime formaté
    unsigned long totalSec = millis() / 1000UL;
    unsigned int days = totalSec / 86400UL;
    totalSec %= 86400UL;
    unsigned int hours = totalSec / 3600UL;
    totalSec %= 3600UL;
    unsigned int mins = totalSec / 60UL;
    unsigned int secs = totalSec % 60UL;
    char uptimeStr[32];
    snprintf(uptimeStr, sizeof(uptimeStr), "%ud %02u:%02u:%02u", days, hours, mins, secs);
    
    // État réseau
    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    const char* wifiStatus;
    char wifiDetail[64] = "";
    if (wifiConnected) {
        char ssidBuf[33];
        WiFiHelpers::getSSID(ssidBuf, sizeof(ssidBuf));
        snprintf(wifiDetail, sizeof(wifiDetail), " (%s)", ssidBuf);
        wifiStatus = "Connecté";
    } else {
        wifiStatus = "Déconnecté";
    }
    
    char timeStr[64];
    _power.getCurrentTimeString(timeStr, sizeof(timeStr));
    
    int n = snprintf(buffer, bufferSize,
        "%s\n\n"
        "Système: %s\n"
        "Heure: %s\n"
        "Durée Gros: %u s\n"
        "Durée Petits: %u s\n"
        "Mode: Manuel\n"
        "Uptime: %s\n"
        "WiFi: %s%s\n",
        type,
        sysInfo,
        timeStr,
        bigDur, smallDur,
        uptimeStr,
        wifiStatus, wifiDetail);
        
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
    if (_acts.isTankPumpRunning()) {
        Serial.println(F("[Auto] Pompe réservoir déjà active - commande ignorée"));
        return;
    }
    const uint32_t nowMs = millis();
    const SensorReadings cur = _sensors.read();
    _acts.startTankPump();
    _lastPumpManual = true;
    _manualTankOverride = true;
    strncpy(_countdownLabel, "Refill", sizeof(_countdownLabel) - 1);
    _countdownLabel[sizeof(_countdownLabel) - 1] = '\0';
    _countdownEnd = nowMs + refillDurationMs;
    _pumpStartMs = nowMs;
    _levelAtPumpStart = cur.wlAqua;
}

void Automatism::stopTankPumpManual() {
    if (!_acts.isTankPumpRunning()) {
        Serial.println(F("[Auto] Pompe réservoir déjà arrêtée - commande ignorée"));
        _manualTankOverride = false;
        _countdownEnd = 0;
        _pumpStartMs = 0;
        return;
    }
    _lastTankStopReason = TankPumpStopReason::MANUAL;
    _acts.stopTankPump(_pumpStartMs);
    _countdownEnd = 0;
    _pumpStartMs = 0;
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
    char json[2048];
    if (_config.loadRemoteVars(json, sizeof(json))) {
        StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
        deserializeJson(doc, json);
        
        // Charger l'email directement depuis NVS si disponible
        char emailFromNVS[128];
        if (g_nvsManager.loadString(NVS_NAMESPACES::CONFIG, "gpio_email", emailFromNVS, sizeof(emailFromNVS), "") == NVSError::SUCCESS) {
            if (strlen(emailFromNVS) > 0) {
                strncpy(_emailAddress, emailFromNVS, EmailConfig::MAX_EMAIL_LENGTH - 1);
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
                                   },
                                   [this]() {
                                       // Callback de fin de nourrissage automatique
                                       // Marquer les événements pour le graphique serveur (gros+petits)
                                       strncpy(bouffePetits, "1", sizeof(bouffePetits) - 1);
                                       bouffePetits[sizeof(bouffePetits) - 1] = '\0';
                                       strncpy(bouffeGros, "1", sizeof(bouffeGros) - 1);
                                       bouffeGros[sizeof(bouffeGros) - 1] = '\0';
                                       Serial.println(F("[Auto] 🍽️ Nourrissage auto terminé - sync serveur"));
                                       
                                       // Envoyer au serveur avec les flags à 1
                                       if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
                                           SensorReadings readings = _sensors.read();
                                           bool ok = sendFullUpdate(readings, nullptr);
                                           Serial.printf("[Auto] 📤 Sync nourrissage auto: %s\n", ok ? "OK" : "FAIL");
                                       }
                                       
                                       // Remettre les flags à 0 pour les prochains envois
                                       strncpy(bouffePetits, "0", sizeof(bouffePetits) - 1);
                                       bouffePetits[sizeof(bouffePetits) - 1] = '\0';
                                       strncpy(bouffeGros, "0", sizeof(bouffeGros) - 1);
                                       bouffeGros[sizeof(bouffeGros) - 1] = '\0';
                                   });
}

// ============================================================================
// MÉTHODES FUSIONNÉES DEPUIS LES CONTRÔLEURS
// ============================================================================

// Sous-fonction: Sécurité aquarium trop plein
void Automatism::handleRefillAquariumOverfillSecurity(const SensorReadings& r) {
    if (r.wlAqua < limFlood) {
        if (!tankPumpLocked || _tankPumpLockReason != TankPumpLockReason::AQUARIUM_OVERFILL) {
            tankPumpLocked = true;
            _tankPumpLockReason = TankPumpLockReason::AQUARIUM_OVERFILL;
            if (_acts.isTankPumpRunning()) {
                _acts.stopTankPump(_pumpStartMs);
                _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
                _countdownEnd = 0;
                if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
                    SensorReadings cur = _sensors.read();
                    sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
                    Serial.println(F("[Auto] Arrêt sécurité notifié - pump_tank=0"));
                }
            }
            Serial.println(F("[CRITIQUE] Aquarium trop plein - pompe verrouillée"));
        }
    } else {
        if (tankPumpLocked && _tankPumpLockReason == TankPumpLockReason::AQUARIUM_OVERFILL && !inFlood) {
            tankPumpLocked = false;
            _tankPumpLockReason = TankPumpLockReason::NONE;
            emailTankSent = false;
            emailTankStartSent = false;
            emailTankStopSent = false;
            Serial.println(F("[Auto] Pompe déverrouillée (aquarium OK)"));
        }
    }
}

// Sous-fonction: Vérification cohérence mode manuel
void Automatism::handleRefillManualModeCheck() {
    if (!_acts.isTankPumpRunning() && _manualTankOverride) {
        Serial.println(F("[CRITIQUE] Mode manuel sans pompe - reset"));
        _manualTankOverride = false;
        _countdownEnd = 0;
        _pumpStartMs = 0;
    }
}

// Sous-fonction: Démarrage automatique (retourne true si bloqué par réserve basse)
bool Automatism::handleRefillAutomaticStart(const SensorReadings& r) {
    if (r.wlAqua > aqThresholdCm && !tankPumpLocked && 
        tankPumpRetries < MAX_PUMP_RETRIES && !_manualTankOverride) {
        if (!_acts.isTankPumpRunning()) {
            // Vérifier si réserve trop basse
            if (r.wlTank > tankThresholdCm) {
                Serial.printf("[CRITIQUE] Réserve basse: %u cm > seuil %u cm\n", 
                              r.wlTank, tankThresholdCm);
                tankPumpLocked = true;
                _tankPumpLockReason = TankPumpLockReason::RESERVOIR_LOW;
                _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
                _countdownEnd = 0;
                const bool startupGrace = (millis() - _startupMs) < STARTUP_ALERT_DELAY_MS;
                if (mailNotif && !emailTankSent && !startupGrace) {
                    char msg[384];
                    snprintf(msg, sizeof(msg),
                             "Remplissage bloqué (réserve basse)\n"
                             "Réserve: %d cm (seuil: %d cm)\nAqua: %d cm",
                             r.wlTank, tankThresholdCm, r.wlAqua);
                    _mailer.sendAlert("Pompe BLOQUÉE (réserve basse)", msg, _emailAddress);
                    emailTankSent = true;
                } else if (startupGrace) {
                    emailTankSent = true;
                }
                return true; // Bloqué
            }
            // Démarrage effectif
            Serial.println(F("[CRITIQUE] === DÉBUT REMPLISSAGE AUTO ==="));
            Serial.printf("[CRITIQUE] Aqua: %d cm, Seuil: %d cm, Durée: %lu s\n", 
                          r.wlAqua, aqThresholdCm, refillDurationMs / 1000);

            _acts.startTankPump();
            strncpy(_countdownLabel, "Refill", sizeof(_countdownLabel) - 1);
            _countdownLabel[sizeof(_countdownLabel) - 1] = '\0';
            _countdownEnd = millis() + refillDurationMs;
            _pumpStartMs = millis();
            _levelAtPumpStart = r.wlAqua;
            logActivity("Démarrage pompe réservoir automatique");

            if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
                SensorReadings currentReadings = _sensors.read();
                sendFullUpdate(currentReadings, "etatPompeTank=1");
            }

            if (mailNotif && !emailTankStartSent) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "Remplissage AUTO démarré\nAqua: %d cm, Réserve: %d cm, Durée: %lu s",
                         r.wlAqua, r.wlTank, refillDurationMs / 1000);
                _mailer.send("Remplissage démarré", msg, "System", _emailAddress);
                emailTankStartSent = true;
                emailTankStopSent = false;
            }
        }
    }
    return false;
}

// Sous-fonction: Fin cycle manuel
void Automatism::handleRefillManualCycleEnd(const SensorReadings& r) {
    if (_manualTankOverride && _acts.isTankPumpRunning()) {
        if (_countdownEnd > 0 && millis() >= _countdownEnd) {
            Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE MANUEL ==="));
            _acts.stopTankPump(_pumpStartMs);
            _lastTankStopReason = TankPumpStopReason::MANUAL;
            _manualTankOverride = false;
            _countdownEnd = 0;
            _pumpStartMs = 0;

            if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
                SensorReadings cur = _sensors.read();
                sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
            }

            if (mailNotif && !emailTankStopSent) {
                SensorReadings cur = _sensors.read();
                int levelImprovement = _levelAtPumpStart - cur.wlAqua;
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "Remplissage MANUEL terminé\nAmélioration: %d cm, Aqua: %d cm",
                         levelImprovement, cur.wlAqua);
                _mailer.send("Remplissage terminé", msg, "System", _emailAddress);
                emailTankStopSent = true;
                emailTankStartSent = false;
            }
        }
    }
}

// Sous-fonction: Arrêt forcé après durée max
void Automatism::handleRefillMaxDurationStop(const SensorReadings& r) {
    if (!_acts.isTankPumpRunning()) return;

    const uint32_t nowMs = millis();
    const uint32_t elapsedMs = (uint32_t)(nowMs - _pumpStartMs);
    const uint32_t maxMs = refillDurationMs;
    
    // Détection anomalie timing (>50 min)
    if (elapsedMs > 3000000UL) {
        Serial.printf("[CRITIQUE] Anomalie timing: elapsed=%u ms, reset\n", (unsigned)elapsedMs);
        _pumpStartMs = nowMs;
        return;
    }
    
    if (elapsedMs < maxMs) return;

    // Arrêt forcé
    Serial.println(F("[CRITIQUE] === ARRÊT FORCÉ REMPLISSAGE ==="));
    Serial.printf("[CRITIQUE] Durée: %u s / %u s max\n", 
                  (unsigned)(elapsedMs/1000U), (unsigned)(maxMs/1000U));

    _lastTankStopReason = TankPumpStopReason::MAX_DURATION;
    _acts.stopTankPump(_pumpStartMs);
    _pumpStartMs = 0;
    _countdownEnd = 0;
    _manualTankOverride = false;

    if (WiFi.status() == WL_CONNECTED) {
        SensorReadings cur = _sensors.read();
        sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
    }

    int levelImprovement = _levelAtPumpStart - r.wlAqua;
    Serial.printf("[CRITIQUE] Amélioration niveau: %d cm\n", levelImprovement);

    if (levelImprovement < 1) {
        ++tankPumpRetries;
        Serial.printf("[CRITIQUE] Pompe inefficace (%u/%u)\n", tankPumpRetries, MAX_PUMP_RETRIES);
        if (tankPumpRetries >= MAX_PUMP_RETRIES) {
            tankPumpLocked = true;
            _tankPumpLockReason = TankPumpLockReason::INEFFICIENT;
            Serial.println(F("[CRITIQUE] Pompe BLOQUÉE - max tentatives"));
            sendFullUpdate(r, "etatPompeTank=0&pump_tankCmd=0&pump_tank=0");
            if (mailNotif && !emailTankSent) {
                char msg[384];
                snprintf(msg, sizeof(msg),
                         "Pompe BLOQUÉE (inefficace)\nTentatives: %d/%d, Amélioration: %d cm",
                         tankPumpRetries, (unsigned)MAX_PUMP_RETRIES, levelImprovement);
                _mailer.sendAlert("Pompe réservoir bloquée", msg, _emailAddress);
                emailTankSent = true;
            }
        }
    } else {
        tankPumpRetries = 0;
        Serial.printf("[CRITIQUE] Remplissage OK: +%d cm\n", levelImprovement);
        if (mailNotif && !emailTankStopSent) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Remplissage TERMINÉ\nDurée: %u s, Amélioration: %d cm, Aqua: %d cm",
                     (unsigned)(elapsedMs / 1000), levelImprovement, r.wlAqua);
            _mailer.send("Remplissage terminé", msg, "System", _emailAddress);
            emailTankStopSent = true;
            emailTankStartSent = false;
        }
    }
    Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE ==="));
}

// Sous-fonction: Sécurité réserve basse avec hystérésis
void Automatism::handleRefillReservoirLowSecurity(const SensorReadings& r) {
    if (_manualTankOverride) return;

    static uint8_t aboveCount = 0;
    static uint8_t belowCount = 0;
    
    if (r.wlTank > tankThresholdCm) {
        aboveCount = min<uint8_t>(aboveCount + 1, 3);
        belowCount = 0;
        if (!tankPumpLocked && aboveCount >= 2) {
            Serial.println(F("[CRITIQUE] === SÉCURITÉ RÉSERVE BASSE ==="));
            Serial.printf("[CRITIQUE] Réservoir: %d cm, Seuil: %d cm\n", r.wlTank, tankThresholdCm);
            tankPumpLocked = true;
            _tankPumpLockReason = TankPumpLockReason::RESERVOIR_LOW;
            _acts.stopTankPump(_pumpStartMs);
            _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
            _countdownEnd = 0;
            const bool startupGrace = (millis() - _startupMs) < STARTUP_ALERT_DELAY_MS;
            if (mailNotif && !emailTankSent && !startupGrace) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "Pompe VERROUILLÉE (réserve basse)\nRéserve: %d cm (seuil: %d cm)",
                         r.wlTank, tankThresholdCm);
                _mailer.sendAlert("Pompe verrouillée (réserve basse)", msg, _emailAddress);
                emailTankSent = true;
            } else if (startupGrace) {
                emailTankSent = true;
            }
        }
    } else if (r.wlTank < tankThresholdCm - 5) {
        belowCount = min<uint8_t>(belowCount + 1, 3);
        aboveCount = 0;
        if (tankPumpLocked && belowCount >= 3) {
            tankPumpLocked = false;
            _tankPumpLockReason = TankPumpLockReason::NONE;
            emailTankSent = false;
            emailTankStartSent = false;
            emailTankStopSent = false;
            Serial.printf("[Auto] Pompe déverrouillée (réserve: %d cm)\n", r.wlTank);
        }
    } else {
        aboveCount = min<uint8_t>(aboveCount, 2);
        belowCount = min<uint8_t>(belowCount, 2);
    }
}

// Sous-fonction: Récupération automatique après blocage
void Automatism::handleRefillAutomaticRecovery(const SensorReadings& r) {
    static unsigned long lastRecoveryAttempt = 0;
    if (tankPumpLocked && tankPumpRetries >= MAX_PUMP_RETRIES) {
        unsigned long currentMillisLocal = millis();
        if (currentMillisLocal - lastRecoveryAttempt > 30 * 1000UL) {
            if (r.wlTank < tankThresholdCm - 10) {
                Serial.println(F("[CRITIQUE] === RÉCUPÉRATION AUTO ==="));
                Serial.printf("[CRITIQUE] Réservoir: %d cm (OK)\n", r.wlTank);
                tankPumpLocked = false;
                tankPumpRetries = 0;
                emailTankSent = false;
                emailTankStartSent = false;
                emailTankStopSent = false;
                _tankPumpLockReason = TankPumpLockReason::NONE;
                lastRecoveryAttempt = currentMillisLocal;
                Serial.println(F("[CRITIQUE] Pompe débloquée"));
            }
        }
    }
}

// Fonction principale refactorisée - appelle les sous-fonctions
void Automatism::handleRefill(const AutomatismRuntimeContext& ctx) {
    const SensorReadings& r = ctx.readings;

    // 0. Sécurité aquarium trop plein
    handleRefillAquariumOverfillSecurity(r);

    // 1. Vérification cohérence mode manuel
    handleRefillManualModeCheck();

    // 2. Démarrage automatique (si conditions remplies)
    if (handleRefillAutomaticStart(r)) {
        return; // Bloqué par réserve basse
    }

    // 3. Fin cycle manuel (timeout)
    handleRefillManualCycleEnd(r);

    // 4. Arrêt forcé après durée max
    handleRefillMaxDurationStop(r);

    // 5. Sécurité réserve basse avec hystérésis
    handleRefillReservoirLowSecurity(r);

    // 6. Récupération automatique après blocage
    handleRefillAutomaticRecovery(r);
}

// Fusionné depuis AutomatismAlertController::process()
void Automatism::handleAlerts(const AutomatismRuntimeContext& ctx) {
    const SensorReadings& readings = ctx.readings;
    const bool mailEnabled = mailNotif;
    
    // v11.162: Délai au démarrage pour éviter saturation queue mail
    // Les alertes non-critiques sont différées de 30s après le boot
    const bool startupGracePeriod = (millis() - _startupMs) < STARTUP_ALERT_DELAY_MS;
    if (startupGracePeriod && mailEnabled) {
        // Pendant la période de grâce, on met à jour les flags mais on n'envoie pas
        // Cela évite d'envoyer des alertes pour des conditions pré-existantes au boot
        if (readings.wlAqua > aqThresholdCm) _lowAquaSent = true;  // Déjà en alerte, pas de mail
        if (readings.wlTank > tankThresholdCm) _lowTankSent = true;  // Déjà en alerte, pas de mail
        return;  // Pas d'alertes pendant la période de grâce
    }

    if (readings.wlAqua > aqThresholdCm && !_lowAquaSent && mailEnabled) {
        char msgBuffer[128];
        formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", readings.wlAqua, " cm (> ", aqThresholdCm);
        _mailer.sendAlert("Alerte - Niveau aquarium BAS", msgBuffer, _emailAddress);
        _lowAquaSent = true;
        armMailBlink();
    }

    if (readings.wlAqua <= aqThresholdCm - 5) {
        _lowAquaSent = false;
    }

    time_t nowEpoch = _power.getCurrentEpoch();
    if (readings.wlAqua < limFlood) {
        if (floodEnterSinceEpoch == 0) {
            floodEnterSinceEpoch = nowEpoch;
        }
        aboveResetSinceEpoch = 0;
        if (!inFlood && mailEnabled) {
            uint32_t elapsedUnder = (nowEpoch >= floodEnterSinceEpoch) ? (nowEpoch - floodEnterSinceEpoch) : 0;
            bool debounceOk = elapsedUnder >= (floodDebounceMin * 60UL);
            bool cooldownOk = (lastFloodEmailEpoch == 0) || ((nowEpoch - lastFloodEmailEpoch) >= (floodCooldownMin * 60UL));
            if (debounceOk && cooldownOk) {
                char msgBuffer[128];
                formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", readings.wlAqua, " cm (< ", limFlood);
                if (tankPumpLocked || _config.getPompeAquaLocked()) {
                    strncat(msgBuffer, " / Pompe verrouillée", sizeof(msgBuffer) - strlen(msgBuffer) - 1);
                }
                bool sent = _mailer.sendAlert("Alerte - Aquarium TROP PLEIN", msgBuffer, _emailAddress);
                if (sent) {
                    inFlood = true;
                    _highAquaSent = true;
                    armMailBlink();
                    lastFloodEmailEpoch = nowEpoch;
                    g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "alert_floodLast", lastFloodEmailEpoch);
                    Serial.println(F("[Auto] Email TROP PLEIN envoyé (anti-spam actif)"));
                } else {
                    Serial.println(F("[Auto] Échec envoi email TROP PLEIN"));
                }
            }
        }
    } else {
        floodEnterSinceEpoch = 0;
        if (readings.wlAqua >= limFlood + floodHystCm) {
            if (aboveResetSinceEpoch == 0) {
                aboveResetSinceEpoch = nowEpoch;
            }
            uint32_t elapsedAbove = (nowEpoch >= aboveResetSinceEpoch) ? (nowEpoch - aboveResetSinceEpoch) : 0;
            if (elapsedAbove >= (floodResetStableMin * 60UL)) {
                inFlood = false;
                _highAquaSent = false;
            }
        } else {
            aboveResetSinceEpoch = 0;
        }
    }

    if (readings.wlTank > tankThresholdCm && !_lowTankSent && mailEnabled) {
        char msgBuffer[128];
        formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", readings.wlTank, " cm (> ", tankThresholdCm);
        _mailer.sendAlert("Alerte - Réserve BASSE", msgBuffer, _emailAddress);
        _lowTankSent = true;
        armMailBlink();
    } else if (_lowTankSent && readings.wlTank <= tankThresholdCm - 5 && mailEnabled) {
        char msgBuffer[128];
        formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", readings.wlTank, " cm (<= ", tankThresholdCm - 5);
        _mailer.sendAlert("Info - Réserve OK", msgBuffer, _emailAddress);
        _lowTankSent = false;
        armMailBlink();
    }

    if (readings.tempWater < heaterThresholdC && !heaterPrevState) {
        _acts.startHeater();
        heaterPrevState = true;
        if (mailEnabled) {
            char msgBuffer[64];
            formatTemperatureAlert(msgBuffer, sizeof(msgBuffer), "Temp eau: ", readings.tempWater);
            _mailer.sendAlert("Chauffage ON", msgBuffer, _emailAddress);
            armMailBlink();
        }
    } else if (readings.tempWater > heaterThresholdC + 2 && heaterPrevState) {
        _acts.stopHeater();
        heaterPrevState = false;
        if (mailEnabled) {
            char msgBuffer[64];
            formatTemperatureAlert(msgBuffer, sizeof(msgBuffer), "Temp eau: ", readings.tempWater);
            _mailer.sendAlert("Chauffage OFF", msgBuffer, _emailAddress);
            armMailBlink();
        }
    }

    esp_task_wdt_reset();
}

// Fusionné depuis AutomatismDisplayController
void Automatism::updateDisplayInternal(const AutomatismRuntimeContext& ctx) {
    const uint32_t providedNow = ctx.nowMs;
    const uint32_t currentMillis = providedNow == 0 ? millis() : providedNow;

    if (!_disp.isPresent()) return;
    if (_disp.isLocked()) return;

    // Gestion du splash screen (timeout de sécurité)
    if (_splashStartTime == 0) {
        // Déjà terminé
    } else if (currentMillis - _splashStartTime > DisplayConfig::SPLASH_DURATION_MS + 2000) {
        // Timeout de sécurité : 2 secondes après la durée normale
        _disp.forceEndSplash();
        _splashStartTime = 0;
    }

    // Basculement automatique d'écran
    if (_lastScreenSwitch == 0) {
        _lastScreenSwitch = currentMillis;
    } else if (currentMillis - _lastScreenSwitch >= DisplayConfig::SCREEN_SWITCH_INTERVAL_MS) {
        _oledToggle = !_oledToggle;
        _lastScreenSwitch += DisplayConfig::SCREEN_SWITCH_INTERVAL_MS;
        _disp.resetMainCache();
        _disp.resetStatusCache();
        _disp.resetVariablesCache();
    }

    // Intervalle de rafraîchissement
    bool hasCountdown = (_countdownEnd > 0 && currentMillis < _countdownEnd);
    bool hasFeedingPhase = (_currentFeedingPhase != FeedingPhase::NONE && currentMillis < _feedingPhaseEnd);
    bool isCountdownMode = hasCountdown || hasFeedingPhase;
    
    // Utiliser l'intervalle recommandé (250ms si countdown, sinon 80ms)
    const uint32_t OLED_INTERVAL_MS = 80;
    const uint32_t displayInterval = isCountdownMode ? 250u : OLED_INTERVAL_MS;
    
    if (currentMillis - _lastOled < displayInterval) {
        return;
    }
    
    _lastOled = currentMillis;

    SensorReadings readings = ctx.readings;

    // Validation basique des lectures pour affichage
    if (readings.tempWater < -50 || readings.tempWater > 100 ||
        readings.tempAir < -50 || readings.tempAir > 100 ||
        readings.humidity < 0 || readings.humidity > 100) {
        readings.tempWater = NAN;
        readings.tempAir = NAN;
        readings.humidity = NAN;
        readings.wlAqua = 0;
        readings.wlTank = 0;
        readings.wlPota = 0;
        readings.luminosite = 0;
    }

    // Calcul de la différence de marée
    int diffMaree = _lastDiffMaree;
    
    if (diffMaree == -1 && _lastReadings.wlAqua > 0) {
        if (readings.wlAqua > 0 && _lastReadings.wlAqua > 0) {
            diffMaree = static_cast<int>(readings.wlAqua) - static_cast<int>(_lastReadings.wlAqua);
        } else {
            diffMaree = 0;
        }
    } else if (diffMaree == -1) {
        diffMaree = 0;
    }
    
    // Calcul de la direction de marée
    int8_t tideDir = 0;
    if (diffMaree > 1) {
        tideDir = 1; // Montée
    } else if (diffMaree < -1) {
        tideDir = -1; // Descente
    } else {
        tideDir = 0; // Stable
    }
    
    // Affichage du countdown si actif
    if (hasCountdown && strlen(_countdownLabel) > 0) {
        uint32_t secLeft = (_countdownEnd > currentMillis) ? 
                          ((_countdownEnd - currentMillis) / 1000) : 0;
        bool isManual = _manualFeedingActive;
        _disp.showCountdown(_countdownLabel, secLeft, isManual);
        bool mailBlink = (mailBlinkUntil > 0 && currentMillis < mailBlinkUntil);
        int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
        _disp.drawStatusEx(sendState, recvState, rssi, mailBlink, tideDir, diffMaree, false);
        return;
    }
    
    // Affichage du countdown de nourrissage si une phase est active
    if (hasFeedingPhase) {
        const char* fishType = "Auto";
        uint32_t secLeft = (_feedingPhaseEnd > currentMillis) ? 
                          ((_feedingPhaseEnd - currentMillis) / 1000) : 0;
        
        if (_currentFeedingType != nullptr) {
            fishType = _currentFeedingType;
        } else if (!_manualFeedingActive) {
            const uint32_t bigCycleMs = (tempsGros + (tempsGros / 2U)) * 1000UL;
            const uint32_t delayMs = 2 * 1000UL;
            const uint32_t bigPhaseTotalMs = bigCycleMs + delayMs;
            const uint32_t smallCycleMs = (tempsPetits + (tempsPetits / 2U)) * 1000UL;
            const uint32_t totalFeedingMs = bigPhaseTotalMs + smallCycleMs;
            
            uint32_t elapsedMs = (_feedingPhaseEnd > currentMillis) ? 
                                 (totalFeedingMs - ((_feedingPhaseEnd - currentMillis))) : totalFeedingMs;
            
            if (elapsedMs < bigPhaseTotalMs) {
                fishType = "Gros";
            } else {
                fishType = "Petits";
            }
        }
        
        const char* phase = (_currentFeedingPhase == FeedingPhase::FEEDING_FORWARD) 
                          ? "Avant" : "Retour";
        bool isManual = _manualFeedingActive;
        _disp.showFeedingCountdown(fishType, phase, secLeft, isManual);
        bool mailBlink = (mailBlinkUntil > 0 && currentMillis < mailBlinkUntil);
        int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
        _disp.drawStatusEx(sendState, recvState, rssi, mailBlink, tideDir, diffMaree, false);
        return;
    }
    
    // Affichage normal (écran principal ou écran variables)
    char timeStr[64];
    _power.getCurrentTimeString(timeStr, sizeof(timeStr));
    bool mailBlink = (mailBlinkUntil > 0 && currentMillis < mailBlinkUntil);
    int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
    
    // Basculement entre écran principal et écran variables
    if (_oledToggle) {
        // Écran principal avec données des capteurs
        _disp.showMain(
            readings.tempWater,
            readings.tempAir,
            readings.humidity,
            readings.wlAqua,
            readings.wlTank,
            readings.wlPota,
            readings.luminosite,
            timeStr
        );
    } else {
        // Écran variables avec états des actionneurs
        _disp.showVariables(
            _acts.isAquaPumpRunning(),
            _acts.isTankPumpRunning(),
            _acts.isHeaterOn(),
            _acts.isLightOn(),
            bouffeMatin,
            bouffeMidi,
            bouffeSoir,
            tempsPetits,
            tempsGros,
            aqThresholdCm,
            tankThresholdCm,
            heaterThresholdC,
            limFlood
        );
    }
    
    // Toujours afficher la barre d'état (en overlay)
    _disp.drawStatus(sendState, recvState, rssi, mailBlink, tideDir, diffMaree);
}

uint32_t Automatism::getRecommendedDisplayIntervalMsInternal(uint32_t nowMs) const {
    // Retourne l'intervalle par défaut pour l'affichage (80ms)
    // L'ajustement pour les countdowns (250ms) se fait dans updateDisplayInternal()
    return 80; // OLED_INTERVAL_MS
}

// ============================================================================
// MÉTHODES DE PERSISTANCE (fusionnées depuis AutomatismPersistence)
// ============================================================================

// Snapshots sleep/wake
void Automatism::saveActuatorSnapshotToNVS(bool pumpAquaWasOn, bool heaterWasOn, bool lightWasOn) {
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_pending", true);
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_aqua", pumpAquaWasOn);
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_heater", heaterWasOn);
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_light", lightWasOn);
    
    Serial.printf("[Auto] Snapshot actionneurs NVS: aqua=%s heater=%s light=%s\n",
                  pumpAquaWasOn?"ON":"OFF", heaterWasOn?"ON":"OFF", lightWasOn?"ON":"OFF");
}

bool Automatism::loadActuatorSnapshotFromNVS(bool& pumpAquaWasOn, bool& heaterWasOn, bool& lightWasOn) {
    bool pending;
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, "snap_pending", pending, false);
    
    if (!pending) {
        return false;
    }
    
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, "snap_aqua", pumpAquaWasOn, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, "snap_heater", heaterWasOn, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, "snap_light", lightWasOn, false);
    
    Serial.printf("[Auto] Snapshot chargé depuis NVS: aqua=%s heater=%s light=%s\n",
                  pumpAquaWasOn?"ON":"OFF", heaterWasOn?"ON":"OFF", lightWasOn?"ON":"OFF");
    
    return true;
}

void Automatism::clearActuatorSnapshotInNVS() {
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_pending", false);
    Serial.println("[Auto] Snapshot actionneurs effacé");
}

// États actuels persistants (méthodes statiques pour compatibilité avec web_server.cpp)
void Automatism::saveCurrentActuatorState(const char* actuator, bool state) {
    char key[32];
    snprintf(key, sizeof(key), "state_%s", actuator);
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, key, state);
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, "state_lastLocal", millis());
    Serial.printf("[Auto] État %s=%s sauvegardé en NVS (priorité locale)\n",
                   actuator, state ? "ON" : "OFF");
}

bool Automatism::loadCurrentActuatorState(const char* actuator, bool defaultValue) {
    char key[32];
    snprintf(key, sizeof(key), "state_%s", actuator);
    bool state;
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, key, state, defaultValue);
    return state;
}

uint32_t Automatism::getLastLocalActionTime() {
    unsigned long timestamp;
    g_nvsManager.loadULong(NVS_NAMESPACES::STATE, "state_lastLocal", timestamp, 0);
    return timestamp;
}

bool Automatism::hasRecentLocalAction(uint32_t timeoutMs) {
    uint32_t lastAction = getLastLocalActionTime();
    if (lastAction == 0) return false;
    uint32_t elapsed = millis() - lastAction;
    return elapsed < timeoutMs;
}

// Pending sync
void Automatism::markPendingSync(const char* actuator, bool state) {
    char key_state[32];
    snprintf(key_state, sizeof(key_state), "sync_%s", actuator);
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, key_state, state);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
    
    bool alreadyPending = false;
    for (int i = 0; i < count; i++) {
        char key_item[16];
        snprintf(key_item, sizeof(key_item), "sync_item_%d", i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, key_item, item, sizeof(item), "");
        if (strcmp(item, actuator) == 0) {
            alreadyPending = true;
            break;
        }
    }
    
    if (!alreadyPending) {
        char key_item[16];
        snprintf(key_item, sizeof(key_item), "sync_item_%d", count);
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, key_item, actuator);
        count++;
        g_nvsManager.saveInt(NVS_NAMESPACES::STATE, "sync_count", count);
    }
    
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, "sync_lastSync", millis());
    Serial.printf("[Auto] ⏳ Pending sync marqué: %s=%s (total: %u)\n",
                   actuator, state ? "ON" : "OFF", count);
}

void Automatism::markConfigPendingSync() {
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "sync_config", true);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
    
    bool alreadyPending = false;
    for (int i = 0; i < count; i++) {
        char key_item[16];
        snprintf(key_item, sizeof(key_item), "sync_item_%d", i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, key_item, item, sizeof(item), "");
        if (strcmp(item, "config") == 0) {
            alreadyPending = true;
            break;
        }
    }
    
    if (!alreadyPending) {
        char key_item[16];
        snprintf(key_item, sizeof(key_item), "sync_item_%d", count);
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, key_item, "config");
        count++;
        g_nvsManager.saveInt(NVS_NAMESPACES::STATE, "sync_count", count);
    }
    
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, "sync_lastSync", millis());
    Serial.printf("[Auto] ⏳ Config pending sync marquée (total: %u)\n", count);
}

void Automatism::clearPendingSync(const char* actuator) {
    char key_state[32];
    snprintf(key_state, sizeof(key_state), "sync_%s", actuator);
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, key_state);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
    
    int newCount = 0;
    for (int i = 0; i < count; i++) {
        char oldKey[16];
        snprintf(oldKey, sizeof(oldKey), "sync_item_%d", i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, oldKey, item, sizeof(item), "");
        
        if (strcmp(item, actuator) != 0 && strlen(item) > 0) {
            if (newCount != i) {
                char newKey[16];
                snprintf(newKey, sizeof(newKey), "sync_item_%d", newCount);
                g_nvsManager.saveString(NVS_NAMESPACES::STATE, newKey, item);
            }
            newCount++;
        }
    }
    
    for (int i = newCount; i < count; i++) {
        char oldKey[16];
        snprintf(oldKey, sizeof(oldKey), "sync_item_%d", i);
        g_nvsManager.removeKey(NVS_NAMESPACES::STATE, oldKey);
    }
    
    g_nvsManager.saveInt(NVS_NAMESPACES::STATE, "sync_count", newCount);
    Serial.printf("[Auto] ✅ Pending sync effacé: %s (reste: %u)\n", actuator, newCount);
}

void Automatism::clearConfigPendingSync() {
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, "sync_config");
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
    
    int newCount = 0;
    for (int i = 0; i < count; i++) {
        char oldKey[16];
        snprintf(oldKey, sizeof(oldKey), "sync_item_%d", i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, oldKey, item, sizeof(item), "");
        
        if (strcmp(item, "config") != 0 && strlen(item) > 0) {
            if (newCount != i) {
                char newKey[16];
                snprintf(newKey, sizeof(newKey), "sync_item_%d", newCount);
                g_nvsManager.saveString(NVS_NAMESPACES::STATE, newKey, item);
            }
            newCount++;
        }
    }
    
    for (int i = newCount; i < count; i++) {
        char oldKey[16];
        snprintf(oldKey, sizeof(oldKey), "sync_item_%d", i);
        g_nvsManager.removeKey(NVS_NAMESPACES::STATE, oldKey);
    }
    
    g_nvsManager.saveInt(NVS_NAMESPACES::STATE, "sync_count", newCount);
    Serial.printf("[Auto] ✅ Config pending sync effacée (reste: %u)\n", newCount);
}

bool Automatism::hasPendingSync() {
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
    return count > 0;
}

uint8_t Automatism::getPendingSyncCount() {
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
    return (uint8_t)count;
}

uint32_t Automatism::getLastPendingSyncTime() {
    unsigned long timestamp;
    g_nvsManager.loadULong(NVS_NAMESPACES::STATE, "sync_lastSync", timestamp, 0);
    return timestamp;
}
