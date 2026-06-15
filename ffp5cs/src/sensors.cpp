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


// -------- WaterTempSensor ------------
WaterTempSensor::WaterTempSensor() : _historyIndex(0), _historyCount(0), _lastValidTemp(NAN),
                                     _failureManager("WaterTemp", 10, 60000, 3) {
  // Initialise l'historique avec des valeurs NaN
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _history[i] = NAN;
  }
}

void WaterTempSensor::begin() {
  _sensors.begin();
  _sensors.setResolution(DS18B20_RESOLUTION); // 10 bits (option 2)
  _sensors.setWaitForConversion(false); // conversions non-bloquantes
  resetHistory();
  
  // Chargement de la dernière température valide depuis NVS
  _lastValidTemp = loadLastValidTempFromNVS();
  if (!isnan(_lastValidTemp)) {
    _lastSavedTempToNVS = _lastValidTemp;  // Évite réécriture immédiate au boot
  }
  
  // Test initial de connectivité avec timeout strict
  uint32_t testStart = millis();
  if (!isSensorConnected()) {
    Serial.println("[WaterTemp] ATTENTION: Capteur non détecté lors de l'initialisation");
    // Enregistrer plusieurs échecs pour désactiver immédiatement si non détecté au boot
    for (uint8_t i = 0; i < 10; ++i) {
      _failureManager.recordFailure();
    }
  } else {
    Serial.printf("[WaterTemp] Capteur détecté et initialisé (résolution: %d bits, conversion: %d ms)\n", 
                  DS18B20_RESOLUTION, CONVERSION_DELAY_MS);
    _failureManager.recordSuccess();
  }
  
  // Vérifier que le test n'a pas pris trop de temps
  if (millis() - testStart > 2000) {
    Serial.println("[WaterTemp] ATTENTION: Test initial trop lent, désactivation préventive");
    for (uint8_t i = 0; i < 10; ++i) {
      _failureManager.recordFailure();
    }
  }

  // Pré-charge pipeline de conversion
  _sensors.requestTemperatures();
  _lastRequestMs = millis();
  _pipelineReady = true;
}

bool WaterTempSensor::isSensorConnected() {
  uint32_t testStart = millis();
  const uint32_t timeoutMs = SensorConfig::Reactivation::TEMPERATURE_TIMEOUT_MS;

  // Reset du bus OneWire pour un test propre
  _oneWire.reset();
  vTaskDelay(pdMS_TO_TICKS(ONEWIRE_RESET_DELAY_MS));
  
  if (millis() - testStart > timeoutMs) {
    return false; // Timeout - capteur trop lent
  }

  // Vérifie la présence du capteur sur le bus OneWire
  uint8_t addr[8];
  _oneWire.reset_search();

  // Tentative de recherche avec retry
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (millis() - testStart > timeoutMs) {
      return false; // Timeout - capteur trop lent
    }
    
    if (_oneWire.search(addr)) {
      // Vérifie que c'est bien un DS18B20
      if (_oneWire.crc8(addr, 7) != addr[7]) {
        Serial.println("[WaterTemp] CRC invalide - capteur corrompu");
        continue; // Retry
      }
      
      // Vérifie le type de capteur (DS18B20 = 0x28)
      if (addr[0] != 0x28) {
        Serial.printf("[WaterTemp] Type de capteur invalide: 0x%02X (attendu: 0x28)\n", addr[0]);
        continue; // Retry
      }
      
      // Test de lecture rapide pour vérifier la fonctionnalité
      _sensors.requestTemperatures();
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::SENSOR_READ_DELAY_MS)); // Délai court pour test

      if (millis() - testStart > timeoutMs) {
        return false; // Timeout - capteur trop lent
      }
      
      float testTemp = _sensors.getTempCByIndex(0);
      
      if (!isnan(testTemp)) {
        Serial.printf("[WaterTemp] Capteur connecté et fonctionnel (test: %.1f°C)\n", testTemp);
        return true;
      } else {
        Serial.println("[WaterTemp] Capteur détecté mais lecture échouée");
        continue; // Retry
      }
    }
    
    // Délai avant retry
    if (attempt < 2) {
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::SENSOR_READ_DELAY_MS));
    }
  }
  
  Serial.println("[WaterTemp] Aucun capteur fonctionnel trouvé sur le bus OneWire");
  return false;
}

