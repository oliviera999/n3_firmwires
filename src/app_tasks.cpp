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
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

namespace {

AppContext* g_ctx = nullptr;
QueueHandle_t g_sensorQueue = nullptr;

// v11.157: Buffers statiques pour stacks (allocation statique pour réduire fragmentation)
// webTask reste en dynamique (heap) : passer en statique dépasserait la limite BSS 160 KB au link.
static StackType_t sensorTaskStack[TaskConfig::SENSOR_TASK_STACK_SIZE];
static StaticTask_t sensorTaskTCB;

static StackType_t automationTaskStack[TaskConfig::AUTOMATION_TASK_STACK_SIZE];
static StaticTask_t automationTaskTCB;

static StackType_t netTaskStack[TaskConfig::NET_TASK_STACK_SIZE];
static StaticTask_t netTaskTCB;
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
enum class NetReqType : uint8_t {
  FetchRemoteState = 1,
  PostRaw = 2,
  Heartbeat = 3,
  OtaCheck = 4,  // Vérification OTA (boot, périodique 2h, ou déclenchement serveur distant)
};

struct NetRequest {
  NetReqType type;
  TaskHandle_t requester;
  uint32_t timeoutMs;
  bool success;
  volatile bool cancelled;  // Flag d'abandon (evite use-after-free si caller timeout)
  bool fromNVSFallback;    // v11.193: true si FetchRemoteState a utilisé le cache NVS (ne pas itérer sur doc)

  ArduinoJson::JsonDocument* doc;   // FetchRemoteState
  const Diagnostics* diag;          // Heartbeat
  char payload[1024];               // PostRaw (copie)
};

QueueHandle_t g_netQueue = nullptr;
TaskHandle_t g_netTaskHandle = nullptr;

// Pool statique NetRequest : évite malloc/free à chaque requête réseau → moins de fragmentation (piste E).
// Pas d'allocation heap par requête ; payload et doc sont dans le pool ou passés par référence.
// Taille 5 (alignée g_netQueue) pour réduire abandons quand replayQueuedData + poll remplissent la file.
static constexpr size_t kNetRequestPoolSize = 5;
static NetRequest s_netRequestPool[kNetRequestPoolSize];
static bool s_netRequestPoolUsed[kNetRequestPoolSize] = {false};

static NetRequest* netRequestAlloc() {
  for (size_t i = 0; i < kNetRequestPoolSize; ++i) {
    if (!s_netRequestPoolUsed[i]) {
      s_netRequestPoolUsed[i] = true;
      memset(&s_netRequestPool[i], 0, sizeof(NetRequest));
      return &s_netRequestPool[i];
    }
  }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[netRPC] Pool plein, netRequestAlloc échoue\n");
#else
  Serial.println(F("[netRPC] Pool plein, netRequestAlloc échoue"));
#endif
  return nullptr;
}

static void netRequestFree(NetRequest* req) {
  if (!req) return;
  if (req >= s_netRequestPool && req < s_netRequestPool + kNetRequestPoolSize) {
    s_netRequestPoolUsed[req - s_netRequestPool] = false;
  } else {
    free(req);  // Sécurité si jamais un malloc legacy
  }
}

#if FEATURE_MAIL
// Réserve 32KB pour SMTP : sur S3 avec PSRAM, allouée en PSRAM pour ne pas fragmenter la RAM interne
// (TLS/SMTP a besoin d'un bloc contigu en RAM interne ; en gardant la réserve en PSRAM on laisse 32KB dispo en interne)
static uint8_t* s_mailReserve = nullptr;
static bool s_mailReserveFromPSRAM = false;

// Alloue la réserve : PSRAM en priorité sur S3 (libère la RAM interne pour TLS), sinon malloc interne.
static void allocMailReserveIfNeeded(uint32_t kMailBlockReq) {
  if (s_mailReserve) return;
#if defined(BOARD_S3) && defined(MALLOC_CAP_SPIRAM)
  size_t lbSpiral = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if (lbSpiral >= kMailBlockReq) {
    s_mailReserve = (uint8_t*)heap_caps_malloc(kMailBlockReq, MALLOC_CAP_SPIRAM);
    if (s_mailReserve) {
      s_mailReserveFromPSRAM = true;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] Réserve 32KB PSRAM SMTP (S3)\n");
#else
      Serial.println(F("[Mail] Réserve 32KB allouée en PSRAM pour SMTP (S3)"));
#endif
      return;
    }
  }
#endif
  uint32_t lbInternal = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  if (lbInternal >= kMailBlockReq) {
    s_mailReserve = (uint8_t*)malloc(kMailBlockReq);
    if (s_mailReserve) {
      s_mailReserveFromPSRAM = false;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] Réserve 32KB boot SMTP\n");
#else
      Serial.println(F("[Mail] Réserve 32KB allouée au boot pour SMTP"));
#endif
    }
  }
}

