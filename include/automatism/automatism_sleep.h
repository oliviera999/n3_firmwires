#pragma once

#include "system_sensors.h"
#include "system_actuators.h"
#include "power.h"
#include "config_manager.h"
#include "task_monitor.h"
#include <esp_sleep.h>

class Automatism; // Forward declaration

class AutomatismSleep {
public:
    AutomatismSleep(PowerManager& power, ConfigManager& cfg);

    // Initialisation
    void begin();

    // Méthode principale appelée par Automatism::update()
    // Renvoie true si le système est entré en veille
    bool process(const SensorReadings& r, SystemActuators& acts, Automatism& core);

    // Gestion du sleep
    void handleAutoSleep(const SensorReadings& r, SystemActuators& acts, Automatism& core);
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

    // Activité
    void notifyLocalWebActivity();
    void logActivity(const char* activity);
    void updateActivityTimestamp();
    bool hasSignificantActivity();

    // Helpers publics
    bool isAdaptiveSleepEnabled() const;
    uint32_t calculateAdaptiveSleepDelay();
    bool hasRecentErrors();
    bool isNightTime();

    // Accesseurs état
    unsigned long getLastWakeMs() const { return _lastWakeMs; }
    unsigned long getLastActivityMs() const { return _lastActivityMs; }
    bool isForceWakeUp() const { return _forceWakeUp; }
    void setForceWakeUp(bool v) { _forceWakeUp = v; }

    // Logs et transitions
    void logSleepTransitionStart(const char* reason,
                                 uint32_t scheduledSeconds,
                                 unsigned long awakeSec,
                                 bool tideAscending,
                                 int diff10s,
                                 uint32_t heapAfterCleanup,
                                 const TaskMonitor::Snapshot& tasks);

    void logSleepTransitionEnd(uint32_t scheduledSeconds,
                               uint32_t actualSeconds,
                               esp_sleep_wakeup_cause_t wakeCause,
                               const TaskMonitor::Snapshot& tasksBefore,
                               const TaskMonitor::Snapshot& tasksAfter);

    // Marées
    void handleMaree(const SensorReadings& r);

private:
    PowerManager& _power;
    ConfigManager& _config;

    // État interne
    unsigned long _lastWakeMs;
    unsigned long _lastActivityMs;
    unsigned long _lastSensorActivityMs;
    unsigned long _lastWebActivityMs;
    bool _forceWakeFromWeb;
    bool _forceWakeUp;
    bool _hasRecentErrors;
    uint8_t _consecutiveWakeupFailures;
    int16_t _tideTriggerCm;

    // Logs et débogage
    unsigned long _wsBlockStartMs{0};
    unsigned long _lastWsLogMs{0};
    unsigned long _lastWebLogMs{0};
    unsigned long _lastForceWakeLogMs{0};
    unsigned long _lastActivityLogMs{0};
    unsigned long _lastWebSocketCheckMs{0};

    // Configuration interne
    struct SleepConfigStruct {
        uint32_t minSleepTime;
        uint32_t maxSleepTime;
        uint32_t normalSleepTime;
        uint32_t errorSleepTime;
        uint32_t nightSleepTime;
        bool adaptiveSleep;
    } _sleepConfig;

    // Helpers internes
    bool handleBlockingConditions(SystemActuators& acts,
                                  bool& forceWakeUp,
                                  bool& forceWakeFromWeb,
                                  unsigned long& lastWebActivityMs,
                                  uint32_t countdownEnd,
                                  unsigned long& lastWakeMs,
                                  bool feedingInProgress,
                                  bool tankPumpRunning,
                                  uint8_t wsClients);

    // Validation
    bool verifySystemStateAfterWakeup();
    void detectSleepAnomalies();
    bool validateSystemStateBeforeSleep();

    // Méthodes découpées
    bool shouldSleep();
    bool validateSystemState();
    void prepareSleep(SystemActuators& acts);
    uint32_t calculateSleepDuration();
    void handleWakeup(SystemActuators& acts, uint32_t actualSlept);
    void handleWakeupFailure();
    
    // Contexte pour évaluation
    struct AutoSleepContext {
        unsigned long currentMillis;
        unsigned long* lastWakeMs;
        int diff10s;
        int16_t tideTriggerCm;
    };

    struct AutoSleepDecision {
        bool tideAscending;
        uint32_t adaptiveDelaySec;
        int diff10s;
        unsigned long awakeSec;
    };

    bool evaluateAutoSleep(const AutoSleepContext& ctx, AutoSleepDecision& outDecision);
    void logSleepDecision(bool pumpReservoirOn,
                          bool feedingActive,
                          bool countdownActive,
                          bool tideAscending,
                          int diff10s,
                          unsigned long awakeSec,
                          uint32_t adaptiveDelaySec,
                          uint16_t nextWakeSec);
};

