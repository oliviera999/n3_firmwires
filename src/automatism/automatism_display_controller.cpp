#include "automatism/automatism_display_controller.h"

#include "automatism.h"
#include "automatism/automatism_utils.h"
#include <WiFi.h>

using namespace AutomatismUtils;

AutomatismDisplayController::AutomatismDisplayController(Automatism& core)
    : _core(core) {}

void AutomatismDisplayController::updateDisplay(const AutomatismRuntimeContext& ctx) {
  _core.updateDisplayInternal(ctx);
}

uint32_t AutomatismDisplayController::getRecommendedDisplayIntervalMs(uint32_t nowMs) const {
  return _core.getRecommendedDisplayIntervalMsInternal(nowMs);
}

void Automatism::updateDisplayInternal(const AutomatismRuntimeContext& ctx) {
  const uint32_t providedNow = ctx.nowMs;
  const uint32_t currentMillis = providedNow == 0 ? millis() : providedNow;

  if (!_disp.isPresent()) return;
  if (_disp.isLocked()) return;

  static unsigned long splashStartTime = 0;
  if (splashStartTime == 0) {
    splashStartTime = currentMillis;
  } else if (currentMillis - splashStartTime > 5000) {
    _disp.forceEndSplash();
    splashStartTime = 0;
  }

  if (_lastScreenSwitch == 0) {
    _lastScreenSwitch = currentMillis;
  } else if (currentMillis - _lastScreenSwitch >= screenSwitchInterval) {
    _oledToggle = !_oledToggle;
    _lastScreenSwitch += screenSwitchInterval;
    _disp.resetMainCache();
    _disp.resetStatusCache();
  }

  const uint32_t displayInterval = getRecommendedDisplayIntervalMsInternal(currentMillis);
  if (currentMillis - _lastOled >= displayInterval) {
    _lastOled = currentMillis;

    static unsigned long lastDebugLog = 0;
    if (currentMillis - lastDebugLog >= 10000) {
      Serial.printf("[Display] updateDisplay appelée - OLED présent: %s\n", _disp.isPresent() ? "OUI" : "NON");
      lastDebugLog = currentMillis;
    }

    SensorReadings readings = ctx.readings;

    if (readings.tempWater < -50 || readings.tempWater > 100 ||
        readings.tempAir < -50 || readings.tempAir > 100 ||
        readings.humidity < 0 || readings.humidity > 100) {
      Serial.println(F("[Display] Erreur lecture capteurs, utilisation valeurs par défaut"));
      readings.tempWater = NAN;
      readings.tempAir = NAN;
      readings.humidity = NAN;
      readings.wlAqua = 0;
      readings.wlTank = 0;
      readings.wlPota = 0;
      readings.luminosite = 0;
    }

    bool isCountdownMode = isStillPending(_countdownEnd, currentMillis);

    if (_currentFeedingPhase != FeedingPhase::NONE) {
      isCountdownMode = true;
    }

    bool forceImmediateMode = isCountdownMode ||
                             (_currentFeedingPhase != FeedingPhase::NONE) ||
                             (currentMillis % 1000 < 250);

    _disp.setUpdateMode(forceImmediateMode);

    if (isCountdownMode) {
      if (_currentFeedingPhase != FeedingPhase::NONE) {
        if (hasExpired(_feedingPhaseEnd, currentMillis) && _currentFeedingPhase == FeedingPhase::FEEDING_FORWARD) {
          _currentFeedingPhase = FeedingPhase::FEEDING_BACKWARD;
          _feedingPhaseEnd = _feedingTotalEnd;
        }

        if (_currentFeedingPhase == FeedingPhase::FEEDING_FORWARD) {
          uint32_t secLeft32 = remainingMs(_feedingPhaseEnd, currentMillis) / 1000UL;
          uint16_t secLeft = (secLeft32 > 65535u) ? 65535u : (uint16_t)secLeft32;
          _disp.showFeedingCountdown("Nourrissage", "avant", secLeft, (_manualFeedingActive || isFeedingInManualMode()));
        } else if (_currentFeedingPhase == FeedingPhase::FEEDING_BACKWARD) {
          uint32_t secLeft32 = remainingMs(_feedingTotalEnd, currentMillis) / 1000UL;
          uint16_t secLeft = (secLeft32 > 65535u) ? 65535u : (uint16_t)secLeft32;
          _disp.showFeedingCountdown("Nourrissage", "arriere", secLeft, (_manualFeedingActive || isFeedingInManualMode()));
        }

        if (hasExpired(_feedingTotalEnd, currentMillis)) {
          _currentFeedingPhase = FeedingPhase::NONE;
          _manualFeedingActive = false;
          if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
            SensorReadings cur = _sensors.read();
            sendFullUpdate(cur, "bouffePetits=0&bouffeGros=0&108=0&109=0");
            Serial.println(F("[Auto] ✅ Fin nourrissage notifiée au serveur - bouffePetits=0, bouffeGros=0"));
          }
        }
      } else {
        uint32_t secLeft32 = remainingMs(_countdownEnd, currentMillis) / 1000UL;
        uint16_t secLeft = (secLeft32 > 65535u) ? 65535u : (uint16_t)secLeft32;
        _disp.showCountdown(_countdownLabel.c_str(), secLeft, isRefillingInManualMode());
      }
    } else {
      _disp.beginUpdate();

      if (_oledToggle) {
        _disp.showMain(readings.tempWater, readings.tempAir, readings.humidity, readings.wlAqua,
                       readings.wlTank, readings.wlPota, readings.luminosite, _power.getCurrentTimeString());
      } else {
        _disp.showServerVars(_acts.isAquaPumpRunning(), _acts.isTankPumpRunning(), _acts.isHeaterOn(), _acts.isLightOn(),
                             bouffeMatin, bouffeMidi, bouffeSoir,
                             tempsPetits, tempsGros,
                             aqThresholdCm, tankThresholdCm, heaterThresholdC,
                             refillDurationMs / 1000, limFlood,
                             forceWakeUp, static_cast<uint16_t>(freqWakeSec));
      }

      int diffNow = _sensors.diffMaree(readings.wlAqua);
      int8_t tideDir = 0;
      if (diffNow > tideTriggerCm) tideDir = 1;
      else if (diffNow < -tideTriggerCm) tideDir = -1;
      _lastDiffMaree = diffNow;

      bool blinkNow = (mailBlinkUntil && isStillPending(mailBlinkUntil, currentMillis) && ((currentMillis / 200) % 2));
      _disp.drawStatus(sendState, recvState, WiFi.isConnected() ? WiFi.RSSI() : -127,
                       blinkNow, tideDir, diffNow);

      _disp.endUpdate();
    }

    if (hasExpired(mailBlinkUntil, currentMillis)) mailBlinkUntil = 0;
  }
}

uint32_t Automatism::getRecommendedDisplayIntervalMsInternal(uint32_t nowMs) const {
  uint32_t now = nowMs == 0 ? millis() : nowMs;
  bool isCountdownMode = (isStillPending(_countdownEnd, now)
                          || (_currentFeedingPhase != FeedingPhase::NONE));
  return isCountdownMode ? 250u : 1000u;
}


