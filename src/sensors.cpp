#include "sensors.h"
#include <math.h> // Pour fabs()
#include <esp_task_wdt.h> // Pour esp_task_wdt_reset()
#include <Preferences.h> // Pour la persistance NVS
#include "project_config.h"

// -------- UltrasonicManager ---------
UltrasonicManager::UltrasonicManager(int pinTrigEcho) : _pinTrigEcho(pinTrigEcho),
                                                       _historyIndex(0), _historyCount(0), _lastValidDistance(0) {
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
      uint16_t cm = duration / ExtendedSensorConfig::ULTRASONIC_US_TO_CM_FACTOR; // Conversion µs -> cm (vitesse du son ~340 m/s)
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
    return 0;
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
    return 0;
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
  
  // Mise à jour de l'historique avec la nouvelle valeur
  _history[_historyIndex] = avgDistance;
  _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
  if (_historyCount < HISTORY_SIZE) _historyCount++;
  
  // Mise à jour dernière valeur valide
  _lastValidDistance = avgDistance;
  
  Serial.printf("[Ultrasonic] Mode réactif: %u cm (moyenne de %d lectures sur 3)\n", avgDistance, validReadings);
  return avgDistance;
}

uint16_t UltrasonicManager::readRobustFiltered() {
  // 1) Lecture A
  uint16_t a = readAdvancedFiltered();
  if (a == 0) {
    // Fallback lecture simple
    a = readFiltered(3);
  }
  if (a == 0) {
    return _lastValidDistance > 0 ? _lastValidDistance : 0;
  }
  
  // 2) Confirmation temporelle: lecture B courte pour confirmer un saut important
  vTaskDelay(pdMS_TO_TICKS(50));
  uint16_t b = readFiltered(3);
  if (b == 0) b = a; // si lecture courte échoue, ne bloque pas
  
  // 3) Si écart > MAX_DISTANCE_DELTA*2, exiger confirmation (proche de A)
  int delta = abs((int)a - (int)b);
  if (_lastValidDistance > 0 && abs((int)a - (int)_lastValidDistance) > (int)MAX_DISTANCE_DELTA*2) {
    if (delta > (int)MAX_DISTANCE_DELTA) {
      // Rejette saut non confirmé, conserve dernière valeur
      return _lastValidDistance;
    }
  }
  
  // 4) Mise à jour dernière valeur valide
  _lastValidDistance = a;
  return a;
}

void UltrasonicManager::resetHistory() {
  _historyIndex = 0;
  _historyCount = 0;
  _lastValidDistance = 0;
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _history[i] = 0;
  }
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
WaterTempSensor::WaterTempSensor() : _historyIndex(0), _historyCount(0), _lastValidTemp(NAN) {
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
  
  // Test initial de connectivité
  if (!isSensorConnected()) {
    Serial.println("[WaterTemp] ATTENTION: Capteur non détecté lors de l'initialisation");
  } else {
    Serial.printf("[WaterTemp] Capteur détecté et initialisé (résolution: %d bits, conversion: %d ms)\n", 
                  DS18B20_RESOLUTION, CONVERSION_DELAY_MS);
  }

  // Pré-charge pipeline de conversion
  _sensors.requestTemperatures();
  _lastRequestMs = millis();
  _pipelineReady = true;
}

bool WaterTempSensor::isSensorConnected() {
  // Reset du bus OneWire pour un test propre
  _oneWire.reset();
  vTaskDelay(pdMS_TO_TICKS(ONEWIRE_RESET_DELAY_MS));
  
  // Vérifie la présence du capteur sur le bus OneWire
  uint8_t addr[8];
  _oneWire.reset_search();
  
  // Tentative de recherche avec retry
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
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
      vTaskDelay(pdMS_TO_TICKS(ExtendedSensorConfig::SENSOR_READ_DELAY_MS)); // Délai court pour test
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
      vTaskDelay(pdMS_TO_TICKS(ExtendedSensorConfig::SENSOR_READ_DELAY_MS));
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
  
  // Reset de l'historique
  resetHistory();
  
  Serial.printf("[WaterTemp] Reset matériel terminé (résolution: %d bits)\n", DS18B20_RESOLUTION);
}

float WaterTempSensor::getTemperatureWithFallback() {
  // NOUVELLE MÉTHODE NON-BLOQUANTE (v11.50)
  // Timeout strict pour éviter tout blocage système
  
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
      _lastValidTemp = temp;
      saveLastValidTempToNVS(temp);
      return temp;
    }
  }
  
  // 2. Si pas prêt, lancer et retourner NaN immédiatement
  if (!_pipelineReady) {
    _sensors.requestTemperatures();
    _lastRequestMs = millis();
    _pipelineReady = true;
  }
  
  // 3. Fallback immédiat - utiliser dernière valeur valide ou valeur par défaut
  if (!isnan(_lastValidTemp)) {
    Serial.printf("[WaterTemp] Capteur défaillant, utilise dernière valeur valide: %.1f°C\n", _lastValidTemp);
    return _lastValidTemp;
  }
  
  Serial.printf("[WaterTemp] Capteur défaillant, utilise valeur par défaut: %.1f°C\n", DefaultValues::WATER_TEMP);
  return DefaultValues::WATER_TEMP;
}

