#include "app_tasks.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <math.h>  // isnan
#include <cstdlib>  // malloc, free (requêtes réseau heap pour éviter use-after-return)
#include <cstring>  // memset
#include <esp_heap_caps.h>  // v11.157: Pour heap_caps_get_largest_free_block()
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>  // Pour xTaskCreateStaticPinnedToCore

#include "gpio_parser.h"
#include "config.h"
#include "tls_mutex.h"  // v11.155: Pour traitement mail séquentiel
#include "diagnostics.h"
#include "system_sensors.h"  // Pour SensorReadings
#include "sd_logger.h"
#include "web_client.h"  // buildHeartbeatPayload, postToUrl (postSenderTask)
#include "net_request_pool.h"  // NetRequest, pool alloc/free (extrait v13.93)
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

#include "app_tasks_internal.h"
#include "task_mail.h"  // réserve mail + processMailQueueIfReady (extrait v13.93)

// g_ctx : linkage externe (corps de tâches déplacés, ex. webTask -> app_tasks_web.cpp),
// fixé par AppTasks::start(). Déclaré dans app_tasks_internal.h.
AppContext* g_ctx = nullptr;
// g_sensorQueue : linkage externe (producteur sensorTask -> app_tasks_sensor.cpp).
QueueHandle_t g_sensorQueue = nullptr;

namespace {


// v11.157: Buffers statiques pour stacks (allocation statique pour réduire fragmentation)
// webTask reste en dynamique (heap) : passer en statique dépasserait la limite BSS 160 KB au link.
static StackType_t sensorTaskStack[TaskConfig::SENSOR_TASK_STACK_SIZE];
static StaticTask_t sensorTaskTCB;

static StackType_t automationTaskStack[TaskConfig::AUTOMATION_TASK_STACK_SIZE];
static StaticTask_t automationTaskTCB;

static StackType_t netTaskStack[TaskConfig::NET_TASK_STACK_SIZE];
static StaticTask_t netTaskTCB;

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
static QueueHandle_t g_otaTriggerQueue = nullptr;  // Notification vers tâche OTA (boot, 2h, triggerOtaCheck)
static constexpr size_t kOtaTriggerQueueLen = 2;
static StackType_t otaTaskStack[TaskConfig::OTA_TASK_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t otaTaskTCB;
TaskHandle_t g_otaTaskHandle = nullptr;
// v13.70 (audit): handle postSender exposé pour task_monitor (HWM monitoring).
TaskHandle_t g_postSenderTaskHandle = nullptr;
#endif

TaskHandle_t g_sensorTaskHandle = nullptr;
TaskHandle_t g_webTaskHandle = nullptr;
TaskHandle_t g_autoTaskHandle = nullptr;
TaskHandle_t g_displayTaskHandle = nullptr;

// v11.160: Document JSON de fallback réseau alloué statiquement
// Évite un gros objet sur la stack d'automationTask lors des timeouts capteurs
static StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> g_remoteFallbackDoc;

// ============================================================================
// Point 2: netTask (unique propriétaire de WebClient/TLS)
// ============================================================================

QueueHandle_t g_netQueue = nullptr;
TaskHandle_t g_netTaskHandle = nullptr;

// Fire-and-forget POST : tâche dédiée (post-data + heartbeat), netTask libère le slot immédiatement
enum class PostSenderType : uint8_t { PostData = 0, Heartbeat = 1 };
struct PostSenderMsg {
  PostSenderType type;
  char payload[BufferConfig::POST_PAYLOAD_MAX_SIZE];
  uint32_t sdSeqNum;
  bool hasSdQueueEntry;
};
static QueueHandle_t g_postSenderQueue = nullptr;
static constexpr size_t kPostSenderQueueLen = 4;



// Logs périodiques bouffe et pompe (centralisés pour alléger automationTask).
static void logBouffeAndPumpStats(AppContext* ctx, unsigned long now,
                                  unsigned long* lastBouffeDisplay, unsigned long* lastPumpStatsDisplay) {
  if (!ctx) return;
  if (now - *lastBouffeDisplay >= TimingConfig::BOUFFE_DISPLAY_INTERVAL_MS) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[Bouffe] M:%d Mi:%d S:%d jour:%d lock:%d\n",
               ctx->config.getBouffeMatinOk() ? 1 : 0, ctx->config.getBouffeMidiOk() ? 1 : 0,
               ctx->config.getBouffeSoirOk() ? 1 : 0, ctx->config.getLastJourBouf(),
               ctx->config.getPompeAquaLocked() ? 1 : 0);
#else
    Serial.println(F("=== ÉTAT DES FLAGS DE BOUFFE ==="));
    Serial.printf("Bouffe Matin: %s\n", ctx->config.getBouffeMatinOk() ? "✓ FAIT" : "✗ À FAIRE");
    Serial.printf("Bouffe Midi:  %s\n", ctx->config.getBouffeMidiOk() ? "✓ FAIT" : "✗ À FAIRE");
    Serial.printf("Bouffe Soir:  %s\n", ctx->config.getBouffeSoirOk() ? "✓ FAIT" : "✗ À FAIRE");
    Serial.printf("Dernier jour: %d\n", ctx->config.getLastJourBouf());
    Serial.printf("Pompe lock:   %s\n", ctx->config.getPompeAquaLocked() ? "VERROUILLÉE" : "LIBRE");
    Serial.println(F("==============================="));
#endif
    *lastBouffeDisplay = now;
  }
  if (now - *lastPumpStatsDisplay >= TimingConfig::PUMP_STATS_DISPLAY_INTERVAL_MS) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[Pump] run:%d total:%lu stops:%lu\n",
               ctx->actuators.isTankPumpRunning() ? 1 : 0,
               (unsigned long)ctx->actuators.getTankPumpTotalRuntime(),
               (unsigned long)ctx->actuators.getTankPumpTotalStops());
