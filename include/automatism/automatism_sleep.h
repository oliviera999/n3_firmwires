#pragma once

#include "system_sensors.h"
#include "system_actuators.h"
#include "power.h"
#include "config_manager.h"

class Automatism; // Forward declaration

class AutomatismSleep {
public:
    AutomatismSleep(PowerManager& power, ConfigManager& cfg);

    // Initialisation
    void begin();

    // Gestion du sleep
    // Renvoie true si le système est entré en veille
    bool handleAutoSleep(const SensorReadings& r, SystemActuators& acts, Automatism& core);
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
    void updateActivityTimestamp();
    // v11.178: hasSignificantActivity() supprimé (toujours false - audit dead-code)

    // Helpers publics
    uint32_t calculateAdaptiveSleepDelay();
    bool hasRecentErrors();
    bool isNightTime();

    // Accesseurs état
    unsigned long getLastWakeMs() const { return _lastWakeMs; }
    unsigned long getLastActivityMs() const { return _lastActivityMs; }
    bool isForceWakeUp() const { return _forceWakeUp; }
    void setForceWakeUp(bool v) { _forceWakeUp = v; }

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
    bool _lastCanSleep;
    bool _lastShouldSleep;

    // Logs et débogage
    unsigned long _wsBlockStartMs{0};
    unsigned long _lastWsLogMs{0};
    unsigned long _lastWebLogMs{0};
    unsigned long _lastForceWakeLogMs{0};
    unsigned long _lastActivityLogMs{0};
    unsigned long _lastWebSocketCheckMs{0};
    unsigned long _lastDecisionLogMs{0};

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

};

