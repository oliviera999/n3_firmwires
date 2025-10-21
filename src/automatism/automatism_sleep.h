#pragma once

#include <Arduino.h>
#include "system_sensors.h"
#include "system_actuators.h"
#include "power.h"
#include "config_manager.h"

/**
 * Module Sleep pour Automatism
 * 
 * Responsabilité: Gestion sommeil adaptatif, marées, activité
 * - Détection marées (sleep trigger)
 * - Sleep adaptatif selon conditions
 * - Tracking activité (web, sensors)
 * - Validation système avant/après sleep
 * - Anomalies sleep
 */
class AutomatismSleep {
public:
    struct AutoSleepContext {
        bool forceWakeUp;
        bool* forceWakeFromWeb;
        unsigned long* lastWebActivityMs;
        bool feedingInProgress;
        bool tankPumpRunning;
        uint32_t countdownEnd;
        unsigned long* lastWakeMs;
        int diff10s;
        int16_t tideTriggerCm;
        uint32_t currentMillis;
    };

    struct AutoSleepDecision {
        bool tideAscending;
        uint32_t adaptiveDelaySec;
        unsigned long awakeSec;
        int diff10s;
    };

    /**
     * Constructeur
     * @param power Référence PowerManager
     * @param cfg Référence ConfigManager
     */
    AutomatismSleep(PowerManager& power, ConfigManager& cfg);
    
    // === SLEEP PRINCIPAL ===
    
    /**
     * Gère le sommeil automatique adaptatif
     * Méthode complexe (443 lignes à diviser)
     * @param r Lectures capteurs
     * @param acts Actionneurs (pour snapshot)
     */
    void handleAutoSleep(const SensorReadings& r, SystemActuators& acts);
    
    /**
     * Vérifie si peut entrer en sleep précoce
     * @param r Lectures capteurs
     * @return true si conditions remplies
     */
    bool shouldEnterSleepEarly(const SensorReadings& r,
                               bool forceWakeUp,
                               bool forceWakeFromWeb,
                               unsigned long lastWebActivityMs,
                               bool feedingInProgress,
                               bool tankPumpRunning,
                               uint32_t countdownEnd,
                               unsigned long lastWakeMs,
                               int diffMaree10s,
                               int16_t tideTriggerCm);

    bool handleBlockingConditions(SystemActuators& acts,
                                  bool& forceWakeUp,
                                  bool& forceWakeFromWeb,
                                  unsigned long& lastWebActivityMs,
                                  uint32_t countdownEnd,
                                  unsigned long& lastWakeMs,
                                  bool feedingInProgress,
                                  bool tankPumpRunning,
                                  uint8_t wsClients);

    bool evaluateAutoSleep(const AutoSleepContext& ctx, AutoSleepDecision& outDecision);
    void logSleepDecision(bool pumpReservoirOn,
                          bool feedingActive,
                          bool countdownActive,
                          bool tideAscending,
                          int diff10s,
                          unsigned long awakeSec,
                          uint32_t adaptiveDelaySec,
                          uint16_t nextWakeSec);
    
    /**
     * Gère la détection de marées (trigger sleep)
     * @param r Lectures capteurs
     */
    void handleMaree(const SensorReadings& r);
    
    // === ACTIVITÉ ===
    
    /**
     * Vérifie si activité significative récente
     */
    bool hasSignificantActivity();
    
    /**
     * Met à jour timestamp dernière activité
     */
    void updateActivityTimestamp();
    
    /**
     * Log une activité
     */
    void logActivity(const char* activity);
    
    /**
     * Notifie activité web locale
     */
    void notifyLocalWebActivity();
    
    // === CALCULS ADAPTATIFS ===
    
    /**
     * Calcule délai sleep adaptatif
     * @return Délai en secondes
     */
    uint32_t calculateAdaptiveSleepDelay();
    
    /**
     * Vérifie si période nocturne
     */
    bool isNightTime();
    
    /**
     * Vérifie si erreurs récentes
     */
    bool hasRecentErrors();
    
    // === VALIDATION SYSTÈME ===
    
    /**
     * Vérifie état système après réveil
     */
    bool verifySystemStateAfterWakeup();
    
    /**
     * Détecte anomalies sleep
     */
    void detectSleepAnomalies();
    
    /**
     * Valide état système avant sleep
     */
    bool validateSystemStateBeforeSleep();
    
    // === GETTERS/SETTERS ===
    
    bool getForceWakeUp() const { return _forceWakeUp; }
    void setForceWakeUp(bool value) { _forceWakeUp = value; }
    
    int16_t getTideTriggerCm() const { return _tideTriggerCm; }
    void setTideTriggerCm(int16_t value) { _tideTriggerCm = value; }
    
private:
    PowerManager& _power;
    ConfigManager& _config;
    
    // Timing wake/sleep
    unsigned long _lastWakeMs;
    unsigned long _lastActivityMs;
    unsigned long _lastSensorActivityMs;
    unsigned long _lastWebActivityMs;
    
    // État
    bool _forceWakeFromWeb;
    bool _forceWakeUp;
    bool _hasRecentErrors;
    uint8_t _consecutiveWakeupFailures;
    int16_t _tideTriggerCm;
    
    // Config sleep adaptatif
    struct SleepConfig {
        uint32_t minSleepTime;
        uint32_t maxSleepTime;
        uint32_t normalSleepTime;
        uint32_t errorSleepTime;
        uint32_t nightSleepTime;
        bool adaptiveSleep;
    } _sleepConfig;
    
    unsigned long _wsBlockStartMs;
    unsigned long _lastWsLogMs;
    unsigned long _lastWebLogMs;
    unsigned long _lastForceWakeLogMs;
    unsigned long _lastActivityLogMs;
    unsigned long _lastWebSocketCheckMs;
    
    // Helpers privés (pour diviser handleAutoSleep)
    bool shouldSleep();
    bool validateSystemState();
    void prepareSleep(SystemActuators& acts);
    uint32_t calculateSleepDuration();
    void handleWakeup(SystemActuators& acts, uint32_t actualSlept);
    void handleWakeupFailure();
};

