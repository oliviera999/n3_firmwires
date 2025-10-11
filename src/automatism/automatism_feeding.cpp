#include "automatism_feeding.h"
#include <ctime>

// ============================================================================
// Module: AutomatismFeeding
// Responsabilité: Nourrissage automatique et manuel
// ============================================================================

AutomatismFeeding::AutomatismFeeding(SystemActuators& acts, ConfigManager& cfg, 
                                     Mailer& mail, PowerManager& power)
    : _acts(acts)
    , _config(cfg)
    , _mailer(mail)
    , _power(power)
    , _feedMorning(8)
    , _feedNoon(12)
    , _feedEvening(19)
    , _feedBigDur(10)
    , _feedSmallDur(10)
    , _lastFeedDay(-1)
    , _currentPhase(Phase::NONE)
    , _phaseEnd(0)
    , _totalEnd(0)
    , _lastManualFeedSmallMs(0)
    , _lastManualFeedBigMs(0)
{
    // Charger dernier jour depuis config
    _lastFeedDay = _config.getLastJourBouf();
    Serial.printf("[Feeding] Initialized - Last feed day: %d\n", _lastFeedDay);
}

// ============================================================================
// GESTION JOUR
// ============================================================================

void AutomatismFeeding::checkNewDay() {
    time_t currentTime = _power.getCurrentEpoch();
    struct tm* timeinfo = localtime(&currentTime);
    
    if (timeinfo != nullptr) {
        int currentDay = timeinfo->tm_yday; // Jour de l'année (0-365)
        
        if (currentDay != _lastFeedDay) {
            Serial.printf("[Feeding] Nouveau jour détecté: %d (précédent: %d)\n", 
                         currentDay, _lastFeedDay);
            
            // Reset des flags de bouffe pour le nouveau jour
            _config.resetBouffeFlags();
            _lastFeedDay = currentDay;
            
            // Persistance immédiate
            saveFeedingState();
            
            Serial.println(F("[Feeding] Flags de bouffe réinitialisés pour le nouveau jour"));
        }
    }
}

void AutomatismFeeding::saveFeedingState() {
    // Sauvegarde du jour de bouffe actuel
    _config.setLastJourBouf(_lastFeedDay);
    
    // Sauvegarde de tous les flags
    _config.saveBouffeFlags();
    
    Serial.printf("[Feeding] État sauvegardé - Jour: %d\n", _lastFeedDay);
}

// ============================================================================
// MESSAGES ET TRAÇAGE
// ============================================================================

String AutomatismFeeding::createMessage(const char* type) {
    String message = String(type) + " - Distribution effectuée\n\n";
    message += "Temps de nourrissage effectif:\n";
    message += "- Gros poissons: " + String(_feedBigDur) + " secondes\n";
    message += "- Petits poissons: " + String(_feedSmallDur) + " secondes\n";
    return message;
}

void AutomatismFeeding::traceFeedingEvent(std::function<void(const char*)> sendUpdateCallback) {
    // Première trame : indicateurs à 1
    sendUpdateCallback("bouffePetits=1&bouffeGros=1");
    delay(500);
    
    // Seconde trame : indicateurs à 0
    sendUpdateCallback("bouffePetits=0&bouffeGros=0");
    
    Serial.println(F("[Feeding] Événement tracé (petits + gros)"));
}

void AutomatismFeeding::traceFeedingEventSelective(bool feedSmall, bool feedBig,
                                                   std::function<void(const char*)> sendUpdateCallback) {
    // Construction paramètres première trame (indicateurs à 1)
    String params1 = "";
    if (feedSmall && feedBig) {
        params1 = "bouffePetits=1&bouffeGros=1";
    } else if (feedSmall) {
        params1 = "bouffePetits=1";
    } else if (feedBig) {
        params1 = "bouffeGros=1";
    }
    
    if (params1.length() > 0) {
        sendUpdateCallback(params1.c_str());
        delay(500);
        
        // Construction paramètres seconde trame (indicateurs à 0)
        String params2 = "";
        if (feedSmall && feedBig) {
            params2 = "bouffePetits=0&bouffeGros=0";
        } else if (feedSmall) {
            params2 = "bouffePetits=0";
        } else if (feedBig) {
            params2 = "bouffeGros=0";
        }
        
        if (params2.length() > 0) {
            sendUpdateCallback(params2.c_str());
        }
        
        Serial.printf("[Feeding] Événement tracé sélectif - Petits: %s, Gros: %s\n",
                     feedSmall ? "OUI" : "NON", feedBig ? "OUI" : "NON");
    }
}