#else
    Serial.println(F("=== STATISTIQUES POMPE DE RÉSERVE ==="));
    Serial.printf("État actuel: %s\n", ctx->actuators.isTankPumpRunning() ? "EN COURS" : "ARRÊTÉE");
    if (ctx->actuators.isTankPumpRunning()) {
      unsigned long currentRuntime = ctx->actuators.getTankPumpCurrentRuntime();
      Serial.printf("Durée actuelle: %lu ms (%lu s)\n", currentRuntime, currentRuntime / 1000);
    }
    Serial.printf("Temps total d'activité: %lu ms (%lu s)\n",
                  ctx->actuators.getTankPumpTotalRuntime(),
                  ctx->actuators.getTankPumpTotalRuntime() / 1000);
    Serial.printf("Nombre total d'arrêts: %lu\n", ctx->actuators.getTankPumpTotalStops());
    if (ctx->actuators.getTankPumpLastStopTime() > 0) {
      unsigned long timeSinceLastStop = now - ctx->actuators.getTankPumpLastStopTime();
      Serial.printf("Dernier arrêt: il y a %lu ms (%lu s)\n", timeSinceLastStop, timeSinceLastStop / 1000);
    }
    Serial.println(F("====================================="));
#endif
    *lastPumpStatsDisplay = now;
  }
  // Yield Core 1 après burst Serial (évite INT_WDT si automationTask enchaîne sans relâche)
  vTaskDelay(pdMS_TO_TICKS(1));
}

// v11.195: Valider req->requester avant xTaskNotifyGive — LoadStoreError dans xTaskGenericNotify
// quand req pointe vers la stack du caller (queue) et requester peut être corrompu.
static void netNotifyDone(NetRequest* req) {
  if (!req) return;
  TaskHandle_t h = req->requester;
  if (!h) return;
  // N'appeler xTaskNotifyGive qu'avec un handle connu (évite crash si requester corrompu)
  if (h == g_autoTaskHandle || h == g_webTaskHandle || h == g_sensorTaskHandle || h == g_displayTaskHandle) {
    xTaskNotifyGive(h);
  }
}

// Tâche dédiée POST (fire-and-forget) : post-data et heartbeat, netTask libère le slot immédiatement
static void postSenderTask(void* pv) {
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
static void otaTask(void* pv) {
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

static void netTask(void* pv) {
  (void)pv;
  
  // Enregistrer watchdog pour netTask
  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }
  esp_task_wdt_reset();  // P1: démarrer la fenêtre TWDT après l'add (évite WDT si GET boot bloque)

#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[netTask] started\n");
#else
  Serial.println(F("[netTask] Démarrée (TLS/HTTP propriétaire unique)"));
#endif

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < TimingConfig::WIFI_BOOT_TIMEOUT_MS) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  bool bootServerReachable = false;
  if (WiFi.status() == WL_CONNECTED && g_ctx) {
    g_ctx->power.waitForNetworkReady();
    esp_task_wdt_reset();
#if defined(BOARD_WROOM)
    const unsigned long FIRST_TLS_DELAY_MS = 2000;
    vTaskDelay(pdMS_TO_TICKS(FIRST_TLS_DELAY_MS));
    esp_task_wdt_reset();
#elif defined(BOARD_S3) && !defined(BOARD_HAS_PSRAM)
    const unsigned long S3_FIRST_TLS_DELAY_MS = 3000;
    vTaskDelay(pdMS_TO_TICKS(S3_FIRST_TLS_DELAY_MS));
    esp_task_wdt_reset();
#endif

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[netTask] Boot tryFetchConfig\n");
#else
    Serial.println(F("[netTask] Boot: tryFetchConfigFromServer()"));
#endif
    if (!g_ctx->otaManager.isOtaExclusive()) {
    int r = g_ctx->webClient.tryFetchConfigFromServer(tmp);
    bootServerReachable = (r >= 1);
    // r==1: HTTP OK — fetchRemoteState remplit s_lastFetchedJson, pas tmp ; copier avant apply
    // r==2: NVS fallback (GET HTTP indisponible)
    if (r == 1 && g_ctx->webClient.copyLastFetchedTo(tmp)) {
      g_ctx->automatism.processFetchedRemoteConfig(tmp);
      if (tmp.size() > 0) {
        g_ctx->automatism.applyRemoteGpioConfig(tmp);
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[netTask] SOURCE SERVEUR RAM boot\n");
#else
        Serial.println(F("[netTask] ✅ SOURCE: SERVEUR (config BDD appliquée RAM au boot)"));
#endif
      }
    } else if (r == 2) {
      char cachedJson[BufferConfig::REMOTE_JSON_CACHE_SIZE];
      if (g_ctx->config.loadRemoteVars(cachedJson, sizeof(cachedJson)) && cachedJson[0] != '\0') {
        tmp.clear();
        if (!deserializeJson(tmp, cachedJson) && tmp.size() > 0) {
          g_ctx->automatism.applyRemoteGpioConfig(tmp);
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
          ets_printf("[netTask] SOURCE NVS RAM boot\n");
#else
          Serial.println(F("[netTask] ✅ SOURCE: NVS (config appliquée RAM au boot)"));
#endif
        }
      } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[netTask] NVS empty boot\n");
#else
        Serial.println(F("[netTask] ⚠️ GET NVS fallback — cache remote_vars vide"));
#endif
      }
    } else if (r >= 1) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[netTask] SOURCE SERVEUR ok\n");
#else
      Serial.println(F("[netTask] ✅ SOURCE: SERVEUR (config distante récupérée)"));
#endif
    } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[netTask] Serveur injoignable NVS/DEFAUT\n");
#else
      Serial.println(F("[netTask] ⚠️ Serveur injoignable - fallback NVS/DEFAUT"));
#endif
    }
    } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[netTask] Boot fetch skip OTA exclusif\n");
#else
      Serial.println(F("[netTask] Boot: fetchRemoteState ignoré (OTA exclusif)"));
#endif
    }
  } else if (!g_ctx) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[netTask] Boot g_ctx NULL skip\n");
#else
    Serial.println(F("[netTask] Boot: g_ctx NULL, skip fetch"));
#endif
  } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[netTask] Boot WiFi not connected skip\n");
#else
    Serial.println(F("[netTask] Boot: WiFi non connecté, fetchRemoteState skip"));
