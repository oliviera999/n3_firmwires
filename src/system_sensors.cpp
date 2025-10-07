#include "system_sensors.h"
#include <Arduino.h>
#include "log.h" // Ajouté pour LOG
#include <math.h> // ajout pour isnan
#include <esp_task_wdt.h> // Pour esp_task_wdt_reset()
#include "project_config.h"

SystemSensors::SystemSensors() {}

void SystemSensors::begin() {
  _air.begin();
  _water.begin();
  Serial.println(F("[Sensors] Initialisation terminée"));
}

SensorReadings SystemSensors::read() {
  SensorReadings r{};
  
  // Timeout global pour éviter les blocages (5 minutes max)
  const uint32_t GLOBAL_TIMEOUT_MS = 300000;
  uint32_t startTime = millis();
  uint32_t phaseStart;

  // Reset watchdog at the start of sensor reading
  esp_task_wdt_reset(); // Réactivé pour éviter les timeouts

  // --- Mesures sécurisées avec validation ---
  const uint8_t MAX_RETRIES = 3;

  // Température eau avec récupération ultra-robuste (0 – 60 °C attendue)
  {
    phaseStart = millis();
    esp_task_wdt_reset(); // Reset watchdog before water temperature reading
    
    // Utilise d'abord la méthode robuste standard
    float val = _water.robustTemperatureC();
    Serial.printf("[SystemSensors] ⏱️ Température eau: %u ms\n", millis() - phaseStart);
    
    // Si échec, utilise la méthode ultra-robuste
    if (isnan(val)) {
      Serial.println("[SystemSensors] Méthode robuste échouée, tentative ultra-robuste...");
      val = _water.ultraRobustTemperatureC();
    }
    
    // Validation finale renforcée pour s'assurer qu'aucune valeur aberrante n'est transmise
    if (isnan(val) || val < SensorConfig::WaterTemp::MIN_VALID || val > SensorConfig::WaterTemp::MAX_VALID) {
      Serial.printf("[SystemSensors] Température eau invalide finale: %.1f°C (plage: %.1f-%.1f°C), force NaN\n", 
                   val, SensorConfig::WaterTemp::MIN_VALID, SensorConfig::WaterTemp::MAX_VALID);
      r.tempWater = NAN;
    } else {
      // Validation supplémentaire : vérifier la cohérence avec la dernière valeur valide
      static float lastValidWaterTemp = NAN;
      if (!isnan(lastValidWaterTemp)) {
        float delta = fabs(val - lastValidWaterTemp);
        if (delta > 5.0f) { // Rejette si changement > 5°C entre lectures
          Serial.printf("[SystemSensors] Changement trop important détecté: %.1f°C -> %.1f°C (écart: %.1f°C), force NaN\n", 
                       lastValidWaterTemp, val, delta);
          r.tempWater = NAN;
        } else {
          r.tempWater = val;
          lastValidWaterTemp = val;
        }
      } else {
        r.tempWater = val;
        lastValidWaterTemp = val;
      }
    }
  }

  // Température air avec récupération robuste (+5 – 60 °C attendue)
  {
    phaseStart = millis();
    esp_task_wdt_reset(); // Reset watchdog before air temperature reading
    float val = _air.robustTemperatureC(); // Utilise la méthode robuste avec récupération
    Serial.printf("[SystemSensors] ⏱️ Température air: %u ms\n", millis() - phaseStart);
    
    // Validation finale pour s'assurer qu'aucune valeur négative n'est transmise
    if (isnan(val) || val <= SensorConfig::AirSensor::TEMP_MIN || val >= SensorConfig::AirSensor::TEMP_MAX) {
      Serial.printf("[SystemSensors] Température air invalide finale: %.1f°C, force NaN\n", val);
      r.tempAir = NAN;
    } else {
      r.tempAir = val;
    }
  }

  // Humidité avec récupération robuste (5 – 100 %)
  {
    phaseStart = millis();
    esp_task_wdt_reset(); // Reset watchdog before humidity reading
    float val = _air.robustHumidity(); // Utilise la méthode robuste avec récupération
    Serial.printf("[SystemSensors] ⏱️ Humidité: %u ms\n", millis() - phaseStart);
    
    // Validation finale pour s'assurer qu'aucune valeur invalide n'est transmise
    if (isnan(val) || val < SensorConfig::AirSensor::HUMIDITY_MIN || val > SensorConfig::AirSensor::HUMIDITY_MAX) {
      Serial.printf("[SystemSensors] Humidité invalide finale: %.1f%%, force NaN\n", val);
      r.humidity = NAN;
    } else {
      r.humidity = val;
    }
  }

  // Niveaux d'eau avec validation
  {
    phaseStart = millis();
    esp_task_wdt_reset(); // Reset watchdog before potager level reading
    uint16_t val = _usPota.readAdvancedFiltered();
    Serial.printf("[SystemSensors] ⏱️ Niveau potager: %u ms\n", millis() - phaseStart);
    if (val == 0 || val > 500) { // 0 = invalide, >500cm = aberrant
      Serial.printf("[SystemSensors] Niveau potager invalide: %u cm, force 0\n", val);
      r.wlPota = 0;
    } else {
      r.wlPota = val;
    }
  }
  
  {
    phaseStart = millis();
    esp_task_wdt_reset(); // Reset watchdog before aquarium level reading
    uint16_t val = _usAqua.readAdvancedFiltered();
    Serial.printf("[SystemSensors] ⏱️ Niveau aquarium: %u ms\n", millis() - phaseStart);
    bool valid = (val > 0 && val <= 500);
    if (!valid) {
      Serial.printf("[SystemSensors] Niveau aquarium invalide (%u), tentative de récupération...\n", val);
      // Tentative de récupération avec méthode simple
      val = _usAqua.readFiltered(3);
      valid = (val > 0 && val <= 500);
      if (valid) {
        Serial.printf("[SystemSensors] Récupération réussie: %u cm\n", val);
        r.wlAqua = val;
        _lastValidWlAqua = val;
      } else if (_lastValidWlAqua > 0) {
        Serial.printf("[SystemSensors] Fallback sur dernière valeur valide aquarium: %u cm\n", _lastValidWlAqua);
        r.wlAqua = _lastValidWlAqua;
      } else {
        Serial.printf("[SystemSensors] Récupération échouée, aucune valeur valide connue – utilise 0\n");
        r.wlAqua = 0;
      }
    } else {
      // Valeur valide
      r.wlAqua = val;
      _lastValidWlAqua = val;
    }
  }
  
  // Met à jour l'historique wlAqua pour calcul ~15s
  {
    uint32_t nowMs = millis();
    pushAquaHist(r.wlAqua, nowMs);
  }
  
  {
    phaseStart = millis();
    esp_task_wdt_reset(); // Reset watchdog before tank level reading
    uint16_t val = _usTank.readAdvancedFiltered();
    Serial.printf("[SystemSensors] ⏱️ Niveau réservoir: %u ms\n", millis() - phaseStart);
    bool valid = (val > 0 && val <= 500);
    if (!valid) {
      Serial.printf("[SystemSensors] Niveau réservoir invalide: %u cm\n", val);
      // Essai de récupération simple
      val = _usTank.readFiltered(3);
      valid = (val > 0 && val <= 500);
      if (valid) {
        Serial.printf("[SystemSensors] Récupération réservoir réussie: %u cm\n", val);
        r.wlTank = val;
        _lastValidWlTank = val;
      } else if (_lastValidWlTank > 0) {
        Serial.printf("[SystemSensors] Fallback sur dernière valeur valide réservoir: %u cm\n", _lastValidWlTank);
        r.wlTank = _lastValidWlTank;
      } else {
        Serial.printf("[SystemSensors] Récupération échouée, aucune valeur valide connue – réservoir=0\n");
        r.wlTank = 0;
      }
    } else {
      r.wlTank = val;
      _lastValidWlTank = val;
    }
  }
  
  // Luminosité avec validation
  {
    phaseStart = millis();
    esp_task_wdt_reset(); // Reset watchdog before luminosity reading
    uint32_t lumiSum = 0;
    const uint8_t NB_LUMI_SAMPLES = 12; // 12 échantillons pour réduire le bruit
    for (uint8_t i = 0; i < NB_LUMI_SAMPLES; ++i) {
      lumiSum += analogRead(Pins::LUMINOSITE);
      vTaskDelay(pdMS_TO_TICKS(1)); // 1 ms entre échantillons
    }
    uint16_t val = static_cast<uint16_t>(lumiSum / NB_LUMI_SAMPLES);
    Serial.printf("[SystemSensors] ⏱️ Luminosité: %u ms\n", millis() - phaseStart);
    
    // Validation de la luminosité (0-4095 pour ESP32 ADC 12-bit)
    if (val > 4095) {
      Serial.printf("[SystemSensors] Luminosité invalide: %u, force 0\n", val);
      r.luminosite = 0;
    } else {
      r.luminosite = val;
    }
  }
  

  // marée diff
  uint16_t oldAquaMax = _aquaMax;
  if (r.wlAqua > _aquaMax) {
    _aquaMax = r.wlAqua;
    Serial.printf("[Maree] Nouveau max: %u cm (précédent: %u cm)\n", _aquaMax, oldAquaMax);
  }

  // Vérification du timeout global et affichage du temps d'exécution
  uint32_t elapsed = millis() - startTime;
  if (elapsed > GLOBAL_TIMEOUT_MS) {
    Serial.printf("[SystemSensors] ⚠️ TIMEOUT GLOBAL: Lecture capteurs a pris %u ms (limite: %u ms)\n", elapsed, GLOBAL_TIMEOUT_MS);
  } else {
    Serial.printf("[SystemSensors] ✓ Lecture capteurs terminée en %u ms\n", elapsed);
  }

  // Reset watchdog at the end of sensor reading
  esp_task_wdt_reset();
  
  LOG(LOG_DEBUG, "Sensors TWater=%.1fC TAir=%.1fC Hum=%.1f%% wlA=%u wlT=%u wlP=%u Lux=%u", r.tempWater, r.tempAir, r.humidity, r.wlAqua, r.wlTank, r.wlPota, r.luminosite);

  return r;
}

int SystemSensors::diffMaree(uint16_t currentAqua) {
  // Nouveau calcul: différence par rapport à la valeur ~15s avant
  uint32_t nowMs = millis();
  int diff10s = diffMaree10s(currentAqua, nowMs);
  
  // Log détaillé du calcul de marée (15s)
  Serial.printf("[Maree] Calcul15s: actuel=%u, diff15s=%d cm\n", currentAqua, diff10s);
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