#include "sensors.h"
#include "nvs_manager.h" // v11.112
#include <math.h> // Pour fabs()
#include <esp_task_wdt.h> // Pour esp_task_wdt_reset()
#include "config.h"
#include "sensor_failure_manager.h"

// -------- UltrasonicManager ---------
UltrasonicManager::UltrasonicManager(int pinTrigEcho, const char* sensorName) 
  : _pinTrigEcho(pinTrigEcho),
    _historyIndex(0), _historyCount(0), _lastValidDistance(0),
    _failureManager(sensorName, 10, 60000, 3) {
  // Initialise l'historique avec des valeurs 0
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _history[i] = 0;
  }
}

uint16_t UltrasonicManager::readFiltered(uint8_t samples) {
  // Mesure manuelle pour limiter le temps passé avec les interruptions désactivées.
  // On envoie un pulse de 10 µs puis on attend l'écho avec un timeout réduit à 25 ms
  // afin de ne jamais bloquer les interruptions assez longtemps pour déclencher
  // l'interrupt watchdog.

  const uint32_t TIMEOUT_US = SensorConfig::Ultrasonic::TIMEOUT_US;
  uint32_t total = 0;
  uint8_t valid = 0;

  for (uint8_t i = 0; i < samples; ++i) {
    // Déclenchement
    pinMode(_pinTrigEcho, OUTPUT);
    digitalWrite(_pinTrigEcho, LOW);
    delayMicroseconds(2);
    digitalWrite(_pinTrigEcho, HIGH);
    delayMicroseconds(10);
    digitalWrite(_pinTrigEcho, LOW);

    // Lecture de l'écho (pin en entrée)
    // Pas de délai nécessaire selon datasheet HC-SR04 - le capteur envoie l'écho immédiatement
    pinMode(_pinTrigEcho, INPUT);
    unsigned long duration = readEchoPulseUs(TIMEOUT_US);

    if (duration > 0) {
      uint16_t cm = duration / SensorConfig::Ultrasonic::US_TO_CM_FACTOR; // Conversion µs -> cm (vitesse du son ~340 m/s)
      if (cm > MIN_DISTANCE && cm < MAX_DISTANCE) {
        total += cm;
        ++valid;
      }
    }

    // Délai minimal entre mesures (10ms suffisant pour éviter les interférences)
    if (i < samples - 1) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  
  // Un seul délai après toutes les mesures (respecte datasheet HC-SR04)
  if (samples > 1) {
    vTaskDelay(pdMS_TO_TICKS(50)); // 50ms après toutes les mesures
  }

  return valid ? total / valid : 0;
}

uint16_t UltrasonicManager::readAdvancedFiltered() {
  // Si capteur désactivé, tester réactivation périodiquement
  if (_failureManager.isDisabled()) {
    if (_failureManager.shouldTestReactivation()) {
      // Test rapide de réactivation avec une seule lecture
      uint32_t testStart = millis();
      const uint32_t REACTIVATION_TEST_TIMEOUT_MS = 500;
      
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      
      // Test de lecture simple
      pinMode(_pinTrigEcho, OUTPUT);
      digitalWrite(_pinTrigEcho, LOW);
      delayMicroseconds(2);
      digitalWrite(_pinTrigEcho, HIGH);
      delayMicroseconds(10);
      digitalWrite(_pinTrigEcho, LOW);
      pinMode(_pinTrigEcho, INPUT);
      unsigned long duration = readEchoPulseUs(SensorConfig::Ultrasonic::TIMEOUT_US);
      
      if (millis() - testStart > REACTIVATION_TEST_TIMEOUT_MS) {
        // Timeout - capteur toujours absent
        _failureManager.recordReactivationTestFailure();
        // Fallback sur dernière valeur valide ou 0
        return _lastValidDistance > 0 ? _lastValidDistance : 0;
      }
      
      if (duration > 0) {
        uint16_t testCm = duration / 58;
        if (testCm >= MIN_DISTANCE && testCm < MAX_DISTANCE) {
          if (_failureManager.recordReactivationTestSuccess()) {
            // Capteur réactivé, retourner la valeur testée
            _lastValidDistance = testCm;
            return testCm;
          }
        } else {
          _failureManager.recordReactivationTestFailure();
        }
      } else {
        _failureManager.recordReactivationTestFailure();
      }
    }
    
    // Capteur toujours désactivé, retourner dernière valeur valide ou 0
    return _lastValidDistance > 0 ? _lastValidDistance : 0;
  }
  
  // Capteur actif, lecture normale
  // 1. Filtrage statistique avancé avec médiane
  uint16_t readings[READINGS_COUNT];
  uint8_t validReadings = 0;
  
  // CORRECTION : Timeout augmenté pour la production
  const uint32_t TIMEOUT_US = SensorConfig::Ultrasonic::TIMEOUT_US; // 30ms par config
  
  // Effectue plusieurs lectures avec délai
  for (uint8_t i = 0; i < READINGS_COUNT; ++i) {
    // Déclenchement
    pinMode(_pinTrigEcho, OUTPUT);
    digitalWrite(_pinTrigEcho, LOW);
    delayMicroseconds(2);
    digitalWrite(_pinTrigEcho, HIGH);
    delayMicroseconds(10);
    digitalWrite(_pinTrigEcho, LOW);

    // Lecture de l'écho (pin en entrée)
    // Pas de délai nécessaire selon datasheet HC-SR04 - le capteur envoie l'écho immédiatement
    pinMode(_pinTrigEcho, INPUT);
    unsigned long duration = readEchoPulseUs(TIMEOUT_US);

    if (duration > 0) {
      uint16_t cm = duration / 58; // Conversion µs -> cm (vitesse du son ~340 m/s)
      
      // CORRECTION : Plage de validation assouplie pour l'aquarium
      if (cm >= MIN_DISTANCE && cm < MAX_DISTANCE) { // >= au lieu de >
        readings[validReadings++] = cm;
        Serial.printf("[Ultrasonic] Lecture %d: %u cm\n", i+1, cm);
      } else {
        Serial.printf("[Ultrasonic] Lecture %d rejetée: %u cm (hors plage %u-%u)\n", i+1, cm, MIN_DISTANCE, MAX_DISTANCE-1);
      }
    } else {
      Serial.printf("[Ultrasonic] Lecture %d timeout\n", i+1);
    }
    
    // Délai minimal entre mesures (10ms suffisant pour éviter les interférences)
    if (i < READINGS_COUNT - 1) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  
  // Un seul délai après toutes les mesures (respecte datasheet HC-SR04)
  if (READINGS_COUNT > 1) {
    vTaskDelay(pdMS_TO_TICKS(50)); // 50ms après toutes les mesures
  }
  
  // CORRECTION : Seuil de lectures valides réduit pour plus de tolérance
  if (validReadings < 1) { // Réduit de MIN_VALID_READINGS (2) à 1
    Serial.printf("[Ultrasonic] Pas assez de lectures valides (%d/1), retourne 0\n", validReadings);
    _failureManager.recordFailure();
    return _lastValidDistance > 0 ? _lastValidDistance : 0;
  }
  
  // Trie les lectures pour calculer la médiane
  for (uint8_t i = 0; i < validReadings - 1; ++i) {
    for (uint8_t j = i + 1; j < validReadings; ++j) {
      if (readings[i] > readings[j]) {
        uint16_t temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }
  
  // Calcule la médiane
  uint16_t medianDistance = readings[validReadings / 2];
  
  // v11.35: NOUVELLE LOGIQUE - Médiane glissante avec consensus pour éviter valeurs figées
  // Calcul de la médiane de l'historique (valeur de référence robuste)
  uint16_t historyMedian = 0;
  if (_historyCount >= 3) {
    // Copie l'historique pour calcul médiane sans modifier l'original
    uint16_t histTemp[HISTORY_SIZE];
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
      if (_history[i] > 0) {
        histTemp[validCount++] = _history[i];
      }
    }
    
    // Tri pour médiane
    if (validCount > 0) {
      for (uint8_t i = 0; i < validCount - 1; ++i) {
        for (uint8_t j = i + 1; j < validCount; ++j) {
          if (histTemp[i] > histTemp[j]) {
            uint16_t temp = histTemp[i];
            histTemp[i] = histTemp[j];
            histTemp[j] = temp;
          }
        }
      }
      historyMedian = histTemp[validCount / 2];
    }
  }
  
  // Utiliser médiane historique si disponible, sinon dernière valeur
  uint16_t referenceValue = (historyMedian > 0) ? historyMedian : _lastValidDistance;
  
  // Détection de saut par rapport à la référence robuste
  if (referenceValue > 0 && abs((int)medianDistance - (int)referenceValue) > MAX_DISTANCE_DELTA) {
    Serial.printf("[Ultrasonic] Saut détecté: %u cm -> %u cm (écart: %d cm, ref: médiane historique)\n", 
                  referenceValue, medianDistance, abs((int)medianDistance - (int)referenceValue));
    
    // v11.35: Si saut vers le bas, accepter (niveau qui baisse)
    if (medianDistance < referenceValue) {
      Serial.printf("[Ultrasonic] Saut vers le bas accepté (niveau qui baisse)\n");
    } 
    // v11.35: Si saut vers le haut, vérifier CONSENSUS sur 3 dernières lectures
    else {
      // Compter combien de lectures récentes concordent avec la nouvelle valeur
      uint8_t consensusCount = 0;
      for (uint8_t i = 0; i < HISTORY_SIZE && i < 3; ++i) {
        uint8_t idx = (_historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
        if (_history[idx] > 0 && abs((int)_history[idx] - (int)medianDistance) <= MAX_DISTANCE_DELTA / 2) {
          consensusCount++;
        }
      }
      
      // Si consensus de 2+ lectures récentes, accepter la nouvelle valeur (reset référence)
      if (consensusCount >= 2) {
        Serial.printf("[Ultrasonic] Consensus détecté (%d/3 lectures), accepte nouvelle référence\n", consensusCount);
      } else {
        // Sinon, utilise la médiane historique par sécurité
        Serial.printf("[Ultrasonic] Pas de consensus (%d/3), utilise médiane historique par sécurité\n", consensusCount);
        return referenceValue;
      }
    }
  }
  
  // Succès - enregistrer et mettre à jour
  _failureManager.recordSuccess();
  
  // Met à jour l'historique
  _history[_historyIndex] = medianDistance;
  _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
  if (_historyCount < HISTORY_SIZE) _historyCount++;
  
  // Met à jour la dernière valeur valide
  _lastValidDistance = medianDistance;
  
  Serial.printf("[Ultrasonic] Distance médiane: %u cm (%d lectures valides)\n", 
                medianDistance, validReadings);
  return medianDistance;
}

uint16_t UltrasonicManager::readReactiveFiltered() {
  // Si capteur désactivé, tester réactivation périodiquement
  if (_failureManager.isDisabled()) {
    if (_failureManager.shouldTestReactivation()) {
      // Test rapide de réactivation avec une seule lecture
      uint32_t testStart = millis();
      const uint32_t REACTIVATION_TEST_TIMEOUT_MS = 500;
      
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      
      // Test de lecture simple
      pinMode(_pinTrigEcho, OUTPUT);
      digitalWrite(_pinTrigEcho, LOW);
      delayMicroseconds(2);
      digitalWrite(_pinTrigEcho, HIGH);
      delayMicroseconds(10);
      digitalWrite(_pinTrigEcho, LOW);
      pinMode(_pinTrigEcho, INPUT);
      unsigned long duration = readEchoPulseUs(SensorConfig::Ultrasonic::TIMEOUT_US);
      
      if (millis() - testStart > REACTIVATION_TEST_TIMEOUT_MS) {
        // Timeout - capteur toujours absent
        _failureManager.recordReactivationTestFailure();
        // Fallback sur dernière valeur valide ou 0
        return _lastValidDistance > 0 ? _lastValidDistance : 0;
      }
      
      if (duration > 0) {
        uint16_t testCm = duration / 58;
        if (testCm >= MIN_DISTANCE && testCm < MAX_DISTANCE) {
          if (_failureManager.recordReactivationTestSuccess()) {
            // Capteur réactivé, retourner la valeur testée
            _lastValidDistance = testCm;
            return testCm;
          }
        } else {
          _failureManager.recordReactivationTestFailure();
        }
      } else {
        _failureManager.recordReactivationTestFailure();
      }
    }
    
    // Capteur toujours désactivé, retourner dernière valeur valide ou 0
    return _lastValidDistance > 0 ? _lastValidDistance : 0;
  }
  
  // Capteur actif, lecture normale
  // v11.41: Mode réactif - lissage minimal pour détecter rapidement les changements de niveau
  // Réduit le lissage excessif des capteurs ultrasoniques tout en gardant la fiabilité
  // Configuration: 3 lectures avec délai de 60ms entre chaque lecture
  uint16_t readings[REACTIVE_READINGS_COUNT];
  uint8_t validReadings = 0;
  
  const uint32_t TIMEOUT_US = SensorConfig::Ultrasonic::TIMEOUT_US;
  
  // Effectue 3 lectures avec délai standard de 60ms
  for (uint8_t i = 0; i < REACTIVE_READINGS_COUNT; ++i) {
    // Déclenchement
    pinMode(_pinTrigEcho, OUTPUT);
    digitalWrite(_pinTrigEcho, LOW);
    delayMicroseconds(2);
    digitalWrite(_pinTrigEcho, HIGH);
    delayMicroseconds(10);
    digitalWrite(_pinTrigEcho, LOW);

    // Lecture de l'écho
    pinMode(_pinTrigEcho, INPUT);
    unsigned long duration = readEchoPulseUs(TIMEOUT_US);

    if (duration > 0) {
      uint16_t cm = duration / 58; // Conversion µs -> cm
      
      if (cm >= MIN_DISTANCE && cm < MAX_DISTANCE) {
        readings[validReadings++] = cm;
        Serial.printf("[Ultrasonic] Lecture réactive %d: %u cm\n", i+1, cm);
      } else {
        Serial.printf("[Ultrasonic] Lecture réactive %d rejetée: %u cm (hors plage)\n", i+1, cm);
      }
    } else {
      Serial.printf("[Ultrasonic] Lecture réactive %d timeout\n", i+1);
    }
    
    // Délai standard de 60ms entre lectures
    if (i < REACTIVE_READINGS_COUNT - 1) {
      vTaskDelay(pdMS_TO_TICKS(60));
    }
  }
  
  if (validReadings < 1) {
    Serial.printf("[Ultrasonic] Pas assez de lectures réactives valides (%d/1), retourne 0\n", validReadings);
    _failureManager.recordFailure();
    return _lastValidDistance > 0 ? _lastValidDistance : 0;
  }
  
  // Calcul simple : moyenne des lectures valides (pas de médiane pour plus de réactivité)
  uint32_t total = 0;
  for (uint8_t i = 0; i < validReadings; ++i) {
    total += readings[i];
  }
  uint16_t avgDistance = total / validReadings;
  
  // Validation minimale : seulement vérifier les valeurs aberrantes extrêmes
  if (_lastValidDistance > 0) {
    int delta = abs((int)avgDistance - (int)_lastValidDistance);
    // Seuil plus permissif pour les sauts importants (50cm au lieu de 30cm)
    if (delta > 50) {
      Serial.printf("[Ultrasonic] Saut important détecté: %u -> %u cm (Δ=%d), accepte pour réactivité\n", 
                   _lastValidDistance, avgDistance, delta);
    }
  }
  
  // Succès - enregistrer et mettre à jour
  _failureManager.recordSuccess();
  
  // Mise à jour de l'historique avec la nouvelle valeur
  _history[_historyIndex] = avgDistance;
  _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
  if (_historyCount < HISTORY_SIZE) _historyCount++;
  
  // Mise à jour dernière valeur valide
  _lastValidDistance = avgDistance;
  
  Serial.printf("[Ultrasonic] Mode réactif: %u cm (moyenne de %d lectures sur 3)\n", avgDistance, validReadings);
  return avgDistance;
}

bool UltrasonicManager::isSensorDisabled() const {
  return _failureManager.isDisabled();
}

void UltrasonicManager::resetHistory() {
  _historyIndex = 0;
  _historyCount = 0;
  _lastValidDistance = 0;
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _history[i] = 0;
  }
  _failureManager.reset();
  Serial.println("[Ultrasonic] Historique réinitialisé");
}

// --- Implémentation utilitaire: lecture d'impulsion via RMT si dispo ---
uint32_t UltrasonicManager::readEchoPulseUs(uint32_t timeoutUs) {
#if CONFIG_IDF_TARGET_ESP32S3
  // Implémentation non-bloquante simple: échantillonnage actif avec timeout
  // Mesure la largeur d'impulsion HIGH en microsecondes sans bloquer trop longtemps
  unsigned long start = micros();
  unsigned long deadline = start + timeoutUs;

  // Attendre front montant
  while (digitalRead(_pinTrigEcho) == LOW) {
    if ((long)(micros() - deadline) >= 0) return 0;
    // petite pause pour laisser le CPU respirer
    delayMicroseconds(2);
  }

  // Mesurer la durée HIGH
  unsigned long highStart = micros();
  while (digitalRead(_pinTrigEcho) == HIGH) {
    if ((long)(micros() - deadline) >= 0) return 0;
    delayMicroseconds(2);
  }
  unsigned long highEnd = micros();
  if (highEnd < highStart) return 0;
  return (uint32_t)(highEnd - highStart);
#else
  return pulseIn(_pinTrigEcho, HIGH, timeoutUs);
#endif
}

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
  const uint32_t CONNECTIVITY_TEST_TIMEOUT_MS = 1000;
  
  // Reset du bus OneWire pour un test propre
  _oneWire.reset();
  vTaskDelay(pdMS_TO_TICKS(ONEWIRE_RESET_DELAY_MS));
  
  if (millis() - testStart > CONNECTIVITY_TEST_TIMEOUT_MS) {
    return false; // Timeout - capteur trop lent
  }
  
  // Vérifie la présence du capteur sur le bus OneWire
  uint8_t addr[8];
  _oneWire.reset_search();
  
  // Tentative de recherche avec retry
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (millis() - testStart > CONNECTIVITY_TEST_TIMEOUT_MS) {
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
      
      if (millis() - testStart > CONNECTIVITY_TEST_TIMEOUT_MS) {
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
      // Test rapide de réactivation avec timeout strict (1 seconde max)
      uint32_t testStart = millis();
      const uint32_t REACTIVATION_TEST_TIMEOUT_MS = 1000;
      
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      
      // Test de connectivité rapide
      bool connected = isSensorConnected();
      if (millis() - testStart > REACTIVATION_TEST_TIMEOUT_MS) {
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
  const uint32_t timeoutMs = GlobalTimeouts::DS18B20_MAX_MS;
  
  // 1. Tentative rapide avec lecture simple
  if (_pipelineReady && (millis() - _lastRequestMs) >= CONVERSION_DELAY_MS) {
    float temp = _sensors.getTempCByIndex(0);
    _pipelineReady = false;
    
    // Re-planifier immédiatement
    _sensors.requestTemperatures();
    _lastRequestMs = millis();
    _pipelineReady = true;
    
    if (!isnan(temp) && temp >= SensorConfig::WaterTemp::MIN_VALID && temp <= SensorConfig::WaterTemp::MAX_VALID) {
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
    if (!isnan(temp) && temp >= SensorConfig::WaterTemp::MIN_VALID && temp <= SensorConfig::WaterTemp::MAX_VALID) {
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
    if (!isnan(temp) && temp >= SensorConfig::WaterTemp::MIN_VALID && temp <= SensorConfig::WaterTemp::MAX_VALID) {
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

// -------- AirSensor ------------------
AirSensor::AirSensor() : _dht(Pins::DHT_PIN, 
#if defined(PROFILE_PROD)
                            DHT22  // Production : utilise DHT22
#else
                            DHT11  // Dev et Test : utilise DHT11
#endif
                            ), 
                        _tempHistoryIndex(0), _tempHistoryCount(0), _lastValidTemp(NAN),
                        _humidityHistoryIndex(0), _humidityHistoryCount(0), _lastValidHumidity(NAN),
                        _consecutiveTempFailures(0), _consecutiveHumidityFailures(0), _sensorDisabled(false), _disableLogged(false),
                        _lastReactivationTestMs(0), _consecutiveReactivationSuccesses(0) {
  // Initialise l'historique avec des valeurs NaN
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _tempHistory[i] = NAN;
    _humidityHistory[i] = NAN;
  }
}

void AirSensor::begin() { 
  _dht.begin(); 
  vTaskDelay(pdMS_TO_TICKS(SensorConfig::DHT::INIT_STABILIZATION_DELAY_MS)); // Délai de stabilisation après initialisation
  resetHistory();
  
  // Test initial avec timeout strict (2 secondes max)
  uint32_t testStart = millis();
  if (!isSensorConnected()) {
    SENSOR_LOG_PRINTLN("[AirSensor] ATTENTION: Capteur non détecté lors de l'initialisation");
    // Désactiver immédiatement si non détecté au boot
    _sensorDisabled = true;
    _disableLogged = true;
    SENSOR_LOG_PRINTLN("[AirSensor] DHT désactivé - capteur absent au démarrage");
    return;
  }
  // Vérifier que le test n'a pas pris trop de temps
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
  const uint32_t CONNECTIVITY_TEST_TIMEOUT_MS = 2000;
  
  // Test de lecture pour vérifier la connectivité du DHT
  // Reset watchdog une seule fois au début
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  
  float temp = _dht.readTemperature();
  if (millis() - testStart > CONNECTIVITY_TEST_TIMEOUT_MS) {
    return false; // Timeout - capteur trop lent ou absent
  }
  
  // FIX: Délai minimum DHT augmenté à 2.5 secondes pour stabilité
  // Le DHT11/DHT22 nécessite au moins 2 secondes entre lectures selon datasheet
  vTaskDelay(pdMS_TO_TICKS(SensorConfig::DHT::MIN_READ_INTERVAL_MS));
  
  float humidity = _dht.readHumidity();
  if (millis() - testStart > CONNECTIVITY_TEST_TIMEOUT_MS) {
    return false; // Timeout - capteur trop lent ou absent
  }
  
  // Vérifie si les lectures sont valides
  if (isnan(temp) && isnan(humidity)) {
    SENSOR_LOG_PRINTLN("[AirSensor] Capteur DHT non détecté ou déconnecté");
    return false;
  }
  
  return true;
}

void AirSensor::resetSensor() {
  SENSOR_LOG_PRINTLN("[AirSensor] Reset matériel du capteur...");
  
  // Reset de la bibliothèque DHT
  _dht.begin();
  vTaskDelay(pdMS_TO_TICKS(SensorConfig::DHT::INIT_STABILIZATION_DELAY_MS)); // Délai de stabilisation
  vTaskDelay(pdMS_TO_TICKS(SENSOR_RESET_DELAY_MS));
  
  // Reset de l'historique
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
      
      // Test rapide de connectivité avec timeout strict (1 seconde max)
      uint32_t testStart = millis();
      const uint32_t REACTIVATION_TEST_TIMEOUT_MS = 1000;
      
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      
      float testTemp = _dht.readTemperature();
      if (millis() - testStart > REACTIVATION_TEST_TIMEOUT_MS) {
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
          SENSOR_LOG_PRINTLN("[AirSensor] ✅ DHT réactivé automatiquement - capteur présent à nouveau");
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
  
  // Timeout total pour éviter blocage: 2000ms (réduit de 3000ms - si capteur ne répond pas en 2s, il est absent)
  const uint32_t DHT_RECOVERY_TIMEOUT_MS = 2000;
  const uint8_t LIMITED_RECOVERY_ATTEMPTS = 2;
  uint32_t recoveryStartMs = millis();
  
  // 1. Tentative avec filtrage avancé
  float result = filteredTemperatureC();
  if (!isnan(result)) {
    // Succès: reset le compteur d'échecs température
    _consecutiveTempFailures = 0;
    return result;
  }
  
  // Échec: incrémenter le compteur température
  _consecutiveTempFailures++;
  
  // Log seulement les premiers échecs pour éviter spam
  if (_consecutiveTempFailures <= 3) {
    SENSOR_LOG_PRINTF("[AirSensor] Échec température %d/%d\n", _consecutiveTempFailures, MAX_CONSECUTIVE_FAILURES);
  }
  
  // Vérifier timeout avant récupération
  if ((millis() - recoveryStartMs) >= DHT_RECOVERY_TIMEOUT_MS) {
    SENSOR_LOG_PRINTLN("[AirSensor] Timeout avant récupération, utilise dernière valeur");
    goto use_last_valid;
  }
  
  SENSOR_LOG_PRINTLN("[AirSensor] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. Vérification de la connectivité (avec timeout)
  if ((millis() - recoveryStartMs) < DHT_RECOVERY_TIMEOUT_MS) {
    if (!isSensorConnected()) {
      SENSOR_LOG_PRINTLN("[AirSensor] Capteur non connecté, reset matériel...");
      resetSensor();
      
      // Nouvelle tentative après reset (si timeout pas atteint)
      if ((millis() - recoveryStartMs) < DHT_RECOVERY_TIMEOUT_MS) {
        result = filteredTemperatureC();
        if (!isnan(result)) {
          SENSOR_LOG_PRINTLN("[AirSensor] Récupération réussie après reset matériel");
          _consecutiveTempFailures = 0; // Reset sur succès
          return result;
        }
      }
    }
  }
  
  // 3. Tentative avec lecture simple répétée (limité par timeout total)
  for (uint8_t attempt = 0; attempt < LIMITED_RECOVERY_ATTEMPTS; ++attempt) {
    // Reset watchdog pendant récupération pour éviter timeout
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    
    // Vérifier timeout avant chaque tentative
    if ((millis() - recoveryStartMs) >= DHT_RECOVERY_TIMEOUT_MS) {
      SENSOR_LOG_PRINTF("[AirSensor] Timeout atteint après %u ms, arrêt récupération\n", 
                        millis() - recoveryStartMs);
      break;
    }
    
    SENSOR_LOG_PRINTF("[AirSensor] Tentative de récupération %d/%d...\n", attempt + 1, LIMITED_RECOVERY_ATTEMPTS);
    
    // Lecture simple avec délai (peut bloquer jusqu'à timeout interne DHT ~2s)
    // Note: _dht.readTemperature() a son propre timeout interne, mais peut prendre jusqu'à 7-8s si capteur défaillant
    float temp = _dht.readTemperature();
    
    // Reset watchdog après lecture pour éviter timeout
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    
    // Vérifier timeout après lecture (peut avoir pris du temps)
    if ((millis() - recoveryStartMs) >= DHT_RECOVERY_TIMEOUT_MS) {
      SENSOR_LOG_PRINTLN("[AirSensor] Timeout après lecture, arrêt récupération");
      break;
    }
    
    if (!isnan(temp) && temp >= SensorConfig::AirSensor::TEMP_MIN && temp <= SensorConfig::AirSensor::TEMP_MAX) {
      SENSOR_LOG_PRINTF("[AirSensor] Récupération réussie: %.1f°C\n", temp);
      _consecutiveTempFailures = 0; // Reset sur succès
      return temp;
    }
    
    // Délai entre tentatives (seulement si timeout pas atteint)
    if ((millis() - recoveryStartMs) < (DHT_RECOVERY_TIMEOUT_MS - RECOVERY_DELAY_MS)) {
      vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
    }
  }
  
  // 4. Utilisation de la dernière valeur valide si disponible
use_last_valid:
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
    SENSOR_LOG_PRINTF("[AirSensor] 🔴 DHT désactivé après %d échecs (temp:%d, hum:%d) (utilise valeur par défaut: %.1f°C)\n",
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

float AirSensor::temperatureC() { return _dht.readTemperature(); }
float AirSensor::humidity() { return _dht.readHumidity(); }

float AirSensor::filteredTemperatureC() {
  // Throttle: une seule lecture toutes les 2s, lissage EMA
  unsigned long now = millis();
  if (_lastDhtReadMs != 0 && (now - _lastDhtReadMs) < SensorConfig::DHT::MIN_READ_INTERVAL_MS) {
    return _emaInit ? _emaTemp : NAN;
  }
  _lastDhtReadMs = now;
  
  float temp = _dht.readTemperature();
  if (isnan(temp) || temp < SensorConfig::AirSensor::TEMP_MIN || temp > SensorConfig::AirSensor::TEMP_MAX) {
    return _emaInit ? _emaTemp : NAN;
  }
  if (!_emaInit) {
    _emaTemp = temp;
    _emaInit = true;
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
  if (_lastDhtReadMs != 0 && (now - _lastDhtReadMs) < SensorConfig::DHT::MIN_READ_INTERVAL_MS) {
    return _emaInit ? _emaHumidity : NAN;
  }
  _lastDhtReadMs = now;
  
  float h = _dht.readHumidity();
  if (isnan(h) || h < SensorConfig::AirSensor::HUMIDITY_MIN || h > SensorConfig::AirSensor::HUMIDITY_MAX) {
    return _emaInit ? _emaHumidity : NAN;
  }
  if (!_emaInit) {
    _emaHumidity = h;
    _emaInit = true;
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
  
  // Timeout total pour éviter blocage: 1500ms (réduit de 2000ms - appelé après robustTemperatureC(), timeout plus court)
  const uint32_t DHT_RECOVERY_TIMEOUT_MS = 1500;
  const uint8_t LIMITED_RECOVERY_ATTEMPTS = 2;
  uint32_t recoveryStartMs = millis();
  
  // 1. Tentative avec filtrage avancé
  float result = filteredHumidity();
  if (!isnan(result)) {
    // Succès: reset le compteur d'échecs humidité
    _consecutiveHumidityFailures = 0;
    return result;
  }
  
  // Échec: incrémenter le compteur humidité
  _consecutiveHumidityFailures++;
  
  // Log seulement les premiers échecs pour éviter spam
  if (_consecutiveHumidityFailures <= 3) {
    SENSOR_LOG_PRINTF("[AirSensor] Échec humidité %d/%d\n", _consecutiveHumidityFailures, MAX_CONSECUTIVE_FAILURES);
  }
  
  // Vérifier timeout avant récupération
  if ((millis() - recoveryStartMs) >= DHT_RECOVERY_TIMEOUT_MS) {
    SENSOR_LOG_PRINTLN("[AirSensor] Timeout avant récupération humidité, utilise dernière valeur");
    goto use_last_valid_humidity;
  }
  
  SENSOR_LOG_PRINTLN("[AirSensor] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. Vérification de la connectivité (avec timeout)
  if ((millis() - recoveryStartMs) < DHT_RECOVERY_TIMEOUT_MS) {
    if (!isSensorConnected()) {
      SENSOR_LOG_PRINTLN("[AirSensor] Capteur non connecté, reset matériel...");
      resetSensor();
      
      // Nouvelle tentative après reset (si timeout pas atteint)
      if ((millis() - recoveryStartMs) < DHT_RECOVERY_TIMEOUT_MS) {
        result = filteredHumidity();
        if (!isnan(result)) {
          SENSOR_LOG_PRINTLN("[AirSensor] Récupération réussie après reset matériel");
          _consecutiveHumidityFailures = 0; // Reset sur succès
          return result;
        }
      }
    }
  }
  
  // 3. Tentative avec lecture simple répétée (limité par timeout total)
  for (uint8_t attempt = 0; attempt < LIMITED_RECOVERY_ATTEMPTS; ++attempt) {
    // Reset watchdog pendant récupération pour éviter timeout
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    
    // Vérifier timeout avant chaque tentative
    if ((millis() - recoveryStartMs) >= DHT_RECOVERY_TIMEOUT_MS) {
      SENSOR_LOG_PRINTF("[AirSensor] Timeout atteint après %u ms, arrêt récupération humidité\n", 
                        millis() - recoveryStartMs);
      break;
    }
    
    SENSOR_LOG_PRINTF("[AirSensor] Tentative de récupération %d/%d...\n", attempt + 1, LIMITED_RECOVERY_ATTEMPTS);
    
    // Lecture simple avec délai (peut bloquer jusqu'à timeout interne DHT ~2s)
    float humidity = _dht.readHumidity();
    
    // Reset watchdog après lecture pour éviter timeout
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    
    // Vérifier timeout après lecture
    if ((millis() - recoveryStartMs) >= DHT_RECOVERY_TIMEOUT_MS) {
      SENSOR_LOG_PRINTLN("[AirSensor] Timeout après lecture humidité, arrêt récupération");
      break;
    }
    
    if (!isnan(humidity) && humidity >= SensorConfig::AirSensor::HUMIDITY_MIN && humidity <= SensorConfig::AirSensor::HUMIDITY_MAX) {
      SENSOR_LOG_PRINTF("[AirSensor] Récupération réussie: %.1f%%\n", humidity);
      _consecutiveHumidityFailures = 0; // Reset sur succès
      return humidity;
    }
    
    // Délai entre tentatives (seulement si timeout pas atteint)
    if ((millis() - recoveryStartMs) < (DHT_RECOVERY_TIMEOUT_MS - RECOVERY_DELAY_MS)) {
      vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
    }
  }
  
  // 4. Utilisation de la dernière valeur valide si disponible
use_last_valid_humidity:
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
    SENSOR_LOG_PRINTF("[AirSensor] 🔴 DHT désactivé après %d échecs (temp:%d, hum:%d) (utilise valeur par défaut: %.1f%%)\n",
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
  g_nvsManager.saveFloat(NVS_NAMESPACES::CONFIG, "temp_lastValid", temp);
  _lastSavedTempToNVS = temp;
  _lastNvsSaveMs = now;
  Serial.printf("[WaterTemp] Dernière température valide sauvegardée en NVS: %.1f°C\n", temp);
}

float WaterTempSensor::loadLastValidTempFromNVS() {
  float temp;
  g_nvsManager.loadFloat(NVS_NAMESPACES::CONFIG, "temp_lastValid", temp, NAN);
  
  if (!isnan(temp) && temp >= SensorConfig::WaterTemp::MIN_VALID && temp <= SensorConfig::WaterTemp::MAX_VALID) {
    Serial.printf("[WaterTemp] Dernière température valide chargée depuis NVS: %.1f°C\n", temp);
    return temp;
  } else {
    Serial.println("[WaterTemp] Aucune température valide trouvée en NVS");
    return NAN;
  }
} 