#endif
  }

  for (;;) {
    esp_task_wdt_reset();  // Reset watchdog dans boucle principale

    if (g_ctx && g_ctx->otaManager.isOtaExclusive()) {
      NetRequest* drainReq = nullptr;
      while (xQueueReceive(g_netQueue, &drainReq, 0) == pdTRUE) {
        if (!drainReq) continue;
        drainReq->cancelled = true;
        netNotifyDone(drainReq);
        netRequestFree(drainReq);
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    NetRequest* req = nullptr;
    const uint32_t queueTimeoutMs = 500;
    if (xQueueReceive(g_netQueue, &req, pdMS_TO_TICKS(queueTimeoutMs)) != pdTRUE) {
      continue;
    }
    if (!req) continue;
    if (!g_ctx) {
      netRequestFree(req);
      continue;
    }

    // Opérations longues (HTTP POST, GET OTA metadata, TLS) : feed WDT dans sous-appels (webClient, ota_manager)
    esp_task_wdt_reset();  // Reset WDT pendant traitement requête (évite reset si HTTP long)

#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[netTask] job: %s\n", netRequestTypeLabel(req->type));
#else
    if (LogConfig::SERIAL_ENABLED) {
      Serial.printf("[netTask] Requête: %s\n", netRequestTypeLabel(req->type));
    }
#endif

    // v11.169: Vérifier si le caller a déjà abandonné (timeout atteint)
    if (req->cancelled) {
      netNotifyDone(req);  // Notifier même si annulé
      netRequestFree(req);  // Pool : rendre le slot
      continue;
    }

    bool ok = false;
    req->fromNVSFallback = false;
    switch (req->type) {
      case NetReqType::FetchRemoteState: {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[netTask] fetchRemoteState start\n");
#else
        if (LogConfig::SERIAL_ENABLED) {
          Serial.println(F("[netTask] Début fetchRemoteState (GET outputs/state ou cache NVS)"));
        }
#endif
        int r = (req->doc != nullptr) ? g_ctx->webClient.tryFetchConfigFromServer(*req->doc) : 0;
        ok = (r >= 1);
        req->fromNVSFallback = (r == 2);
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[netTask] fetchRemoteState done r=%d (1=HTTP,2=NVS)\n", r);
#else
        if (LogConfig::SERIAL_ENABLED) {
          Serial.printf("[netTask] Fin fetchRemoteState: r=%d (%s)\n", r,
                        r == 1 ? "JSON serveur"
                               : r == 2 ? "fallback cache NVS" : "échec");
        }
#endif
        esp_task_wdt_reset();
        break;
      }
      case NetReqType::PostRaw: {
        PostSenderMsg msg;
        msg.type = PostSenderType::PostData;
        strncpy(msg.payload, req->payload, sizeof(msg.payload) - 1);
        msg.payload[sizeof(msg.payload) - 1] = '\0';
        msg.sdSeqNum = req->sdSeqNum;
        msg.hasSdQueueEntry = req->hasSdQueueEntry;
        if (xQueueSend(g_postSenderQueue, &msg, 0) != pdTRUE) {
          g_ctx->webClient.queueFailedPost(req->payload);
        }
        netRequestFree(req);
        continue;
      }
      case NetReqType::Heartbeat:
        netRequestFree(req);  // Heartbeat envoyé via postSenderQueue par netSendHeartbeat
        continue;
#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
      case NetReqType::OtaCheck: {
        Serial.println(F("[netTask] Demande distante OTA → tâche OTA dédiée"));
        uint8_t t = 1;
        if (g_otaTriggerQueue) {
          xQueueSend(g_otaTriggerQueue, &t, 0);
        }
        netRequestFree(req);
        continue;
      }
#endif
      default:
        ok = false;
        break;
    }

    // v11.169: Re-vérifier avant d'écrire success (le caller peut avoir timeout pendant le traitement)
    if (!req->cancelled) {
      req->success = ok;
    }
    netNotifyDone(req);
    // Pool : si caller a timeout, free déjà fait au début du tour ; sinon caller netRequestFree
    if (req->cancelled) {
      netRequestFree(req);
    }
  }
}


void automationTask(void* pv) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[autoTask] started\n");
  vTaskDelay(pdMS_TO_TICKS(100));  // Priorité 3 > loop 1: laisser tâche principale finir boot
#endif
  SensorReadings readings;
  // v11.160: Compteurs persistants en statique pour réduire légèrement l'empreinte stack
  static unsigned long lastHeartbeat = 0;
  static unsigned long lastBouffeDisplay = 0;
  static unsigned long lastPumpStatsDisplay = 0;
  static uint32_t s_sensorTimeoutCount = 0;
  static SensorReadings s_lastReadings;
  static bool s_haveReadings = false;

  #if defined(PROFILE_TEST) && FEATURE_DIAG_STACK_LOGS
  unsigned long lastStackCheck = 0;
  const unsigned long stackCheckInterval = 30000; // Toutes les 30 secondes
  #endif

  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }

#if FEATURE_MAIL
  // Réserve 32KB déjà allouée au boot si possible (reserveMailBlockAtBoot) ; sinon tentative au 1er passage
  allocMailReserveIfNeeded(HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS);
