// app_tasks_automation.cpp — Tâche FreeRTOS automationTask : orchestrateur de
// contrôle (consomme la file capteurs, applique l'automatisme, affichage,
// poll config distante, logs périodiques). Extrait d'app_tasks.cpp (refonte
// god-file, audit v13.93) ; déplacement verbatim. État partagé via
// app_tasks_internal.h ; helpers propres à cette tâche.
#include "app_tasks_internal.h"  // g_ctx, g_sensorQueue, g_netTaskHandle, automationTask
#include "app_tasks.h"           // AppTasks:: API
#include "task_mail.h"           // allocMailReserveIfNeeded, processMailQueueIfReady
#include "tls_mutex.h"           // TLS_MIN_HEAP_BYTES
#include "app_context.h"
#include "config.h"
#include "automatism.h"
#include "system_sensors.h"      // SensorReadings (file capteurs)
#include "sd_logger.h"           // SdLogger:: (replay file SD, branche BOARD_S3)
#include <Arduino.h>
#include <ArduinoJson.h>         // StaticJsonDocument (documents outputs/state sur heap)
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <memory>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

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
          // Document dédié GET outputs/state sur heap : payload v14.24 > 1024 o sur WROOM.
          std::unique_ptr<StaticJsonDocument<BufferConfig::OUTPUTS_STATE_JSON_DOCUMENT_SIZE>> remoteDoc(
              new (std::nothrow) StaticJsonDocument<BufferConfig::OUTPUTS_STATE_JSON_DOCUMENT_SIZE>());
          if (!remoteDoc) {
            Serial.println(F("[Auto] Fetch distant fallback: heap insuffisante pour JSON"));
            continue;
          }
          if (g_ctx->automatism.fetchRemoteState(*remoteDoc) &&
              remoteDoc->size() > 0) {
            Serial.printf("[Auto] Fetch distant fallback: OK, keys=%u\n",
                         static_cast<unsigned>(remoteDoc->size()));
            g_ctx->automatism.applyRemoteGpioConfig(*remoteDoc);
          } else {
            Serial.println(F("[Auto] Fetch distant fallback: KO ou doc vide"));
          }
        }
      }
    }
  }
}
