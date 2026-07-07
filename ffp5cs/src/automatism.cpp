#include "automatism.h"
#include <Arduino.h>
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include "esp_task_wdt.h"
#include "task_monitor.h"
#include "gpio_parser.h"
#include "nvs_manager.h"
#include "nvs_keys.h"  // v11.176: Constantes NVS centralisées
#include "dbvars_cache.h"
#include "automatism/actuator_snapshot.h"  // C4: capture/restore actionneurs (testable via IActuators)
#include "automatism/feeding_timing.h"  // C4: durée de cycle nourrissage (pure, testée, dédup ×6)
#include "automatism/feeding_finalize_orchestrator.h"  // C4: sync fin de cycle nourrissage (testable)
#include <cstring>
#include <cstdio>
#include <memory>

// ============================================================================
// Automatism: Chef d'orchestre
// Responsabilité: Coordonner les modules spécialisés
// ============================================================================

Automatism::Automatism(SystemSensors& sensors, IActuators& acts, WebClient& web,
                       DisplayView& disp, PowerManager& power, IMailer& mail, ConfigManager& config)
    : _sensors(sensors)
    , _acts(acts)
    , _web(web)
    , _disp(disp)
    , _power(power)
    , _mailer(mail)
    , _config(config)
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
  // v11.178: restoreActuatorState() supprimé (code mort - audit dead-code)
    restoreRemoteConfigFromCache();

    // Phase 0 (arbitrage mails) : relire le cooldown anti-spam trop-plein persisté.
    // ALERT_FLOOD_LAST était écrit à chaque envoi mais jamais relu -> après un
    // reboot, le cooldown repartait de zéro (risque de re-spam immédiat).
    {
        unsigned long lastFloodTs = 0;
        g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, NVSKeys::Automatism::ALERT_FLOOD_LAST, lastFloodTs, 0);
        lastFloodEmailEpoch = static_cast<uint32_t>(lastFloodTs);
        if (lastFloodEmailEpoch != 0) {
            Serial.printf("[Auto] Cooldown trop-plein restauré depuis NVS (dernier mail epoch=%lu)\n",
                          static_cast<unsigned long>(lastFloodEmailEpoch));
        }
    }
    
    Serial.println(F("[Auto] Initialisation terminée"));
}