#endif
  for (;;) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));  // Yield Core 1 chaque itération (évite INT_WDT si enchaîne long)

    // v11.165: Protection NULL pointer (audit robustesse)
    if (!g_ctx) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // v11.192: Draine la sauvegarde NVS différée (évite assert xTaskPriorityDisinherit depuis net task)
    g_ctx->automatism.processDeferredRemoteVarsSave();

    if (xQueueReceive(g_sensorQueue, &readings, pdMS_TO_TICKS(TimingConfig::AUTOMATION_QUEUE_RECEIVE_TIMEOUT_MS)) == pdTRUE) {
      // Drainer les lectures en attente et ne traiter que la plus récente (évite queue pleine)
      while (xQueueReceive(g_sensorQueue, &readings, 0) == pdTRUE) { }
      esp_task_wdt_reset();
      s_lastReadings = readings;
      s_haveReadings = true;
      g_ctx->sensors.setLastCachedReadings(readings);  // Pour handlers web (évite _sensors.read() bloquant)

      #if FEATURE_DIAG_STACK_LOGS
      // Vérification périodique de la stack (wroom-test uniquement)
      unsigned long now = millis();
      if (now - lastStackCheck > stackCheckInterval) {
        UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
        uint32_t stackUsed = TaskConfig::AUTOMATION_TASK_STACK_SIZE - stackHighWaterMark;
        float stackPercent = (1.0 - (float)stackHighWaterMark / TaskConfig::AUTOMATION_TASK_STACK_SIZE) * 100.0;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[autoTask] Stack HWM: %u free (%.1f%% used)\n",
                   (unsigned)stackHighWaterMark, (double)stackPercent);
        if (stackPercent > 85.0) {
          ets_printf("[autoTask] CRITICAL Stack > 85%%\n");
        } else if (stackPercent > 70.0) {
          ets_printf("[autoTask] WARN Stack > 70%%\n");
        }
#else
        Serial.printf("[autoTask] Stack HWM: %u bytes libres (sur %u bytes configurés)\n", 
                      stackHighWaterMark, 
                      TaskConfig::AUTOMATION_TASK_STACK_SIZE);
        Serial.printf("[autoTask] Stack utilisée: %u bytes (%.1f%%)\n",
                      stackUsed,
                      stackPercent);
        if (stackPercent > 85.0) {
          Serial.printf("[autoTask] ⚠️ CRITIQUE: Stack utilisation > 85%% (%u bytes libres)\n", 
                        stackHighWaterMark);
          Serial.println("[Event] CRITICAL: Stack usage > 85% in automationTask");
        } else if (stackPercent > 70.0) {
          Serial.printf("[autoTask] ⚠️ ATTENTION: Stack utilisation > 70%% (%u bytes libres)\n", 
                        stackHighWaterMark);
        }
#endif
        lastStackCheck = now;
      }
      #else
      unsigned long now = millis();
      #endif
      
      #if FEATURE_DIAG_STATS
      // Monitoring unifié de la mémoire (toutes les 10-15 minutes, activable par flag)
      static unsigned long lastHeapCheck = 0;
      const unsigned long heapCheckInterval = 600000; // 10 minutes
      if (now - lastHeapCheck > heapCheckInterval) {
        uint32_t heapFree = ESP.getFreeHeap();
        uint32_t heapMin = ESP.getMinFreeHeap();
        uint32_t heapTotal = ESP.getHeapSize();
        uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        lastHeapCheck = now;
        
        // Vérification critique basée sur TLS_MIN_HEAP_BYTES
        uint32_t fragmentation = (heapFree > 0) ? ((heapFree - largestBlock) * 100 / heapFree) : 0;
        UBaseType_t hwmS = g_sensorTaskHandle ? uxTaskGetStackHighWaterMark(g_sensorTaskHandle) : 0;
        UBaseType_t hwmW = g_webTaskHandle ? uxTaskGetStackHighWaterMark(g_webTaskHandle) : 0;
        UBaseType_t hwmA = g_autoTaskHandle ? uxTaskGetStackHighWaterMark(g_autoTaskHandle) : 0;
        UBaseType_t hwmN = g_netTaskHandle ? uxTaskGetStackHighWaterMark(g_netTaskHandle) : 0;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        static bool s_minHeapBelowTlsLogged = false;
        if (heapFree < TLS_MIN_HEAP_BYTES) {
          ets_printf("[autoTask] CRIT Heap free=%u\n", (unsigned)heapFree);
        } else if (heapMin < TLS_MIN_HEAP_BYTES && !s_minHeapBelowTlsLogged) {
          ets_printf("[autoTask] WARN Min heap historique sous %u KB (TLS): %u\n",
                     (unsigned)(TLS_MIN_HEAP_BYTES / 1024), (unsigned)heapMin);
          s_minHeapBelowTlsLogged = true;
        }
        if (largestBlock < HeapConfig::MIN_HEAP_BLOCK_FOR_ASYNC_TASK) {
          ets_printf("[autoTask] WARN Frag blk=%u\n", (unsigned)largestBlock);
        }
        ets_printf("[MEM] free=%u min=%u blk=%u frag=%u hwm_s=%u w=%u a=%u n=%u\n",
                  (unsigned)heapFree, (unsigned)heapMin, (unsigned)largestBlock, (unsigned)fragmentation,
                  (unsigned)hwmS, (unsigned)hwmW, (unsigned)hwmA, (unsigned)hwmN);
#else
        static bool s_minHeapBelowTlsLogged = false;
        if (heapFree < TLS_MIN_HEAP_BYTES) {
          Serial.printf("[autoTask] CRIT: Heap free=%u (<%uKB TLS)\n",
                        heapFree, TLS_MIN_HEAP_BYTES / 1024);
          Serial.println("[Event] CRITICAL: Low heap - TLS may fail");
        } else if (heapMin < TLS_MIN_HEAP_BYTES && !s_minHeapBelowTlsLogged) {
          Serial.printf("[autoTask] WARN: Min heap historique sous %u KB (TLS): %u\n",
                        TLS_MIN_HEAP_BYTES / 1024, heapMin);
          s_minHeapBelowTlsLogged = true;
        }
        if (largestBlock < HeapConfig::MIN_HEAP_BLOCK_FOR_ASYNC_TASK) {
          Serial.printf("[autoTask] WARN: Fragmentation (blk=%u < 12KB)\n", (unsigned)largestBlock);
        }
        Serial.printf("[MEM] free=%u min=%u total=%u blk=%u frag=%u hwm_s=%u hwm_w=%u hwm_a=%u hwm_n=%u\n",
                      (unsigned)heapFree, (unsigned)heapMin, (unsigned)heapTotal,
                      (unsigned)largestBlock, (unsigned)fragmentation,
                      (unsigned)hwmS, (unsigned)hwmW, (unsigned)hwmA, (unsigned)hwmN);
        #if defined(PROFILE_TEST)
        Serial.printf("[autoTask] Heap: %u/%u blk=%u frag=%u%%\n",
                      heapFree, heapMin, largestBlock, fragmentation);
        #endif
#endif
      }
      #endif

      // Piste F: log périodique heap (free + plus grand bloc) toutes les 90 s pour analyse fragmentation
      static unsigned long lastHeapDiagMs = 0;
      if (now - lastHeapDiagMs >= 90000) {
        lastHeapDiagMs = now;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[HeapDiag] free=%u blk=%u\n",
                   (unsigned)ESP.getFreeHeap(),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
#else
        Serial.printf("[HeapDiag] free=%u blk=%u\n",
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
#endif
      }

#if FEATURE_DIAG_OLED_LOGS
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("{\"h\":\"H3\",\"branch\":\"data\",\"t\":%lu}\n", (unsigned long)now);
#else
      Serial.printf("{\"h\":\"H3\",\"branch\":\"data\",\"t\":%lu}\n", (unsigned long)now);
#endif
#endif
      g_ctx->automatism.update(readings);
      g_ctx->automatism.updateDisplayWithReadings(readings);
      g_ctx->power.resetWatchdog();
      g_ctx->diagnostics.update();
      if (!g_ctx->otaManager.isUpdating()) {
        // Plan simplification: push réseau toutes les 5 min (au lieu de 30s)
        static const unsigned long HEARTBEAT_INTERVAL_MS = 300000;  // 5 min
        if (now - lastHeartbeat > HEARTBEAT_INTERVAL_MS) {
          esp_task_wdt_reset();
          AppTasks::netSendHeartbeat(g_ctx->diagnostics, 10000);
          lastHeartbeat = now;
          
          // v11.171: Traiter la queue de POSTs échoués après le heartbeat
          if (WiFi.status() == WL_CONNECTED && g_ctx->webClient.getQueuedPostsCount() > 0) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            ets_printf("[autoTask] Queue POSTs: %u\n", (unsigned)g_ctx->webClient.getQueuedPostsCount());
#else
            Serial.printf("[autoTask] Queue POSTs: %u en attente\n", g_ctx->webClient.getQueuedPostsCount());
#endif
            esp_task_wdt_reset();
            g_ctx->webClient.processQueuedPosts();
          }
#if defined(BOARD_S3)
          if (WiFi.status() == WL_CONNECTED && SdLogger::pendingCount() > 0) {
            esp_task_wdt_reset();
            uint16_t replayed = SdLogger::replayPending(5);
            if (replayed > 0) {
              Serial.printf("[autoTask] SD replay: %u POSTs envoyés\n", replayed);
            }
          }
#endif
        }
        
        // Priorité 2: Mails en attente (traitement séquentiel - v11.155)
#if FEATURE_MAIL
        processMailQueueIfReady(g_ctx, now);
        vTaskDelay(pdMS_TO_TICKS(1));  // Yield Core 1 après envoi mail (évite INT_WDT)
#endif
      }

      logBouffeAndPumpStats(g_ctx, now, &lastBouffeDisplay, &lastPumpStatsDisplay);

      esp_task_wdt_reset();
      s_sensorTimeoutCount = 0;  // Réinitialiser quand on reçoit des capteurs
    } else {
#if FEATURE_DIAG_OLED_LOGS
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("{\"h\":\"H3\",\"branch\":\"timeout\",\"t\":%lu}\n", (unsigned long)millis());
#else
      Serial.printf("{\"h\":\"H3\",\"branch\":\"timeout\",\"t\":%lu}\n", (unsigned long)millis());
#endif
#endif
      // Rafraîchir l'OLED avec la dernière lecture (heure RTC, barre d'état) sans nouvelle donnée capteur
      if (s_haveReadings) {
        g_ctx->automatism.updateDisplayWithReadings(s_lastReadings);
        g_ctx->power.resetWatchdog();
        // H1: exécuter sync (poll + POST si intervalle) avec dernières lectures pour ne pas couper la publication
        g_ctx->automatism.update(s_lastReadings);
      }
      // Log au plus tous les 10 timeouts pour limiter le spam (intervalle capteurs 2,5 s)
      if ((++s_sensorTimeoutCount % 10) == 1) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[Auto] Timeout queue x%u\n", (unsigned)s_sensorTimeoutCount);
#else
        Serial.printf("[Auto] Timeout queue capteurs (x%u) - cycle continu\n", s_sensorTimeoutCount);
#endif
      }
      // Throttle GET fallback : même intervalle que branche data (6s) pour ne pas saturer netTask
      if (WiFi.status() == WL_CONNECTED && !g_ctx->otaManager.isOtaExclusive()) {
        static unsigned long s_lastFallbackFetchMs = 0;
        unsigned long nowMs = millis();
        if (nowMs - s_lastFallbackFetchMs >= NetworkConfig::REMOTE_FETCH_FALLBACK_INTERVAL_MS) {
          s_lastFallbackFetchMs = nowMs;
          esp_task_wdt_reset();
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
          ets_printf("[Auto] Poll distant fallback\n");
#else
          Serial.println(F("[Auto] ▶️ Poll distant (fallback sans capteurs)"));
#endif
          // v11.160: Utilise un document JSON statique pour éviter un gros objet sur la stack
          g_remoteFallbackDoc.clear();
          if (g_ctx->automatism.fetchRemoteState(g_remoteFallbackDoc) &&
              g_remoteFallbackDoc.size() > 0) {
            Serial.printf("[Auto] Fetch distant fallback: OK, keys=%u\n",
                         static_cast<unsigned>(g_remoteFallbackDoc.size()));
            g_ctx->automatism.applyRemoteGpioConfig(g_remoteFallbackDoc);
          } else {
            Serial.println(F("[Auto] Fetch distant fallback: KO ou doc vide"));
          }
        }
      }
    }
  }
}

}  // namespace

