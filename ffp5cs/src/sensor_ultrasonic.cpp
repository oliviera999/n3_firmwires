// sensor_ultrasonic.cpp — implémentation UltrasonicManager (capteur niveau eau).
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

namespace {

namespace TankCfg = SensorConfig::Ultrasonic::Tank;

void sortU16(uint16_t* arr, uint8_t n) {
  for (uint8_t i = 0; i + 1 < n; ++i) {
    for (uint8_t j = i + 1; j < n; ++j) {
      if (arr[i] > arr[j]) {
        uint16_t t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
      }
    }
  }
}

// Cuve étroite : en cas de deux clusters (échos courts vs surface réelle),
// retourne la médiane du cluster le plus haut (distance = moins d'eau = sécurité).
uint16_t pickTankDistanceFromBatch(uint16_t* readings, uint8_t count) {
  if (count == 0) return 0;
  if (count == 1) return readings[0];

  sortU16(readings, count);

  uint8_t splitAt = 0;
  uint16_t maxGap = 0;
  for (uint8_t i = 0; i + 1 < count; ++i) {
    const uint16_t gap = readings[i + 1] - readings[i];
    if (gap > maxGap) {
      maxGap = gap;
      splitAt = i + 1;
    }
  }

  uint8_t start = 0;
  uint8_t clusterCount = count;
  if (maxGap >= TankCfg::BIMODAL_GAP_MM) {
    const uint8_t lowCount = splitAt;
    const uint8_t highCount = count - splitAt;
    const uint16_t lowMedian = readings[lowCount / 2];
    const uint16_t highMedian = readings[splitAt + highCount / 2];
    if (highMedian >= lowMedian) {
      start = splitAt;
      clusterCount = highCount;
    } else {
      clusterCount = lowCount;
    }
    Serial.printf("[Ultrasonic] Bimodal gap=%u mm, cluster haut start=%u n=%u\n",
                  maxGap, start, clusterCount);
  }

  uint16_t refined[TankCfg::SAMPLES_COUNT];
  uint8_t refinedCount = 0;
  const uint16_t clusterMedian = readings[start + clusterCount / 2];
  for (uint8_t i = start; i < start + clusterCount; ++i) {
    if (abs((int)readings[i] - (int)clusterMedian) <= (int)TankCfg::OUTLIER_SPREAD_MM) {
      refined[refinedCount++] = readings[i];
    }
  }
  if (refinedCount == 0) {
    return clusterMedian;
  }
  sortU16(refined, refinedCount);
  return refined[refinedCount / 2];
}

bool acceptTankJump(uint16_t candidate, uint16_t reference, uint8_t keptCount,
                    uint16_t batchSpread, bool expectDrain) {
  if (reference == 0) return true;

  const int delta = abs((int)candidate - (int)reference);
  const bool drainDirection = candidate > reference;
  const bool tightBatch = batchSpread <= TankCfg::TIGHT_BATCH_SPREAD_MM;
  const bool strongBatch = keptCount >= TankCfg::STRONG_BATCH_MIN_READINGS;

  if (drainDirection) {
    if (delta <= (int)TankCfg::DRAIN_MAX_DELTA_MM) return true;
    if (expectDrain && tightBatch && keptCount >= TankCfg::ADVANCED_MIN_VALID_READINGS) {
      return true;
    }
    if (strongBatch && tightBatch) return true;
    return false;
  }

  // Remplissage ou écho court : plus strict (évite faux « plein »)
  if (delta <= (int)TankCfg::REFILL_MAX_DELTA_MM && strongBatch && tightBatch) {
    return true;
  }
  return false;
}

}  // namespace

