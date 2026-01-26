#include "app_tasks.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <math.h> // isnan
#include <esp_heap_caps.h>  // v11.157: Pour heap_caps_get_largest_free_block()
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>  // Pour xTaskCreateStaticPinnedToCore

#include "gpio_parser.h"
#include "config.h"
#include "event_log.h"
#include "tls_mutex.h"  // v11.155: Pour traitement mail séquentiel
#include "diagnostics.h"
#include "memory_diagnostics.h"  // Diagnostic fragmentation mémoire
#include "system_sensors.h"  // Pour SensorReadings

namespace {

AppContext* g_ctx = nullptr;
QueueHandle_t g_sensorQueue = nullptr;

// v11.157: Buffers statiques pour stacks des petites tâches (allocation statique pour réduire fragmentation)
// Approche hybride: statique pour petites tâches, dynamique pour grandes (évite overflow DRAM)
// Ces buffers sont alloués à la compilation, pas sur le heap
static StackType_t sensorTaskStack[TaskConfig::SENSOR_TASK_STACK_SIZE];
static StaticTask_t sensorTaskTCB;

static StackType_t displayTaskStack[TaskConfig::DISPLAY_TASK_STACK_SIZE];
static StaticTask_t displayTaskTCB;

// Note: webTask, automationTask et netTask utilisent allocation dynamique (trop grandes pour data segment)
TaskHandle_t g_sensorTaskHandle = nullptr;
TaskHandle_t g_webTaskHandle = nullptr;
TaskHandle_t g_autoTaskHandle = nullptr;
TaskHandle_t g_displayTaskHandle = nullptr;

// ============================================================================
// Point 2: netTask (unique propriétaire de WebClient/TLS)
// ============================================================================
enum class NetReqType : uint8_t {
  FetchRemoteState = 1,
  PostRaw = 2,
  Heartbeat = 3,
  BootFetchRemoteState = 4,
};

struct NetRequest {
  NetReqType type;
  TaskHandle_t requester;
  uint32_t timeoutMs;
  bool success;

  ArduinoJson::JsonDocument* doc;   // FetchRemoteState
  const Diagnostics* diag;          // Heartbeat
  char payload[1024];               // PostRaw (copie)
};

QueueHandle_t g_netQueue = nullptr;
TaskHandle_t g_netTaskHandle = nullptr;

static void netNotifyDone(NetRequest* req) {
  if (req && req->requester) {
    xTaskNotifyGive(req->requester);
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
  
  Serial.println(F("[netTask] Démarrée (TLS/HTTP propriétaire unique)"));

  // Remplacer les fetchRemoteState() du boot (qui se faisaient dans loopTask)
  // par une tentative depuis netTask dès que le WiFi est disponible.
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 30000) {
    esp_task_wdt_reset();  // Reset watchdog pendant attente WiFi
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (WiFi.status() == WL_CONNECTED) {
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
    Serial.println(F("[netTask] Boot: fetchRemoteState() (déplacé hors loopTask)"));
    // CORRECTION DEADLOCK: Appeler directement webClient au lieu de automatism.fetchRemoteState()
    // qui utilise RPC vers la même queue (deadlock)
    bool ok = g_ctx->webClient.fetchRemoteState(tmp);
    Serial.printf("[netTask] Boot fetchRemoteState: %s\n", ok ? "OK" : "ECHEC");
  } else {
    Serial.println(F("[netTask] Boot: WiFi non connecté, fetchRemoteState skip"));
  }

  for (;;) {
    esp_task_wdt_reset();  // Reset watchdog dans boucle principale
    NetRequest* req = nullptr;
    if (xQueueReceive(g_netQueue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) {
      continue;
    }
    if (!req || !g_ctx) continue;

    bool ok = false;
    switch (req->type) {
      case NetReqType::FetchRemoteState:
        ok = (req->doc != nullptr) ? g_ctx->webClient.fetchRemoteState(*req->doc) : false;
        break;
      case NetReqType::PostRaw:
        ok = g_ctx->webClient.postRaw(req->payload);
        break;
      case NetReqType::Heartbeat:
        ok = (req->diag != nullptr) ? g_ctx->webClient.sendHeartbeat(*req->diag) : false;
        break;
      case NetReqType::BootFetchRemoteState: {
        StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
        ok = g_ctx->automatism.fetchRemoteState(tmp);
        break;
      }
      default:
        ok = false;
        break;
    }

    req->success = ok;
    netNotifyDone(req);
  }
}

void sensorTask(void* pv) {
  SensorReadings readings;
  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }

  SENSOR_LOG_PRINTLN(F("[Sensor] Tâche sensorTask démarrée - exécution à rythme naturel avec repos de 500ms"));

  for (;;) {
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
    // Les ultrasons restent valides même si DHT22 échoue
    
    // Vérification température eau
    if (isnan(readings.tempWater) ||
        readings.tempWater < SensorConfig::WaterTemp::MIN_VALID ||
        readings.tempWater > SensorConfig::WaterTemp::MAX_VALID) {
      SENSOR_LOG_PRINTLN(F("[Sensor] ⚠️ Température eau invalide, utilise défaut"));
      readings.tempWater = SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
    }
    
    // Vérification température air + humidité (DHT22)
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
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void displayTask(void* pv) {
  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }

  Serial.println(F("[Display] Tâche displayTask démarrée - cadence dynamique"));

  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    if (g_ctx->otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(250));
      continue;
    }
    g_ctx->automatism.updateDisplay();
    esp_task_wdt_reset();
    uint32_t intervalMs = g_ctx->automatism.getRecommendedDisplayIntervalMs();
    if (intervalMs < TimingConfig::MIN_DISPLAY_INTERVAL_MS) {
      intervalMs = TimingConfig::MIN_DISPLAY_INTERVAL_MS;
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(intervalMs));
  }
}

void webTask(void* pv) {
  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }

