#include "automatism_sleep.h"
#include "project_config.h"
#include <ctime>

// ============================================================================
// Module: AutomatismSleep
// Responsabilité: Gestion sommeil adaptatif et marées
// ============================================================================

AutomatismSleep::AutomatismSleep(PowerManager& power, ConfigManager& cfg)
    : _power(power)
    , _config(cfg)
    , _lastWakeMs(0)
    , _lastActivityMs(0)
    , _lastSensorActivityMs(0)
    , _lastWebActivityMs(0)
    , _forceWakeFromWeb(false)
    , _forceWakeUp(false)
    , _hasRecentErrors(false)
    , _consecutiveWakeupFailures(0)
    , _tideTriggerCm(0)
{
    // Configuration sleep adaptatif (valeurs par défaut)
    _sleepConfig.minSleepTime = 30;      // 30s min
    _sleepConfig.maxSleepTime = 600;     // 10 min max
    _sleepConfig.normalSleepTime = 120;  // 2 min normal
    _sleepConfig.errorSleepTime = 60;    // 1 min si erreurs
    _sleepConfig.nightSleepTime = 180;   // 3 min la nuit
    _sleepConfig.adaptiveSleep = true;
    
    _lastWakeMs = millis();
    Serial.println(F("[Sleep] Module initialisé - Sleep adaptatif activé"));
}

// ============================================================================
// MARÉES
// ============================================================================

void AutomatismSleep::handleMaree(const SensorReadings& r) {
    // Note: Le sleep anticipé marée est géré dans handleAutoSleep()
    // Cette méthode fait juste le logging
    Serial.printf("[Sleep] Marée - wlAqua=%u cm\n", r.wlAqua);
}

// ============================================================================
// ACTIVITÉ
// ============================================================================

bool AutomatismSleep::hasSignificantActivity() {
    // Désactivé: seuls nourrissage/remplissage retardent le sleep
    // Gérés explicitement dans handleAutoSleep()
    return false;
}

void AutomatismSleep::updateActivityTimestamp() {
    unsigned long currentMillis = millis();
    _lastActivityMs = currentMillis;
    _lastWakeMs = currentMillis;
    Serial.println(F("[Sleep] Timestamp activité mis à jour"));
}

void AutomatismSleep::logActivity(const char* activity) {
    Serial.printf("[Sleep] Activité détectée: %s\n", activity);
    updateActivityTimestamp();
}

void AutomatismSleep::notifyLocalWebActivity() {
    _lastWebActivityMs = millis();
    _forceWakeFromWeb = true;
    
    if (!_forceWakeUp) {
        _forceWakeUp = true;
        Serial.println(F("[Sleep] forceWakeUp activé par activité web locale"));
        _config.setForceWakeUp(_forceWakeUp);
        _config.saveBouffeFlags();
    }
}

// ============================================================================
// CALCULS ADAPTATIFS
// ============================================================================

uint32_t AutomatismSleep::calculateAdaptiveSleepDelay() {
    if (!_sleepConfig.adaptiveSleep) {
        return _sleepConfig.normalSleepTime;
    }
    
    uint32_t baseDelay = _sleepConfig.normalSleepTime;
    
    // Réduire si erreurs récentes
    if (hasRecentErrors()) {
        baseDelay = _sleepConfig.errorSleepTime;
        Serial.println(F("[Sleep] Délai réduit (erreurs)"));
    }
    
    // Réduire la nuit (économie énergie)
    if (isNightTime()) {
        baseDelay = _sleepConfig.nightSleepTime;
        Serial.println(F("[Sleep] Délai réduit (nuit)"));
    }
    
    // Ajuster selon échecs réveil
    if (_consecutiveWakeupFailures > 0) {
        baseDelay = std::min(baseDelay * 2, _sleepConfig.maxSleepTime);
        Serial.printf("[Sleep] Délai ajusté (échecs: %d)\n", _consecutiveWakeupFailures);
    }
    
    // Limiter aux bornes
    baseDelay = std::max(baseDelay, _sleepConfig.minSleepTime);
    baseDelay = std::min(baseDelay, _sleepConfig.maxSleepTime);
    
    return baseDelay;
}

