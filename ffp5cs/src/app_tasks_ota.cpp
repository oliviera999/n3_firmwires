// app_tasks_ota.cpp — Tâche FreeRTOS OTA : vérification/déclenchement des mises
// à jour firmware (boot + périodique + trigger distant), avec gestion du heap
// exclusif. Extrait d'app_tasks.cpp (refonte god-file, audit v13.93) ;
// déplacement verbatim. État partagé via app_tasks_internal.h ; réserve mail
// via task_mail.h.
#include "app_tasks_internal.h"  // g_ctx, g_otaTaskHandle, g_otaTriggerQueue, otaTask
#include "task_mail.h"           // allocMailReserveIfNeeded, mailReserveReleaseIfInternal
#include "app_context.h"         // AppContext (g_ctx->otaManager)
#include "config.h"              // TaskConfig, HeapConfig, TimingConfig
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
// Libère la réserve mail interne pour maximiser le bloc contigu heap avant OTA.
static bool prepareOtaExclusiveHeap() {
  bool freed = false;
#if FEATURE_MAIL
  if (mailReserveReleaseIfInternal()) {
    freed = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    Serial.printf("[otaTask] Réserve mail libérée pour OTA: free=%u blk=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
  }
#endif
  return freed;
}

// Vérification OTA (boot ou périodique) : prio absolue, attente fin OTA_Update si nouvelle version.
static void runOtaCheckCycle(uint32_t minContiguousBlock, const char* phaseLabel) {
  if (!g_ctx || g_ctx->otaManager.isOtaExclusive()) return;

  bool mailReserveFreed = prepareOtaExclusiveHeap();
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

  Serial.printf("[otaTask] %s heap: free=%u blk=%u (min OTA=%u, min blk=%u)\n",
                phaseLabel,
                (unsigned)freeHeap, (unsigned)largestBlock,
                (unsigned)HeapConfig::MIN_HEAP_OTA, (unsigned)minContiguousBlock);

  if (freeHeap < HeapConfig::MIN_HEAP_OTA || largestBlock < minContiguousBlock) {
    Serial.printf("[otaTask] OTA reportée (%s): heap=%u blk=%u\n",
                  phaseLabel, (unsigned)freeHeap, (unsigned)largestBlock);
#if FEATURE_MAIL
    if (mailReserveFreed && !g_ctx->otaManager.isOtaExclusive()) {
      allocMailReserveIfNeeded(HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS);
    }
#endif
    return;
  }

  esp_task_wdt_reset();
  g_ctx->otaManager.setCurrentVersion(ProjectConfig::VERSION);
  if (g_otaTaskHandle) {
    vTaskPrioritySet(g_otaTaskHandle, TaskConfig::OTA_TASK_PRIORITY_WHILE_RUNNING);
  }

#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[otaTask] %s check\n", phaseLabel);
#else
  Serial.printf("[otaTask] %s: vérification OTA (priorité absolue)\n", phaseLabel);
#endif

  if (g_ctx->otaManager.checkForUpdate()) {
    g_ctx->otaManager.performUpdate();
    while (g_ctx && g_ctx->otaManager.isOtaExclusive()) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }

  if (g_otaTaskHandle && g_ctx && !g_ctx->otaManager.isOtaExclusive()) {
    vTaskPrioritySet(g_otaTaskHandle, TaskConfig::OTA_TASK_PRIORITY);
  }
#if FEATURE_MAIL
  if (mailReserveFreed && g_ctx && !g_ctx->otaManager.isOtaExclusive()) {
    allocMailReserveIfNeeded(HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS);
  }
#endif
}

// Tâche OTA dédiée : priorité supérieure à netTask, stack dédiée (évite stack overflow TLS/Update).
// Exécute OTA au boot puis boucle périodique (2h) ou sur trigger (queue). Seul propriétaire des appels OTAManager.
void otaTask(void* pv) {
  (void)pv;
  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }
  esp_task_wdt_reset();

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < TimingConfig::WIFI_BOOT_TIMEOUT_MS) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  if (WiFi.status() == WL_CONNECTED && g_ctx) {
#if !defined(PROFILE_TEST) || defined(BOARD_S3)
    g_ctx->power.waitForNetworkReady();
    esp_task_wdt_reset();
#if defined(BOARD_WROOM)
    // Attente longue au boot : laisser netTask/mailTask finir leurs premières allocations
    // pour que le heap se défragmente avant la connexion TLS OTA (~45 KB contigus requis)
    constexpr unsigned long OTA_BOOT_SETTLE_MS = 8000;
    constexpr uint32_t OTA_MIN_CONTIGUOUS_BLOCK = 45000;
    for (unsigned long waited = 0; waited < OTA_BOOT_SETTLE_MS; waited += 1000) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_task_wdt_reset();
      uint32_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
      if (blk >= OTA_MIN_CONTIGUOUS_BLOCK) break;
    }
    esp_task_wdt_reset();
#elif defined(BOARD_S3) && !defined(BOARD_HAS_PSRAM)
    const unsigned long S3_FIRST_TLS_DELAY_MS = 3000;
    vTaskDelay(pdMS_TO_TICKS(S3_FIRST_TLS_DELAY_MS));
    esp_task_wdt_reset();
#endif
    runOtaCheckCycle(HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS, "Boot");
#endif
  }

  // Ne jamais bloquer plus de OTA_WDT_FEED_INTERVAL_MS sans reset WDT (TWDT 30s/60s)
  unsigned long lastOtaCheckMs = millis();
  for (;;) {
    uint8_t trigger = 0;
    BaseType_t received = xQueueReceive(g_otaTriggerQueue, &trigger, pdMS_TO_TICKS(TimingConfig::OTA_WDT_FEED_INTERVAL_MS));
    esp_task_wdt_reset();
    bool doCheck = (received == pdTRUE) || (millis() - lastOtaCheckMs >= TimingConfig::OTA_CHECK_INTERVAL_MS);
    if (!doCheck) {
      continue;
    }
    lastOtaCheckMs = millis();
    if (!g_ctx || WiFi.status() != WL_CONNECTED || g_ctx->otaManager.isOtaExclusive()) {
      continue;
    }
    if (ESP.getFreeHeap() < HeapConfig::MIN_HEAP_OTA) {
      continue;
    }
    // WROOM: metadata fetch en HTTP (pas de TLS), seuil réduit à 28 KB (marge post-réveil)
    // S3: garde TLS complète (metadata en HTTPS)
#if defined(BOARD_WROOM)
    constexpr uint32_t OTA_MIN_BLOCK_PERIODIC = 28000;
#else
    constexpr uint32_t OTA_MIN_BLOCK_PERIODIC = HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS;
#endif
    if (heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) < OTA_MIN_BLOCK_PERIODIC) {
      Serial.printf("[otaTask] OTA reportée: bloc contigu insuffisant (%u < %u)\n",
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
                    (unsigned)OTA_MIN_BLOCK_PERIODIC);
      continue;
    }
    runOtaCheckCycle(OTA_MIN_BLOCK_PERIODIC, "Periodic");
    esp_task_wdt_reset();
  }
}
#endif