// ============================================================================
// NOURRISSAGE MANUEL
// ============================================================================

void AutomatismFeeding::feedSmallManual(std::function<void(const char*, unsigned long)> countdownCallback) {
    Serial.println(F("[CRITIQUE] === DÉBUT NOURRISSAGE MANUEL PETITS ==="));
    Serial.printf("[CRITIQUE] Durées configurées - Gros: %u s, Petits: %u s\n", 
                 _feedBigDur, _feedSmallDur);
    
    // EXÉCUTION CIBLÉE - PRIORITÉ ABSOLUE (uniquement servo "petits")
    unsigned long startTime = millis();
    _acts.feedSmallFish(_feedSmallDur);
    
    unsigned long executionTime = millis() - startTime;
    Serial.printf("[CRITIQUE] Temps d'exécution total: %lu ms\n", executionTime);
    
    // Protection double exécution
    unsigned long now = millis();
    _lastManualFeedSmallMs = now;
    
    // Initialisation des phases de nourrissage
    _currentPhase = Phase::FEEDING_FORWARD;
    _phaseEnd = millis() + static_cast<unsigned long>(_feedSmallDur) * 1000UL;
    _totalEnd = millis() + static_cast<unsigned long>(_feedSmallDur + _feedSmallDur/2) * 1000UL;
    
    // Mise à jour countdown OLED
    countdownCallback("Feed Petits", _totalEnd);
    
    Serial.println(F("[CRITIQUE] === FIN NOURRISSAGE MANUEL PETITS ==="));
}