bool AutomatismSleep::isNightTime() {
    time_t currentTime = _power.getCurrentEpoch();
    struct tm* timeinfo = localtime(&currentTime);
    
    if (timeinfo != nullptr) {
        int hour = timeinfo->tm_hour;
        // Nuit: 22h-6h (selon SleepConfig)
        return (hour >= 22 || hour < 6);
    }
    
    return false;
}

bool AutomatismSleep::hasRecentErrors() {
    return _hasRecentErrors;
}

// ============================================================================
// SLEEP CONDITIONS
// ============================================================================

bool AutomatismSleep::shouldEnterSleepEarly(const SensorReadings& r) {
    // Sommeil forcé désactivé ?
    if (_forceWakeUp) {
        return false;
    }
    
    // Conditions critiques à vérifier
    // TODO: Implémenter logique complète depuis automatism.cpp ligne 2313
    
    Serial.println(F("[Sleep] shouldEnterSleepEarly() - Implémentation simple"));
    return false;  // Placeholder
}

// ============================================================================
// VALIDATION SYSTÈME
// ============================================================================

bool AutomatismSleep::verifySystemStateAfterWakeup() {
    // TODO: Implémenter vérifications complètes
    Serial.println(F("[Sleep] Vérification système après réveil"));
    return true;  // Placeholder
}

void AutomatismSleep::detectSleepAnomalies() {
    // TODO: Détection anomalies
    Serial.println(F("[Sleep] Détection anomalies sleep"));
}

bool AutomatismSleep::validateSystemStateBeforeSleep() {
    // TODO: Validation système avant sleep
    Serial.println(F("[Sleep] Validation système avant sleep"));
    return true;  // Placeholder
}

// ============================================================================
// HANDLE AUTO SLEEP (Méthode ÉNORME - 443 lignes)
// NOTE: Cette méthode reste dans automatism.cpp pour l'instant
// Sera divisée et migrée en Phase 2.8
// ============================================================================

void AutomatismSleep::handleAutoSleep(const SensorReadings& r, SystemActuators& acts) {
    // TODO: Méthode de 443 lignes à diviser en:
    // - shouldSleep()
    // - validateSystemState()
    // - prepareSleep()
    // - executeSleep()
    // - handleWakeup()
    
    // Pour l'instant, implémentation dans automatism.cpp
    Serial.println(F("[Sleep] handleAutoSleep() - Implémentation temporaire"));
}

// ============================================================================
// HELPERS PRIVÉS
// ============================================================================

bool AutomatismSleep::shouldSleep() {
    if (_forceWakeUp) return false;
    
    unsigned long now = millis();
    unsigned long timeSinceWake = now - _lastWakeMs;
    
    // Critères de base
    return (timeSinceWake > _sleepConfig.normalSleepTime * 1000);
}

bool AutomatismSleep::validateSystemState() {
    // Vérifications basiques
    return true;  // Placeholder
}

void AutomatismSleep::prepareSleep(SystemActuators& acts) {
    // Sauvegarde snapshot actionneurs
    Serial.println(F("[Sleep] Préparation sleep (snapshot)"));
}

uint32_t AutomatismSleep::calculateSleepDuration() {
    return calculateAdaptiveSleepDelay();
}

void AutomatismSleep::handleWakeup(SystemActuators& acts, uint32_t actualSlept) {
    Serial.printf("[Sleep] Réveil après %u secondes\n", actualSlept);
    _lastWakeMs = millis();
}

void AutomatismSleep::handleWakeupFailure() {
    _consecutiveWakeupFailures++;
    _hasRecentErrors = true;
    Serial.printf("[Sleep] Échec réveil #%d\n", _consecutiveWakeupFailures);
}

