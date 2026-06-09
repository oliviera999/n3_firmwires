// app_tasks_sensor.cpp — Corps de la tâche FreeRTOS sensorTask (lecture capteurs
// -> file g_sensorQueue). Extrait d'app_tasks.cpp (refonte god-file, audit v13.93) ;
// déplacement verbatim, comportement identique. État partagé via app_tasks_internal.h.
#include "app_tasks_internal.h"
#include "app_context.h"        // AppContext (g_ctx->...)
#include "system_sensors.h"     // SensorReadings
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "config.h"
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

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