void Automatism::update() {
    // Collecte des capteurs
    SensorReadings readings = _sensors.read();
    
    // v11.176: Utilisation des helpers de validation (audit élimination duplications)
    readings.tempWater = SensorValidation::sanitizeWaterTemp(readings.tempWater);
    readings.tempAir = SensorValidation::sanitizeAirTemp(readings.tempAir);
    readings.humidity = SensorValidation::sanitizeHumidity(readings.humidity);
    
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

void Automatism::applyRemoteGpioConfig(const ArduinoJson::JsonDocument& doc) {
    if (doc.size() == 0) {
        Serial.println(F("[Auto] applyRemoteGpioConfig: doc vide, ignoré"));
        return;
    }
    _network.seedInitialStateIfFirstPoll(doc);
    GPIOParser::parseAndApply(doc, *this);
    invalidateDbvarsCache();
    Serial.printf(
        "[Sync] Config RAM appliquée: bouffeMatin=%u bouffeMidi=%u bouffeSoir=%u "
        "aqThr=%u tankThr=%u FreqWake=%u\n",
        getBouffeMatin(), getBouffeMidi(), getBouffeSoir(),
        getAqThresholdCm(), getTankThresholdCm(), getFreqWakeSec());
}

void Automatism::updateNetworkSync(const SensorReadings& r, uint32_t nowMs) {
    // 4. Gestion réseau (polling commandes)
    std::unique_ptr<StaticJsonDocument<BufferConfig::OUTPUTS_STATE_JSON_DOCUMENT_SIZE>> doc(
        new (std::nothrow) StaticJsonDocument<BufferConfig::OUTPUTS_STATE_JSON_DOCUMENT_SIZE>());
    bool pollResult = false;
    if (doc) {
        pollResult = _network.pollRemoteState(*doc, nowMs);
    } else {
        Serial.println(F("[Sync] Poll distant ignoré: heap insuffisante pour JSON outputs/state"));
    }

    if (pollResult && doc && doc->size() > 0) {
        Serial.printf("[DBG] updateNetworkSync poll OK docSize=%u\n", (unsigned)doc->size());
        applyRemoteGpioConfig(*doc);
    } else if (pollResult) {
        Serial.println(F("[DBG] updateNetworkSync poll OK mais doc vide — pas d'application"));
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
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
    
    // Alertes (fusionné depuis AutomatismAlertController)
    handleAlerts(ctx);
    
    // 6. Gestion sommeil (délégué)
    _sleep.handleAutoSleep(r, _acts, *this);
}

void Automatism::updateDisplay() {
    SensorReadings r;
    if (!_sensors.getLastCachedReadings(r)) r = _lastReadings;
    unsigned long now = millis();
    AutomatismRuntimeContext ctx;
    ctx.readings = r;
    ctx.nowMs = now;
    updateDisplayInternal(ctx);
}

void Automatism::updateDisplayWithReadings(const SensorReadings& r) {
    AutomatismRuntimeContext ctx(r, millis());
    updateDisplayInternal(ctx);
}

int Automatism::computeDiffMaree(uint16_t currentAqua) {
    int diff = _sensors.diffMaree(currentAqua);
    _lastDiffMaree = diff;
    return diff;
}

// ============================================================================
// ACTIONS MANUELLES (Exposées pour le serveur Web)
// ============================================================================

void Automatism::manualFeedSmall() {
    Serial.println(F("[Auto] Nourrissage manuel PETITS déclenché"));
    Serial.printf("[Auto] Durée configurée: %u s\n", tempsPetits);
    _acts.feedSmallFish(tempsPetits);
    _manualFeedingActive = true;
    _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
    const uint32_t cycleMs = FeedingTiming::cycleDurationMs(tempsPetits);
    _feedingPhaseEnd = millis() + cycleMs;
    _currentFeedingType = "Petits";
}

void Automatism::manualFeedBig() {
    Serial.println(F("[Auto] Nourrissage manuel GROS déclenché"));
    Serial.printf("[Auto] Durée configurée: %u s\n", tempsGros);
    _acts.feedBigFish(tempsGros);
    _manualFeedingActive = true;
    _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
    const uint32_t cycleMs = FeedingTiming::cycleDurationMs(tempsGros);
    _feedingPhaseEnd = millis() + cycleMs;
    _currentFeedingType = "Gros";
}

void Automatism::manualFeedBoth() {
    Serial.println(F("[Auto] Nourrissage manuel GROS+PETITS (séquentiel) déclenché"));
    Serial.printf("[Auto] Durées: gros=%u s, petits=%u s\n", tempsGros, tempsPetits);
    // feedSequential refuse (false) si une séquence est déjà en cours : on n'arme alors
    // pas l'état logique (pas de double comptage / phase fantôme).
    if (!_acts.feedSequential(tempsGros, tempsPetits, AutomatismFeedingSchedule::FEEDING_DELAY_BETWEEN_SEC)) {
        Serial.println(F("[Auto] ⚠️ Séquence déjà en cours - nourrissage gros+petits ignoré"));
        return;
    }
    _manualFeedingActive = true;
    _currentFeedingType = "Gros";  // affichage cohérent avec le nourrissage auto (gros puis petits)
    _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
    // Durée totale = gros + délai + petits (aligné avec feedSequential / nourrissage auto)
    const uint32_t bigCycleMs = FeedingTiming::cycleDurationMs(tempsGros);
    const uint32_t delayMs = static_cast<uint32_t>(AutomatismFeedingSchedule::FEEDING_DELAY_BETWEEN_SEC) * 1000UL;
    const uint32_t smallCycleMs = FeedingTiming::cycleDurationMs(tempsPetits);
    _feedingPhaseEnd = millis() + bigCycleMs + delayMs + smallCycleMs;
}

Automatism::ManualFeedResult Automatism::triggerLocalManualFeed(bool isBig, const SensorReadings& readings) {
    // Garde anti-cycle centralisée (politique unique pour les 3 endpoints web locaux).
    if (isFeedingInProgress()) {
        return ManualFeedResult::Busy;
    }
    if (isBig) {
        manualFeedBig();
        setBouffeGrosFlag("1");
    } else {
        manualFeedSmall();
        setBouffePetitsFlag("1");
    }
    // M4: aligne l'edge local à 1 pour que l'écho serveur (10X=1) ne redéclenche pas
    // un nourrissage distant au prochain GET. Le retour à 0 est réconcilié naturellement
    // (front descendant) quand le serveur renvoie 10X=0, sans re-trigger.
    GPIOParser::noteLocalFeedTriggered(isBig);
    // Trace serveur (enregistre l'évènement, 10X=1) — désormais identique pour les 3 endpoints.
    (void)sendFullUpdate(readings, nullptr);
    // Le flag local "1" n'a servi qu'au POST ci-dessus : on le remet à 0 (cohérence /json).
    if (isBig) {
        setBouffeGrosFlag("0");
    } else {
        setBouffePetitsFlag("0");
    }
    // Email (si activé) — même politique et même format que le chemin distant.
    if (_network.isEmailEnabled()) {
        char messageBuffer[256];
        const char* title = isBig ? "Bouffe manuelle - Gros poissons"
                                  : "Bouffe manuelle - Petits poissons";
        const char* subject = isBig ? "Nourrissage manuel - Gros poissons"
                                    : "Nourrissage manuel - Petits poissons";
        createFeedingMessage(messageBuffer, sizeof(messageBuffer), title, tempsGros, tempsPetits);
        // Repli DEFAULT_RECIPIENT si l'adresse est vide (même politique que veille/réveil
        // et que le chemin distant) : évite un envoi à un destinataire vide.
        const char* to = _network.getEmailAddress();
        sendEmail(subject, messageBuffer, "System",
                  (to && strlen(to) > 0) ? to : EmailConfig::DEFAULT_RECIPIENT);
    }
    armMailBlink();
    return ManualFeedResult::Started;
}

void Automatism::toggleEmailNotifications() {
    // v11.172: Source de vérité = _network
    bool current = _network.isEmailEnabled();
    _network.setEmailEnabled(!current);
    bool enabled = _network.isEmailEnabled();
    // Le bouton local est un simple on/off (Full/None) ; propager au mailer.
    _mailer.setNotifMode(_network.notifMode());

    // Persistance NVS (remote vars JSON) pour survie au reboot et cohérence /dbvars
    char cached[2048];
    if (_config.loadRemoteVars(cached, sizeof(cached)) && strlen(cached) > 0) {
        ArduinoJson::StaticJsonDocument<2048> doc;
        if (!deserializeJson(doc, cached)) {
            doc["mailNotif"] = enabled ? "checked" : "";
            char out[2048];
            size_t len = serializeJson(doc, out, sizeof(out));
            if (len > 0 && len < sizeof(out)) {
                out[len] = '\0';
                _config.saveRemoteVars(out);
            }
        }
    } else {
        ArduinoJson::StaticJsonDocument<256> minDoc;
        minDoc["mailNotif"] = enabled ? "checked" : "";
        char out[256];
        size_t len = serializeJson(minDoc, out, sizeof(out));
        if (len > 0 && len < sizeof(out)) {
            out[len] = '\0';
            _config.saveRemoteVars(out);
        }
    }

    invalidateDbvarsCache();

    // Sync vers serveur distant (cache ou _lastReadings pour éviter _sensors.read() bloquant)
    SensorReadings r;
    if (!_sensors.getLastCachedReadings(r)) r = _lastReadings;
    bool sent = sendFullUpdate(r, nullptr);
    Serial.printf("[Auto] mailNotif %s, NVS + cache OK, sync distant %s\n",
                  enabled ? "ON" : "OFF", sent ? "OK" : "pending");
}

void Automatism::toggleForceWakeup() {
    forceWakeUp = !forceWakeUp;
    _sleep.setForceWakeUp(forceWakeUp);
    Serial.printf("[Auto] ForceWakeUp basculé: %s\n", forceWakeUp ? "ON" : "OFF");
    // v11.176: Utilise constante NVS centralisée
    g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::FORCE_WAKE_UP, forceWakeUp);
    // Migration: supprimer l'ancienne clé si elle existe
    g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, "forceWakeUp");
}