float WaterTempSensor::robustTemperatureC() {
  // MÉTHODE DÉPRÉCIÉE - Utiliser getTemperatureWithFallback() à la place
  Serial.println("[WaterTemp] ⚠️ robustTemperatureC() dépréciée - utilise getTemperatureWithFallback()");
  return getTemperatureWithFallback();
}

float WaterTempSensor::ultraRobustTemperatureC() {
  // SIMPLIFICATION: Utilise la méthode simple avec valeur par défaut
  Serial.println("[WaterTemp] Lecture simplifiée (ultra-robuste dépréciée)...");
  
  float temp = getTemperatureWithFallback();
  
  // Si échec, retourner une valeur par défaut raisonnable
  if (isnan(temp)) {
    Serial.println("[WaterTemp] Lecture échouée, utilisation valeur par défaut: 20.0°C");
    return 20.0f; // Température par défaut pour l'aquaponie
  }
  
  return temp;
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
      vTaskDelay(pdMS_TO_TICKS(SensorSpecificConfig::DS18B20_STABILIZATION_DELAY_MS)); // Délai de stabilisation
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
    vTaskDelay(pdMS_TO_TICKS(SensorSpecificConfig::DS18B20_STABILIZATION_DELAY_MS)); // Délai de stabilisation
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
                        _humidityHistoryIndex(0), _humidityHistoryCount(0), _lastValidHumidity(NAN) {
  // Initialise l'historique avec des valeurs NaN
  for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
    _tempHistory[i] = NAN;
    _humidityHistory[i] = NAN;
  }
}

void AirSensor::begin() { 
  _dht.begin(); 
  vTaskDelay(pdMS_TO_TICKS(ExtendedSensorConfig::DHT_INIT_STABILIZATION_DELAY_MS)); // Délai de stabilisation après initialisation
  resetHistory();
  
  // Test initial de connectivité
  if (!isSensorConnected()) {
    SENSOR_LOG_PRINTLN("[AirSensor] ATTENTION: Capteur non détecté lors de l'initialisation");
  } else {
    SENSOR_LOG_PRINTLN("[AirSensor] Capteur détecté et initialisé");
  }
}

bool AirSensor::isSensorConnected() {
  // Test de lecture pour vérifier la connectivité du DHT
  // Reset watchdog une seule fois au début
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  
  float temp = _dht.readTemperature();
  // FIX: Délai minimum DHT augmenté à 2.5 secondes pour stabilité
  // Le DHT11/DHT22 nécessite au moins 2 secondes entre lectures selon datasheet
  vTaskDelay(pdMS_TO_TICKS(ExtendedSensorConfig::DHT_MIN_READ_INTERVAL_MS));
  
  float humidity = _dht.readHumidity();
  
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
  vTaskDelay(pdMS_TO_TICKS(ExtendedSensorConfig::DHT_INIT_STABILIZATION_DELAY_MS)); // Délai de stabilisation
  vTaskDelay(pdMS_TO_TICKS(SENSOR_RESET_DELAY_MS));
  
  // Reset de l'historique
  resetHistory();
  
  SENSOR_LOG_PRINTLN("[AirSensor] Reset matériel terminé");
}