void WaterTempSensor::resetSensor() {
  Serial.println("[WaterTemp] Reset matériel du capteur...");
  
  // Reset du bus OneWire
  _oneWire.reset();
  vTaskDelay(pdMS_TO_TICKS(SENSOR_RESET_DELAY_MS));
  
  // Réinitialisation de la bibliothèque DallasTemperature
  _sensors.begin();
  _sensors.setResolution(DS18B20_RESOLUTION); // Utilise la résolution optimisée
  
  // Reset de l'historique et du gestionnaire de défaillances
  resetHistory();
  _failureManager.reset();
  
  Serial.printf("[WaterTemp] Reset matériel terminé (résolution: %d bits)\n", DS18B20_RESOLUTION);
}

float WaterTempSensor::getTemperatureWithFallback() {
  // NOUVELLE MÉTHODE NON-BLOQUANTE (v11.50)
  // Timeout strict pour éviter tout blocage système
  
  // Si capteur désactivé, tester réactivation périodiquement
  if (_failureManager.isDisabled()) {
    if (_failureManager.shouldTestReactivation()) {
      // Test rapide de réactivation avec timeout strict
      uint32_t testStart = millis();
      
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      
      // Test de connectivité rapide
      bool connected = isSensorConnected();
      if (millis() - testStart > SensorConfig::Reactivation::TEMPERATURE_TIMEOUT_MS) {
        // Timeout - capteur toujours absent
        _failureManager.recordReactivationTestFailure();
        // Fallback sur dernière valeur ou défaut
        if (!isnan(_lastValidTemp)) {
          return _lastValidTemp;
        }
        return SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
      }
      
      // Si connecté, tester une lecture rapide
      if (connected) {
        if (_pipelineReady && (millis() - _lastRequestMs) >= CONVERSION_DELAY_MS) {
          float testTemp = _sensors.getTempCByIndex(0);
          _pipelineReady = false;
          _sensors.requestTemperatures();
          _lastRequestMs = millis();
          _pipelineReady = true;
          
          if (!isnan(testTemp) && 
              testTemp >= SensorConfig::WaterTemp::MIN_VALID && 
              testTemp <= SensorConfig::WaterTemp::MAX_VALID) {
            if (_failureManager.recordReactivationTestSuccess()) {
              // Capteur réactivé, retourner la valeur testée
              _lastValidTemp = testTemp;
              saveLastValidTempToNVS(testTemp);
              return testTemp;
            }
          } else {
            _failureManager.recordReactivationTestFailure();
          }
        } else {
          // Pipeline pas prêt, lancer conversion pour prochaine fois
          if (!_pipelineReady) {
            _sensors.requestTemperatures();
            _lastRequestMs = millis();
            _pipelineReady = true;
          }
          _failureManager.recordReactivationTestFailure();
        }
      } else {
        _failureManager.recordReactivationTestFailure();
      }
    }
    
    // Capteur toujours désactivé, retourner valeur par défaut ou dernière valide
    if (!isnan(_lastValidTemp)) {
      return _lastValidTemp;
    }
    return SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
  }
  
  // Capteur actif, tentative de lecture normale
  uint32_t startTime = millis();
  const uint32_t timeoutMs = SensorConfig::DS18B20::TIMEOUT_MS;
  
  // 1. Tentative rapide avec lecture simple
  if (_pipelineReady && (millis() - _lastRequestMs) >= CONVERSION_DELAY_MS) {
    float temp = _sensors.getTempCByIndex(0);
    _pipelineReady = false;
    
    // Re-planifier immédiatement
    _sensors.requestTemperatures();
    _lastRequestMs = millis();
    _pipelineReady = true;
    
    if (SensorValidation::isValidWaterTemp(temp)) {
      // Succès
      _failureManager.recordSuccess();
      _lastValidTemp = temp;
      saveLastValidTempToNVS(temp);
      return temp;
    } else {
      // Échec - valeur invalide
      _failureManager.recordFailure();
    }
  }
  
  // 2. Si pas prêt, lancer et retourner fallback
  if (!_pipelineReady) {
    _sensors.requestTemperatures();
    _lastRequestMs = millis();
    _pipelineReady = true;
  }
  
  // 3. Fallback immédiat - utiliser dernière valeur valide ou valeur par défaut
  _failureManager.recordFailure();
  if (!isnan(_lastValidTemp)) {
    Serial.printf("[WaterTemp] Capteur défaillant, utilise dernière valeur valide: %.1f°C\n", _lastValidTemp);
    return _lastValidTemp;
  }
  
  Serial.printf("[WaterTemp] Capteur défaillant, utilise valeur par défaut: %.1f°C\n", SensorConfig::DefaultValues::TEMP_WATER_DEFAULT);
  return SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
}