namespace AppTasks {

bool start(AppContext& ctx) {
  g_ctx = &ctx;
  // v11.157: CORRECTION CRITIQUE - Créer les queues AVANT les tâches
  // Les tâches utilisent immédiatement les queues, elles doivent donc exister
  // Créer la queue capteurs (utilisée par sensorTask et automationTask)
  // 8 slots (données toutes les SENSOR_TASK_INTERVAL_MS, typ. 10s) — marge pendant blocages
  if (!g_sensorQueue) {
    g_sensorQueue = xQueueCreate(8, sizeof(SensorReadings));
    if (!g_sensorQueue) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[App] CRITICAL g_sensorQueue fail\n");
#else
      Serial.println(F("[App] ❌ CRITIQUE: Échec création g_sensorQueue"));
      Serial.println("[Event] CRITICAL: g_sensorQueue creation failure");
#endif
      return false;
    }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[App] queue sensor ok\n");
#else
    Serial.println(F("[App] ✅ Queue capteurs créée"));
#endif
  }

  // Créer la queue réseau (utilisée par netTask)
  // Taille alignée kNetRequestPoolSize (10 WROOM / 16 S3) pour absorber pics poll+POST.
  if (!g_netQueue) {
    g_netQueue = xQueueCreate(netRequestPoolSize(), sizeof(NetRequest*));
    if (!g_netQueue) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[App] CRITICAL g_netQueue fail\n");
#else
      Serial.println(F("[App] ❌ CRITIQUE: Échec création g_netQueue"));
      Serial.println("[Event] CRITICAL: g_netQueue creation failure");
#endif
      return false;
    }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[App] queue net ok\n");
#else
    Serial.println(F("[App] ✅ Queue réseau créée"));
#endif
  }

  // Queue fire-and-forget POST (postSenderTask)
  if (!g_postSenderQueue) {
    g_postSenderQueue = xQueueCreate(kPostSenderQueueLen, sizeof(PostSenderMsg));
    if (!g_postSenderQueue) {
      Serial.println(F("[App] ❌ CRITIQUE: Échec création g_postSenderQueue"));
      return false;
    }
    BaseType_t postSenderCreated = xTaskCreatePinnedToCore(
        postSenderTask,
        "postSender",
        TaskConfig::POST_SENDER_TASK_STACK_SIZE / sizeof(StackType_t),
        nullptr,
        TaskConfig::POST_SENDER_TASK_PRIORITY,
        &g_postSenderTaskHandle,  // v13.70 (audit): capturer handle pour task_monitor
        TaskConfig::POST_SENDER_TASK_CORE_ID);
    if (postSenderCreated != pdPASS) {
      Serial.println(F("[App] ❌ CRITIQUE: Échec création postSenderTask"));
      vQueueDelete(g_postSenderQueue);
      g_postSenderQueue = nullptr;
      return false;
    }
  }

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  // Tâche OTA dédiée (priorité > netTask) : créée avant netTask pour que l'OTA soit prioritaire au boot
  if (!g_otaTriggerQueue) {
    g_otaTriggerQueue = xQueueCreate(kOtaTriggerQueueLen, sizeof(uint8_t));
    if (g_otaTriggerQueue) {
      g_otaTaskHandle = xTaskCreateStaticPinnedToCore(
          otaTask,
          "otaTask",
          TaskConfig::OTA_TASK_STACK_SIZE / sizeof(StackType_t),
          nullptr,
          TaskConfig::OTA_TASK_PRIORITY,
          otaTaskStack,
          &otaTaskTCB,
          TaskConfig::OTA_TASK_CORE_ID);
      if (g_otaTaskHandle == nullptr) {
        vQueueDelete(g_otaTriggerQueue);
        g_otaTriggerQueue = nullptr;
      }
    }
  }