void AutomatismFeeding::feedBigManual(std::function<void(const char*, unsigned long)> countdownCallback) {
    Serial.println(F("[CRITIQUE] === DÉBUT NOURRISSAGE MANUEL GROS ==="));
    Serial.printf("[CRITIQUE] Durées configurées - Gros: %u s, Petits: %u s\n",
                 _feedBigDur, _feedSmallDur);
    
    // EXÉCUTION SÉQUENTIELLE - PRIORITÉ ABSOLUE (gros puis petits)
    unsigned long startTime = millis();
    
    // 1. Gros poissons
    _acts.feedBigFish(_feedBigDur);
    
    // 2. Petits poissons
    _acts.feedSmallFish(_feedSmallDur);
    
    unsigned long executionTime = millis() - startTime;
    Serial.printf("[CRITIQUE] Temps d'exécution total: %lu ms\n", executionTime);
    
    // Protection double exécution
    unsigned long now = millis();
    _lastManualFeedBigMs = now;
    
    // Initialisation des phases de nourrissage
    _currentPhase = Phase::FEEDING_FORWARD;
    uint16_t totalDur = _feedBigDur + _feedSmallDur;
    _phaseEnd = millis() + static_cast<unsigned long>(totalDur) * 1000UL;
    _totalEnd = millis() + static_cast<unsigned long>(totalDur + totalDur/2) * 1000UL;
    
    // Mise à jour countdown OLED
    countdownCallback("Feed Gros", _totalEnd);
    
    Serial.println(F("[CRITIQUE] === FIN NOURRISSAGE MANUEL GROS ==="));
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void AutomatismFeeding::setSchedule(uint8_t morning, uint8_t noon, uint8_t evening) {
    _feedMorning = morning;
    _feedNoon = noon;
    _feedEvening = evening;
    Serial.printf("[Feeding] Horaires configurés - Matin: %uh, Midi: %uh, Soir: %uh\n",
                 morning, noon, evening);
}

void AutomatismFeeding::setDurations(uint16_t bigDur, uint16_t smallDur) {
    _feedBigDur = bigDur;
    _feedSmallDur = smallDur;
    Serial.printf("[Feeding] Durées configurées - Gros: %us, Petits: %us\n",
                 bigDur, smallDur);
}

AutomatismFeeding::Status AutomatismFeeding::getStatus() const {
    Status status;
    status.morningDone = _config.getBouffeMatinOk();
    status.noonDone = _config.getBouffeMidiOk();
    status.eveningDone = _config.getBouffeSoirOk();
    status.lastDay = _lastFeedDay;
    return status;
}

// ============================================================================
// NOURRISSAGE AUTOMATIQUE (handleSchedule)
// ============================================================================

void AutomatismFeeding::handleSchedule(int hour, int minute, const String& emailAddress,
                                      bool emailEnabled, std::function<void()> mailBlinkCallback) {
    // Vérifier changement de jour
    checkNewDay();
    
    // Obtenir état actuel des flags
    bool bouffeMatin = _config.getBouffeMatinOk();
    bool bouffeMidi = _config.getBouffeMidiOk();
    bool bouffeSoir = _config.getBouffeSoirOk();
    
    // === BOUFFE MATIN ===
    if (hour == _feedMorning && minute == 0 && !bouffeMatin) {
        Serial.println(F("[Feeding] === DÉCLENCHEMENT BOUFFE MATIN ==="));
        
        performFeedingCycle(true, emailAddress, emailEnabled, mailBlinkCallback);
        
        // Marquer comme effectué
        _config.setBouffeMatinOk(true);
        saveFeedingState();
        
        Serial.println(F("[Feeding] === BOUFFE MATIN TERMINÉE ==="));
    }
    
    // === BOUFFE MIDI ===
    if (hour == _feedNoon && minute == 0 && !bouffeMidi) {
        Serial.println(F("[Feeding] === DÉCLENCHEMENT BOUFFE MIDI ==="));
        
        performFeedingCycle(true, emailAddress, emailEnabled, mailBlinkCallback);
        
        // Marquer comme effectué
        _config.setBouffeMidiOk(true);
        saveFeedingState();
        
        Serial.println(F("[Feeding] === BOUFFE MIDI TERMINÉE ==="));
    }
    
    // === BOUFFE SOIR ===
    if (hour == _feedEvening && minute == 0 && !bouffeSoir) {
        Serial.println(F("[Feeding] === DÉCLENCHEMENT BOUFFE SOIR ==="));
        
        // Soir = seulement petits poissons
        Serial.println(F("[Feeding] Nourrissage soir (petits uniquement)"));
        _acts.feedSmallFish(_feedSmallDur);
        
        // Notification email si activée
        if (emailEnabled) {
            String message = createMessage("Bouffe soir");
            _mailer.sendAlert("Bouffe soir", message, emailAddress.c_str());
            mailBlinkCallback();
        }
        
        // Marquer comme effectué
        _config.setBouffeSoirOk(true);
        saveFeedingState();
        
        // Phases pour OLED
        _currentPhase = Phase::FEEDING_FORWARD;
        _phaseEnd = millis() + static_cast<unsigned long>(_feedSmallDur) * 1000UL;
        _totalEnd = millis() + static_cast<unsigned long>(_feedSmallDur + _feedSmallDur/2) * 1000UL;
        
        Serial.println(F("[Feeding] === BOUFFE SOIR TERMINÉE ==="));
    }
}

// ============================================================================
// HELPER PRIVÉ
// ============================================================================

void AutomatismFeeding::performFeedingCycle(bool isLarge, const String& emailAddress,
                                           bool emailEnabled, std::function<void()> mailBlinkCallback) {
    if (isLarge) {
        // Gros + petits
        Serial.println(F("[Feeding] Nourrissage complet (gros + petits)"));
        _acts.feedBigFish(_feedBigDur);
        _acts.feedSmallFish(_feedSmallDur);
        
        // Phases pour OLED
        uint16_t totalDur = _feedBigDur + _feedSmallDur;
        _currentPhase = Phase::FEEDING_FORWARD;
        _phaseEnd = millis() + static_cast<unsigned long>(totalDur) * 1000UL;
        _totalEnd = millis() + static_cast<unsigned long>(totalDur + totalDur/2) * 1000UL;
    } else {
        // Seulement petits
        Serial.println(F("[Feeding] Nourrissage petits uniquement"));
        _acts.feedSmallFish(_feedSmallDur);
        
        // Phases pour OLED
        _currentPhase = Phase::FEEDING_FORWARD;
        _phaseEnd = millis() + static_cast<unsigned long>(_feedSmallDur) * 1000UL;
        _totalEnd = millis() + static_cast<unsigned long>(_feedSmallDur + _feedSmallDur/2) * 1000UL;
    }
    
    // Notification email si activée
    if (emailEnabled) {
        const char* type = isLarge ? "Bouffe complète" : "Bouffe petits";
        String message = createMessage(type);
        _mailer.sendAlert(type, message, emailAddress.c_str());
        mailBlinkCallback();
    }
}