// ============================================================================
// COMMUNICATION RÉSEAU (Proxy vers AutomatismNetwork)
// ============================================================================

bool Automatism::sendFullUpdate(const SensorReadings& readings, const char* extraPairs,
                                 AppTasks::PostCategory category) {
    return _network.sendFullUpdate(readings, _acts, *this, extraPairs, category);
}

bool Automatism::fetchRemoteState(ArduinoJson::JsonDocument& doc) {
    return _network.fetchRemoteState(doc);
}

bool Automatism::processFetchedRemoteConfig(ArduinoJson::JsonDocument& doc) {
    return _network.processFetchedRemoteConfig(doc);
}

void Automatism::processDeferredRemoteVarsSave() {
    _network.processDeferredRemoteVarsSave();
}

void Automatism::waitForNetworkReady() {
    _power.waitForNetworkReady();
}

// Applique la config depuis JSON (poll ou NVS). Clés appliquées ici + AutomatismSync:
// Ici: tempsRemplissageSec/refillDuration/113, tempsGros/111, tempsPetits/112,
// bouffeMatin/105, bouffeMidi/106, bouffeSoir/107, forceWakeUp/WakeUp/115.
// Sync: mail, mailNotif, FreqWakeUp, limFlood, aqThreshold, tankThreshold, chauffageThreshold.
// Référence: GPIOMap::ALL_MAPPINGS (include/gpio_mapping.h).
void Automatism::applyConfigFromJson(const ArduinoJson::JsonDocument& doc) {
    _network.applyConfigFromJson(doc);
    // Propager le mode de notification gradué (source de vérité = _network) au mailer,
    // qui filtre chaque mail par sévérité P1-P4. Point unique couvrant tous les applies.
    _mailer.setNotifMode(_network.notifMode());
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

    auto parseServoAngle = [&parseIntValue](ArduinoJson::JsonVariantConst value) -> int {
        int a = parseIntValue(value);
        if (a < 0) a = 0;
        if (a > 180) a = 180;
        return a;
    };

    if (doc.containsKey("angleReposGros") || doc.containsKey("118")) {
        auto v = doc.containsKey("angleReposGros") ? doc["angleReposGros"] : doc["118"];
        _acts.setServoGrosRest(parseServoAngle(v));
    }
    if (doc.containsKey("angleDistribGros") || doc.containsKey("119")) {
        auto v = doc.containsKey("angleDistribGros") ? doc["angleDistribGros"] : doc["119"];
        _acts.setServoGrosFeed(parseServoAngle(v));
    }
    if (doc.containsKey("angleInterGros") || doc.containsKey("120")) {
        auto v = doc.containsKey("angleInterGros") ? doc["angleInterGros"] : doc["120"];
        _acts.setServoGrosInter(parseServoAngle(v));
    }
    if (doc.containsKey("angleReposPetits") || doc.containsKey("121")) {
        auto v = doc.containsKey("angleReposPetits") ? doc["angleReposPetits"] : doc["121"];
        _acts.setServoPetitsRest(parseServoAngle(v));
    }
    if (doc.containsKey("angleDistribPetits") || doc.containsKey("122")) {
        auto v = doc.containsKey("angleDistribPetits") ? doc["angleDistribPetits"] : doc["122"];
        _acts.setServoPetitsFeed(parseServoAngle(v));
    }
    if (doc.containsKey("angleInterPetits") || doc.containsKey("123")) {
        auto v = doc.containsKey("angleInterPetits") ? doc["angleInterPetits"] : doc["123"];
        _acts.setServoPetitsInter(parseServoAngle(v));
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
    
    // 115: Force Wake Up (maintien en éveil) - v11.170
    if (doc.containsKey("forceWakeUp") || doc.containsKey("WakeUp") || doc.containsKey("115")) {
        auto v = doc.containsKey("forceWakeUp") ? doc["forceWakeUp"] : 
                 (doc.containsKey("WakeUp") ? doc["WakeUp"] : doc["115"]);
        bool newValue = false;
        if (v.is<bool>()) {
            newValue = v.as<bool>();
        } else if (v.is<int>()) {
            newValue = (v.as<int>() != 0);
        } else if (v.is<const char*>()) {
            const char* str = v.as<const char*>();
            newValue = (str && (strcmp(str, "1") == 0 || strcmp(str, "true") == 0 || strcmp(str, "TRUE") == 0));
        }
        
        if (newValue != forceWakeUp) {
            forceWakeUp = newValue;
            _sleep.setForceWakeUp(forceWakeUp);
            // v11.176: Utilise constante NVS centralisée
            g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::FORCE_WAKE_UP, forceWakeUp);
            // Migration: supprimer l'ancienne clé si elle existe
            g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, "forceWakeUp");
            Serial.printf("[Auto] ✅ ForceWakeUp mis à jour depuis serveur: %s\n", forceWakeUp ? "ON" : "OFF");
        }
    }
}

