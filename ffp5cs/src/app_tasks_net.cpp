// app_tasks_net.cpp — Tâche FreeRTOS netTask : unique propriétaire de
// WebClient/TLS, traite la file de requêtes réseau (RPC pool NetRequest :
// FetchRemoteState, PostRaw, Heartbeat, OtaCheck) et notifie les tâches
// appelantes. Extrait d'app_tasks.cpp (refonte god-file, audit v13.93) ;
// déplacement verbatim. État partagé via app_tasks_internal.h ; pool via
// net_request_pool.h ; mail via task_mail.h.
#include "app_tasks_internal.h"  // g_ctx, g_netQueue, handles, queues, netTask
#include "net_request_pool.h"    // NetRequest, netRequestFree, netRequestTypeLabel
#include "task_mail.h"           // processMailQueueIfReady
#include "app_context.h"
#include "config.h"
#include "web_client.h"          // postToUrl, buildHeartbeatPayload
#include "tls_mutex.h"
#include "diagnostics.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

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

void netTask(void* pv) {
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

  // v14.00 : le fetch config boot est terminé → débloque le POST initial (postConfiguration).
  AppTasks::markBootConfigFetchDone();

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

