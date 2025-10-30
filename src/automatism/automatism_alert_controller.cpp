#include "automatism/automatism_alert_controller.h"

#include "automatism.h"
#include "automatism/automatism_utils.h"
#include <WiFi.h>

using namespace AutomatismUtils;

AutomatismAlertController::AutomatismAlertController(Automatism& core)
    : _core(core) {}

void AutomatismAlertController::process(const AutomatismRuntimeContext& ctx) {
  _core.handleAlertsInternal(ctx);
  esp_task_wdt_reset();
}

void Automatism::handleAlertsInternal(const AutomatismRuntimeContext& ctx) {
  const SensorReadings& r = ctx.readings;

  static bool lowAquaSent  = false;
  static bool highAquaSent = false;

  if (r.wlAqua > aqThresholdCm && !lowAquaSent && mailNotif) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", r.wlAqua, " cm (> ", aqThresholdCm);
    _mailer.sendAlert("Alerte - Niveau aquarium BAS", msgBuffer, _emailAddress);
    lowAquaSent = true;
    armMailBlink();
  }

  if (r.wlAqua <= aqThresholdCm - 5) lowAquaSent = false;

  {
    time_t nowEpoch = _power.getCurrentEpoch();
    if (r.wlAqua < limFlood) {
      if (floodEnterSinceEpoch == 0) floodEnterSinceEpoch = nowEpoch;
      aboveResetSinceEpoch = 0;
      if (!inFlood && mailNotif) {
        uint32_t elapsedUnder = (nowEpoch >= floodEnterSinceEpoch) ? (nowEpoch - floodEnterSinceEpoch) : 0;
        bool debounceOk = elapsedUnder >= (floodDebounceMin * 60UL);
        bool cooldownOk = (lastFloodEmailEpoch == 0) || ((nowEpoch - lastFloodEmailEpoch) >= (floodCooldownMin * 60UL));
        if (debounceOk && cooldownOk) {
          char msgBuffer[128];
          formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", r.wlAqua, " cm (< ", limFlood);
          if (tankPumpLocked || _config.getPompeAquaLocked()) {
            strncat(msgBuffer, " / Pompe verrouillée", sizeof(msgBuffer) - strlen(msgBuffer) - 1);
          }
          bool sent = _mailer.sendAlert("Alerte - Aquarium TROP PLEIN", msgBuffer, _emailAddress);
          if (sent) {
            inFlood = true;
            highAquaSent = true;
            armMailBlink();
            lastFloodEmailEpoch = nowEpoch;
            Preferences prefs; prefs.begin("alerts", false); prefs.putULong("floodLast", lastFloodEmailEpoch); prefs.end();
            Serial.println(F("[Auto] Email TROP PLEIN envoyé (anti-spam actif)"));
          } else {
            Serial.println(F("[Auto] Échec envoi email TROP PLEIN"));
          }
        }
      }
    } else {
      floodEnterSinceEpoch = 0;
      if (r.wlAqua >= limFlood + floodHystCm) {
        if (aboveResetSinceEpoch == 0) aboveResetSinceEpoch = nowEpoch;
        uint32_t elapsedAbove = (nowEpoch >= aboveResetSinceEpoch) ? (nowEpoch - aboveResetSinceEpoch) : 0;
        if (elapsedAbove >= (floodResetStableMin * 60UL)) {
          inFlood = false;
          highAquaSent = false;
        }
      } else {
        aboveResetSinceEpoch = 0;
      }
    }
  }

  static bool lowTankSent = false;

  if (r.wlTank > tankThresholdCm && !lowTankSent && mailNotif) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", r.wlTank, " cm (> ", tankThresholdCm);
    _mailer.sendAlert("Alerte - Réserve BASSE", msgBuffer, _emailAddress);
    lowTankSent = true;
    armMailBlink();
  }

  else if (lowTankSent && r.wlTank <= tankThresholdCm - 5 && mailNotif) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", r.wlTank, " cm (<= ", tankThresholdCm - 5);
    _mailer.sendAlert("Info - Réserve OK", msgBuffer, _emailAddress);
    lowTankSent = false;
    armMailBlink();
  }

  if (r.tempWater < heaterThresholdC && !heaterPrevState) {
    _acts.startHeater();
    heaterPrevState = true;
    if (mailNotif) {
      char msgBuffer[64];
      formatTemperatureAlert(msgBuffer, sizeof(msgBuffer), "Temp eau: ", r.tempWater);
      _mailer.sendAlert("Chauffage ON", msgBuffer, _emailAddress);
      armMailBlink();
    }
  } else if (r.tempWater > heaterThresholdC + 2 && heaterPrevState) {
    _acts.stopHeater();
    heaterPrevState = false;
    if (mailNotif) {
      char msgBuffer[64];
      formatTemperatureAlert(msgBuffer, sizeof(msgBuffer), "Temp eau: ", r.tempWater);
      _mailer.sendAlert("Chauffage OFF", msgBuffer, _emailAddress);
      armMailBlink();
    }
  }
}


