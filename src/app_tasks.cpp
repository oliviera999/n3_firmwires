#include "app_tasks.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "event_log.h"
#include "gpio_parser.h"
#include "config.h"

namespace {

AppContext* g_ctx = nullptr;
QueueHandle_t g_sensorQueue = nullptr;
TaskHandle_t g_sensorTaskHandle = nullptr;
TaskHandle_t g_webTaskHandle = nullptr;
TaskHandle_t g_autoTaskHandle = nullptr;
TaskHandle_t g_displayTaskHandle = nullptr;

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

    if (readings.tempWater < SensorConfig::WaterTemp::MIN_VALID ||
        readings.tempWater > SensorConfig::WaterTemp::MAX_VALID ||
        readings.tempAir < SensorConfig::AirSensor::TEMP_MIN ||
        readings.tempAir > SensorConfig::AirSensor::TEMP_MAX ||
        readings.humidity < SensorConfig::AirSensor::HUMIDITY_MIN ||
        readings.humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
      SENSOR_LOG_PRINTLN(F("[Sensor] Erreur lors de la lecture des capteurs"));
      readings.tempAir = SensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
      readings.humidity = SensorConfig::DefaultValues::HUMIDITY_DEFAULT;
      readings.tempWater = SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
      readings.wlPota = 0;
      readings.wlAqua = 0;
      readings.wlTank = 0;
      readings.luminosite = 0;
    }

    esp_task_wdt_reset();

    if (g_sensorQueue) {
      BaseType_t result = xQueueSendToBack(g_sensorQueue,
                                           &readings,
                                           pdMS_TO_TICKS(200));
      if (result != pdTRUE) {
        SENSOR_LOG_PRINTLN(F("[Sensor] ⚠️ Queue pleine - donnée de capteur perdue!"));
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

  unsigned long lastDriftDisplay = 0;
  const unsigned long driftInterval = TimingConfig::DRIFT_DISPLAY_INTERVAL_MS;

  static bool wdtRegistered = false;
  if (!wdtRegistered) {
    esp_task_wdt_add(nullptr);
    wdtRegistered = true;
  }

  for (;;) {
    esp_task_wdt_reset();

    if (xQueueReceive(g_sensorQueue, &readings, pdMS_TO_TICKS(1000)) == pdTRUE) {
      esp_task_wdt_reset();
      g_ctx->automatism.update(readings);
      g_ctx->power.resetWatchdog();
      g_ctx->diagnostics.update();

      unsigned long now = millis();
      if (!g_ctx->otaManager.isUpdating()) {
        if (now - lastHeartbeat > 30000) {
          esp_task_wdt_reset();
          g_ctx->webClient.sendHeartbeat(g_ctx->diagnostics);
          lastHeartbeat = now;
        }
      }

      g_ctx->wifi.checkConnectionStability();
      g_ctx->wifi.loop(&g_ctx->display);

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

      if (now - lastDriftDisplay > driftInterval) {
        LOG_TIME(LOG_INFO, "=== INFORMATIONS DE DÉRIVE TEMPORELLE ===");
        // Simplification : lecture directe de la dernière sync
        time_t lastSync = g_ctx->timeDriftMonitor.getLastSyncTime();
        if (lastSync > 0) {
          char buf[64];
          struct tm ti;
          localtime_r(&lastSync, &ti);
          strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
          LOG_TIME(LOG_INFO, "Dernière sync NTP: %s", buf);
        } else {
          LOG_TIME(LOG_INFO, "Aucune sync NTP effectuée");
        }

        time_t currentEpoch = time(nullptr);
        struct tm timeinfo;
        if (localtime_r(&currentEpoch, &timeinfo)) {
          LOG_TIME(LOG_INFO,
                   "État temporel - Heure: %02d:%02d:%02d, Date: %02d/%02d/%04d",
                   timeinfo.tm_hour,
                   timeinfo.tm_min,
                   timeinfo.tm_sec,
                   timeinfo.tm_mday,
                   timeinfo.tm_mon + 1,
                   timeinfo.tm_year + 1900);
          LOG_TIME(LOG_INFO,
                   "Jour semaine: %d, Jour année: %d, DST: %s",
                   timeinfo.tm_wday,
                   timeinfo.tm_yday,
                   timeinfo.tm_isdst ? "OUI" : "NON");
        }

        lastDriftDisplay = now;
      }

      esp_task_wdt_reset();
    } else {
      Serial.println(F("[Auto] Timeout queue capteurs - cycle continu"));
      if (WiFi.status() == WL_CONNECTED) {
        esp_task_wdt_reset();
        Serial.println(F("[Auto] ▶️ Poll distant (fallback sans capteurs)"));
        StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmpDoc;
        bool ok = g_ctx->automatism.fetchRemoteState(tmpDoc);
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

  g_sensorQueue = xQueueCreate(100, sizeof(SensorReadings));
  if (!g_sensorQueue) {
    Serial.println(F("[App] ❌ CRITIQUE: Échec création queue capteurs - arrêt système"));
    EventLog::add("CRITICAL: Failed to create sensor queue");
    return false;
  }

  Serial.printf("[App] ✅ Queue capteurs créée avec succès (100 slots)\n");

  BaseType_t sensorCreated = xTaskCreatePinnedToCore(sensorTask,
                                                     "sensorTask",
                                                     TaskConfig::SENSOR_TASK_STACK_SIZE,
                                                     nullptr,
                                                     TaskConfig::SENSOR_TASK_PRIORITY,
                                                     &g_sensorTaskHandle,
                                                     TaskConfig::SENSOR_TASK_CORE_ID);

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

  BaseType_t displayCreated = xTaskCreatePinnedToCore(displayTask,
                                                      "displayTask",
                                                      TaskConfig::DISPLAY_TASK_STACK_SIZE,
                                                      nullptr,
                                                      TaskConfig::DISPLAY_TASK_PRIORITY,
                                                      &g_displayTaskHandle,
                                                      TaskConfig::DISPLAY_TASK_CORE_ID);

  if (sensorCreated != pdPASS || webCreated != pdPASS ||
      autoCreated != pdPASS || displayCreated != pdPASS) {
    Serial.println(F("[App] ❌ CRITIQUE: Échec création d'une tâche FreeRTOS"));
    EventLog::add("CRITICAL: Task creation failure");
  }

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

}  // namespace AppTasks