#endif

  // v11.157: Approche hybride - allocation statique pour petites tâches, dynamique pour grandes
  // sensorTask: allocation statique (displayTask supprimée, affichage dans automationTask)
  g_sensorTaskHandle = xTaskCreateStaticPinnedToCore(
                                                     sensorTask,
                                                     "sensorTask",
                                                     TaskConfig::SENSOR_TASK_STACK_SIZE,
                                                     nullptr,
                                                     TaskConfig::SENSOR_TASK_PRIORITY,
                                                     sensorTaskStack,
                                                     &sensorTaskTCB,
                                                     TaskConfig::SENSOR_TASK_CORE_ID);
  BaseType_t sensorCreated = (g_sensorTaskHandle != nullptr) ? pdPASS : pdFAIL;

  // v11.169: webTask en dynamique (stack 10KB sur heap) – statique dépasserait limite BSS 160 KB
  BaseType_t webCreated = xTaskCreatePinnedToCore(
                                                     webTask,
                                                     "webTask",
                                                     TaskConfig::WEB_TASK_STACK_SIZE / sizeof(StackType_t),
                                                     nullptr,
                                                     TaskConfig::WEB_TASK_PRIORITY,
                                                     &g_webTaskHandle,
                                                     TaskConfig::WEB_TASK_CORE_ID);
  webCreated = (webCreated == pdPASS && g_webTaskHandle != nullptr) ? pdPASS : pdFAIL;

  // S3 PSRAM: mêmes priorités que les autres envs (loop cède avec vTaskDelay(1))
  const UBaseType_t autoPrio = TaskConfig::AUTOMATION_TASK_PRIORITY;
  g_autoTaskHandle = xTaskCreateStaticPinnedToCore(
                                                     automationTask,
                                                     "autoTask",
                                                     TaskConfig::AUTOMATION_TASK_STACK_SIZE,
                                                     nullptr,
                                                     autoPrio,
                                                     automationTaskStack,
                                                     &automationTaskTCB,
                                                     TaskConfig::AUTOMATION_TASK_CORE_ID);
  BaseType_t autoCreated = (g_autoTaskHandle != nullptr) ? pdPASS : pdFAIL;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  vTaskDelay(pdMS_TO_TICKS(50));  // Laisser autoTask démarrer
#endif

  // displayTask supprimée : affichage séquentiel dans automationTask via updateDisplayWithReadings(readings)
  g_displayTaskHandle = nullptr;

  BaseType_t netCreated = pdFAIL;
  // Note: netTask créé après création de g_netQueue (voir plus bas)
  
  if (sensorCreated != pdPASS || webCreated != pdPASS || autoCreated != pdPASS) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[App] CRITICAL task creation fail\n");
#else
    Serial.println(F("[App] ❌ CRITIQUE: Échec création d'une tâche FreeRTOS"));
    Serial.println("[Event] CRITICAL: Task creation failure");
#endif
  }

  // Créer netTask APRÈS création de g_netQueue (nécessaire pour la queue)
  // v11.159: netTask: allocation statique (Phase 2 - libère ~30KB heap)
  if (g_netQueue) {
    const UBaseType_t netPrio = TaskConfig::NET_TASK_PRIORITY;
    g_netTaskHandle = xTaskCreateStaticPinnedToCore(
                                                     netTask,
                                                     "netTask",
                                                     TaskConfig::NET_TASK_STACK_SIZE,
                                                     nullptr,
                                                     netPrio,
                                                     netTaskStack,
                                                     &netTaskTCB,
                                                     TaskConfig::NET_TASK_CORE_ID);
    netCreated = (g_netTaskHandle != nullptr) ? pdPASS : pdFAIL;
    if (netCreated != pdPASS) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[App] CRITICAL netTask creation fail\n");
#else
      Serial.println(F("[App] ❌ CRITIQUE: Échec création netTask (TLS)"));
      Serial.println("[Event] CRITICAL: netTask creation failure");
#endif
    }
  }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  // Pas de log ici pour éviter collision UART avec setup done (main task)
  // S3 PSRAM: pas de snapshot HWM ici pour que le setup termine et affiche "setup done"
  return sensorCreated == pdPASS && webCreated == pdPASS &&
         autoCreated == pdPASS && netCreated == pdPASS;