void Automatism::finalizeFeedingIfNeeded(uint32_t nowMs) {
    if (_feedingPhaseEnd == 0 || nowMs < _feedingPhaseEnd) {
        return;
    }

    // Distinguer nourrissage auto (gros+petits) vs manuel avant reset
    const bool wasAutomatic = !_manualFeedingActive;

    // v11.179: Offline-first - l'état local est toujours mis à jour en premier
    _manualFeedingActive = false;
    _currentFeedingPhase = FeedingPhase::NONE;
    _feedingPhaseEnd = 0;
    _currentFeedingType = nullptr;

    // Tentative de sync distante (non bloquante, avec timeout court).
    // C4: la décision « quoi publier (auto/manuel) et quel résultat » est extraite dans
    // FeedingFinalizeOrchestrator (testable nativement via FakeStatusPublisher). WiFi/temps
    // restent ici (non abstraits) ; readSensors() n'est appelé qu'en ligne ; GPIOParser et
    // les logs Serial restent dans l'appelant, pilotés par l'Outcome (parité ligne à ligne).
    const bool connected = WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled();
    SensorReadings curReadings{};  // valeur-init : seulement lue/publiée si connected
    if (connected) {
        curReadings = readSensors();
    }
    const FeedingFinalizeOrchestrator::Outcome outcome =
        FeedingFinalizeOrchestrator::run(connected, wasAutomatic, curReadings, _statusPublisher);

    switch (outcome) {
        case FeedingFinalizeOrchestrator::Outcome::AutoSynced:
        case FeedingFinalizeOrchestrator::Outcome::AutoSyncFailed:
            // Évite un faux front 0→1 au poll GET suivant (108/109 repassent à 1 côté BDD)
            GPIOParser::syncFeedEdgeStateAfterLocalPost(true, true);
            Serial.println(outcome == FeedingFinalizeOrchestrator::Outcome::AutoSynced ?
                               F("[Auto] ✅ Nourrissage auto enregistré (sync distant)") :
                               F("[Auto] ⚠️ Nourrissage auto - sync distant échoué"));
            break;
        case FeedingFinalizeOrchestrator::Outcome::ManualSynced:
        case FeedingFinalizeOrchestrator::Outcome::ManualSyncFailed:
            // v14.02 (M4): NE PLUS forcer l'edge 108/109 à 0 ici. L'état edge a déjà été
            // posé à 1 au déclenchement (local: noteLocalFeedTriggered ; distant:
            // resolveFeedCommands), et le reset (10X=0) part dans le POST ci-dessus. Le
            // retour à 0 est réconcilié naturellement quand le serveur renvoie 10X=0
            // (front descendant, sans action). Si ce POST de reset est perdu (offline-first),
            // l'edge reste à 1 et n'engendre donc PAS de re-déclenchement au poll suivant.
            Serial.println(outcome == FeedingFinalizeOrchestrator::Outcome::ManualSynced ?
                               F("[Auto] ✅ Variables nourrissage réinitialisées (locales + distantes)") :
                               F("[Auto] ⚠️ Variables nourrissage réinitialisées (locales), sync distant échoué"));
            break;
        case FeedingFinalizeOrchestrator::Outcome::Offline:
            Serial.println(F("[Auto] ✅ Variables nourrissage réinitialisées (locales uniquement - offline)"));
            break;
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
    _power.getCurrentTimeStringForMail(timeStr, sizeof(timeStr));
    
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
  Serial.printf("[Auto] Activité: %s\n", activity);
  _sleep.updateActivityTimestamp();
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
    SensorReadings cur;
    if (!_sensors.getLastCachedReadings(cur)) cur = _lastReadings;
    _acts.startTankPump();
    _lastPumpManual = true;
    _manualTankOverride = true;
    strncpy(_countdownLabel, "Refill", sizeof(_countdownLabel) - 1);
    _countdownLabel[sizeof(_countdownLabel) - 1] = '\0';
    _countdownEnd = nowMs + refillDurationMs;
    _pumpStartMs = nowMs;
    // v11.165: Validation niveau eau avant assignation (audit robustesse)
    _levelAtPumpStart = SensorValidation::isWaterLevelKnown(cur.wlAqua) ? cur.wlAqua : 0;
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
    // v11.176: Utilise constante NVS centralisée
    g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::FORCE_WAKE_UP, saved, false);
    forceWakeUp = saved;
    _sleep.setForceWakeUp(saved);
}

