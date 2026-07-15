// sensor_air.cpp — implémentation AirSensor (DHT/BME280, gardé USE_AIR_SENSOR_AUTO).
// Extrait de sensors.cpp (audit : découpe par classe). Comportement identique.
#include "sensors.h"
#include "nvs_manager.h" // v11.112
#include "nvs_keys.h"
#include "i2c_bus.h"
#include <math.h> // Pour fabs()
#include <esp_task_wdt.h> // Pour esp_task_wdt_reset()
#include "config.h"
#include "sensor_failure_manager.h"
#if defined(USE_AIR_SENSOR_AUTO) || defined(USE_AIR_SENSOR_BME280)
#include <Wire.h>
#endif

// -------- AirSensor ------------------
AirSensor::AirSensor()
#if defined(USE_AIR_SENSOR_AUTO)
  : _dht(Pins::DHT_PIN,
#if defined(USE_DHT22)
         DHT22
#else
         DHT11
#endif
        ),
    _useBme280(false),
    _tempHistoryIndex(0), _tempHistoryCount(0), _lastValidTemp(NAN),
    _humidityHistoryIndex(0), _humidityHistoryCount(0), _lastValidHumidity(NAN),
    _consecutiveTempFailures(0), _consecutiveHumidityFailures(0), _sensorDisabled(false), _disableLogged(false),
    _lastReactivationTestMs(0), _consecutiveReactivationSuccesses(0)
#elif defined(USE_AIR_SENSOR_BME280)
  : _tempHistoryIndex(0), _tempHistoryCount(0), _lastValidTemp(NAN),
    _humidityHistoryIndex(0), _humidityHistoryCount(0), _lastValidHumidity(NAN),
    _consecutiveTempFailures(0), _consecutiveHumidityFailures(0), _sensorDisabled(false), _disableLogged(false),
    _lastReactivationTestMs(0), _consecutiveReactivationSuccesses(0)
#else
  : _dht(Pins::DHT_PIN,
#if defined(USE_DHT22)
         DHT22  // -DUSE_DHT22 dans build_flags
#else
         DHT11  // défaut (wroom-prod, wroom-test, wroom-beta, wroom-s3-*)
#endif
        ),
    _tempHistoryIndex(0), _tempHistoryCount(0), _lastValidTemp(NAN),
    _humidityHistoryIndex(0), _humidityHistoryCount(0), _lastValidHumidity(NAN),
    _consecutiveTempFailures(0), _consecutiveHumidityFailures(0), _sensorDisabled(false), _disableLogged(false),
    _lastReactivationTestMs(0), _consecutiveReactivationSuccesses(0)
#endif
{
  // Initialise l'historique avec des valeurs NaN
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _tempHistory[i] = NAN;
    _humidityHistory[i] = NAN;
  }
}

void AirSensor::begin() {
#if defined(USE_AIR_SENSOR_AUTO)
  {
    I2CBusGuard guard;
    if (guard && _bme.begin(SensorConfig::BME280::I2C_ADDRESS, &Wire)) {
      _useBme280 = true;
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::BME280::INIT_STABILIZATION_DELAY_MS));
      SENSOR_LOG_PRINTLN("[AirSensor] BME280 détecté, utilisation capteur I2C");
      float pressureHpa = _bme.readPressure() / 100.0f;  // readPressure() en Pa -> hPa
      SENSOR_LOG_PRINTF("[AirSensor] BME280 pression: %.1f hPa\n", pressureHpa);
    } else {
      _useBme280 = false;
      _dht.begin();
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::DHT::INIT_STABILIZATION_DELAY_MS));
#if defined(USE_DHT22)
      SENSOR_LOG_PRINTLN("[AirSensor] BME280 absent, utilisation DHT22");
#else
      SENSOR_LOG_PRINTLN("[AirSensor] BME280 absent, utilisation DHT11");
#endif
    }
  }
