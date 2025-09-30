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
    // Reset watchdog before each measurement
    esp_task_wdt_reset();
    
    // Déclenchement
    pinMode(_pinTrigEcho, OUTPUT);
    digitalWrite(_pinTrigEcho, LOW);
    delayMicroseconds(2);
    digitalWrite(_pinTrigEcho, HIGH);
    delayMicroseconds(10);
    digitalWrite(_pinTrigEcho, LOW);

    // Lecture de l'écho (pin en entrée)
    pinMode(_pinTrigEcho, INPUT);
    delayMicroseconds(SETTLE_TIME_US);
    unsigned long duration = readEchoPulseUs(TIMEOUT_US);

    if (duration > 0) {
      uint16_t cm = duration / ExtendedSensorConfig::ULTRASONIC_US_TO_CM_FACTOR; // Conversion µs -> cm (vitesse du son ~340 m/s)
      if (cm > MIN_DISTANCE && cm < MAX_DISTANCE) {
        total += cm;
        ++valid;
      }
    }

    // Laisse respirer le CPU et respecte le temps entre ping pour éviter échos retardés
    vTaskDelay(pdMS_TO_TICKS(SensorConfig::Ultrasonic::DEFAULT_SAMPLES * 20));
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
    // Reset watchdog before each measurement
    esp_task_wdt_reset();
    
    // Déclenchement
    pinMode(_pinTrigEcho, OUTPUT);
    digitalWrite(_pinTrigEcho, LOW);
    delayMicroseconds(2);
    digitalWrite(_pinTrigEcho, HIGH);
    delayMicroseconds(10);
    digitalWrite(_pinTrigEcho, LOW);

    // Lecture de l'écho (pin en entrée)
    pinMode(_pinTrigEcho, INPUT);
    delayMicroseconds(SETTLE_TIME_US);
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
    
    // Reset watchdog before delay
    esp_task_wdt_reset();
    
    // Délai entre les mesures pour éviter les interférences
    vTaskDelay(pdMS_TO_TICKS(SensorConfig::Ultrasonic::DEFAULT_SAMPLES * 20));
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
  
  // CORRECTION : Détection de sauts brutaux assouplie
  if (_lastValidDistance > 0 && abs((int)medianDistance - (int)_lastValidDistance) > MAX_DISTANCE_DELTA) {
    // CORRECTION : Au lieu de retourner l'ancienne valeur, on accepte le changement si cohérent
    Serial.printf("[Ultrasonic] Saut détecté: %u cm -> %u cm (écart: %d cm)\n", 
                  _lastValidDistance, medianDistance, abs((int)medianDistance - (int)_lastValidDistance));
    
    // Si c'est un saut vers une valeur plus basse (niveau qui baisse), on l'accepte
    if (medianDistance < _lastValidDistance) {
      Serial.printf("[Ultrasonic] Saut vers le bas accepté (niveau qui baisse)\n");
    } else {
      // Si c'est un saut vers le haut, on vérifie la cohérence
      Serial.printf("[Ultrasonic] Saut vers le haut, utilise ancienne valeur par sécurité\n");
      return _lastValidDistance;
    }
  }
  
  // Historique glissant pour détection d'aberrations
  if (_historyCount >= 2) {
    uint32_t avgHistory = 0;
    uint8_t validHistory = 0;
    
    for (uint8_t i = 0; i < HISTORY_SIZE; ++i) {
      if (_history[i] > 0) {
        avgHistory += _history[i];
        validHistory++;
      }
    }
    
    if (validHistory > 0) {
      avgHistory /= validHistory;
      int deviation = abs((int)medianDistance - (int)avgHistory);
      
      // CORRECTION : Tolérance augmentée pour les variations de niveau d'eau
      if (deviation > MAX_DISTANCE_DELTA * 3) { // Augmenté de *2 à *3
        Serial.printf("[Ultrasonic] Écart important avec l'historique: %u cm (moyenne: %lu cm), utilise ancienne valeur\n", 
                      medianDistance, avgHistory);
        return _lastValidDistance;
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

float WaterTempSensor::robustTemperatureC() {
  // 1. Tentative avec filtrage avancé
  float result = filteredTemperatureC();
  if (!isnan(result)) {
    return result;
  }
  
  Serial.println("[WaterTemp] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. Vérification de la connectivité
  if (!isSensorConnected()) {
    Serial.println("[WaterTemp] Capteur non connecté, reset matériel...");
    resetSensor();
    
    // Nouvelle tentative après reset
    result = filteredTemperatureC();
    if (!isnan(result)) {
      Serial.println("[WaterTemp] Récupération réussie après reset matériel");
      return result;
    }
  }
  
  // 3. Tentative avec lecture simple répétée et reset matériel
  for (uint8_t attempt = 0; attempt < MAX_RECOVERY_ATTEMPTS; ++attempt) {
    Serial.printf("[WaterTemp] Tentative de récupération %d/%d...\n", attempt + 1, MAX_RECOVERY_ATTEMPTS);
    
    // Reset watchdog before recovery attempt
    esp_task_wdt_reset();
    
    // Reset matériel du bus OneWire avant chaque tentative
    _oneWire.reset();
    vTaskDelay(pdMS_TO_TICKS(ONEWIRE_RESET_DELAY_MS));
    
    // Réinitialisation de la bibliothèque DallasTemperature
    _sensors.begin();
    _sensors.setResolution(DS18B20_RESOLUTION);
    
    // Lecture simple avec délai de conversion approprié
    _sensors.requestTemperatures();
    vTaskDelay(pdMS_TO_TICKS(CONVERSION_DELAY_MS)); // Attendre la fin de la conversion
    float temp = _sensors.getTempCByIndex(0);
    
    if (!isnan(temp) && temp >= SensorConfig::WaterTemp::MIN_VALID && temp <= SensorConfig::WaterTemp::MAX_VALID) {
      Serial.printf("[WaterTemp] Récupération réussie: %.1f°C\n", temp);
      return temp;
    }
    
    // Reset watchdog before delay
    esp_task_wdt_reset();
    
    // Délai progressif entre tentatives (backoff)
    uint16_t delay = RECOVERY_DELAY_MS * (attempt + 1);
    vTaskDelay(pdMS_TO_TICKS(delay));
  }
  
  // 4. Utilisation de la dernière valeur valide si disponible
  if (!isnan(_lastValidTemp)) {
    Serial.printf("[WaterTemp] Utilisation de la dernière valeur valide: %.1f°C\n", _lastValidTemp);
    return _lastValidTemp;
  }
  
  Serial.println("[WaterTemp] Échec de toutes les tentatives de récupération");
  return NAN;
}

float WaterTempSensor::ultraRobustTemperatureC() {
  Serial.println("[WaterTemp] Démarrage lecture ultra-robuste...");
  
  // 1. Vérification de connectivité renforcée
  if (!isSensorConnected()) {
    Serial.println("[WaterTemp] Capteur non connecté, tentative de récupération...");
    resetSensor();
    
    // Nouvelle vérification après reset
    if (!isSensorConnected()) {
      Serial.println("[WaterTemp] Capteur toujours non connecté après reset");
      return NAN;
    }
  }
  
  // 2. Lecture avec validation croisée (3 séries de lectures)
  float seriesResults[3];
  uint8_t validSeries = 0;
  
  for (uint8_t series = 0; series < 3; ++series) {
    Serial.printf("[WaterTemp] Série de lecture %d/3...\n", series + 1);
    
    // Reset du bus avant chaque série
    _oneWire.reset();
    vTaskDelay(pdMS_TO_TICKS(ONEWIRE_RESET_DELAY_MS));
    
    // Lecture avec filtrage
    float result = filteredTemperatureC();
    
    if (!isnan(result)) {
      seriesResults[validSeries++] = result;
      Serial.printf("[WaterTemp] Série %d réussie: %.1f°C\n", series + 1, result);
    } else {
      Serial.printf("[WaterTemp] Série %d échouée\n", series + 1);
    }
    
    // Délai entre séries
    if (series < 2) {
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
  
  // 3. Validation croisée des résultats
  if (validSeries == 0) {
    Serial.println("[WaterTemp] Toutes les séries ont échoué");
    return NAN;
  }
  
  if (validSeries == 1) {
    Serial.printf("[WaterTemp] Une seule série réussie: %.1f°C\n", seriesResults[0]);
    return seriesResults[0];
  }
  
  // Calcul de la cohérence entre les séries
  float minTemp = seriesResults[0];
  float maxTemp = seriesResults[0];
  float sumTemp = seriesResults[0];
  
  for (uint8_t i = 1; i < validSeries; ++i) {
    if (seriesResults[i] < minTemp) minTemp = seriesResults[i];
    if (seriesResults[i] > maxTemp) maxTemp = seriesResults[i];
    sumTemp += seriesResults[i];
  }
  
  float avgTemp = sumTemp / validSeries;
  float spread = maxTemp - minTemp;
  
  Serial.printf("[WaterTemp] Validation croisée: %.1f°C (écart: %.1f°C, %d séries)\n", 
                avgTemp, spread, validSeries);
  
  // Accepte si l'écart est raisonnable (moins de 1°C) et dans la plage d'eau
  if (spread <= 1.0f && avgTemp >= SensorConfig::WaterTemp::MIN_VALID && avgTemp <= SensorConfig::WaterTemp::MAX_VALID) {
    Serial.printf("[WaterTemp] Lecture ultra-robuste réussie: %.1f°C\n", avgTemp);
    return avgTemp;
  } else {
    Serial.printf("[WaterTemp] Écart trop important entre séries (%.1f°C) ou hors plage, utilise médiane\n", spread);
    // Retourne la médiane des valeurs valides
    float medianTemp;
    if (validSeries == 2) {
      medianTemp = (seriesResults[0] + seriesResults[1]) / 2.0f;
    } else {
      // Tri pour trouver la médiane
      for (uint8_t i = 0; i < validSeries - 1; ++i) {
        for (uint8_t j = i + 1; j < validSeries; ++j) {
          if (seriesResults[i] > seriesResults[j]) {
            float temp = seriesResults[i];
            seriesResults[i] = seriesResults[j];
            seriesResults[j] = temp;
          }
        }
      }
      medianTemp = seriesResults[validSeries / 2];
    }
    
    // Validation finale de la médiane
    if (medianTemp >= SensorConfig::WaterTemp::MIN_VALID && medianTemp <= SensorConfig::WaterTemp::MAX_VALID) {
      return medianTemp;
    } else {
      Serial.printf("[WaterTemp] Médiane hors plage d'eau: %.1f°C, retourne NaN\n", medianTemp);
      return NAN;
    }
  }
}

float WaterTempSensor::temperatureC() {
  // Non-bloquant: si pipeline prêt et délai écoulé, lire
  esp_task_wdt_reset();
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
  // 0. Stabilisation minimale (option 2)
  for (uint8_t i = 0; i < STABILIZATION_READINGS; ++i) {
    esp_task_wdt_reset();
    _sensors.requestTemperatures();
    vTaskDelay(pdMS_TO_TICKS(CONVERSION_DELAY_MS));
    _sensors.getTempCByIndex(0);
    vTaskDelay(pdMS_TO_TICKS(READING_INTERVAL_MS));
  }
  
  // 1. Filtrage statistique avancé avec médiane et validation croisée
  float readings[READINGS_COUNT];
  uint8_t validReadings = 0;
  
  // Effectue plusieurs lectures avec validation croisée
  for (uint8_t i = 0; i < READINGS_COUNT; ++i) {
    esp_task_wdt_reset(); // Reset watchdog before each reading
    
    // Non-bloquant: vérifier si une conversion précédente est prête
    if (!_pipelineReady) {
      _sensors.requestTemperatures();
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
    
    // Reset watchdog before delay
    esp_task_wdt_reset();
    
    // Délai entre les mesures
    vTaskDelay(pdMS_TO_TICKS(READING_INTERVAL_MS));
  }
  
  // Vérifie qu'on a assez de lectures valides - seuil adaptatif
  uint8_t minRequired = MIN_VALID_READINGS;
  if (validReadings >= 2 && validReadings < MIN_VALID_READINGS) {
    // Si on a au moins 2 lectures cohérentes, on peut accepter avec un seuil plus bas
    minRequired = 2;
    Serial.printf("[WaterTemp] Seuil adaptatif: accepte %d lectures (minimum: %d)\n", validReadings, minRequired);
  }
  
  if (validReadings < minRequired) {
    Serial.printf("[WaterTemp] Pas assez de lectures valides (%d/%d), retourne NaN\n", validReadings, minRequired);
    return NAN;
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
      esp_task_wdt_reset();
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
      esp_task_wdt_reset();
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
#if defined(ENVIRONMENT_PROD_TEST)
                            DHT11  // wroom-test (ENVIRONMENT_PROD_TEST) : utilise DHT11
#else
                            DHT22  // Environnements de production et autres : utilise DHT22
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
  resetHistory();
  
  // Test initial de connectivité
  if (!isSensorConnected()) {
    Serial.println("[AirSensor] ATTENTION: Capteur non détecté lors de l'initialisation");
  } else {
    Serial.println("[AirSensor] Capteur détecté et initialisé");
  }
}

bool AirSensor::isSensorConnected() {
  // Test de lecture pour vérifier la connectivité du DHT
  float temp = _dht.readTemperature();
  // Respecter la fenêtre minimale avant une 2e lecture
  vTaskDelay(pdMS_TO_TICKS(ExtendedSensorConfig::SENSOR_READ_DELAY_MS));
  float humidity = _dht.readHumidity();
  
  // Vérifie si les lectures sont valides
  if (isnan(temp) && isnan(humidity)) {
    Serial.println("[AirSensor] Capteur DHT non détecté ou déconnecté");
    return false;
  }
  
  return true;
}

void AirSensor::resetSensor() {
  Serial.println("[AirSensor] Reset matériel du capteur...");
  
  // Reset de la bibliothèque DHT
  _dht.begin();
  vTaskDelay(pdMS_TO_TICKS(SENSOR_RESET_DELAY_MS));
  
  // Reset de l'historique
  resetHistory();
  
  Serial.println("[AirSensor] Reset matériel terminé");
}

float AirSensor::robustTemperatureC() {
  // 1. Tentative avec filtrage avancé
  float result = filteredTemperatureC();
  if (!isnan(result)) {
    return result;
  }
  
  Serial.println("[AirSensor] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. Vérification de la connectivité
  if (!isSensorConnected()) {
    Serial.println("[AirSensor] Capteur non connecté, reset matériel...");
    resetSensor();
    
    // Nouvelle tentative après reset
    result = filteredTemperatureC();
    if (!isnan(result)) {
      Serial.println("[AirSensor] Récupération réussie après reset matériel");
      return result;
    }
  }
  
  // 3. Tentative avec lecture simple répétée
  for (uint8_t attempt = 0; attempt < MAX_RECOVERY_ATTEMPTS; ++attempt) {
    Serial.printf("[AirSensor] Tentative de récupération %d/%d...\n", attempt + 1, MAX_RECOVERY_ATTEMPTS);
    
    // Lecture simple avec délai
    float temp = _dht.readTemperature();
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
    
    if (!isnan(temp) && temp > -40.0f && temp < 80.0f) {
      Serial.printf("[AirSensor] Récupération réussie: %.1f°C\n", temp);
      return temp;
    }
    
    // Délai entre tentatives
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
  }
  
  // 4. Utilisation de la dernière valeur valide si disponible
  if (!isnan(_lastValidTemp)) {
    Serial.printf("[AirSensor] Utilisation de la dernière valeur valide: %.1f°C\n", _lastValidTemp);
    return _lastValidTemp;
  }
  
  Serial.println("[AirSensor] Échec de toutes les tentatives de récupération");
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
  if (isnan(temp) || temp <= -40.0f || temp >= 80.0f) {
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
  if (isnan(h) || h < ExtendedSensorConfig::ValidationRanges::HUMIDITY_MIN || h > ExtendedSensorConfig::ValidationRanges::HUMIDITY_MAX) {
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
  
  Serial.println("[AirSensor] Filtrage avancé échoué, tentative de récupération...");
  
  // 2. Vérification de la connectivité
  if (!isSensorConnected()) {
    Serial.println("[AirSensor] Capteur non connecté, reset matériel...");
    resetSensor();
    
    // Nouvelle tentative après reset
    result = filteredHumidity();
    if (!isnan(result)) {
      Serial.println("[AirSensor] Récupération réussie après reset matériel");
      return result;
    }
  }
  
  // 3. Tentative avec lecture simple répétée
  for (uint8_t attempt = 0; attempt < MAX_RECOVERY_ATTEMPTS; ++attempt) {
    Serial.printf("[AirSensor] Tentative de récupération %d/%d...\n", attempt + 1, MAX_RECOVERY_ATTEMPTS);
    
    // Lecture simple avec délai
    float humidity = _dht.readHumidity();
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
    
    if (!isnan(humidity) && humidity >= ExtendedSensorConfig::ValidationRanges::HUMIDITY_MIN && humidity <= ExtendedSensorConfig::ValidationRanges::HUMIDITY_MAX) {
      Serial.printf("[AirSensor] Récupération réussie: %.1f%%\n", humidity);
      return humidity;
    }
    
    // Délai entre tentatives
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
  }
  
  // 4. Utilisation de la dernière valeur valide si disponible
  if (!isnan(_lastValidHumidity)) {
    Serial.printf("[AirSensor] Utilisation de la dernière valeur valide: %.1f%%\n", _lastValidHumidity);
    return _lastValidHumidity;
  }
  
  Serial.println("[AirSensor] Échec de toutes les tentatives de récupération");
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
  Serial.println("[AirSensor] Historique réinitialisé");
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