// Traitement séquentiel des mails en attente (extrait de automationTask pour lisibilité).
static void processMailQueueIfReady(AppContext* ctx, unsigned long now) {
  if (!ctx) return;
  const uint32_t kMailBlockReq = HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS;
  const uint32_t kMailFreeReq = HeapConfig::MIN_HEAP_MAIL_SEND;
  if (!ctx->mailer.hasPendingMails()) {
    if (s_mailReserve) {
      free(s_mailReserve);
      s_mailReserve = nullptr;
      s_mailReserveFromPSRAM = false;
    }
    return;
  }
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  bool canConn = TLSMutex::canConnect();
  uint32_t pending = ctx->mailer.getQueuedMails();

  if (!canConn) {
    static unsigned long lastTlsSkipMs = 0;
    if (now - lastTlsSkipMs > 30000) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] skip TLS pending=%u heap=%u\n", (unsigned)pending, (unsigned)freeHeap);
#else
      Serial.printf("[Mail] ⏸️ En attente: %u mail(s), skip TLS (heap=%u < 35K ou sleep)\n", pending, freeHeap);
#endif
      lastTlsSkipMs = now;
    }
    return;
  }
  // Garde heap libre (Core 1): éviter abort() si TLS/allocs internes échouent pendant processOneMailSync
  if (largestBlock >= kMailBlockReq && freeHeap >= kMailFreeReq) {
    allocMailReserveIfNeeded(kMailBlockReq);
    // Libérer la réserve AVANT l'envoi seulement si elle est en RAM interne, pour créer un bloc 32KB pour TLS.
    if (s_mailReserve && !s_mailReserveFromPSRAM) {
      free(s_mailReserve);
      s_mailReserve = nullptr;
    }
    esp_task_wdt_reset();
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[Mail] ENVOI heap=%u bloc=%u\n", (unsigned)freeHeap, (unsigned)largestBlock);
#else
    Serial.printf("[Mail] >>> ENVOI EFFECTIF (tentative) - heap free=%u bloc=%u <<<\n", freeHeap, largestBlock);
#endif
    ctx->mailer.processOneMailSync();
    return;
  }
  if (s_mailReserve && !s_mailReserveFromPSRAM) {
    free(s_mailReserve);
    s_mailReserve = nullptr;
    vTaskDelay(pdMS_TO_TICKS(100));
    largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    freeHeap = ESP.getFreeHeap();
    if (largestBlock >= kMailBlockReq && freeHeap >= kMailFreeReq) {
      esp_task_wdt_reset();
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] ENVOI apres liberation heap=%u\n", (unsigned)freeHeap);
#else
      Serial.printf("[Mail] >>> ENVOI EFFECTIF (tentative après libération réserve) - heap free=%u <<<\n", freeHeap);
#endif
      ctx->mailer.processOneMailSync();
    }
    return;
  }
  static unsigned long lastMemWarnMs = 0;
  if (now - lastMemWarnMs > 60000) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[Mail] skip heap pending=%u blk=%u\n", (unsigned)pending, (unsigned)largestBlock);
#else
    Serial.printf("[Mail] ⏸️ En attente: %u mail(s), skip heap (bloc=%u < %uK requis, free=%u)\n",
                  pending, largestBlock, kMailBlockReq / 1024, freeHeap);
