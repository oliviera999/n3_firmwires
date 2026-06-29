#pragma once

#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

#ifndef PGL_VERBOSE_LOG
#define PGL_VERBOSE_LOG 0
#endif

#define PGL_LOG(fmt, ...) Serial.printf("[PGL] " fmt "\n", ##__VA_ARGS__)

#if PGL_VERBOSE_LOG
#define PGL_LOG_V(fmt, ...) Serial.printf("[PGL][V] " fmt "\n", ##__VA_ARGS__)
#else
#define PGL_LOG_V(fmt, ...) ((void)0)
#endif

#ifndef PGL_DISPLAY_DEBUG
#define PGL_DISPLAY_DEBUG 0
#endif

#if PGL_DISPLAY_DEBUG
#define PGL_LOG_DISP(fmt, ...) Serial.printf("[PGL][DISP] " fmt "\n", ##__VA_ARGS__)
#else
#define PGL_LOG_DISP(fmt, ...) ((void)0)
#endif

inline void pglLogWakeupCause() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      PGL_LOG("Reveil: reset / premier demarrage");
      break;
    case ESP_SLEEP_WAKEUP_EXT0:
      PGL_LOG("Reveil: EXT0 (GPIO IR, pin %d)", PGL_IR_PIN);
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      PGL_LOG("Reveil: EXT1");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      PGL_LOG("Reveil: timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      PGL_LOG("Reveil: touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      PGL_LOG("Reveil: ULP");
      break;
    case ESP_SLEEP_WAKEUP_GPIO:
      PGL_LOG("Reveil: GPIO");
      break;
    default:
      PGL_LOG("Reveil: cause=%d", static_cast<int>(cause));
      break;
  }
}

inline void pglLogMemory() {
  PGL_LOG("Memoire: heap=%lu minHeap=%lu psram=%lu",
          static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getFreePsram()));
#if PGL_VERBOSE_LOG
  PGL_LOG_V("Flash size=%lu chipRev=%d cpu=%dMHz",
            static_cast<unsigned long>(ESP.getFlashChipSize()),
            ESP.getChipRevision(),
            ESP.getCpuFreqMHz());
#endif
}

void pglLogBootBanner();

inline const char* pglWifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAIL";
    case WL_CONNECTION_LOST: return "LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

/** Libelle court pour WiFi.reason() (ESP-IDF wifi_err_reason_t). */
inline const char* pglWifiDisconnectReasonName(uint8_t reason) {
  switch (reason) {
    case 0: return "-";
    case 1: return "UNSPEC";
    case 2: return "AUTH_EXPIRE";
    case 15: return "4WAY_HANDSHAKE";
    case 17: return "HANDSHAKE_TO";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_FAIL";
    default: return "REASON";
  }
}
