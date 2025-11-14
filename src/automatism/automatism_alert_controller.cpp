#include "automatism/automatism_alert_controller.h"
#include "nvs_manager.h" // v11.114

#include "automatism.h"
#include "automatism/automatism_utils.h"
#include <WiFi.h>
#include <cstring>
#include "esp_task_wdt.h"

using namespace AutomatismUtils;

AutomatismAlertController::AutomatismAlertController(Automatism& core)
    : _core(core) {}

void AutomatismAlertController::process(const AutomatismRuntimeContext& ctx) {
  const SensorReadings& readings = ctx.readings;
  Automatism& core = _core;
  const bool mailEnabled = core.mailNotif;

  if (readings.wlAqua > core.aqThresholdCm && !_lowAquaSent && mailEnabled) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer,
                        sizeof(msgBuffer),
                        "Distance: ",
                        readings.wlAqua,
                        " cm (> ",
                        core.aqThresholdCm);
    core._mailer.sendAlert("Alerte - Niveau aquarium BAS", msgBuffer, core._emailAddress);
    _lowAquaSent = true;
    core.armMailBlink();
  }

  if (readings.wlAqua <= core.aqThresholdCm - 5) {
    _lowAquaSent = false;
  }

  time_t nowEpoch = core._power.getCurrentEpoch();
  if (readings.wlAqua < core.limFlood) {
    if (core.floodEnterSinceEpoch == 0) {
      core.floodEnterSinceEpoch = nowEpoch;
    }
    core.aboveResetSinceEpoch = 0;
    if (!core.inFlood && mailEnabled) {
      uint32_t elapsedUnder = (nowEpoch >= core.floodEnterSinceEpoch) ? (nowEpoch - core.floodEnterSinceEpoch) : 0;
      bool debounceOk = elapsedUnder >= (core.floodDebounceMin * 60UL);
      bool cooldownOk = (core.lastFloodEmailEpoch == 0) || ((nowEpoch - core.lastFloodEmailEpoch) >= (core.floodCooldownMin * 60UL));
      if (debounceOk && cooldownOk) {
        char msgBuffer[128];
        formatDistanceAlert(msgBuffer,
                            sizeof(msgBuffer),
                            "Distance: ",
                            readings.wlAqua,
                            " cm (< ",
                            core.limFlood);
        if (core.tankPumpLocked || core._config.getPompeAquaLocked()) {
          strncat(msgBuffer, " / Pompe verrouillée", sizeof(msgBuffer) - strlen(msgBuffer) - 1);
        }
        bool sent = core._mailer.sendAlert("Alerte - Aquarium TROP PLEIN", msgBuffer, core._emailAddress);
        if (sent) {
          core.inFlood = true;
          _highAquaSent = true;
          core.armMailBlink();
          core.lastFloodEmailEpoch = nowEpoch;
          g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "alert_floodLast", core.lastFloodEmailEpoch);
          Serial.println(F("[Auto] Email TROP PLEIN envoyé (anti-spam actif)"));
        } else {
          Serial.println(F("[Auto] Échec envoi email TROP PLEIN"));
        }
      }
    }
  } else {
    core.floodEnterSinceEpoch = 0;
    if (readings.wlAqua >= core.limFlood + core.floodHystCm) {
      if (core.aboveResetSinceEpoch == 0) {
        core.aboveResetSinceEpoch = nowEpoch;
      }
      uint32_t elapsedAbove = (nowEpoch >= core.aboveResetSinceEpoch) ? (nowEpoch - core.aboveResetSinceEpoch) : 0;
      if (elapsedAbove >= (core.floodResetStableMin * 60UL)) {
        core.inFlood = false;
        _highAquaSent = false;
      }
    } else {
      core.aboveResetSinceEpoch = 0;
    }
  }

  if (readings.wlTank > core.tankThresholdCm && !_lowTankSent && mailEnabled) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer,
                        sizeof(msgBuffer),
                        "Distance: ",
                        readings.wlTank,
                        " cm (> ",
                        core.tankThresholdCm);
    core._mailer.sendAlert("Alerte - Réserve BASSE", msgBuffer, core._emailAddress);
    _lowTankSent = true;
    core.armMailBlink();
  } else if (_lowTankSent && readings.wlTank <= core.tankThresholdCm - 5 && mailEnabled) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer,
                        sizeof(msgBuffer),
                        "Distance: ",
                        readings.wlTank,
                        " cm (<= ",
                        core.tankThresholdCm - 5);
    core._mailer.sendAlert("Info - Réserve OK", msgBuffer, core._emailAddress);
    _lowTankSent = false;
    core.armMailBlink();
  }

  if (readings.tempWater < core.heaterThresholdC && !core.heaterPrevState) {
    core._acts.startHeater();
    core.heaterPrevState = true;
    if (mailEnabled) {
      char msgBuffer[64];
      formatTemperatureAlert(msgBuffer,
                             sizeof(msgBuffer),
                             "Temp eau: ",
                             readings.tempWater);
      core._mailer.sendAlert("Chauffage ON", msgBuffer, core._emailAddress);
      core.armMailBlink();
    }
  } else if (readings.tempWater > core.heaterThresholdC + 2 && core.heaterPrevState) {
    core._acts.stopHeater();
    core.heaterPrevState = false;
    if (mailEnabled) {
      char msgBuffer[64];
      formatTemperatureAlert(msgBuffer,
                             sizeof(msgBuffer),
                             "Temp eau: ",
                             readings.tempWater);
      core._mailer.sendAlert("Chauffage OFF", msgBuffer, core._emailAddress);
      core.armMailBlink();
    }
  }

  esp_task_wdt_reset();
}