#elif defined(USE_AIR_SENSOR_BME280)
  {
    I2CBusGuard guard;
    if (!guard) {
      SENSOR_LOG_PRINTLN("[AirSensor] ATTENTION: mutex I2C non acquis");
      _sensorDisabled = true;
      _disableLogged = true;
      return;
    }
    if (!_bme.begin(SensorConfig::BME280::I2C_ADDRESS, &Wire)) {
      SENSOR_LOG_PRINTLN("[AirSensor] ATTENTION: BME280 non détecté sur I2C");
      _sensorDisabled = true;
      _disableLogged = true;
      SENSOR_LOG_PRINTLN("[AirSensor] BME280 désactivé - capteur absent au démarrage");
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(SensorConfig::BME280::INIT_STABILIZATION_DELAY_MS));
    float pressureHpa = _bme.readPressure() / 100.0f;  // readPressure() en Pa -> hPa
    SENSOR_LOG_PRINTF("[AirSensor] BME280 pression: %.1f hPa\n", pressureHpa);
  }
#else
  _dht.begin();
  vTaskDelay(pdMS_TO_TICKS(SensorConfig::DHT::INIT_STABILIZATION_DELAY_MS));
#endif
  resetHistory();

  // Test initial avec timeout strict (2 secondes max)
  uint32_t testStart = millis();
  if (!isSensorConnected()) {
    SENSOR_LOG_PRINTLN("[AirSensor] ATTENTION: Capteur non détecté lors de l'initialisation");
    _sensorDisabled = true;
    _disableLogged = true;
#if defined(USE_AIR_SENSOR_AUTO)
    SENSOR_LOG_PRINTLN("[AirSensor] Capteur air désactivé - absent au démarrage");
#elif defined(USE_AIR_SENSOR_BME280)
    SENSOR_LOG_PRINTLN("[AirSensor] BME280 désactivé - capteur absent au démarrage");
#else
    SENSOR_LOG_PRINTLN("[AirSensor] DHT désactivé - capteur absent au démarrage");
#endif
    return;
  }
  if (millis() - testStart > 2000) {
    SENSOR_LOG_PRINTLN("[AirSensor] ATTENTION: Test initial trop lent, désactivation préventive");
    _sensorDisabled = true;
    _disableLogged = true;
    return;
  }
  SENSOR_LOG_PRINTLN("[AirSensor] Capteur détecté et initialisé");
}

bool AirSensor::isSensorConnected() {
  uint32_t testStart = millis();
  const uint32_t timeoutMs = SensorConfig::Reactivation::TEMPERATURE_TIMEOUT_MS;

  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }

  float temp;
#if defined(USE_AIR_SENSOR_AUTO)
  if (_useBme280) {
    I2CBusGuard guard;
    if (!guard) return false;
    temp = _bme.readTemperature();
  } else {
    temp = _dht.readTemperature();
  }
#elif defined(USE_AIR_SENSOR_BME280)
  {
    I2CBusGuard guard;
    if (!guard) return false;
    temp = _bme.readTemperature();
  }
#else
  temp = _dht.readTemperature();
#endif

  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }

  if (millis() - testStart > timeoutMs) {
    SENSOR_LOG_PRINTLN("[AirSensor] Timeout connectivité - capteur absent");
    return false;
  }

  if (!isnan(temp)) {
    return true;
  }

#if defined(USE_AIR_SENSOR_AUTO)
  SENSOR_LOG_PRINTLN("[AirSensor] Capteur air non détecté (lecture NaN)");
#elif defined(USE_AIR_SENSOR_BME280)
  SENSOR_LOG_PRINTLN("[AirSensor] Capteur BME280 non détecté (lecture NaN)");
#else
  SENSOR_LOG_PRINTLN("[AirSensor] Capteur DHT non détecté (lecture NaN)");
#endif
  return false;
}

void AirSensor::resetSensor() {
  SENSOR_LOG_PRINTLN("[AirSensor] Reset matériel du capteur...");

  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }

#if defined(USE_AIR_SENSOR_AUTO)
  if (_useBme280) {
    I2CBusGuard guard;
    if (guard) {
      _bme.begin(SensorConfig::BME280::I2C_ADDRESS, &Wire);
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::BME280::INIT_STABILIZATION_DELAY_MS));
    }
  } else {
    _dht.begin();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
#elif defined(USE_AIR_SENSOR_BME280)
  {
    I2CBusGuard guard;
    if (guard) {
      _bme.begin(SensorConfig::BME280::I2C_ADDRESS, &Wire);
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::BME280::INIT_STABILIZATION_DELAY_MS));
    }
  }
#else
  _dht.begin();
  vTaskDelay(pdMS_TO_TICKS(500));
#endif

  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }

  resetHistory();
  SENSOR_LOG_PRINTLN("[AirSensor] Reset matériel terminé");
}

float AirSensor::robustTemperatureC() {
  // v11.156: Si capteur désactivé après échecs répétés, tester périodiquement la réactivation
  if (_sensorDisabled) {
    // Tester la réactivation toutes les REACTIVATION_TEST_INTERVAL_MS
    uint32_t now = millis();
    if (now - _lastReactivationTestMs >= REACTIVATION_TEST_INTERVAL_MS) {
      _lastReactivationTestMs = now;
      
      // Test rapide de connectivité avec timeout strict
      uint32_t testStart = millis();
      
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      
#if defined(USE_AIR_SENSOR_AUTO)
      float testTemp;
      if (_useBme280) {
        I2CBusGuard guard;
        testTemp = guard ? _bme.readTemperature() : NAN;
      } else {
        testTemp = _dht.readTemperature();
      }
#elif defined(USE_AIR_SENSOR_BME280)
      float testTemp;
      {
        I2CBusGuard guard;
        testTemp = guard ? _bme.readTemperature() : NAN;
      }
#else
      float testTemp = _dht.readTemperature();
#endif
      if (millis() - testStart > SensorConfig::Reactivation::TEMPERATURE_TIMEOUT_MS) {
        // Timeout - capteur toujours absent
        _consecutiveReactivationSuccesses = 0;
        return SensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
      }
      
      // Vérifier si la valeur est valide
      if (!isnan(testTemp) && 
          testTemp >= SensorConfig::AirSensor::TEMP_MIN && 
          testTemp <= SensorConfig::AirSensor::TEMP_MAX) {
        _consecutiveReactivationSuccesses++;
        SENSOR_LOG_PRINTF("[AirSensor] Test réactivation: succès %d/%d (temp: %.1f°C)\n", 
                          _consecutiveReactivationSuccesses, 
                          REACTIVATION_SUCCESS_THRESHOLD, 
                          testTemp);
        
        // Si 3 succès consécutifs, réactiver le capteur
        if (_consecutiveReactivationSuccesses >= REACTIVATION_SUCCESS_THRESHOLD) {
          _sensorDisabled = false;
          _disableLogged = false;
          _consecutiveTempFailures = 0;
          _consecutiveHumidityFailures = 0;
          _consecutiveReactivationSuccesses = 0;
#if defined(USE_AIR_SENSOR_AUTO)
          SENSOR_LOG_PRINTLN("[AirSensor] Capteur air réactivé automatiquement - présent à nouveau");
#elif defined(USE_AIR_SENSOR_BME280)
          SENSOR_LOG_PRINTLN("[AirSensor] ✅ BME280 réactivé automatiquement - capteur présent à nouveau");
#else
          SENSOR_LOG_PRINTLN("[AirSensor] ✅ DHT réactivé automatiquement - capteur présent à nouveau");
#endif
          // Retourner la valeur testée
          return testTemp;
        }
      } else {
        // Échec du test - réinitialiser le compteur
        _consecutiveReactivationSuccesses = 0;
      }
    }
    
    // Capteur toujours désactivé, retourner valeur par défaut
    return SensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
  }
  
  // v11.180: Timeout réduit pour éviter INT_WDT (max ~2s au lieu de ~5-9s cumulés)
  uint32_t recoveryStartMs = millis();
  
  // Reset watchdog au début de la récupération
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  
  // 1. Tentative avec filtrage avancé
  float result = filteredTemperatureC();
  if (!isnan(result)) {
    // Succès: reset le compteur d'échecs température
    _consecutiveTempFailures = 0;
    return result;
  }
  
  // Reset watchdog après filtrage avancé
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  
  // Vérifier timeout avant récupération
  if ((millis() - recoveryStartMs) >= SensorConfig::DHT::RECOVERY_TIMEOUT_MS) {
    SENSOR_LOG_PRINTLN("[AirSensor] Timeout avant récupération, utilise dernière valeur");
    goto use_last_valid;
  }
  
  SENSOR_LOG_PRINTLN("[AirSensor] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. UNE SEULE tentative de lecture directe (pas de boucle ni de reset)
  {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    float temp;
#if defined(USE_AIR_SENSOR_AUTO)
    if (_useBme280) {
      I2CBusGuard guard;
      temp = guard ? _bme.readTemperature() : NAN;
    } else {
      temp = _dht.readTemperature();
    }
#elif defined(USE_AIR_SENSOR_BME280)
    {
      I2CBusGuard guard;
      temp = guard ? _bme.readTemperature() : NAN;
    }
#else
    temp = _dht.readTemperature();
#endif

    // Reset watchdog après lecture
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }

    if (SensorValidation::isValidAirTemp(temp)) {
      SENSOR_LOG_PRINTF("[AirSensor] Récupération réussie: %.1f°C\n", temp);
      _consecutiveTempFailures = 0;
      return temp;
    }
  }
  
  // 4. Utilisation de la dernière valeur valide si disponible (échec réel: filtrage et récupération ont échoué)
use_last_valid:
  _consecutiveTempFailures++;
  if (_consecutiveTempFailures <= 3) {
    SENSOR_LOG_PRINTF("[AirSensor] Échec température %d/%d\n", _consecutiveTempFailures, MAX_CONSECUTIVE_FAILURES);
  }
  if (!isnan(_lastValidTemp)) {
    if (_consecutiveTempFailures <= 5) {
      SENSOR_LOG_PRINTF("[AirSensor] Utilisation de la dernière valeur valide: %.1f°C\n", _lastValidTemp);
    }
    return _lastValidTemp;
  }
  
  // 5. Désactiver le capteur si température OU humidité atteint MAX_CONSECUTIVE_FAILURES échecs
  if ((_consecutiveTempFailures >= MAX_CONSECUTIVE_FAILURES ||
       _consecutiveHumidityFailures >= MAX_CONSECUTIVE_FAILURES) &&
      !_disableLogged) {
    _sensorDisabled = true;
    _disableLogged = true;
#if defined(USE_AIR_SENSOR_AUTO)
    SENSOR_LOG_PRINTF("[AirSensor] Capteur air désactivé après %d échecs (temp:%d, hum:%d) (défaut: %.1f°C)\n",
#elif defined(USE_AIR_SENSOR_BME280)
    SENSOR_LOG_PRINTF("[AirSensor] 🔴 BME280 désactivé après %d échecs (temp:%d, hum:%d) (utilise valeur par défaut: %.1f°C)\n",
#else
    SENSOR_LOG_PRINTF("[AirSensor] 🔴 DHT désactivé après %d échecs (temp:%d, hum:%d) (utilise valeur par défaut: %.1f°C)\n",
#endif
                      MAX_CONSECUTIVE_FAILURES,
                      _consecutiveTempFailures,
                      _consecutiveHumidityFailures,
                      SensorConfig::DefaultValues::TEMP_AIR_DEFAULT);
  }

  if (_consecutiveTempFailures < MAX_CONSECUTIVE_FAILURES) {
    SENSOR_LOG_PRINTLN("[AirSensor] Échec de toutes les tentatives de récupération");
  }
  return NAN;
}

float AirSensor::temperatureC() {
  float val;
#if defined(USE_AIR_SENSOR_AUTO)
  if (_useBme280) {
    I2CBusGuard guard;
    val = guard ? _bme.readTemperature() : NAN;
  } else {
    val = _dht.readTemperature();
  }
#elif defined(USE_AIR_SENSOR_BME280)
  {
    I2CBusGuard guard;
    val = guard ? _bme.readTemperature() : NAN;
  }
#else
  val = _dht.readTemperature();
#endif
  if (!SensorValidation::isValidAirTemp(val)) {
    return SensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
  }
  return val;
}

float AirSensor::humidity() {
  float val;
#if defined(USE_AIR_SENSOR_AUTO)
  if (_useBme280) {
    I2CBusGuard guard;
    val = guard ? _bme.readHumidity() : NAN;
  } else {
    val = _dht.readHumidity();
  }
#elif defined(USE_AIR_SENSOR_BME280)
  {
    I2CBusGuard guard;
    val = guard ? _bme.readHumidity() : NAN;
  }
#else
  val = _dht.readHumidity();
#endif
  if (isnan(val) || val < SensorConfig::AirSensor::HUMIDITY_MIN || val > SensorConfig::AirSensor::HUMIDITY_MAX) {
    return SensorConfig::DefaultValues::HUMIDITY_DEFAULT;
  }
  return val;
}

float AirSensor::filteredTemperatureC() {
  unsigned long now = millis();
#if defined(USE_AIR_SENSOR_AUTO)
  const uint32_t minInterval = _useBme280 ? SensorConfig::BME280::MIN_READ_INTERVAL_MS : SensorConfig::DHT::MIN_READ_INTERVAL_MS;
#elif defined(USE_AIR_SENSOR_BME280)
  const uint32_t minInterval = SensorConfig::BME280::MIN_READ_INTERVAL_MS;
#else
  const uint32_t minInterval = SensorConfig::DHT::MIN_READ_INTERVAL_MS;
#endif
  if (_lastTempReadMs != 0 && (now - _lastTempReadMs) < minInterval) {
    return _emaInitTemp ? _emaTemp : NAN;
  }
  _lastTempReadMs = now;

  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  float temp;
#if defined(USE_AIR_SENSOR_AUTO)
  if (_useBme280) {
    I2CBusGuard guard;
    temp = guard ? _bme.readTemperature() : NAN;
  } else {
    temp = _dht.readTemperature();
  }
#elif defined(USE_AIR_SENSOR_BME280)
  {
    I2CBusGuard guard;
    temp = guard ? _bme.readTemperature() : NAN;
  }
#else
  temp = _dht.readTemperature();
#endif
  if (!SensorValidation::isValidAirTemp(temp)) {
    return _emaInitTemp ? _emaTemp : NAN;
  }
  if (!_emaInitTemp) {
    _emaTemp = temp;
    _emaInitTemp = true;
  } else {
    _emaTemp = 0.3f * temp + (1.0f - 0.3f) * _emaTemp;
  }
  _lastValidTemp = _emaTemp;
  // Historique pour détection d'aberrations (optionnel, conservé)
  _tempHistory[_tempHistoryIndex] = _emaTemp;
  _tempHistoryIndex = (_tempHistoryIndex + 1) % HISTORY_SIZE;
  if (_tempHistoryCount < HISTORY_SIZE) _tempHistoryCount++;
  return _emaTemp;
}

float AirSensor::filteredHumidity() {
  unsigned long now = millis();
#if defined(USE_AIR_SENSOR_AUTO)
  const uint32_t minInterval = _useBme280 ? SensorConfig::BME280::MIN_READ_INTERVAL_MS : SensorConfig::DHT::MIN_READ_INTERVAL_MS;
#elif defined(USE_AIR_SENSOR_BME280)
  const uint32_t minInterval = SensorConfig::BME280::MIN_READ_INTERVAL_MS;
#else
  const uint32_t minInterval = SensorConfig::DHT::MIN_READ_INTERVAL_MS;
#endif
  if (_lastHumReadMs != 0 && (now - _lastHumReadMs) < minInterval) {
    return _emaInitHum ? _emaHumidity : NAN;
  }
  _lastHumReadMs = now;

  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  float h;
#if defined(USE_AIR_SENSOR_AUTO)
  if (_useBme280) {
    I2CBusGuard guard;
    h = guard ? _bme.readHumidity() : NAN;
  } else {
    h = _dht.readHumidity();
  }
#elif defined(USE_AIR_SENSOR_BME280)
  {
    I2CBusGuard guard;
    h = guard ? _bme.readHumidity() : NAN;
  }
#else
  h = _dht.readHumidity();
#endif
  if (!SensorValidation::isValidHumidity(h)) {
    return _emaInitHum ? _emaHumidity : NAN;
  }
  if (!_emaInitHum) {
    _emaHumidity = h;
    _emaInitHum = true;
  } else {
    _emaHumidity = 0.3f * h + (1.0f - 0.3f) * _emaHumidity;
  }
  _lastValidHumidity = _emaHumidity;
  _humidityHistory[_humidityHistoryIndex] = _emaHumidity;
  _humidityHistoryIndex = (_humidityHistoryIndex + 1) % HISTORY_SIZE;
  if (_humidityHistoryCount < HISTORY_SIZE) _humidityHistoryCount++;
  return _emaHumidity;
}

float AirSensor::robustHumidity() {
  // v11.156: Si capteur désactivé après échecs répétés, tester périodiquement la réactivation
  // Note: La réactivation est testée dans robustTemperatureC(), donc ici on vérifie juste l'état
  if (_sensorDisabled) {
    // Si le capteur vient d'être réactivé dans robustTemperatureC(), on peut essayer de lire
    // Sinon, retourner valeur par défaut
    return SensorConfig::DefaultValues::HUMIDITY_DEFAULT;
  }
  
  // v11.180: Timeout réduit pour éviter INT_WDT (harmonisé avec robustTemperatureC)
  uint32_t recoveryStartMs = millis();
  
  // Reset watchdog au début
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  
  // 1. Tentative avec filtrage avancé
  float result = filteredHumidity();
  if (!isnan(result)) {
    // Succès: reset le compteur d'échecs humidité
    _consecutiveHumidityFailures = 0;
    return result;
  }
  
  // Reset watchdog après filtrage
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  
  // Vérifier timeout avant récupération
  if ((millis() - recoveryStartMs) >= SensorConfig::DHT::RECOVERY_TIMEOUT_MS) {
    SENSOR_LOG_PRINTLN("[AirSensor] Timeout avant récupération humidité, utilise dernière valeur");
    goto use_last_valid_humidity;
  }
  
  SENSOR_LOG_PRINTLN("[AirSensor] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. UNE SEULE tentative de lecture directe (pas de boucle ni de reset)
  {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    float humidity;
#if defined(USE_AIR_SENSOR_AUTO)
    if (_useBme280) {
      I2CBusGuard guard;
      humidity = guard ? _bme.readHumidity() : NAN;
    } else {
      humidity = _dht.readHumidity();
    }
#elif defined(USE_AIR_SENSOR_BME280)
    {
      I2CBusGuard guard;
      humidity = guard ? _bme.readHumidity() : NAN;
    }
#else
    humidity = _dht.readHumidity();
#endif

    // Reset watchdog après lecture
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }

    if (SensorValidation::isValidHumidity(humidity)) {
      SENSOR_LOG_PRINTF("[AirSensor] Récupération réussie: %.1f%%\n", humidity);
      _consecutiveHumidityFailures = 0;
      return humidity;
    }
  }

  // 4. Utilisation de la dernière valeur valide si disponible (échec réel: filtrage et récupération ont échoué)
use_last_valid_humidity:
  _consecutiveHumidityFailures++;
  if (_consecutiveHumidityFailures <= 3) {
    SENSOR_LOG_PRINTF("[AirSensor] Échec humidité %d/%d\n", _consecutiveHumidityFailures, MAX_CONSECUTIVE_FAILURES);
  }
  if (!isnan(_lastValidHumidity)) {
    if (_consecutiveHumidityFailures <= 5) {
      SENSOR_LOG_PRINTF("[AirSensor] Utilisation de la dernière valeur valide: %.1f%%\n", _lastValidHumidity);
    }
    return _lastValidHumidity;
  }

  // 5. Désactiver le capteur si température OU humidité atteint MAX_CONSECUTIVE_FAILURES échecs
  if ((_consecutiveTempFailures >= MAX_CONSECUTIVE_FAILURES ||
       _consecutiveHumidityFailures >= MAX_CONSECUTIVE_FAILURES) &&
      !_disableLogged) {
    _sensorDisabled = true;
    _disableLogged = true;
#if defined(USE_AIR_SENSOR_AUTO)
    SENSOR_LOG_PRINTF("[AirSensor] Capteur air désactivé après %d échecs (temp:%d, hum:%d) (défaut: %.1f%%)\n",
#elif defined(USE_AIR_SENSOR_BME280)
    SENSOR_LOG_PRINTF("[AirSensor] 🔴 BME280 désactivé après %d échecs (temp:%d, hum:%d) (utilise valeur par défaut: %.1f%%)\n",
#else
    SENSOR_LOG_PRINTF("[AirSensor] 🔴 DHT désactivé après %d échecs (temp:%d, hum:%d) (utilise valeur par défaut: %.1f%%)\n",
#endif
                      MAX_CONSECUTIVE_FAILURES,
                      _consecutiveTempFailures,
                      _consecutiveHumidityFailures,
                      SensorConfig::DefaultValues::HUMIDITY_DEFAULT);
  }

  if (_consecutiveHumidityFailures < MAX_CONSECUTIVE_FAILURES) {
    SENSOR_LOG_PRINTLN("[AirSensor] Échec de toutes les tentatives de récupération");
  }
  return NAN;
}

float AirSensor::pressureHpa() {
#if defined(USE_AIR_SENSOR_AUTO)
  if (!_useBme280) return NAN;
  I2CBusGuard guard;
  if (!guard) return NAN;
  float pa = _bme.readPressure();
  if (isnan(pa) || pa <= 0.0f) return NAN;
  return pa / 100.0f;  // Pa -> hPa
#elif defined(USE_AIR_SENSOR_BME280)
  if (_sensorDisabled) return NAN;
  I2CBusGuard guard;
  if (!guard) return NAN;
  float pa = _bme.readPressure();
  if (isnan(pa) || pa <= 0.0f) return NAN;
  return pa / 100.0f;  // Pa -> hPa
#else
  (void)0;
  return NAN;  // DHT seul : pas de pression
#endif
}

void AirSensor::resetHistory() {
  // v11.156: Réinitialiser aussi le compteur d'échecs pour permettre réactivation
  _consecutiveTempFailures = 0;
  _consecutiveHumidityFailures = 0;
  _sensorDisabled = false;
  _disableLogged = false;
  _lastReactivationTestMs = 0;
  _consecutiveReactivationSuccesses = 0;
  _tempHistoryIndex = 0;
  _tempHistoryCount = 0;
  _lastValidTemp = NAN;
  _humidityHistoryIndex = 0;
  _humidityHistoryCount = 0;
  _lastValidHumidity = NAN;
  
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _tempHistory[i] = NAN;
    _humidityHistory[i] = NAN;
  }
  SENSOR_LOG_PRINTLN("[AirSensor] Historique réinitialisé");
}