// v11.178: restoreActuatorState() supprimé (code mort - audit dead-code)

bool Automatism::restoreRemoteConfigFromCache() {
    // Chargement config depuis NVS
    char json[2048];
    bool cacheOk = _config.loadRemoteVars(json, sizeof(json));
    if (cacheOk) {
        StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
        // v11.165: Check DeserializationError (audit robustesse)
        DeserializationError err = deserializeJson(doc, json);
        if (err) {
            Serial.printf("[Auto] ⚠️ Erreur parsing JSON cache: %s\n", err.c_str());
            return false;
        }
        
        // Charger l'email directement depuis NVS si disponible
        // v11.174: Clé NVS centralisée (NVSKeys::Config::EMAIL alignée avec GPIOMap::EMAIL_ADDR.nvsKey)
        char emailFromNVS[128];
        if (g_nvsManager.loadString(NVS_NAMESPACES::CONFIG, NVSKeys::Config::EMAIL, emailFromNVS, sizeof(emailFromNVS), "") == NVSError::SUCCESS) {
            if (strlen(emailFromNVS) > 0) {
                // v11.172: Stocker dans _network (source de vérité)
                _network.setEmailAddress(emailFromNVS);
            }
        }
        
        applyConfigFromJson(doc);
        return true;
    }
    // Charger l'email depuis NVS même sans cache remote_json (offline-first: source de vérité NVS)
    char emailFromNVS[128];
    if (g_nvsManager.loadString(NVS_NAMESPACES::CONFIG, NVSKeys::Config::EMAIL, emailFromNVS, sizeof(emailFromNVS), "") == NVSError::SUCCESS) {
        if (strlen(emailFromNVS) > 0) {
            _network.setEmailAddress(emailFromNVS);
        }
    }
    return false;
}

