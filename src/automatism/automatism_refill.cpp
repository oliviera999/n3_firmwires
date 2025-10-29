#include "automatism/automatism_refill.h"

#include "automatism/automatism_utils.h"

AutomatismRefillController::AutomatismRefillController(SystemActuators& acts,
                                                       ConfigManager& config,
                                                       Mailer& mailer,
                                                       SystemSensors& sensors,
                                                       RuntimeState& runtime,
                                                       SharedUiState ui)
    : _acts(acts)
    , _config(config)
    , _mailer(mailer)
    , _sensors(sensors)
    , _state(runtime)
    , _ui(ui) {}

AutomatismRefillController::AutomatismRefillController(const Dependencies& deps,
                                                       RuntimeState& runtime,
                                                       SharedUiState ui)
    : AutomatismRefillController(deps.acts, deps.config, deps.mailer, deps.sensors, runtime, ui) {}

void AutomatismRefillController::configure(const Config& cfg) {
  _cfg = cfg;
}

void AutomatismRefillController::tick(const SensorReadings& readings, const String& mail, bool mailNotif) {
  (void)readings;
  (void)mail;
  (void)mailNotif;
}

void AutomatismRefillController::startManual(const SensorReadings& readings, uint32_t durationMs) {
  (void)readings;
  (void)durationMs;
}

void AutomatismRefillController::stopManual(const SensorReadings& readings) {
  (void)readings;
}

void AutomatismRefillController::evaluateFloodAlerts(const SensorReadings& readings, const String& mail, bool mailNotif) {
  (void)readings;
  (void)mail;
  (void)mailNotif;
}

void AutomatismRefillController::evaluateLocks(const SensorReadings& readings, const String& mail, bool mailNotif) {
  (void)readings;
  (void)mail;
  (void)mailNotif;
}

void AutomatismRefillController::processStart(const SensorReadings& readings, const String& mail, bool mailNotif) {
  (void)readings;
  (void)mail;
  (void)mailNotif;
}

void AutomatismRefillController::processRunning(const SensorReadings& readings, const String& mail, bool mailNotif) {
  (void)readings;
  (void)mail;
  (void)mailNotif;
}

void AutomatismRefillController::processStop(const SensorReadings& readings, const String& mail, bool mailNotif, bool forced) {
  (void)readings;
  (void)mail;
  (void)mailNotif;
  (void)forced;
}

void AutomatismRefillController::processRetries(const SensorReadings& readings, const String& mail, bool mailNotif) {
  (void)readings;
  (void)mail;
  (void)mailNotif;
}

void AutomatismRefillController::notifyServer(const SensorReadings& readings, const char* extraPairs) {
  (void)readings;
  (void)extraPairs;
}

void AutomatismRefillController::sendMail(const char* subject, const char* body, const String& mail) {
  (void)subject;
  (void)body;
  (void)mail;
}

