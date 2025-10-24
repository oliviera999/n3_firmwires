#pragma once

#include "automatism/automatism_utils.h"
#include "config_manager.h"
#include "mailer.h"
#include "system_actuators.h"
#include "system_sensors.h"

class AutomatismRefillController {
 public:
  struct Dependencies {
    SystemActuators& acts;
    ConfigManager& config;
    Mailer& mailer;
    SystemSensors& sensors;
  };

  enum class LockReason {
    NONE,
    INEFFICIENT,
    RESERVOIR_LOW,
    AQUARIUM_OVERFILL
  };

  struct Config {
    uint16_t aquariumThresholdCm;
    uint16_t tankThresholdCm;
    uint16_t floodLimitCm;
    uint16_t floodHysteresisCm;
    uint32_t floodResetStableMin;
    uint8_t maxPumpRetries;
    uint32_t refillDurationMs;
    uint32_t floodCooldownMin;
    uint32_t floodDebounceMin;
  };

  struct RuntimeState {
    bool tankPumpLocked;
    uint8_t tankPumpRetries;
    bool manualOverride;
    bool lastPumpManual;
    LockReason lockReason;
    uint32_t pumpStartMs;
    uint16_t levelAtPumpStart;
    uint32_t countdownEnd;
    bool emailTankSent;
    bool emailTankStartSent;
    bool emailTankStopSent;
    bool heaterPrevState;
    bool inFlood;
    uint32_t lastFloodEmailEpoch;
    uint32_t floodEnterSinceEpoch;
    uint32_t aboveResetSinceEpoch;
  };

  struct SharedUiState {
    String& countdownLabel;
    uint32_t& countdownEnd;
  };

  AutomatismRefillController(SystemActuators& acts,
                             ConfigManager& config,
                             Mailer& mailer,
                             SystemSensors& sensors,
                             RuntimeState& runtime,
                             SharedUiState ui);

  explicit AutomatismRefillController(const Dependencies& deps,
                                      RuntimeState& runtime,
                                      SharedUiState ui);

  void configure(const Config& cfg);
  void tick(const SensorReadings& readings, const String& mail, bool mailNotif);
  void startManual(const SensorReadings& readings, uint32_t durationMs);
  void stopManual(const SensorReadings& readings);

 private:
  SystemActuators& _acts;
  ConfigManager& _config;
  Mailer& _mailer;
  SystemSensors& _sensors;
  RuntimeState& _state;
  SharedUiState _ui;
  Config _cfg{};

  void evaluateFloodAlerts(const SensorReadings& readings, const String& mail, bool mailNotif);
  void evaluateLocks(const SensorReadings& readings, const String& mail, bool mailNotif);
  void processStart(const SensorReadings& readings, const String& mail, bool mailNotif);
  void processRunning(const SensorReadings& readings, const String& mail, bool mailNotif);
  void processStop(const SensorReadings& readings, const String& mail, bool mailNotif, bool forced);
  void processRetries(const SensorReadings& readings, const String& mail, bool mailNotif);
  void notifyServer(const SensorReadings& readings, const char* extraPairs);
  void sendMail(const char* subject, const char* body, const String& mail);
};