#endif

  // Snapshot après création des tâches principales (piste 5: HWM loop = loopTask; piste 7: heap au boot)

  vTaskDelay(pdMS_TO_TICKS(50));

  UBaseType_t hwmSensor = g_sensorTaskHandle ? uxTaskGetStackHighWaterMark(g_sensorTaskHandle) : 0;
  UBaseType_t hwmWeb = g_webTaskHandle ? uxTaskGetStackHighWaterMark(g_webTaskHandle) : 0;
  UBaseType_t hwmAuto = g_autoTaskHandle ? uxTaskGetStackHighWaterMark(g_autoTaskHandle) : 0;
  UBaseType_t hwmNet = g_netTaskHandle ? uxTaskGetStackHighWaterMark(g_netTaskHandle) : 0;
  UBaseType_t hwmLoop = uxTaskGetStackHighWaterMark(nullptr);

#if !defined(BOARD_S3) || !defined(BOARD_HAS_PSRAM)
  Serial.printf("[Stacks] HWM at boot - sensor:%u web:%u auto:%u net:%u loop:%u bytes\n",
                static_cast<unsigned>(hwmSensor),
                static_cast<unsigned>(hwmWeb),
                static_cast<unsigned>(hwmAuto),
                static_cast<unsigned>(hwmNet),
                static_cast<unsigned>(hwmLoop));
  Serial.printf("[Event] Stacks HWM boot sensor=%u web=%u auto=%u net=%u loop=%u\n",
                 static_cast<unsigned>(hwmSensor),
                 static_cast<unsigned>(hwmWeb),
                 static_cast<unsigned>(hwmAuto),
                 static_cast<unsigned>(hwmNet),
                 static_cast<unsigned>(hwmLoop));
  uint32_t heapFree = ESP.getFreeHeap();
  uint32_t heapTotal = ESP.getHeapSize();
  Serial.printf("[Heap] Boot - free:%u total:%u largestBlock:%u bytes\n",
                (unsigned)heapFree,
                (unsigned)heapTotal,
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
#endif

  return sensorCreated == pdPASS && webCreated == pdPASS &&
         autoCreated == pdPASS && netCreated == pdPASS;
}

Handles getHandles() {
  Handles handles{};
  handles.sensor = g_sensorTaskHandle;
  handles.web = g_webTaskHandle;
  handles.automation = g_autoTaskHandle;
  handles.display = g_displayTaskHandle;       // déprécié v13.65+ (task supprimée)
  handles.net = g_netTaskHandle;
  handles.postSender = g_postSenderTaskHandle; // v13.70 (audit)
  handles.ota = g_otaTaskHandle;               // v13.70 (audit)
  return handles;
}

QueueHandle_t getSensorQueue() {
  return g_sensorQueue;
}

// Requête allouée dans le pool statique : éviter use-after-return quand le caller timeout
// avant la fin du traitement netTask.
// Caller alloue, envoie ; sur timeout caller met req->cancelled et retourne (netTask finalise).
// Sur réponse notifiée (succès ou échec métier/réseau), caller garde la propriété et libère le slot.
enum class NetRpcResult : uint8_t {
  CompletedSuccess,
  CompletedFailure,
  Abandoned
};

static NetRpcResult netRpcAlloc(NetRequest* req, uint32_t timeoutMs) {
  if (!req) return NetRpcResult::Abandoned;
  if (!g_netQueue || !g_netTaskHandle) {
    netRequestFree(req);
    return NetRpcResult::Abandoned;
  }
  req->requester = xTaskGetCurrentTaskHandle();
  req->success = false;
  req->cancelled = false;

  (void)ulTaskNotifyTake(pdTRUE, 0);

  NetRequest* ptr = req;
  const uint32_t QUEUE_SEND_TIMEOUT_MS = 200;  // Laisser une place se libérer avant d'abandonner
  if (xQueueSend(g_netQueue, &ptr, pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS)) != pdTRUE) {
    Serial.println(F("[netRPC] Requête abandonnée: file net pleine"));
    netRequestFree(req);
    return NetRpcResult::Abandoned;
  }

  // Timeout : utiliser req->timeoutMs borné (aligné offline-first, max 30 s)
  const uint32_t MIN_TIMEOUT_MS = 1000;
  const uint32_t MAX_TIMEOUT_MS = 30000;
  uint32_t limitMs = (timeoutMs >= MIN_TIMEOUT_MS && timeoutMs <= MAX_TIMEOUT_MS)
                         ? timeoutMs
                         : 8000;
  uint32_t waitStart = millis();
  const uint32_t CHECK_INTERVAL_MS = 100;

  while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CHECK_INTERVAL_MS)) == 0) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();  // Caller (autoTask/webTask) attend réponse netTask
    }
    uint32_t elapsed = millis() - waitStart;
    if (elapsed > limitMs) {
      Serial.printf("[netRPC] Timeout (%u ms), abandon\n", (unsigned)limitMs);
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[NETDBG] hypothesis=H1 callerTimeout limitMs=%u\n", (unsigned)limitMs);
#else
      Serial.printf("[NETDBG] hypothesis=H1 callerTimeout limitMs=%u\n", (unsigned)limitMs);
#endif
      req->cancelled = true;
      // Si la notification de fin arrive juste après le timeout caller, libérer ici le slot.
      // netTask peut aussi exécuter netRequestFree(req) en parallèle ; double free inoffensif
      // (pool statique, slot remis à false).
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) > 0) {
        netRequestFree(req);
      }
      return NetRpcResult::Abandoned;  // timeout/abandon : slot libéré par netTask ou sur notif tardive ci-dessus
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return req->success ? NetRpcResult::CompletedSuccess : NetRpcResult::CompletedFailure;
}

bool netFetchRemoteState(ArduinoJson::JsonDocument& doc, uint32_t timeoutMs, bool* outFromNVSFallback) {
  if (g_ctx && g_ctx->otaManager.isOtaExclusive()) return false;
  NetRequest* req = netRequestAllocForFetch();
  if (!req) return false;
  req->type = NetReqType::FetchRemoteState;
  req->timeoutMs = timeoutMs;
  req->doc = &doc;
  req->diag = nullptr;
  req->payload[0] = '\0';
  NetRpcResult rpcResult = netRpcAlloc(req, timeoutMs);
  if (rpcResult == NetRpcResult::CompletedSuccess) {
    if (outFromNVSFallback) *outFromNVSFallback = req->fromNVSFallback;
    netRequestFree(req);
    return true;
  }
  if (rpcResult == NetRpcResult::CompletedFailure) {
    netRequestFree(req);
  }
  return false;  // timeout/abandon : slot déjà géré ; échec notifié : slot libéré ci-dessus
}