// ============================================================================
// NOURRISSAGE AUTOMATIQUE
// ============================================================================

void Automatism::handleFeeding() {
    // Évite double nourrissage : ne pas lancer l'auto si un cycle est déjà en cours
    if (isFeedingInProgress()) {
        return;
    }
    // Garde de plausibilité : ne jamais nourrir sur une heure invraisemblable
    // (RTC désynchronisée, pas de NTP). getCurrentEpochSafe() retournerait un fallback
    // figé qui décalerait tous les créneaux (double ou zéro nourrissage).
    time_t now = time(nullptr);
    if (!_power.isValidEpoch(now)) {
        static uint32_t lastWarnMs = 0;
        if (lastWarnMs == 0 || millis() - lastWarnMs > 60000UL) {
            lastWarnMs = millis();
            Serial.println(F("[Auto] ⚠️ Heure non plausible (RTC/NTP) - nourrissage auto suspendu"));
        }
        return;
    }
    struct tm timeinfo;
    if (!localtime_r(&now, &timeinfo)) {
        Serial.println(F("[Auto] ❌ Erreur récupération heure pour nourrissage"));
        return;
    }

    int dayOfYear = timeinfo.tm_yday;
    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;

    const AutomatismFeedingSchedule::FeedingParams feedParams{
        .morningHour = bouffeMatin,
        .noonHour = bouffeMidi,
        .eveningHour = bouffeSoir,
        .bigDuration = tempsGros,
        .smallDuration = tempsPetits,
    };
    _feedingSchedule.checkAndFeed(hour, minute, dayOfYear, millis(),
                                   feedParams,
                                   _network.getEmailAddress(), _network.isEmailEnabled(),
                                   [this]() { armMailBlink(); },
                                   [this](const char* type) {
                                       _currentFeedingType = type;
                                       _manualFeedingActive = false;
                                       // Durée totale = gros + délai + petits (aligné avec feedSequential)
                                       const uint32_t bigCycleMs = FeedingTiming::cycleDurationMs(tempsGros);
                                       const uint32_t delayMs = static_cast<uint32_t>(AutomatismFeedingSchedule::FEEDING_DELAY_BETWEEN_SEC) * 1000UL;
                                       const uint32_t smallCycleMs = FeedingTiming::cycleDurationMs(tempsPetits);
                                       _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
                                       _feedingPhaseEnd = millis() + bigCycleMs + delayMs + smallCycleMs;
                                   },
                                   nullptr);  // Sync déplacée dans finalizeFeedingIfNeeded (à la vraie fin du cycle)
}