#endif
    lastMemWarnMs = now;
  }
}
#endif

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

  // Remplacer les fetchRemoteState() du boot (qui se faisaient dans loopTask)
  // par une tentative depuis netTask dès que le WiFi est disponible.
  // v11.168: Timeout boot augmenté à 8s pour récupérer config serveur (source de vérité)
  // Ce timeout plus long au boot est acceptable car c'est le seul moment critique
  // pour synchroniser la config distante. Après le boot, on utilise le timeout standard.
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < TimingConfig::WIFI_BOOT_TIMEOUT_MS) {
    esp_task_wdt_reset();  // Reset watchdog pendant attente WiFi
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  bool bootServerReachable = false;
  if (WiFi.status() == WL_CONNECTED && g_ctx) {
    // Cause basale: attente stabilisation stack TCP/IP avant 1ère requête HTTPS
    // (évite "connection refused" / SSL fatal au boot, même logique que power.cpp après réveil)
    g_ctx->power.waitForNetworkReady();
    esp_task_wdt_reset();  // P1: reset avant GET boot (fetch peut bloquer jusqu'à OUTPUTS_STATE_HTTP_TIMEOUT_MS)
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[netTask] Boot tryFetchConfig\n");
#else
    Serial.println(F("[netTask] Boot: tryFetchConfigFromServer()"));
#endif
    int r = g_ctx->webClient.tryFetchConfigFromServer(tmp);
    bootServerReachable = (r >= 1);
    // r==1: HTTP OK, r==2: NVS fallback (ne pas appeler processFetchedRemoteConfig sur doc NVS)
    if (r == 1 && tmp.size() > 0) {
      if (g_ctx->automatism.processFetchedRemoteConfig(tmp)) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[netTask] SOURCE SERVEUR applied\n");
#else
        Serial.println(F("[netTask] ✅ SOURCE: SERVEUR (config distante récupérée et appliquée)"));
#endif
      } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
        ets_printf("[netTask] SOURCE SERVEUR partial\n");
#else
        Serial.println(F("[netTask] ✅ SOURCE: SERVEUR (config récupérée, application partielle)"));
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

  // Vérification OTA au boot puis toutes les 2h (prod / wroom-beta)
  // Ne faire l'OTA au boot que si le serveur a répondu (évite blocage TLS > 30s sur serveur injoignable)
  // wroom-test (PROFILE_TEST) uniquement: skip OTA au boot (downloadMetadata HTTPS peut bloquer netTask > TWDT). OTA manuel via /api/ota.
  static unsigned long lastOtaCheckMs = 0;
#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  #if !defined(PROFILE_TEST)
  if (g_ctx && WiFi.status() == WL_CONNECTED && bootServerReachable) {
    esp_task_wdt_reset();  // Fenêtre TWDT avant checkForUpdate (downloadMetadata peut bloquer jusqu'à HTTP_TIMEOUT)
    g_ctx->otaManager.setCurrentVersion(ProjectConfig::VERSION);
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[netTask] Boot OTA check\n");
#else
    Serial.println(F("[netTask] Boot: vérification OTA (serveur distant)"));
#endif
    if (g_ctx->otaManager.checkForUpdate()) {
      g_ctx->otaManager.performUpdate();  // Lance la tâche OTA, ne bloque pas longtemps
    }
    lastOtaCheckMs = millis();
  } else if (g_ctx && WiFi.status() == WL_CONNECTED && !bootServerReachable) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[netTask] Boot OTA skip\n");
#else
    Serial.println(F("[netTask] Boot: OTA skip (serveur injoignable, évite TWDT)"));
#endif
    lastOtaCheckMs = millis();  // Pour ne pas refaire OTA immédiatement en boucle
  }
  #else
  // wroom-test (PROFILE_TEST): OTA au boot désactivé (HTTPS peut bloquer netTask > TWDT). OTA manuel via /api/ota.
  lastOtaCheckMs = millis();
  #endif
#endif

  for (;;) {
    esp_task_wdt_reset();  // Reset watchdog dans boucle principale
    NetRequest* req = nullptr;
    const uint32_t queueTimeoutMs = 500;  // Court pour permettre vérif OTA périodique quand file vide
      if (xQueueReceive(g_netQueue, &req, pdMS_TO_TICKS(queueTimeoutMs)) != pdTRUE) {
#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0 && !defined(PROFILE_TEST)
      // Vérification OTA périodique toutes les 2h (prod / wroom-beta). wroom-test: désactivé (HTTPS > TWDT)
      if (g_ctx && WiFi.status() == WL_CONNECTED && !g_ctx->otaManager.isUpdating() &&
          (millis() - lastOtaCheckMs >= TimingConfig::OTA_CHECK_INTERVAL_MS)) {
        g_ctx->otaManager.setCurrentVersion(ProjectConfig::VERSION);
        if (g_ctx->otaManager.checkForUpdate()) {
          g_ctx->otaManager.performUpdate();
        }
        lastOtaCheckMs = millis();
      }
#endif
      continue;
    }
    if (!req || !g_ctx) continue;

    esp_task_wdt_reset();  // Reset WDT pendant traitement requête (évite reset si HTTP long)

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
        int r = (req->doc != nullptr) ? g_ctx->webClient.tryFetchConfigFromServer(*req->doc) : 0;
        ok = (r >= 1);
        req->fromNVSFallback = (r == 2);
        esp_task_wdt_reset();
        break;
      }
      case NetReqType::PostRaw:
        ok = g_ctx->webClient.tryPushStatusToServer(req->payload);
        esp_task_wdt_reset();
        break;
      case NetReqType::Heartbeat:
        ok = (req->diag != nullptr) ? g_ctx->webClient.sendHeartbeat(*req->diag) : false;
        esp_task_wdt_reset();
        break;
#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
      case NetReqType::OtaCheck: {
        Serial.println(F("[netTask] Demande distante recherche OTA (triggerOtaCheck)"));
        g_ctx->otaManager.setCurrentVersion(ProjectConfig::VERSION);
        ok = g_ctx->otaManager.checkForUpdate();
        if (ok) {
          g_ctx->otaManager.performUpdate();
        }
        esp_task_wdt_reset();
        netRequestFree(req);  // Fire-and-forget, rendre le slot
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

void sensorTask(void* pv) {
  SensorReadings readings;
  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }

  SENSOR_LOG_PRINTF("[Sensor] Tâche sensorTask démarrée - intervalle %u ms\n", TimingConfig::SENSOR_TASK_INTERVAL_MS);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1));  // Yield Core 1 avant lecture ~1s (évite INT_WDT début de run)
    // v11.165: Protection NULL pointer (audit robustesse)
    if (!g_ctx) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    
    if (g_ctx->otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(4000));
      continue;
    }

    esp_task_wdt_reset();

    uint32_t sensorStartTime = millis();
    const uint32_t MAX_SENSOR_TIME_MS = 30000;
    readings = g_ctx->sensors.read();

    uint32_t sensorDuration = millis() - sensorStartTime;
    if (sensorDuration > MAX_SENSOR_TIME_MS) {
      SENSOR_LOG_PRINTF(
        "[Sensor] ⚠️ LECTURE CAPTEURS TROP LENTE: %u ms (limite: %u ms)\n",
        sensorDuration,
        MAX_SENSOR_TIME_MS);
    }

    esp_task_wdt_reset();

    // v11.153: Validation INDÉPENDANTE par capteur
    // Ne plus réinitialiser TOUS les capteurs si un seul échoue
    // Les ultrasons restent valides même si le DHT (air) échoue
    
    // Vérification température eau
    if (isnan(readings.tempWater) ||
        readings.tempWater < SensorConfig::WaterTemp::MIN_VALID ||
        readings.tempWater > SensorConfig::WaterTemp::MAX_VALID) {
      SENSOR_LOG_PRINTLN(F("[Sensor] ⚠️ Température eau invalide, utilise défaut"));
      readings.tempWater = SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
    }
    
    // Vérification température air + humidité (DHT air)
    if (isnan(readings.tempAir) ||
        readings.tempAir < SensorConfig::AirSensor::TEMP_MIN ||
        readings.tempAir > SensorConfig::AirSensor::TEMP_MAX) {
      SENSOR_LOG_PRINTLN(F("[Sensor] ⚠️ Température air invalide, utilise défaut"));
      readings.tempAir = SensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
    }
    
    if (isnan(readings.humidity) ||
        readings.humidity < SensorConfig::AirSensor::HUMIDITY_MIN ||
        readings.humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
      SENSOR_LOG_PRINTLN(F("[Sensor] ⚠️ Humidité invalide, utilise défaut"));
      readings.humidity = SensorConfig::DefaultValues::HUMIDITY_DEFAULT;
    }
    
    // Les ultrasons ne sont PAS réinitialisés - leurs valeurs sont préservées
    // même si les capteurs de température/humidité échouent

    esp_task_wdt_reset();

    if (g_sensorQueue) {
      BaseType_t result = xQueueSendToBack(g_sensorQueue,
                                           &readings,
                                           pdMS_TO_TICKS(200));
      if (result != pdTRUE) {
        // v11.158: Simplification - queue pleine: retirer la plus ancienne et réessayer immédiatement
        SensorReadings oldReading;
        xQueueReceive(g_sensorQueue, &oldReading, 0);  // Ignore résultat (peut échouer si queue vide)
        xQueueSendToBack(g_sensorQueue, &readings, 0);  // Réessayer immédiatement (timeout 0)
        SENSOR_LOG_PRINTLN(F("[Sensor] ⚠️ Queue pleine - ancienne donnée écrasée"));
      }
    } else {
      SENSOR_LOG_PRINTLN(F("[Sensor] ❌ Queue non disponible - donnée ignorée"));
    }

    esp_task_wdt_reset();
    // Découper le délai en tranches avec reset WDT pour ne pas dépasser le timeout (5s typ.)
    const uint32_t intervalMs = TimingConfig::SENSOR_TASK_INTERVAL_MS;
    const uint32_t chunkMs = 1000;
    for (uint32_t remaining = intervalMs; remaining > 0; remaining -= (remaining > chunkMs ? chunkMs : remaining)) {
      vTaskDelay(pdMS_TO_TICKS(remaining > chunkMs ? chunkMs : remaining));
      esp_task_wdt_reset();
    }
  }
}

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
  const TickType_t period = pdMS_TO_TICKS(100);

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
        if (heapMin < TLS_MIN_HEAP_BYTES) {
          ets_printf("[autoTask] CRIT Heap min=%u\n", (unsigned)heapMin);
        } else if (heapFree < TLS_MIN_HEAP_BYTES) {
          ets_printf("[autoTask] WARN Heap=%u\n", (unsigned)heapFree);
        }
        if (largestBlock < HeapConfig::MIN_HEAP_BLOCK_FOR_ASYNC_TASK) {
          ets_printf("[autoTask] WARN Frag blk=%u\n", (unsigned)largestBlock);
        }
        ets_printf("[MEM] free=%u min=%u blk=%u frag=%u hwm_s=%u w=%u a=%u n=%u\n",
                  (unsigned)heapFree, (unsigned)heapMin, (unsigned)largestBlock, (unsigned)fragmentation,
                  (unsigned)hwmS, (unsigned)hwmW, (unsigned)hwmA, (unsigned)hwmN);