// -------- UltrasonicManager ---------
UltrasonicManager::UltrasonicManager(int pinTrigEcho, const char* sensorName) 
  : _pinTrigEcho(pinTrigEcho),
    _historyIndex(0), _historyCount(0), _lastValidDistance(0),
    _failureManager(sensorName, 10, 60000, 3),
    _timeoutCount(0) {  // v11.173: Init compteur timeout pour rate-limiting logs
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
      uint16_t mm = static_cast<uint16_t>((duration * 10UL + (SensorConfig::Ultrasonic::US_TO_CM_FACTOR / 2U)) /
                                          SensorConfig::Ultrasonic::US_TO_CM_FACTOR); // Conversion µs -> mm
      if (mm > MIN_DISTANCE && mm < MAX_DISTANCE) {
        total += mm;
        ++valid;
      }
    }

    // Délai entre mesures conforme datasheet HC-SR04 (cycle > 60 ms)
    if (i < samples - 1) {
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::Ultrasonic::MIN_DELAY_MS));
    }
  }
  
  if (samples > 1) {
    vTaskDelay(pdMS_TO_TICKS(SensorConfig::Ultrasonic::MIN_DELAY_MS));
  }

  return valid ? total / valid : 0;
}

uint16_t UltrasonicManager::readAdvancedFiltered() {
  // Si capteur désactivé, tester réactivation périodiquement
  if (_failureManager.isDisabled()) {
    if (_failureManager.shouldTestReactivation()) {
      // Test rapide de réactivation avec une seule lecture
      uint32_t testStart = millis();
      
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
      
      if (millis() - testStart > SensorConfig::Reactivation::ULTRASONIC_TIMEOUT_MS) {
        // Timeout - capteur toujours absent
        _failureManager.recordReactivationTestFailure();
        // Fallback sur dernière valeur valide ou 0
        return _lastValidDistance > 0 ? _lastValidDistance : 0;
      }
      
      if (duration > 0) {
        uint16_t testMm = static_cast<uint16_t>((duration * 10UL + (SensorConfig::Ultrasonic::US_TO_CM_FACTOR / 2U)) /
                                                SensorConfig::Ultrasonic::US_TO_CM_FACTOR);
        if (testMm >= MIN_DISTANCE && testMm < MAX_DISTANCE) {
          if (_failureManager.recordReactivationTestSuccess()) {
            // Capteur réactivé, retourner la valeur testée
            _lastValidDistance = testMm;
            return testMm;
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
  
  // Capteur actif — réservoir : lot étendu + détection bimodalité
  const uint8_t sampleCount = TankCfg::SAMPLES_COUNT;
  uint16_t readings[TankCfg::SAMPLES_COUNT];
  uint8_t validReadings = 0;

  const uint32_t TIMEOUT_US = SensorConfig::Ultrasonic::TIMEOUT_US;

  for (uint8_t i = 0; i < sampleCount; ++i) {
    pinMode(_pinTrigEcho, OUTPUT);
    digitalWrite(_pinTrigEcho, LOW);
    delayMicroseconds(2);
    digitalWrite(_pinTrigEcho, HIGH);
    delayMicroseconds(10);
    digitalWrite(_pinTrigEcho, LOW);

    pinMode(_pinTrigEcho, INPUT);
    unsigned long duration = readEchoPulseUs(TIMEOUT_US);

    if (duration > 0) {
      uint16_t mm = static_cast<uint16_t>((duration * 10UL + (SensorConfig::Ultrasonic::US_TO_CM_FACTOR / 2U)) /
                                          SensorConfig::Ultrasonic::US_TO_CM_FACTOR);

      if (TankCfg::isRawReadingMm(mm)) {
        readings[validReadings++] = mm;
        Serial.printf("[Ultrasonic] Lecture %d: %u mm\n", i + 1, mm);
      } else {
        Serial.printf("[Ultrasonic] Lecture %d rejetée: %u mm (hors plage réservoir %u-%u)\n",
                      i + 1, mm, TankCfg::MIN_RAW_MM, SensorConfig::Ultrasonic::MAX_DISTANCE_MM - 1);
      }
    } else {
      _timeoutCount++;
      if (_timeoutCount <= 3 || _timeoutCount % TIMEOUT_LOG_INTERVAL == 0) {
        Serial.printf("[Ultrasonic] Lecture %d timeout (total: %u)\n", i + 1, _timeoutCount);
      }
    }

    if (i < sampleCount - 1) {
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::Ultrasonic::MIN_DELAY_MS));
    }
  }

  if (sampleCount > 1) {
    vTaskDelay(pdMS_TO_TICKS(SensorConfig::Ultrasonic::MIN_DELAY_MS));
  }

  const uint8_t minValid = TankCfg::ADVANCED_MIN_VALID_READINGS;
  if (validReadings < minValid) {
    Serial.printf("[Ultrasonic] Pas assez de lectures valides (%d/%d), retourne fallback\n",
                  validReadings, minValid);
    _failureManager.recordFailure();
    return _lastValidDistance > 0 ? _lastValidDistance : 0;
  }

  uint16_t batchCopy[TankCfg::SAMPLES_COUNT];
  for (uint8_t i = 0; i < validReadings; ++i) {
    batchCopy[i] = readings[i];
  }
  const uint16_t candidateDistance = pickTankDistanceFromBatch(batchCopy, validReadings);

  sortU16(readings, validReadings);
  const uint16_t batchSpread = readings[validReadings - 1] - readings[0];

  if (!TankCfg::isOperationalMm(candidateDistance)) {
    Serial.printf("[Ultrasonic] Distance hors plage métier: %u mm (attendu %u-%u), fallback\n",
                  candidateDistance, TankCfg::MIN_OPERATIONAL_MM, TankCfg::MAX_OPERATIONAL_MM);
    _failureManager.recordFailure();
    return _lastValidDistance > 0 ? _lastValidDistance : 0;
  }

  uint16_t historyMedian = 0;
  if (_historyCount >= 3) {
    uint16_t histTemp[HISTORY_SIZE];
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
      if (_history[i] > 0) {
        histTemp[validCount++] = _history[i];
      }
    }
    if (validCount > 0) {
      sortU16(histTemp, validCount);
      historyMedian = histTemp[validCount / 2];
    }
  }

  const uint16_t referenceValue = (historyMedian > 0) ? historyMedian : _lastValidDistance;

  if (referenceValue > 0 &&
      abs((int)candidateDistance - (int)referenceValue) > (int)SensorConfig::Ultrasonic::MAX_DELTA_MM) {
    Serial.printf("[Ultrasonic] Saut détecté: %u mm -> %u mm (écart: %d mm, drain=%d)\n",
                  referenceValue, candidateDistance,
                  abs((int)candidateDistance - (int)referenceValue),
                  _expectTankDrain ? 1 : 0);

    if (!acceptTankJump(candidateDistance, referenceValue, validReadings, batchSpread, _expectTankDrain)) {
      uint8_t consensusCount = 0;
      for (uint8_t i = 0; i < HISTORY_SIZE && i < 3; ++i) {
        uint8_t idx = (_historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
        if (_history[idx] > 0 &&
            abs((int)_history[idx] - (int)candidateDistance) <= (int)TankCfg::OUTLIER_SPREAD_MM) {
          consensusCount++;
        }
      }
      if (consensusCount >= 2) {
        Serial.printf("[Ultrasonic] Consensus historique (%d/3), accepte nouvelle valeur\n", consensusCount);
      } else {
        Serial.printf("[Ultrasonic] Saut rejeté (spread=%u mm, n=%d), conserve référence\n",
                      batchSpread, validReadings);
        return referenceValue;
      }
    }
  }

  _failureManager.recordSuccess();

  _history[_historyIndex] = candidateDistance;
  _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
  if (_historyCount < HISTORY_SIZE) _historyCount++;

  _lastValidDistance = candidateDistance;

  Serial.printf("[Ultrasonic] Distance réservoir: %u mm (%d lectures, spread=%u mm, drain=%d)\n",
                candidateDistance, validReadings, batchSpread, _expectTankDrain ? 1 : 0);
  return candidateDistance;
}

uint16_t UltrasonicManager::readReactiveFiltered() {
  // Si capteur désactivé, tester réactivation périodiquement
  if (_failureManager.isDisabled()) {
    if (_failureManager.shouldTestReactivation()) {
      // Test rapide de réactivation avec une seule lecture
      uint32_t testStart = millis();
      
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
      
      if (millis() - testStart > SensorConfig::Reactivation::ULTRASONIC_TIMEOUT_MS) {
        // Timeout - capteur toujours absent
        _failureManager.recordReactivationTestFailure();
        // Fallback sur dernière valeur valide ou 0
        return _lastValidDistance > 0 ? _lastValidDistance : 0;
      }
      
      if (duration > 0) {
        uint16_t testMm = static_cast<uint16_t>((duration * 10UL + (SensorConfig::Ultrasonic::US_TO_CM_FACTOR / 2U)) /
                                                SensorConfig::Ultrasonic::US_TO_CM_FACTOR);
        if (testMm >= MIN_DISTANCE && testMm < MAX_DISTANCE) {
          if (_failureManager.recordReactivationTestSuccess()) {
            // Capteur réactivé, retourner la valeur testée
            _lastValidDistance = testMm;
            return testMm;
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
  // v11.41: Mode réactif - médiane de 5 lectures pour surface agitée (prod)
  uint16_t readings[REACTIVE_READINGS_COUNT];
  uint8_t validReadings = 0;
  
  const uint32_t TIMEOUT_US = SensorConfig::Ultrasonic::TIMEOUT_US;
  
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
      uint16_t mm = static_cast<uint16_t>((duration * 10UL + (SensorConfig::Ultrasonic::US_TO_CM_FACTOR / 2U)) /
                                          SensorConfig::Ultrasonic::US_TO_CM_FACTOR); // Conversion µs -> mm
      
      if (mm >= MIN_DISTANCE && mm < MAX_DISTANCE) {
        readings[validReadings++] = mm;
        Serial.printf("[Ultrasonic] Lecture réactive %d: %u mm\n", i+1, mm);
      } else {
        Serial.printf("[Ultrasonic] Lecture réactive %d rejetée: %u mm (hors plage)\n", i+1, mm);
      }
    } else {
      // v11.175: Rate-limiting amélioré des logs timeout
      // - Log les 3 premiers, puis tous les 10, puis arrêt après 100 (capteur absent)
      // - Log synthèse périodique toutes les 60s
      _timeoutCount++;
      if (_timeoutCount <= 3 || (_timeoutCount % TIMEOUT_LOG_INTERVAL == 0 && _timeoutCount <= 100)) {
        Serial.printf("[Ultrasonic] Lecture réactive %d timeout (total: %u)\n", i+1, _timeoutCount);
      }
      // Log synthèse périodique si beaucoup de timeouts
      static unsigned long lastTimeoutSummary = 0;
      unsigned long now = millis();
      if (_timeoutCount > 100 && (now - lastTimeoutSummary > 60000)) {
        Serial.printf("[Ultrasonic] Résumé: %u timeouts (capteur probablement absent)\n", _timeoutCount);
        lastTimeoutSummary = now;
      }
    }
    
    // Délai entre mesures conforme datasheet HC-SR04 (cycle > 60 ms)
    if (i < REACTIVE_READINGS_COUNT - 1) {
      vTaskDelay(pdMS_TO_TICKS(SensorConfig::Ultrasonic::MIN_DELAY_MS));
    }
  }
  
  if (validReadings < 1) {
    Serial.printf("[Ultrasonic] Pas assez de lectures réactives valides (%d/1), retourne 0\n", validReadings);
    _failureManager.recordFailure();
    return _lastValidDistance > 0 ? _lastValidDistance : 0;
  }
  
  // Tri + médiane (robuste à l'eau agitée)
  for (uint8_t i = 0; i < validReadings - 1; ++i) {
    for (uint8_t j = i + 1; j < validReadings; ++j) {
      if (readings[i] > readings[j]) {
        uint16_t t = readings[i];
        readings[i] = readings[j];
        readings[j] = t;
      }
    }
  }
  uint16_t medianDistance = readings[validReadings / 2];
  
  if (_lastValidDistance > 0) {
    int delta = abs((int)medianDistance - (int)_lastValidDistance);
    if (delta > 500) {
      Serial.printf("[Ultrasonic] Saut important détecté: %u -> %u mm (Δ=%d), accepte pour réactivité\n",
                   _lastValidDistance, medianDistance, delta);
    }
  }
  
  _failureManager.recordSuccess();
  _history[_historyIndex] = medianDistance;
  _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
  if (_historyCount < HISTORY_SIZE) _historyCount++;
  _lastValidDistance = medianDistance;
  
  Serial.printf("[Ultrasonic] Mode réactif: %u mm (médiane de %d lectures)\n", medianDistance, validReadings);
  return medianDistance;
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
