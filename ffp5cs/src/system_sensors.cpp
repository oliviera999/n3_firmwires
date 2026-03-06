#include "system_sensors.h"
#include <Arduino.h>
#include "log.h" // Ajouté pour LOG
#include <math.h> // ajout pour isnan
#include <esp_task_wdt.h> // Pour esp_task_wdt_reset()
#include "config.h"

SystemSensors::SystemSensors() {}

void SystemSensors::begin() {
  _air.begin();
  _water.begin();
  SENSOR_LOG_PRINTLN(F("[Sensors] Initialisation terminée"));
}

SensorReadings SystemSensors::read() {
  SensorReadings r{};
  vTaskDelay(pdMS_TO_TICKS(1));  // Yield Core 1 avant phase ~1s (évite INT_WDT début de run)
  
  // NOUVELLE VERSION NON-BLOQUANTE (v11.50)
  // Timeout global strict pour éviter tout blocage système
  const uint32_t GLOBAL_TIMEOUT_MS = NetworkConfig::HTTP_TIMEOUT_MS;
  uint32_t startTime = millis();
  uint32_t phaseStart;
  auto timeoutExceeded = [&](const char* phase) -> bool {
    uint32_t elapsed = millis() - startTime;
    if (elapsed > GLOBAL_TIMEOUT_MS) {
      SENSOR_LOG_PRINTF("[SystemSensors] ⚠️ TIMEOUT GLOBAL: %s apres %u ms (limite: %u ms)\n",
                        phase, elapsed, GLOBAL_TIMEOUT_MS);
      return true;
    }
    return false;
  };
  auto finalizeOnTimeout = [&]() {
    r.tempWater = NAN;
    r.tempAir = NAN;
    r.humidity = NAN;
    r.pressureHpa = NAN;
  };

  // --- Mesures sécurisées avec timeout strict ---
  // Ultrasons en premier (niveaux critiques, lecture rapide ; DHT peut timeout 7+ s)
  
  // v11.41: Niveaux d'eau avec validation - Mode réactif pour détecter rapidement les changements
  {
    phaseStart = millis();
    uint16_t val = _usPota.readReactiveFiltered();
    SENSOR_LOG_PRINTF("[SystemSensors] ⏱️ Niveau potager: %u ms\n", millis() - phaseStart);
    if (val == 0 || val > SensorConfig::Ultrasonic::MAX_VALID_LEVEL_CM) {
      SENSOR_LOG_PRINTF("[SystemSensors] Niveau potager invalide: %u cm, force 0\n", val);
      r.wlPota = 0;
    } else {
      r.wlPota = val;
    }
  }
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  vTaskDelay(pdMS_TO_TICKS(1));  // Yield pour IWDT (ultrason ~300 ms)
  if (timeoutExceeded("niveau potager")) {
    finalizeOnTimeout();
    return r;
  }
  
  {
    phaseStart = millis();
    uint16_t val = _usAqua.readReactiveFiltered();
    SENSOR_LOG_PRINTF("[SystemSensors] ⏱️ Niveau aquarium: %u ms\n", millis() - phaseStart);
    bool valid = (val > 0 && val <= SensorConfig::Ultrasonic::MAX_VALID_LEVEL_CM);
    if (!valid) {
      SENSOR_LOG_PRINTF("[SystemSensors] Niveau aquarium invalide (%u), tentative de récupération...\n", val);
      // Tentative de récupération avec méthode simple
      val = _usAqua.readFiltered(3);
      valid = (val > 0 && val <= SensorConfig::Ultrasonic::MAX_VALID_LEVEL_CM);
      if (valid) {
        SENSOR_LOG_PRINTF("[SystemSensors] Récupération réussie: %u cm\n", val);
        r.wlAqua = val;
        _lastValidWlAqua = val;
      } else if (_lastValidWlAqua > 0) {
        SENSOR_LOG_PRINTF("[SystemSensors] Fallback sur dernière valeur valide aquarium: %u cm\n", _lastValidWlAqua);
        r.wlAqua = _lastValidWlAqua;
      } else {
        SENSOR_LOG_PRINTF("[SystemSensors] Récupération échouée, aucune valeur valide connue – utilise 0\n");
        r.wlAqua = 0;
      }
    } else {
      // Valeur valide
      r.wlAqua = val;
      _lastValidWlAqua = val;
    }
  }
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  vTaskDelay(pdMS_TO_TICKS(1));  // Yield pour IWDT (ultrason ~300 ms)
  if (timeoutExceeded("niveau aquarium")) {
    finalizeOnTimeout();
    return r;
  }
  
  // Met à jour l'historique wlAqua pour calcul ~15s
  {
    uint32_t nowMs = millis();
    pushAquaHist(r.wlAqua, nowMs);
  }
  
  {
    phaseStart = millis();
    uint16_t val = _usTank.readAdvancedFiltered();
    SENSOR_LOG_PRINTF("[SystemSensors] ⏱️ Niveau réservoir: %u ms\n", millis() - phaseStart);
    bool valid = (val > 0 && val <= SensorConfig::Ultrasonic::MAX_VALID_LEVEL_CM);
    if (!valid) {
      uint32_t nowMs = millis();
      bool shouldLog = _lastWlTankWasValid || (nowMs - _lastWlTankInvalidLogMs >= 30000);
      if (shouldLog) {
        SENSOR_LOG_PRINTF("[SystemSensors] Niveau réservoir invalide: %u cm\n", val);
        _lastWlTankInvalidLogMs = nowMs;
      }
      // Essai de récupération simple
      val = _usTank.readFiltered(3);
      valid = (val > 0 && val <= SensorConfig::Ultrasonic::MAX_VALID_LEVEL_CM);
      if (valid) {
        if (shouldLog) {
          SENSOR_LOG_PRINTF("[SystemSensors] Récupération réservoir réussie: %u cm\n", val);
        }
        r.wlTank = val;
        _lastValidWlTank = val;
      } else if (_lastValidWlTank > 0) {
        if (shouldLog) {
          SENSOR_LOG_PRINTF("[SystemSensors] Fallback sur dernière valeur valide réservoir: %u cm\n", _lastValidWlTank);
        }
        r.wlTank = _lastValidWlTank;
      } else {
        // Aucune valeur valide connue : utiliser valeur par défaut sûre (cohérente API /json)
        r.wlTank = static_cast<uint16_t>(SensorConfig::Fallback::WATER_LEVEL_TANK + 0.5f);
        if (shouldLog) {
          SENSOR_LOG_PRINTF("[SystemSensors] Récupération échouée, réservoir=defaut %u cm\n", r.wlTank);
        }
      }
    } else {
      r.wlTank = val;
      _lastValidWlTank = val;
    }
    _lastWlTankWasValid = valid;
  }
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  vTaskDelay(pdMS_TO_TICKS(1));  // Yield pour IWDT (ultrason ~300 ms)
  if (timeoutExceeded("niveau reservoir")) {
    finalizeOnTimeout();
    return r;
  }

  // Température eau (DS18B20) avec méthode non-bloquante
  {
    phaseStart = millis();
    float val = _water.getTemperatureWithFallback();
    SENSOR_LOG_PRINTF("[SystemSensors] ⏱️ Température eau: %u ms\n", millis() - phaseStart);
    if (isnan(val) || val < SensorConfig::WaterTemp::MIN_VALID || val > SensorConfig::WaterTemp::MAX_VALID) {
      SENSOR_LOG_PRINTF("[SystemSensors] Température eau invalide finale: %.1f°C (plage: %.1f-%.1f°C), force NaN\n",
                   val, SensorConfig::WaterTemp::MIN_VALID, SensorConfig::WaterTemp::MAX_VALID);
      r.tempWater = NAN;
    } else {
      r.tempWater = val;
    }
  }
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  vTaskDelay(pdMS_TO_TICKS(1));  // Yield pour IWDT (phase longue)
  if (timeoutExceeded("temperature eau")) {
    finalizeOnTimeout();
    return r;
  }

  // Température air (DHT) avec méthode non-bloquante
  {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    phaseStart = millis();
    float val = _air.robustTemperatureC();
    SENSOR_LOG_PRINTF("[SystemSensors] ⏱️ Température air: %u ms\n", millis() - phaseStart);
    if (isnan(val) || val < SensorConfig::AirSensor::TEMP_MIN || val > SensorConfig::AirSensor::TEMP_MAX) {
      SENSOR_LOG_PRINTF("[SystemSensors] Température air invalide finale: %.1f°C, force NaN\n", val);
      r.tempAir = NAN;
    } else {
      r.tempAir = val;
    }
  }
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  vTaskDelay(pdMS_TO_TICKS(1));  // Yield pour IWDT (phase longue)
  if (timeoutExceeded("temperature air")) {
    finalizeOnTimeout();
    return r;
  }

  // Luminosité avec validation
  {
    phaseStart = millis();
    uint32_t lumiSum = 0;
    const uint8_t NB_LUMI_SAMPLES = 12; // 12 échantillons pour réduire le bruit
    for (uint8_t i = 0; i < NB_LUMI_SAMPLES; ++i) {
      lumiSum += analogRead(Pins::LUMINOSITE);
      vTaskDelay(pdMS_TO_TICKS(1)); // 1 ms entre échantillons
      if ((i % 4) == 0 && esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      if (timeoutExceeded("luminosite")) {
        r.luminosite = 0;
        break;
      }
    }
    uint16_t val = static_cast<uint16_t>(lumiSum / NB_LUMI_SAMPLES);
    SENSOR_LOG_PRINTF("[SystemSensors] ⏱️ Luminosité: %u ms\n", millis() - phaseStart);
    
    // Validation de la luminosité (0-4095 pour ESP32 ADC 12-bit)
    if (val > 4095) {
      SENSOR_LOG_PRINTF("[SystemSensors] Luminosité invalide: %u, force 0\n", val);
      r.luminosite = 0;
    } else {
      r.luminosite = val;
    }
  }
  
  // v11.154: Humidité EN DERNIER (peut prendre 7+ secondes si DHT air échoue)
  // Ainsi, même en cas de timeout, les ultrasons sont déjà lus
  {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    phaseStart = millis();
    float val = _air.robustHumidity(); // Garde la méthode robuste pour DHT (air)
    SENSOR_LOG_PRINTF("[SystemSensors] ⏱️ Humidité: %u ms\n", millis() - phaseStart);
    
    // Validation finale
    if (isnan(val) || val < SensorConfig::AirSensor::HUMIDITY_MIN || val > SensorConfig::AirSensor::HUMIDITY_MAX) {
      SENSOR_LOG_PRINTF("[SystemSensors] Humidité invalide finale: %.1f%%, force NaN\n", val);
      r.humidity = NAN;
    } else {
      r.humidity = val;
    }
  }
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  vTaskDelay(pdMS_TO_TICKS(1));  // Yield pour IWDT (DHT peut être long)
  // Note: Pas de return anticipé ici - on continue même si timeout pour avoir le log final

  // Pression atmosphérique (BME280 uniquement)
  {
    float val = _air.pressureHpa();
    r.pressureHpa = (isnan(val) || val <= 0.0f || val > 1200.0f) ? NAN : val;
  }

  // marée diff
  uint16_t oldAquaMax = _aquaMax;
  if (r.wlAqua > _aquaMax) {
    _aquaMax = r.wlAqua;
    SENSOR_LOG_PRINTF("[Maree] Nouveau max: %u cm (précédent: %u cm)\n", _aquaMax, oldAquaMax);
  }

  // Vérification du timeout global et affichage du temps d'exécution
  uint32_t elapsed = millis() - startTime;
  if (elapsed > GLOBAL_TIMEOUT_MS) {
    SENSOR_LOG_PRINTF("[SystemSensors] ⚠️ TIMEOUT GLOBAL: Lecture capteurs a pris %u ms (limite: %u ms)\n", elapsed, GLOBAL_TIMEOUT_MS);
  } else {
    SENSOR_LOG_PRINTF("[SystemSensors] ✓ Lecture capteurs terminée en %u ms\n", elapsed);
  }

  LOG(LOG_DEBUG, "Sensors TWater=%.1fC TAir=%.1fC Hum=%.1f%% wlA=%u wlT=%u wlP=%u Lux=%u", r.tempWater, r.tempAir, r.humidity, r.wlAqua, r.wlTank, r.wlPota, r.luminosite);

  return r;
}

int SystemSensors::diffMaree(uint16_t currentAqua) {
  // Nouveau calcul: différence par rapport à la valeur ~15s avant
  uint32_t nowMs = millis();
  int diff10s = diffMaree10s(currentAqua, nowMs);
  
  // Log détaillé du calcul de marée (15s)
  SENSOR_LOG_PRINTF("[Maree] Calcul15s: actuel=%u, diff15s=%d cm\n", currentAqua, diff10s);
  return diff10s;
} 

void SystemSensors::pushAquaHist(uint16_t value, uint32_t nowMs) {
  _aquaHist[_aquaHistHead] = value;
  _aquaHistTime[_aquaHistHead] = nowMs;
  _aquaHistHead = (uint8_t)((_aquaHistHead + 1) % AQUA_HIST_SIZE);
  if (_aquaHistCount < AQUA_HIST_SIZE) _aquaHistCount++;
}

int SystemSensors::diffMaree10s(uint16_t currentAqua, uint32_t nowMs) const {
  if (_aquaHistCount == 0) return 0;
  // Cherche l'échantillon le plus proche de now-15s
  const uint32_t target = nowMs - _tideWindowMs;
  int bestIdx = -1;
  uint32_t bestDt = 0xFFFFFFFFUL;
  for (uint8_t i = 0; i < _aquaHistCount; ++i) {
    uint8_t idx = (uint8_t)((_aquaHistHead + AQUA_HIST_SIZE - 1 - i) % AQUA_HIST_SIZE);
    uint32_t t = _aquaHistTime[idx];
    int32_t diff = static_cast<int32_t>(t - target);
    uint32_t dt = diff >= 0 ? static_cast<uint32_t>(diff) : static_cast<uint32_t>(-diff);
    if (dt < bestDt) { bestDt = dt; bestIdx = idx; }
    // petit early break si on est déjà très proche (<1s)
    if (bestDt < 1000UL) break;
  }
  if (bestIdx < 0) return 0;
  int past = (int)_aquaHist[bestIdx];
  int cur  = (int)currentAqua;
  // diffMaree = (valeur ~15s avant) - (valeur actuelle)
  return past - cur;
}

void SystemSensors::setLastCachedReadings(const SensorReadings& r) {
  _lastCachedReadings = r;
  _lastCachedReadingsValid = true;
}

bool SystemSensors::getLastCachedReadings(SensorReadings& out) const {
  if (!_lastCachedReadingsValid) return false;
  out = _lastCachedReadings;
  return true;
}