// ============================================================================
// MÉTHODES FUSIONNÉES DEPUIS LES CONTRÔLEURS
// ============================================================================

// Sous-fonction: Sécurité aquarium trop plein

void Automatism::restoreActuatorsAfterWake(IActuators& acts) {
    ActuatorSnapshot::State snap;
    if (!loadActuatorSnapshotFromNVS(snap.aqua, snap.heater, snap.light)) {
        return;
    }
    ActuatorSnapshot::apply(acts, snap);
    clearActuatorSnapshotInNVS();
    Serial.printf("[Auto] État restauré au réveil: aqua=%s heater=%s light=%s\n",
                  snap.aqua ? "ON" : "OFF", snap.heater ? "ON" : "OFF", snap.light ? "ON" : "OFF");
}

// États actuels persistants (méthodes statiques pour compatibilité avec web_server.cpp)
// v11.178: Utilisation des clés NVS centralisées pour state_lastLocal (audit nvs-keys)
void Automatism::saveCurrentActuatorState(const char* actuator, bool state) {
    char key[32];
    snprintf(key, sizeof(key), "state_%s", actuator);  // Dynamique basé sur nom actionneur
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, key, state);
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, NVSKeys::Automatism::STATE_LAST_LOCAL, millis());
    Serial.printf("[Auto] État %s=%s sauvegardé en NVS (priorité locale)\n",
                   actuator, state ? "ON" : "OFF");
}

bool Automatism::loadCurrentActuatorState(const char* actuator, bool defaultValue) {
    char key[32];
    snprintf(key, sizeof(key), "state_%s", actuator);  // Dynamique basé sur nom actionneur
    bool state;
    g_nvsManager.loadBool(NVS_NAMESPACES::STATE, key, state, defaultValue);
    return state;
}

uint32_t Automatism::getLastLocalActionTime() {
    unsigned long timestamp;
    g_nvsManager.loadULong(NVS_NAMESPACES::STATE, NVSKeys::Automatism::STATE_LAST_LOCAL, timestamp, 0);
    return timestamp;
}

bool Automatism::hasRecentLocalAction(uint32_t timeoutMs) {
    uint32_t lastAction = getLastLocalActionTime();
    if (lastAction == 0) return false;
    uint32_t elapsed = millis() - lastAction;
    return elapsed < timeoutMs;
}

// Pending sync
// v11.178: Utilisation des clés NVS centralisées (audit nvs-keys)
void Automatism::markPendingSync(const char* actuator, bool state) {
    char key_state[32];
    snprintf(key_state, sizeof(key_state), "sync_%s", actuator);
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, key_state, state);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    
    bool alreadyPending = false;
    for (int i = 0; i < count; i++) {
        char key_item[24];
        snprintf(key_item, sizeof(key_item), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, key_item, item, sizeof(item), "");
        if (strcmp(item, actuator) == 0) {
            alreadyPending = true;
            break;
        }
    }
    
    if (!alreadyPending) {
        char key_item[24];
        snprintf(key_item, sizeof(key_item), "%s%d", NVSKeys::Sync::ITEM_PREFIX, count);
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, key_item, actuator);
        count++;
        g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count);
    }
    
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, NVSKeys::Sync::LAST_SYNC, millis());
    Serial.printf("[Auto] ⏳ Pending sync marqué: %s=%s (total: %u)\n",
                   actuator, state ? "ON" : "OFF", count);
}