float AirSensor::robustTemperatureC() {
  // 1. Tentative avec filtrage avancé
  float result = filteredTemperatureC();
  if (!isnan(result)) {
    return result;
  }
  
  SENSOR_LOG_PRINTLN("[AirSensor] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. Vérification de la connectivité
  if (!isSensorConnected()) {
    SENSOR_LOG_PRINTLN("[AirSensor] Capteur non connecté, reset matériel...");
    resetSensor();
    
    // Nouvelle tentative après reset
    result = filteredTemperatureC();
    if (!isnan(result)) {
      SENSOR_LOG_PRINTLN("[AirSensor] Récupération réussie après reset matériel");
      return result;
    }
  }
  
  // 3. Tentative avec lecture simple répétée (limité à 2 tentatives pour éviter les blocages)
  const uint8_t LIMITED_RECOVERY_ATTEMPTS = 2;
  for (uint8_t attempt = 0; attempt < LIMITED_RECOVERY_ATTEMPTS; ++attempt) {
    SENSOR_LOG_PRINTF("[AirSensor] Tentative de récupération %d/%d...\n", attempt + 1, LIMITED_RECOVERY_ATTEMPTS);
    
    
    // Lecture simple avec délai
    float temp = _dht.readTemperature();
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
    
    if (!isnan(temp) && temp >= SensorConfig::AirSensor::TEMP_MIN && temp <= SensorConfig::AirSensor::TEMP_MAX) {
      SENSOR_LOG_PRINTF("[AirSensor] Récupération réussie: %.1f°C\n", temp);
      return temp;
    }
    
    // Délai entre tentatives
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
  }
  
  // 4. Utilisation de la dernière valeur valide si disponible
  if (!isnan(_lastValidTemp)) {
    SENSOR_LOG_PRINTF("[AirSensor] Utilisation de la dernière valeur valide: %.1f°C\n", _lastValidTemp);
    return _lastValidTemp;
  }
  
  SENSOR_LOG_PRINTLN("[AirSensor] Échec de toutes les tentatives de récupération");
  return NAN;
}

float AirSensor::temperatureC() { return _dht.readTemperature(); }
float AirSensor::humidity() { return _dht.readHumidity(); }

float AirSensor::filteredTemperatureC() {
  // Throttle: une seule lecture toutes les 2s, lissage EMA
  unsigned long now = millis();
  if (_lastDhtReadMs != 0 && (now - _lastDhtReadMs) < ExtendedSensorConfig::DHT_MIN_READ_INTERVAL_MS) {
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
  if (_lastDhtReadMs != 0 && (now - _lastDhtReadMs) < ExtendedSensorConfig::DHT_MIN_READ_INTERVAL_MS) {
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
  // 1. Tentative avec filtrage avancé
  float result = filteredHumidity();
  if (!isnan(result)) {
    return result;
  }
  
  SENSOR_LOG_PRINTLN("[AirSensor] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. Vérification de la connectivité
  if (!isSensorConnected()) {
    SENSOR_LOG_PRINTLN("[AirSensor] Capteur non connecté, reset matériel...");
    resetSensor();
    
    // Nouvelle tentative après reset
    result = filteredHumidity();
    if (!isnan(result)) {
      SENSOR_LOG_PRINTLN("[AirSensor] Récupération réussie après reset matériel");
      return result;
    }
  }
  
  // 3. Tentative avec lecture simple répétée (limité à 2 tentatives pour éviter les blocages)
  const uint8_t LIMITED_RECOVERY_ATTEMPTS = 2;
  for (uint8_t attempt = 0; attempt < LIMITED_RECOVERY_ATTEMPTS; ++attempt) {
    SENSOR_LOG_PRINTF("[AirSensor] Tentative de récupération %d/%d...\n", attempt + 1, LIMITED_RECOVERY_ATTEMPTS);
    
    
    // Lecture simple avec délai
    float humidity = _dht.readHumidity();
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
    
    if (!isnan(humidity) && humidity >= SensorConfig::AirSensor::HUMIDITY_MIN && humidity <= SensorConfig::AirSensor::HUMIDITY_MAX) {
      SENSOR_LOG_PRINTF("[AirSensor] Récupération réussie: %.1f%%\n", humidity);
      return humidity;
    }
    
    // Délai entre tentatives
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
  }
  
  // 4. Utilisation de la dernière valeur valide si disponible
  if (!isnan(_lastValidHumidity)) {
    SENSOR_LOG_PRINTF("[AirSensor] Utilisation de la dernière valeur valide: %.1f%%\n", _lastValidHumidity);
    return _lastValidHumidity;
  }
  
  SENSOR_LOG_PRINTLN("[AirSensor] Échec de toutes les tentatives de récupération");
  return NAN;
}

void AirSensor::resetHistory() {
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
void WaterTempSensor::saveLastValidTempToNVS(float temp) {
  Preferences prefs;
  prefs.begin("waterTemp", false);
  prefs.putFloat("lastValid", temp);
  prefs.end();
  Serial.printf("[WaterTemp] Dernière température valide sauvegardée en NVS: %.1f°C\n", temp);
}

float WaterTempSensor::loadLastValidTempFromNVS() {
  Preferences prefs;
  prefs.begin("waterTemp", true);
  float temp = prefs.getFloat("lastValid", NAN);
  prefs.end();
  
  if (!isnan(temp) && temp >= SensorConfig::WaterTemp::MIN_VALID && temp <= SensorConfig::WaterTemp::MAX_VALID) {
    Serial.printf("[WaterTemp] Dernière température valide chargée depuis NVS: %.1f°C\n", temp);
    return temp;
  } else {
    Serial.println("[WaterTemp] Aucune température valide trouvée en NVS");
    return NAN;
  }
} 