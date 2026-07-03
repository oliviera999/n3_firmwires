#include "camera_sleep.h"

#include <Arduino.h>
#include "config.h"
#include "n3_time.h"
#include <time.h>

const char* currentTargetName() {
#if defined(TARGET_MSP1)
  return "msp1";
#elif defined(TARGET_N3PP)
  return "n3pp";
#elif defined(TARGET_FFP3)
  return "ffp3";
#else
  return "unknown";
#endif
}

const char* wakeupCauseText(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0: return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1: return "ext1";
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP: return "ulp";
    default: return "non_deep_sleep";
  }
}

const char* resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN: return "unknown";
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "ext";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "other";
  }
}

bool inPhotoWindow() {
  if (!n3TimeHasPlausibleEpoch()) {
    Serial.println("[TIME][WARN] creneau ignore, horloge non fiable");
    return false;
  }
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[TIME][WARN] creneau ignore, getLocalTime indisponible");
    return false;
  }
  const int h = timeinfo.tm_hour;
  return (h >= HOUR_START && h < HOUR_END);
}

#if USE_DEEP_SLEEP
void n3EnterDeepSleepSeconds(uint32_t seconds) {
  Serial.printf("[SLEEP] Entree en deep sleep (%u s)\n", static_cast<unsigned int>(seconds));
  delay(200);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}
#endif
