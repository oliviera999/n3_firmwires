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

    // 1. Gestion affichage (délégué)
    AutomatismRuntimeContext ctx;
    ctx.readings = r;
    ctx.nowMs = now;
    _displayController.updateDisplay(ctx, _acts, *this);

    // 2. Gestion réseau (polling commandes)
    JsonDocument doc;
    if (_network.pollRemoteState(doc, now)) {
        // Appliquer commandes reçues
        _network.handleRemoteFeedingCommands(doc, *this);
    }

    // 3. Gestion logique métier via contrôleurs
    _refillController.process(ctx);
    _alertController.process(ctx);
    
    // 4. Gestion sommeil (délégué)
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
    _feeding.feedSmall();
  _manualFeedingActive = true;
    _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD; // Début animation
    _feedingPhaseEnd = millis() + (_feeding.getSmallDuration() * 1000);
}

void Automatism::manualFeedBig() {
    Serial.println(F("[Auto] Nourrissage manuel GROS déclenché"));
    _feeding.feedBig();
  _manualFeedingActive = true;
    _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
    _feedingPhaseEnd = millis() + (_feeding.getBigDuration() * 1000);
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
    // Appliquer localement les changements (seuils, etc.)
    // Logique de parsing simplifiée ici ou déléguée
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
        applyConfigFromJson(doc);
        return true;
    }
      return false;
}