bool netPostRaw(const char* payload, uint32_t timeoutMs, PostCategory category, NetPostFailureReason* outFailure, uint32_t sdSeqNum) {
  if (!payload) return false;
  if (g_ctx && g_ctx->otaManager.isOtaExclusive()) {
    if (outFailure) *outFailure = NetPostFailureReason::None;
    return false;
  }
  if (outFailure) *outFailure = NetPostFailureReason::None;
  if (!g_netQueue) return false;
  NetRequest* req = netRequestAllocForPostCategory(category);
  if (!req) {
    if (outFailure) *outFailure = NetPostFailureReason::PoolFull;
    return false;
  }
  req->type = NetReqType::PostRaw;
  req->timeoutMs = timeoutMs;
  req->requester = nullptr;  // fire-and-forget : pas de notification au caller
  req->cancelled = false;
  req->success = false;
  req->doc = nullptr;
  req->diag = nullptr;
  req->sdSeqNum = sdSeqNum;
  req->hasSdQueueEntry = (sdSeqNum != 0);
  strncpy(req->payload, payload, sizeof(req->payload) - 1);
  req->payload[sizeof(req->payload) - 1] = '\0';
  // Envoyer sans attendre — netTask libère le slot après traitement HTTP
  NetRequest* ptr = req;
  const uint32_t QUEUE_SEND_TIMEOUT_MS = 200;
  if (xQueueSend(g_netQueue, &ptr, pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS)) != pdTRUE) {
    Serial.println(F("[netRPC] POST abandonné: file net pleine"));
    netRequestFree(req);
    if (outFailure) *outFailure = NetPostFailureReason::PoolFull;
    return false;
  }
  return true;  // "mis en queue" — résultat HTTP traité en arrière-plan par netTask
}

// v13.70 (audit): compteur diagnostique pour les heartbeats perdus quand postSender est saturé.
// Permet de remonter une métrique observable côté serveur ou logs si la file POST est régulièrement
// pleine (signal d'investigation : POST périodiques trop fréquents ou serveur HS).
static uint32_t s_heartbeatDroppedCount = 0;

bool netSendHeartbeat(const Diagnostics& diag, uint32_t timeoutMs) {
  (void)timeoutMs;
  if (!g_ctx || !g_postSenderQueue) return false;
  if (g_ctx->otaManager.isOtaExclusive()) return false;
  PostSenderMsg msg;
  msg.type = PostSenderType::Heartbeat;
  char buf[320];
  if (!g_ctx->webClient.buildHeartbeatPayload(diag, buf, sizeof(buf))) return false;
  strncpy(msg.payload, buf, sizeof(msg.payload) - 1);
  msg.payload[sizeof(msg.payload) - 1] = '\0';
  if (xQueueSend(g_postSenderQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
    return true;
  }
  // v13.70 (audit): incrémenter le compteur global de pertes pour diagnostic.
  s_heartbeatDroppedCount++;
  static unsigned long s_lastHeartbeatDropLogMs = 0;
  unsigned long now = millis();
  if (LogConfig::SERIAL_ENABLED && (now - s_lastHeartbeatDropLogMs) >= 60000UL) {
    Serial.printf("[netRPC] Heartbeat non placé: file postSender pleine (perdus cumul=%u, retry au prochain cycle)\n",
                  (unsigned)s_heartbeatDroppedCount);
    s_lastHeartbeatDropLogMs = now;
  }
  return false;
}

uint32_t netHeartbeatDroppedCount() {
  return s_heartbeatDroppedCount;
}

void netRequestOtaCheck() {
#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  if (g_ctx && g_ctx->otaManager.isOtaExclusive()) return;
  if (g_otaTriggerQueue) {
    uint8_t t = 1;
    if (xQueueSend(g_otaTriggerQueue, &t, 0) != pdTRUE) {
      Serial.println(F("[OTA] Trigger renoncé: file tâche OTA pleine"));
    }
    return;
  }
  if (!g_netQueue) return;
  NetRequest* req = netRequestAlloc(true);  // Fallback si tâche OTA non créée
  if (!req) {
    Serial.println(F("[netRPC] OTA check renoncé: pool plein (slot OTA occupé)"));
    return;
  }
  req->type = NetReqType::OtaCheck;
  req->requester = nullptr;
  NetRequest* ptr = req;
  if (xQueueSend(g_netQueue, &ptr, pdMS_TO_TICKS(100)) != pdTRUE) {
    netRequestFree(req);
    Serial.println(F("[netRPC] OTA check renoncé: queue net pleine"));
  }
#endif
}


#if FEATURE_MAIL
void reserveMailBlockAtBoot() {
  allocMailReserveIfNeeded(HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS);
}
#endif

bool quiesceHttpBeforeLightSleep(uint32_t timeoutMs) {
  const unsigned long startMs = millis();
  while ((millis() - startMs) < timeoutMs) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    const UBaseType_t postWaiting =
        (g_postSenderQueue != nullptr) ? uxQueueMessagesWaiting(g_postSenderQueue) : 0;
    const UBaseType_t netWaiting =
        (g_netQueue != nullptr) ? uxQueueMessagesWaiting(g_netQueue) : 0;
    if (postWaiting == 0 && netWaiting == 0) {
      const unsigned long elapsed = millis() - startMs;
      const uint32_t remain = (elapsed < timeoutMs) ? static_cast<uint32_t>(timeoutMs - elapsed) : 1U;
      if (WebClient::acquireHttpTransportLock(remain)) {
        Serial.println(F("[Auto] Quiesce HTTP: files vides, mutex transport acquis avant veille"));
        return true;
      }
      Serial.println(F("[Auto] Quiesce HTTP: échec acquisition mutex (transport occupé)"));
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  Serial.println(F("[Auto] Quiesce HTTP: timeout attente files — tentative mutex courte"));
  if (WebClient::acquireHttpTransportLock(500)) {
    return true;
  }
  return false;
}

void releaseHttpAfterLightSleep() {
  WebClient::releaseHttpTransportLockIfHeld();
}

}  // namespace AppTasks


