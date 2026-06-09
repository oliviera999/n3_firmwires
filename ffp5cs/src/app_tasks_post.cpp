// app_tasks_post.cpp — Tâche FreeRTOS postSenderTask : envoi "fire-and-forget"
// des POST (données + heartbeat) via la file g_postSenderQueue, pour que netTask
// libère son slot immédiatement. Extrait d'app_tasks.cpp (refonte god-file,
// audit v13.93) ; déplacement verbatim. État partagé via app_tasks_internal.h.
#include "app_tasks_internal.h"  // g_ctx, g_postSenderQueue, PostSenderMsg, postSenderTask
#include "app_context.h"         // AppContext (g_ctx->...)
#include "web_client.h"          // postToUrl, buildHeartbeatPayload
#include "sd_logger.h"           // file SD (suppression entrée après succès)
#include "config.h"
#include "tls_mutex.h"
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

void postSenderTask(void* pv) {
  (void)pv;
  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }
  PostSenderMsg msg;
  const TickType_t queueReceiveTicks = pdMS_TO_TICKS(15000);  // 15s max sans feed WDT (TWDT 30s/60s)
  for (;;) {
    BaseType_t got = xQueueReceive(g_postSenderQueue, &msg, queueReceiveTicks);
    esp_task_wdt_reset();
    if (got != pdTRUE) continue;
    if (!g_ctx) continue;
    if (g_ctx->otaManager.isOtaExclusive()) {
      if (LogConfig::SERIAL_ENABLED) {
        Serial.println(F("[postSender] OTA exclusif — POST reporté"));
      }
      if (xQueueSendToFront(g_postSenderQueue, &msg, 0) != pdTRUE) {
        if (msg.type == PostSenderType::PostData && msg.payload[0] != '\0') {
          (void)g_ctx->webClient.queueFailedPost(msg.payload);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    if (msg.type == PostSenderType::PostData) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[postSender] post-data start\n");
#else
      if (LogConfig::SERIAL_ENABLED) {
        Serial.println(F("[postSender] Exécution POST mesures (post-data) — le verdict HTTP suit sous [HTTP]"));
      }
#endif
      bool postOk = g_ctx->webClient.tryPushStatusToServer(msg.payload);
      if (postOk && msg.hasSdQueueEntry) {
        SdLogger::markSent(msg.sdSeqNum);
      }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[postSender] post-data end ok=%d\n", postOk ? 1 : 0);
#else
      if (LogConfig::SERIAL_ENABLED) {
        Serial.printf("[postSender] Fin POST mesures: %s\n",
                      postOk ? "succès (2xx/3xx) ou accepté"
                             : "échec — voir [HTTP] Verdict ; payload peut être en file NVS");
      }
#endif
    } else if (msg.type == PostSenderType::Heartbeat) {
      char heartbeatUrl[256];
      ServerConfig::getHeartbeatUrl(heartbeatUrl, sizeof(heartbeatUrl));
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[postSender] heartbeat start\n");
#else
      if (LogConfig::SERIAL_ENABLED) {
        Serial.println(F("[postSender] Exécution POST heartbeat"));
      }
#endif
      bool hbOk = g_ctx->webClient.postToUrl(heartbeatUrl, msg.payload, NetworkConfig::HTTP_POST_TIMEOUT_MS);
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[postSender] heartbeat end ok=%d\n", hbOk ? 1 : 0);
#else
      if (LogConfig::SERIAL_ENABLED) {
        Serial.printf("[postSender] Fin heartbeat: %s\n", hbOk ? "OK" : "échec — voir [HTTP] Verdict");
      }
#endif
    }
    esp_task_wdt_reset();
  }
}