float WaterTempSensor::temperatureC() {
  // Non-bloquant: si pipeline prêt et délai écoulé, lire
  unsigned long now = millis();
  if (_pipelineReady && (now - _lastRequestMs) >= CONVERSION_DELAY_MS) {
    float temp = _sensors.getTempCByIndex(0);
    _pipelineReady = false; // consomme la conversion
    // Re-planifie immédiatement la prochaine conversion
    _sensors.requestTemperatures();
    _lastRequestMs = now;
    _pipelineReady = true;
    if (SensorValidation::isValidWaterTemp(temp)) {
      return temp;
    }
    return NAN;
  }
  // Si pas prêt, lancer si nécessaire et signaler indisponible
  if (!_pipelineReady) {
    _sensors.requestTemperatures();
    _lastRequestMs = now;
    _pipelineReady = true;
  }
  return NAN;
}

float WaterTempSensor::filteredTemperatureC() {
  // OPTIMISATION : Phase de stabilisation supprimée (pipeline pré-chargé dans begin() suffit)
  // Gain de temps : ~520ms économisés
  
  // Filtrage statistique avec médiane et validation croisée
  float readings[READINGS_COUNT];
  uint8_t validReadings = 0;
  
  // Effectue plusieurs lectures avec validation croisée
  for (uint8_t i = 0; i < READINGS_COUNT; ++i) {
    // Non-bloquant: vérifier si une conversion précédente est prête
    if (!_pipelineReady) {
      _sensors.requestTemperatures();
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::DS18B20::STABILIZATION_DELAY_MS)); // Délai de stabilisation
      _lastRequestMs = millis();
      _pipelineReady = true;
      vTaskDelay(pdMS_TO_TICKS(CONVERSION_DELAY_MS));
    }
    // Attendre si nécessaire
    unsigned long now = millis();
    if ((now - _lastRequestMs) < CONVERSION_DELAY_MS) {
      vTaskDelay(pdMS_TO_TICKS(CONVERSION_DELAY_MS - (now - _lastRequestMs)));
    }
    float temp = _sensors.getTempCByIndex(0);
    _pipelineReady = false;
    // Planifier suivante
    _sensors.requestTemperatures();
    vTaskDelay(pdMS_TO_TICKS(SensorConfig::DS18B20::STABILIZATION_DELAY_MS)); // Délai de stabilisation
    _lastRequestMs = millis();
    _pipelineReady = true;
    
    // Validation renforcée avec vérification de cohérence et plage d'eau
    if (SensorValidation::isValidWaterTemp(temp)) {
      // Vérification de cohérence avec les lectures précédentes
      bool isCoherent = true;
      if (validReadings > 0) {
        float avg = 0.0f;
        for (uint8_t j = 0; j < validReadings; ++j) {
          avg += readings[j];
        }
        avg /= validReadings;
        
        // Rejette si l'écart est trop important (plus de 3°C au lieu de 5°C)
        if (fabs(temp - avg) > 3.0f) {
          Serial.printf("[WaterTemp] Lecture incohérente rejetée: %.1f°C (moyenne: %.1f°C)\n", temp, avg);
          isCoherent = false;
        }
      }
      
      // Validation temporelle : vérifier la cohérence avec la dernière valeur valide
      if (isCoherent && !isnan(_lastValidTemp)) {
        float timeDelta = fabs(temp - _lastValidTemp);
        if (timeDelta > 2.0f) { // Rejette si changement > 2°C par rapport à la dernière valeur
          Serial.printf("[WaterTemp] Changement temporel trop important rejeté: %.1f°C -> %.1f°C (écart: %.1f°C)\n", 
                       _lastValidTemp, temp, timeDelta);
          isCoherent = false;
        }
      }
      
      if (isCoherent) {
        readings[validReadings++] = temp;
      }
    } else {
      bool inRange = (temp >= SensorConfig::WaterTemp::MIN_VALID) && (temp <= SensorConfig::WaterTemp::MAX_VALID);
      Serial.printf("[WaterTemp] Lecture invalide rejetée: %.1f°C (NaN=%d, inRange=%d)\n", temp, isnan(temp), inRange);
    }
    
    // Délai entre les mesures
    vTaskDelay(pdMS_TO_TICKS(READING_INTERVAL_MS));
  }
  
  // OPTIMISATION : Avec 2 lectures, accepte au moins 1 lecture valide
  // Cela réduit les échecs tout en maintenant la qualité avec validation croisée
  if (validReadings < 1) {
    Serial.printf("[WaterTemp] Pas assez de lectures valides (%d/1 minimum), retourne NaN\n", validReadings);
    return NAN;
  }
  
  if (validReadings < 2) {
    Serial.printf("[WaterTemp] Une seule lecture valide (%d/2), utilise cette valeur\n", validReadings);
  }
  
  // Trie les lectures pour calculer la médiane
  for (uint8_t i = 0; i < validReadings - 1; ++i) {
    for (uint8_t j = i + 1; j < validReadings; ++j) {
      if (readings[i] > readings[j]) {
        float temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }
  
  // Calcule la médiane
  float medianTemp = readings[validReadings / 2];
  
  // VALIDATION FINALE RENFORCÉE - S'assure qu'aucune valeur aberrante ne passe
  if (isnan(medianTemp) || medianTemp < SensorConfig::WaterTemp::MIN_VALID || medianTemp > SensorConfig::WaterTemp::MAX_VALID) {
    Serial.printf("[WaterTemp] Médiane invalide rejetée: %.1f°C (hors plage %.1f-%.1f°C), utilise ancienne valeur\n", 
                  medianTemp, SensorConfig::WaterTemp::MIN_VALID, SensorConfig::WaterTemp::MAX_VALID);
    return _lastValidTemp;
  }
  
  // Filtrage par moyenne mobile pour lisser les variations
  float smoothedTemp = medianTemp;
  if (!isnan(_lastValidTemp)) {
    // Coefficient de lissage : 0.7 pour la nouvelle valeur, 0.3 pour l'ancienne
    smoothedTemp = 0.7f * medianTemp + 0.3f * _lastValidTemp;
    
    // Vérifier que le lissage n'a pas créé de valeur aberrante
    if (fabs(smoothedTemp - _lastValidTemp) > 1.5f) {
      Serial.printf("[WaterTemp] Lissage rejeté (écart trop important: %.1f°C), utilise médiane brute\n", 
                   fabs(smoothedTemp - _lastValidTemp));
      smoothedTemp = medianTemp;
    } else {
      Serial.printf("[WaterTemp] Température lissée: %.1f°C -> %.1f°C\n", medianTemp, smoothedTemp);
    }
  }
  
  // 2. Détection de sauts brutaux avec confirmation temporelle (inspiré ultrason)
  if (!isnan(_lastValidTemp) && fabs(smoothedTemp - _lastValidTemp) > MAX_TEMP_DELTA) {
    float jumpDelta = fabs(smoothedTemp - _lastValidTemp);
    Serial.printf("[WaterTemp] Saut détecté: %.1f°C -> %.1f°C (écart: %.1f°C), confirmation en cours...\n",
                  _lastValidTemp, smoothedTemp, jumpDelta);

    // Petite pause avant confirmation pour éviter lecture transitoire
    vTaskDelay(pdMS_TO_TICKS(READING_INTERVAL_MS * 2));

    // Nouveau lot de lectures pour confirmer la tendance
    float confirmReadings[READINGS_COUNT];
    uint8_t confirmValid = 0;
    for (uint8_t i = 0; i < READINGS_COUNT; ++i) {
      _oneWire.reset();
      vTaskDelay(pdMS_TO_TICKS(ONEWIRE_RESET_DELAY_MS));
      _sensors.requestTemperatures();
      vTaskDelay(pdMS_TO_TICKS(CONVERSION_DELAY_MS));
      float t2 = _sensors.getTempCByIndex(0);
      if (!isnan(t2) && t2 >= SensorConfig::WaterTemp::MIN_VALID && t2 <= SensorConfig::WaterTemp::MAX_VALID) {
        confirmReadings[confirmValid++] = t2;
      } else {
        Serial.printf("[WaterTemp] Confirmation: lecture rejetée: %.1f°C\n", t2);
      }
      vTaskDelay(pdMS_TO_TICKS(READING_INTERVAL_MS));
    }

    if (confirmValid < MIN_VALID_READINGS) {
      Serial.printf("[WaterTemp] Confirmation échouée (%d/%d valides), utilise dernière valeur\n",
                    confirmValid, MIN_VALID_READINGS);
      return _lastValidTemp;
    }

    // Trie pour médiane de confirmation
    for (uint8_t i = 0; i < confirmValid - 1; ++i) {
      for (uint8_t j = i + 1; j < confirmValid; ++j) {
        if (confirmReadings[i] > confirmReadings[j]) {
          float tmp = confirmReadings[i];
          confirmReadings[i] = confirmReadings[j];
          confirmReadings[j] = tmp;
        }
      }
    }
    float medianConfirm = confirmReadings[confirmValid / 2];
    float deltaConfirm = fabs(medianConfirm - medianTemp);

    // Si les deux médianes sont proches, accepte le saut (moyenne pour lisser)
    if (deltaConfirm <= 1.0f) {
      Serial.printf("[WaterTemp] Saut confirmé (médiane2=%.1f°C, Δ=%.1f°C)\n", medianConfirm, deltaConfirm);
      smoothedTemp = (smoothedTemp + medianConfirm) / 2.0f;
    } else {
      Serial.printf("[WaterTemp] Confirmation non cohérente (médiane2=%.1f°C, Δ=%.1f°C), conserve ancienne valeur\n",
                    medianConfirm, deltaConfirm);
      return _lastValidTemp;
    }
  }
  
  // 5. Historique glissant pour détection d'aberrations
  // Vérifie la cohérence avec l'historique
  if (_historyCount >= 2) {
    float avgHistory = 0.0f;
    uint8_t validHistory = 0;
    
    for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
      if (!isnan(_history[i]) && _history[i] > 0.0f) {
        avgHistory += _history[i];
        validHistory++;
      }
    }
    
    if (validHistory > 0) {
      avgHistory /= validHistory;
      float deviation = fabs(smoothedTemp - avgHistory);
      
      // Si l'écart avec la moyenne historique est trop important, suspect
      if (deviation > MAX_TEMP_DELTA * 2) {
        Serial.printf("[WaterTemp] Écart important avec l'historique: %.1f°C (moyenne: %.1f°C), utilise ancienne valeur\n", 
                      smoothedTemp, avgHistory);
        return _lastValidTemp;
      }
    }
  }
  
  // Met à jour l'historique avec la température lissée
  _history[_historyIndex] = smoothedTemp;
  _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
  if (_historyCount < HISTORY_SIZE) _historyCount++;
  
  // Met à jour la dernière valeur valide avec la température lissée
  _lastValidTemp = smoothedTemp;
  
  // Sauvegarde en NVS pour persistance après redémarrage
  saveLastValidTempToNVS(smoothedTemp);
  
  Serial.printf("[WaterTemp] Température filtrée: %.1f°C (médiane: %.1f°C, lissée: %.1f°C, %d lectures, résolution: %d bits)\n", 
                smoothedTemp, medianTemp, smoothedTemp, validReadings, DS18B20_RESOLUTION);
  return smoothedTemp;
}