  Serial.println(F("[Web] Tâche webTask démarrée - interface web dédiée"));

  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(100);

  for (;;) {
    if (g_ctx->otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));
      continue;
    }

    esp_task_wdt_reset();
    g_ctx->webServer.loop();
    vTaskDelayUntil(&lastWake, period);
  }
}

void automationTask(void* pv) {
  SensorReadings readings;
  unsigned long lastHeartbeat = 0;
  unsigned long lastBouffeDisplay = 0;
  const unsigned long bouffeInterval = TimingConfig::BOUFFE_DISPLAY_INTERVAL_MS;

  unsigned long lastPumpStatsDisplay = 0;
  const unsigned long pumpStatsInterval = TimingConfig::PUMP_STATS_DISPLAY_INTERVAL_MS;

  #if defined(PROFILE_TEST) && FEATURE_DIAG_STACK_LOGS
  unsigned long lastStackCheck = 0;
  const unsigned long stackCheckInterval = 30000; // Toutes les 30 secondes
  #endif

  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }
  
  for (;;) {
    esp_task_wdt_reset();

    if (xQueueReceive(g_sensorQueue, &readings, pdMS_TO_TICKS(1000)) == pdTRUE) {
      esp_task_wdt_reset();
      
      #if defined(PROFILE_TEST) && FEATURE_DIAG_STACK_LOGS
      // Vérification périodique de la stack (wroom-test uniquement)
      unsigned long now = millis();
      if (now - lastStackCheck > stackCheckInterval) {
        UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
        uint32_t stackUsed = TaskConfig::AUTOMATION_TASK_STACK_SIZE - stackHighWaterMark;
        float stackPercent = (1.0 - (float)stackHighWaterMark / TaskConfig::AUTOMATION_TASK_STACK_SIZE) * 100.0;
        Serial.printf("[autoTask] Stack HWM: %u bytes libres (sur %u bytes configurés)\n", 
                      stackHighWaterMark, 
                      TaskConfig::AUTOMATION_TASK_STACK_SIZE);
        Serial.printf("[autoTask] Stack utilisée: %u bytes (%.1f%%)\n",
                      stackUsed,
                      stackPercent);
        if (stackPercent > 85.0) {
          Serial.printf("[autoTask] ⚠️ CRITIQUE: Stack utilisation > 85%% (%u bytes libres)\n", 
                        stackHighWaterMark);
          EventLog::add("CRITICAL: Stack usage > 85% in automationTask");
        } else if (stackPercent > 70.0) {
          Serial.printf("[autoTask] ⚠️ ATTENTION: Stack utilisation > 70%% (%u bytes libres)\n", 
                        stackHighWaterMark);
        }
        lastStackCheck = now;
      }
      #else
      unsigned long now = millis();
      #endif
      
      // v11.156: Monitoring proactif de la mémoire
      static unsigned long lastHeapCheck = 0;
      static unsigned long lastDiagSnapshot = 0;
      // v11.157: Monitoring heap toutes les minutes avec diagnostics détaillés
      const unsigned long heapCheckInterval = 60000; // Toutes les 60 secondes
      const unsigned long diagSnapshotInterval = 300000; // Toutes les 5 minutes pour snapshots
      if (now - lastHeapCheck > heapCheckInterval) {
        uint32_t heapFree = ESP.getFreeHeap();
        uint32_t heapMin = ESP.getMinFreeHeap();
        uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        uint32_t fragmentation = (heapFree > 0) ? ((heapFree - largestBlock) * 100 / heapFree) : 0;
        lastHeapCheck = now;
        
        // v11.157: Vérification critique basée sur TLS_MIN_HEAP_BYTES
        if (heapMin < TLS_MIN_HEAP_BYTES) {
          Serial.printf("[autoTask] 🔴 CRITICAL: Heap minimum critique: %u bytes (< %u KB requis pour TLS)\n", 
                        heapMin, TLS_MIN_HEAP_BYTES / 1024);
          Serial.printf("[autoTask] 🔴 Fragmentation: %u%%, Largest block: %u bytes\n", 
                        fragmentation, largestBlock);
          EventLog::add("CRITICAL: Low heap minimum - TLS operations may fail");
        } else if (heapFree < TLS_MIN_HEAP_BYTES) {
          Serial.printf("[autoTask] ⚠️ WARN: Heap libre faible: %u bytes (< %u KB requis pour TLS)\n", 
                        heapFree, TLS_MIN_HEAP_BYTES / 1024);
          Serial.printf("[autoTask] ⚠️ Fragmentation: %u%%, Largest block: %u bytes\n", 
                        fragmentation, largestBlock);
        } else if (largestBlock < 45000) {
          Serial.printf("[autoTask] ⚠️ WARN: Plus grand bloc insuffisant: %u bytes (< 45KB pour TLS)\n", 
                        largestBlock);
          Serial.printf("[autoTask] ⚠️ Fragmentation: %u%%, Heap libre: %u bytes\n", 
                        fragmentation, heapFree);
        } else {
          Serial.printf("[autoTask] 📊 Heap: libre=%u bytes, min=%u bytes, largest_block=%u bytes, fragmentation=%u%%\n", 
                        heapFree, heapMin, largestBlock, fragmentation);
        }
      }
      
      // Snapshot périodique pour diagnostic fragmentation (toutes les 5 minutes)
      if (now - lastDiagSnapshot > diagSnapshotInterval) {
        MEM_DIAG_SNAPSHOT("periodic_5min");
        lastDiagSnapshot = now;
      }
      
      g_ctx->automatism.update(readings);
      g_ctx->power.resetWatchdog();
      g_ctx->diagnostics.update();
      if (!g_ctx->otaManager.isUpdating()) {
        // Priorité 1: Heartbeat (toutes les 30s)
        if (now - lastHeartbeat > 30000) {
          esp_task_wdt_reset();
          AppTasks::netSendHeartbeat(g_ctx->diagnostics, 10000);
          lastHeartbeat = now;
        }
        
        // Priorité 2: Mails en attente (traitement séquentiel - v11.155)
        #if FEATURE_MAIL
        if (g_ctx->mailer.hasPendingMails() && TLSMutex::canConnect()) {
          esp_task_wdt_reset();
          if (TLSMutex::acquire(3000)) {  // Timeout court pour ne pas bloquer
            g_ctx->mailer.processOneMailSync();  // Traite UN mail
            TLSMutex::release();
          }
        }
        #endif
      }

      if (now - lastBouffeDisplay > bouffeInterval) {
        Serial.println(F("=== ÉTAT DES FLAGS DE BOUFFE ==="));
        Serial.printf("Bouffe Matin: %s\n", g_ctx->config.getBouffeMatinOk() ? "✓ FAIT" : "✗ À FAIRE");
        Serial.printf("Bouffe Midi:  %s\n", g_ctx->config.getBouffeMidiOk() ? "✓ FAIT" : "✗ À FAIRE");
        Serial.printf("Bouffe Soir:  %s\n", g_ctx->config.getBouffeSoirOk() ? "✓ FAIT" : "✗ À FAIRE");
        Serial.printf("Dernier jour: %d\n", g_ctx->config.getLastJourBouf());
        Serial.printf("Pompe lock:   %s\n", g_ctx->config.getPompeAquaLocked() ? "VERROUILLÉE" : "LIBRE");
        Serial.println(F("==============================="));
        lastBouffeDisplay = now;
      }

      if (now - lastPumpStatsDisplay > pumpStatsInterval) {
        Serial.println(F("=== STATISTIQUES POMPE DE RÉSERVE ==="));
        Serial.printf("État actuel: %s\n", g_ctx->actuators.isTankPumpRunning() ? "EN COURS" : "ARRÊTÉE");
        if (g_ctx->actuators.isTankPumpRunning()) {
          unsigned long currentRuntime = g_ctx->actuators.getTankPumpCurrentRuntime();
          Serial.printf("Durée actuelle: %lu ms (%lu s)\n",
                       currentRuntime,
                       currentRuntime / 1000);
        }
        Serial.printf("Temps total d'activité: %lu ms (%lu s)\n",
                     g_ctx->actuators.getTankPumpTotalRuntime(),
                     g_ctx->actuators.getTankPumpTotalRuntime() / 1000);
        Serial.printf("Nombre total d'arrêts: %lu\n", g_ctx->actuators.getTankPumpTotalStops());
        if (g_ctx->actuators.getTankPumpLastStopTime() > 0) {
          unsigned long timeSinceLastStop = now - g_ctx->actuators.getTankPumpLastStopTime();
          Serial.printf("Dernier arrêt: il y a %lu ms (%lu s)\n",
                       timeSinceLastStop,
                       timeSinceLastStop / 1000);
        }
        Serial.println(F("====================================="));
        lastPumpStatsDisplay = now;
      }


      esp_task_wdt_reset();
    } else {
      Serial.println(F("[Auto] Timeout queue capteurs - cycle continu"));
      if (WiFi.status() == WL_CONNECTED) {
        esp_task_wdt_reset();
        Serial.println(F("[Auto] ▶️ Poll distant (fallback sans capteurs)"));
        StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmpDoc;
        bool ok = AppTasks::netFetchRemoteState(tmpDoc, 30000);
        Serial.printf("[Auto] Fetch distant fallback: %s, keys=%u\n",
                     ok ? "OK" : "KO",
                     static_cast<unsigned>(tmpDoc.size()));
        if (ok) {
          Serial.println(F("[Auto] ▶️ Application immédiate des GPIO (fallback)"));
          GPIOParser::parseAndApply(tmpDoc, g_ctx->automatism);
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
  // Snapshot avant création des queues
  HeapSnapshot snapshot_before_tasks = MEM_DIAG_SNAPSHOT("before_tasks");

  // Créer la queue capteurs (utilisée par sensorTask et automationTask)
  // v11.158: Réduit de 10 à 5 slots pour réduire fragmentation (données redondantes toutes les 500ms)
  if (!g_sensorQueue) {
    g_sensorQueue = xQueueCreate(5, sizeof(SensorReadings));
    if (!g_sensorQueue) {
      Serial.println(F("[App] ❌ CRITIQUE: Échec création g_sensorQueue"));
      EventLog::add("CRITICAL: g_sensorQueue creation failure");
      return false;
    }
    Serial.println(F("[App] ✅ Queue capteurs créée"));
  }

  // Créer la queue réseau (utilisée par netTask)
  // v11.158: Réduit de 5 à 3 slots (requêtes séquentielles via mutex TLS)
  if (!g_netQueue) {
    g_netQueue = xQueueCreate(3, sizeof(NetRequest*));
    if (!g_netQueue) {
      Serial.println(F("[App] ❌ CRITIQUE: Échec création g_netQueue"));
      EventLog::add("CRITICAL: g_netQueue creation failure");
      return false;
    }
    Serial.println(F("[App] ✅ Queue réseau créée"));
  }

  // v11.157: Approche hybride - allocation statique pour petites tâches, dynamique pour grandes
  // sensorTask et displayTask: allocation statique (réduit fragmentation, petites tailles)
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

  // webTask, automationTask: allocation dynamique (trop grandes pour data segment)
  BaseType_t webCreated = xTaskCreatePinnedToCore(webTask,
                                                  "webTask",
                                                  TaskConfig::WEB_TASK_STACK_SIZE,
                                                  nullptr,
                                                  TaskConfig::WEB_TASK_PRIORITY,
                                                  &g_webTaskHandle,
                                                  TaskConfig::WEB_TASK_CORE_ID);

  BaseType_t autoCreated = xTaskCreatePinnedToCore(automationTask,
                                                   "autoTask",
                                                   TaskConfig::AUTOMATION_TASK_STACK_SIZE,
                                                   nullptr,
                                                   TaskConfig::AUTOMATION_TASK_PRIORITY,
                                                   &g_autoTaskHandle,
                                                   TaskConfig::AUTOMATION_TASK_CORE_ID);

  g_displayTaskHandle = xTaskCreateStaticPinnedToCore(
                                                      displayTask,
                                                      "displayTask",
                                                      TaskConfig::DISPLAY_TASK_STACK_SIZE,
                                                      nullptr,
                                                      TaskConfig::DISPLAY_TASK_PRIORITY,
                                                      displayTaskStack,
                                                      &displayTaskTCB,
                                                      TaskConfig::DISPLAY_TASK_CORE_ID);
  BaseType_t displayCreated = (g_displayTaskHandle != nullptr) ? pdPASS : pdFAIL;

  BaseType_t netCreated = pdFAIL;
  // Note: netTask créé après création de g_netQueue (voir plus bas)
  
  if (sensorCreated != pdPASS || webCreated != pdPASS ||
      autoCreated != pdPASS || displayCreated != pdPASS) {
    Serial.println(F("[App] ❌ CRITIQUE: Échec création d'une tâche FreeRTOS"));
    EventLog::add("CRITICAL: Task creation failure");
  }

  // Créer netTask APRÈS création de g_netQueue (nécessaire pour la queue)
  // netTask: allocation dynamique (trop grande pour data segment)
  if (g_netQueue) {
    netCreated = xTaskCreatePinnedToCore(netTask,
                                         "netTask",
                                         TaskConfig::NET_TASK_STACK_SIZE,
                                         nullptr,
                                         TaskConfig::NET_TASK_PRIORITY,
                                         &g_netTaskHandle,
                                         TaskConfig::NET_TASK_CORE_ID);
    if (netCreated != pdPASS) {
      Serial.println(F("[App] ❌ CRITIQUE: Échec création netTask (TLS)"));
      EventLog::add("CRITICAL: netTask creation failure");
    }
  }

  // Snapshot après création des tâches principales
  HeapSnapshot snapshot_after_main_tasks = MEM_DIAG_SNAPSHOT("after_main_tasks");
  MEM_DIAG_COMPARE(snapshot_before_tasks, snapshot_after_main_tasks);

  vTaskDelay(pdMS_TO_TICKS(50));

  UBaseType_t hwmSensor = g_sensorTaskHandle ? uxTaskGetStackHighWaterMark(g_sensorTaskHandle) : 0;
  UBaseType_t hwmWeb = g_webTaskHandle ? uxTaskGetStackHighWaterMark(g_webTaskHandle) : 0;
  UBaseType_t hwmAuto = g_autoTaskHandle ? uxTaskGetStackHighWaterMark(g_autoTaskHandle) : 0;
  UBaseType_t hwmDisplay = g_displayTaskHandle ? uxTaskGetStackHighWaterMark(g_displayTaskHandle) : 0;
  UBaseType_t hwmLoop = uxTaskGetStackHighWaterMark(nullptr);

  Serial.printf("[Stacks] HWM at boot - sensor:%u web:%u auto:%u display:%u loop:%u bytes\n",
                static_cast<unsigned>(hwmSensor),
                static_cast<unsigned>(hwmWeb),
                static_cast<unsigned>(hwmAuto),
                static_cast<unsigned>(hwmDisplay),
                static_cast<unsigned>(hwmLoop));
  EventLog::addf("Stacks HWM boot sensor=%u web=%u auto=%u display=%u loop=%u",
                 static_cast<unsigned>(hwmSensor),
                 static_cast<unsigned>(hwmWeb),
                 static_cast<unsigned>(hwmAuto),
                 static_cast<unsigned>(hwmDisplay),
                 static_cast<unsigned>(hwmLoop));

  return sensorCreated == pdPASS && webCreated == pdPASS &&
         autoCreated == pdPASS && displayCreated == pdPASS;
}

Handles getHandles() {
  Handles handles{};
  handles.sensor = g_sensorTaskHandle;
  handles.web = g_webTaskHandle;
  handles.automation = g_autoTaskHandle;
  handles.display = g_displayTaskHandle;
  return handles;
}

QueueHandle_t getSensorQueue() {
  return g_sensorQueue;
}

static bool netRpc(NetRequest& req) {
  if (!g_netQueue || !g_netTaskHandle) return false;
  req.requester = xTaskGetCurrentTaskHandle();
  req.success = false;

  // Clear any pending notification
  (void)ulTaskNotifyTake(pdTRUE, 0);

  NetRequest* ptr = &req;
  if (xQueueSend(g_netQueue, &ptr, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  
  // v11.158: Simplification et correction synchronisation
  // Utiliser un timeout absolu unique plus court (15s au lieu de 35s)
  // pour éviter blocages longs tout en gardant une marge de sécurité
  uint32_t waitStart = millis();
  const uint32_t ABSOLUTE_TIMEOUT_MS = 15000;  // 15 secondes max (réduit de 35s)
  const uint32_t CHECK_INTERVAL_MS = 100;      // Vérifier toutes les 100ms
  
  // Attendre la notification avec timeout absolu
  while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CHECK_INTERVAL_MS)) == 0) {
    esp_task_wdt_reset();  // Reset watchdog pendant attente
    
    // Vérifier timeout absolu
    uint32_t elapsed = millis() - waitStart;
    if (elapsed > ABSOLUTE_TIMEOUT_MS) {
      Serial.printf("[netRPC] ⚠️ Timeout absolu atteint (%lu ms), abandon requête\n", ABSOLUTE_TIMEOUT_MS);
      return false;
    }
    
    // Petit délai pour éviter consommation CPU excessive
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  // Notification reçue
  return req.success;
}

bool netFetchRemoteState(ArduinoJson::JsonDocument& doc, uint32_t timeoutMs) {
  NetRequest req{};
  req.type = NetReqType::FetchRemoteState;
  req.timeoutMs = timeoutMs;
  req.doc = &doc;
  req.diag = nullptr;
  req.payload[0] = '\0';
  return netRpc(req);
}

bool netPostRaw(const char* payload, uint32_t timeoutMs) {
  if (!payload) return false;
  NetRequest req{};
  req.type = NetReqType::PostRaw;
  req.timeoutMs = timeoutMs;
  req.doc = nullptr;
  req.diag = nullptr;
  strncpy(req.payload, payload, sizeof(req.payload) - 1);
  req.payload[sizeof(req.payload) - 1] = '\0';
  return netRpc(req);
}

bool netSendHeartbeat(const Diagnostics& diag, uint32_t timeoutMs) {
  NetRequest req{};
  req.type = NetReqType::Heartbeat;
  req.timeoutMs = timeoutMs;
  req.doc = nullptr;
  req.diag = &diag;
  req.payload[0] = '\0';
  return netRpc(req);
}

}  // namespace AppTasks