#else
        if (heapMin < TLS_MIN_HEAP_BYTES) {
          Serial.printf("[autoTask] CRIT: Heap min=%u (<%uKB TLS)\n", 
                        heapMin, TLS_MIN_HEAP_BYTES / 1024);
          Serial.println("[Event] CRITICAL: Low heap - TLS may fail");
        } else if (heapFree < TLS_MIN_HEAP_BYTES) {
          Serial.printf("[autoTask] WARN: Heap=%u (<%uKB TLS)\n", 
                        heapFree, TLS_MIN_HEAP_BYTES / 1024);
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
      if (WiFi.status() == WL_CONNECTED) {
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
          bool fromNVSFallback = false;
          bool ok = AppTasks::netFetchRemoteState(g_remoteFallbackDoc,
                                                  NetworkConfig::FETCH_REMOTE_STATE_RPC_TIMEOUT_MS,
                                                  &fromNVSFallback);
          if (ok && !fromNVSFallback && g_ctx->webClient.copyLastFetchedTo(g_remoteFallbackDoc)) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            ets_printf("[Auto] Fetch fallback OK keys=%u\n", static_cast<unsigned>(g_remoteFallbackDoc.size()));
#else
            Serial.printf("[Auto] Fetch distant fallback: OK, keys=%u\n",
                         static_cast<unsigned>(g_remoteFallbackDoc.size()));
#endif
            if (g_remoteFallbackDoc.size() > 0) {
              g_ctx->automatism.processFetchedRemoteConfig(g_remoteFallbackDoc);
            }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            ets_printf("[Auto] GPIO fallback applied\n");
#else
            Serial.println(F("[Auto] ▶️ Application immédiate des GPIO (fallback)"));
#endif
            GPIOParser::parseAndApply(g_remoteFallbackDoc, g_ctx->automatism);
          } else {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
            ets_printf("[Auto] Fetch fallback %s\n", ok ? "OK (NVS)" : "KO");
#else
            Serial.printf("[Auto] Fetch distant fallback: %s\n", ok ? "OK (NVS)" : "KO");
#endif
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
  // Taille 5 pour réduire abandons POST quand file temporairement pleine (replay + poll + heartbeat).
  if (!g_netQueue) {
    g_netQueue = xQueueCreate(5, sizeof(NetRequest*));
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
  handles.display = g_displayTaskHandle;
  handles.net = g_netTaskHandle;
  return handles;
}

QueueHandle_t getSensorQueue() {
  return g_sensorQueue;
}

// Requête allouée sur le heap : évite use-after-return quand le caller timeout avant la fin du traitement netTask.
// Caller alloue, envoie ; sur timeout caller met req->cancelled et retourne → netTask free. Sur succès caller free.
static bool netRpcAlloc(NetRequest* req, uint32_t timeoutMs) {
  if (!g_netQueue || !g_netTaskHandle || !req) return false;
  req->requester = xTaskGetCurrentTaskHandle();
  req->success = false;
  req->cancelled = false;

  (void)ulTaskNotifyTake(pdTRUE, 0);

  NetRequest* ptr = req;
  const uint32_t QUEUE_SEND_TIMEOUT_MS = 200;  // Laisser une place se libérer avant d'abandonner
  if (xQueueSend(g_netQueue, &ptr, pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS)) != pdTRUE) {
    Serial.println(F("[netRPC] Requête abandonnée: file net pleine"));
    netRequestFree(req);
    return false;
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
      req->cancelled = true;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
      return false;  // netTask free(req)
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return req->success;
}

bool netFetchRemoteState(ArduinoJson::JsonDocument& doc, uint32_t timeoutMs, bool* outFromNVSFallback) {
  NetRequest* req = netRequestAlloc();
  if (!req) return false;
  req->type = NetReqType::FetchRemoteState;
  req->timeoutMs = timeoutMs;
  req->doc = &doc;
  req->diag = nullptr;
  req->payload[0] = '\0';
  bool ok = netRpcAlloc(req, timeoutMs);
  if (ok) {
    if (outFromNVSFallback) *outFromNVSFallback = req->fromNVSFallback;
    netRequestFree(req);
    return true;
  }
  return false;  // timeout ou échec : netTask a netRequestFree(req) si cancelled
}

bool netPostRaw(const char* payload, uint32_t timeoutMs) {
  if (!payload) return false;
  NetRequest* req = netRequestAlloc();
  if (!req) return false;
  req->type = NetReqType::PostRaw;
  req->timeoutMs = timeoutMs;
  req->doc = nullptr;
  req->diag = nullptr;
  strncpy(req->payload, payload, sizeof(req->payload) - 1);
  req->payload[sizeof(req->payload) - 1] = '\0';
  bool ok = netRpcAlloc(req, timeoutMs);
  if (ok) netRequestFree(req);  // sur timeout netTask a netRequestFree(req)
  return ok;
}

bool netSendHeartbeat(const Diagnostics& diag, uint32_t timeoutMs) {
  NetRequest* req = netRequestAlloc();
  if (!req) return false;
  req->type = NetReqType::Heartbeat;
  req->timeoutMs = timeoutMs;
  req->doc = nullptr;
  req->diag = &diag;
  req->payload[0] = '\0';
  bool ok = netRpcAlloc(req, timeoutMs);
  if (ok) netRequestFree(req);
  return ok;
}

void netRequestOtaCheck() {
#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  if (!g_netQueue) return;
  NetRequest* req = netRequestAlloc();
  if (!req) {
    Serial.println(F("[netRPC] OTA check renoncé: pool plein"));
    return;
  }
  req->type = NetReqType::OtaCheck;
  req->requester = nullptr;  // Fire-and-forget, netTask netRequestFree(req) après traitement
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

}  // namespace AppTasks