void Automatism::markConfigPendingSync() {
    g_nvsManager.saveBool(NVS_NAMESPACES::STATE, NVSKeys::Sync::CONFIG, true);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    
    bool alreadyPending = false;
    for (int i = 0; i < count; i++) {
        char key_item[24];
        snprintf(key_item, sizeof(key_item), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, key_item, item, sizeof(item), "");
        if (strcmp(item, "config") == 0) {
            alreadyPending = true;
            break;
        }
    }
    
    if (!alreadyPending) {
        char key_item[24];
        snprintf(key_item, sizeof(key_item), "%s%d", NVSKeys::Sync::ITEM_PREFIX, count);
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, key_item, "config");
        count++;
        g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count);
    }
    
    g_nvsManager.saveULong(NVS_NAMESPACES::STATE, NVSKeys::Sync::LAST_SYNC, millis());
    Serial.printf("[Auto] ⏳ Config pending sync marquée (total: %u)\n", count);
}

void Automatism::clearPendingSync(const char* actuator) {
    char key_state[32];
    snprintf(key_state, sizeof(key_state), "sync_%s", actuator);
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, key_state);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    
    int newCount = 0;
    for (int i = 0; i < count; i++) {
        char oldKey[24];
        snprintf(oldKey, sizeof(oldKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, oldKey, item, sizeof(item), "");
        
        if (strcmp(item, actuator) != 0 && strlen(item) > 0) {
            if (newCount != i) {
                char newKey[24];
                snprintf(newKey, sizeof(newKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, newCount);
                g_nvsManager.saveString(NVS_NAMESPACES::STATE, newKey, item);
            }
            newCount++;
        }
    }
    
    for (int i = newCount; i < count; i++) {
        char oldKey[24];
        snprintf(oldKey, sizeof(oldKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        g_nvsManager.removeKey(NVS_NAMESPACES::STATE, oldKey);
    }
    
    g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, newCount);
    Serial.printf("[Auto] ✅ Pending sync effacé: %s (reste: %u)\n", actuator, newCount);
}

void Automatism::clearConfigPendingSync() {
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, NVSKeys::Sync::CONFIG);
    
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    
    int newCount = 0;
    for (int i = 0; i < count; i++) {
        char oldKey[24];
        snprintf(oldKey, sizeof(oldKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        char item[64];
        g_nvsManager.loadString(NVS_NAMESPACES::STATE, oldKey, item, sizeof(item), "");
        
        if (strcmp(item, "config") != 0 && strlen(item) > 0) {
            if (newCount != i) {
                char newKey[24];
                snprintf(newKey, sizeof(newKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, newCount);
                g_nvsManager.saveString(NVS_NAMESPACES::STATE, newKey, item);
            }
            newCount++;
        }
    }
    
    for (int i = newCount; i < count; i++) {
        char oldKey[24];
        snprintf(oldKey, sizeof(oldKey), "%s%d", NVSKeys::Sync::ITEM_PREFIX, i);
        g_nvsManager.removeKey(NVS_NAMESPACES::STATE, oldKey);
    }
    
    g_nvsManager.saveInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, newCount);
    Serial.printf("[Auto] ✅ Config pending sync effacée (reste: %u)\n", newCount);
}

bool Automatism::hasPendingSync() {
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    return count > 0;
}

uint8_t Automatism::getPendingSyncCount() {
    int count;
    g_nvsManager.loadInt(NVS_NAMESPACES::STATE, NVSKeys::Sync::COUNT, count, 0);
    return (uint8_t)count;
}

uint32_t Automatism::getLastPendingSyncTime() {
    unsigned long timestamp;
    g_nvsManager.loadULong(NVS_NAMESPACES::STATE, NVSKeys::Sync::LAST_SYNC, timestamp, 0);
    return timestamp;
}