bool WaterTempSensor::isSensorDisabled() const {
  return _failureManager.isDisabled();
}

void WaterTempSensor::resetHistory() {
  _historyIndex = 0;
  _historyCount = 0;
  _lastValidTemp = NAN;
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _history[i] = NAN;
  }
  Serial.println("[WaterTemp] Historique réinitialisé");
}


// -------- Méthodes NVS pour WaterTempSensor --------
// v11.168: Debounce pour réduire usure flash - min 60s entre écritures, ou si delta ≥ 0.5°C
static constexpr uint32_t NVS_TEMP_DEBOUNCE_MS = 60000;
static constexpr float NVS_TEMP_MIN_DELTA = 0.5f;

void WaterTempSensor::saveLastValidTempToNVS(float temp) {
  if (isnan(temp) || temp < SensorConfig::WaterTemp::MIN_VALID || temp > SensorConfig::WaterTemp::MAX_VALID) {
    return;
  }
  uint32_t now = millis();
  bool deltaOk = isnan(_lastSavedTempToNVS) || (fabsf(temp - _lastSavedTempToNVS) >= NVS_TEMP_MIN_DELTA);
  bool timeOk = (_lastNvsSaveMs == 0) || ((now - _lastNvsSaveMs) >= NVS_TEMP_DEBOUNCE_MS);
  if (!deltaOk && !timeOk) {
    return;  // Pas de changement significatif ni délai écoulé
  }
  // v11.172: Clé unique (migration terminée)
  g_nvsManager.saveFloat(NVS_NAMESPACES::CONFIG, NVSKeys::Sensors::TEMP_LAST_VALID, temp);
  _lastSavedTempToNVS = temp;
  _lastNvsSaveMs = now;
  Serial.printf("[WaterTemp] Dernière température valide sauvegardée en NVS: %.1f°C\n", temp);
}

float WaterTempSensor::loadLastValidTempFromNVS() {
  float temp;
  // v11.172: Clé unique (migration terminée)
  g_nvsManager.loadFloat(NVS_NAMESPACES::CONFIG, NVSKeys::Sensors::TEMP_LAST_VALID, temp, NAN);
  
  if (!isnan(temp) && temp >= SensorConfig::WaterTemp::MIN_VALID && temp <= SensorConfig::WaterTemp::MAX_VALID) {
    Serial.printf("[WaterTemp] Dernière température valide chargée depuis NVS: %.1f°C\n", temp);
    return temp;
  } else {
    Serial.println("[WaterTemp] Aucune température valide trouvée en NVS");
    return NAN;
  }
} 