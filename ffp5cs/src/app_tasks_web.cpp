// app_tasks_web.cpp — Corps de la tâche FreeRTOS webTask (interface web dédiée).
// Extrait d'app_tasks.cpp (refonte god-file, audit v13.93) ; déplacement
// verbatim, comportement identique. État partagé via app_tasks_internal.h.
#include "app_tasks_internal.h"
#include "app_context.h"   // AppContext (g_ctx->otaManager, g_ctx->webServer)
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

void webTask(void* pv) {
  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }

#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[Web] webTask started\n");
#else
  Serial.println(F("[Web] Tâche webTask démarrée - interface web dédiée"));
#endif

  // v11.171: Monitoring HWM webTask (toutes builds - marge critique ~212 bytes)
  static unsigned long lastStackCheckMs = 0;
  static const unsigned long STACK_CHECK_INTERVAL_MS = 60000;  // Toutes les 60 secondes
  static UBaseType_t minHwmObserved = UINT16_MAX;  // Track minimum HWM

  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(200);

  for (;;) {
    // v11.165: Protection NULL pointer (audit robustesse)
    if (!g_ctx) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
      continue;
    }
    
    if (g_ctx->otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));
      continue;
    }

    esp_task_wdt_reset();
    g_ctx->webServer.loop();
    
    // v11.171: Vérification périodique HWM webTask (activé en prod car marge critique)
    unsigned long nowMs = millis();
    if (nowMs - lastStackCheckMs > STACK_CHECK_INTERVAL_MS) {
      UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
      if (hwm < minHwmObserved) {
        minHwmObserved = hwm;
      }
      // Alerte si moins de 512 bytes libres (critique pour webTask)
      if (hwm < 512) {
        Serial.printf("[webTask] CRIT: HWM=%u bytes (<512)\n", (unsigned)hwm);
        Serial.println("[Event] CRITICAL: webTask stack dangerously low");
      } else if (hwm < 1024) {
        Serial.printf("[webTask] WARN: HWM=%u bytes (<1KB)\n", (unsigned)hwm);
      }
      #if FEATURE_DIAG_STACK_LOGS
      else {
        Serial.printf("[webTask] HWM: %u bytes libres (min observé: %u)\n", 
                      (unsigned)hwm, (unsigned)minHwmObserved);
      }
      #endif
      lastStackCheckMs = nowMs;
    }
    
    vTaskDelayUntil(&lastWake, period);
  }
}
