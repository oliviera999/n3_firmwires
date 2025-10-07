#pragma once
#include "sensors.h"
#include <stdint.h>

struct SensorReadings {
  float tempWater;
  float tempAir;
  float humidity;
  uint16_t wlPota;
  uint16_t wlAqua;
  uint16_t wlTank;
  uint16_t luminosite;
};

class SystemSensors {
 public:
  SystemSensors();
  void begin();
  SensorReadings read();
  // Calcul de la différence de marée à partir de la dernière mesure fournie
  int diffMaree(uint16_t currentAqua);
  
  // Configuration fenêtre d'analyse marée (~15s par défaut)
  void setTideWindowMs(uint32_t ms) { _tideWindowMs = ms; }
  uint32_t getTideWindowMs() const { return _tideWindowMs; }
  
  // Accesseurs pour la gestion de la marée
  uint16_t getAquaMax() const { return _aquaMax; }
  void resetAquaMax() { _aquaMax = 0; }
 private:
  UltrasonicManager _usAqua{Pins::ULTRASON_AQUA};
  UltrasonicManager _usTank{Pins::ULTRASON_TANK};
  UltrasonicManager _usPota{Pins::ULTRASON_POTA};
  AirSensor _air;
  WaterTempSensor _water;
  uint16_t _aquaMax{0};
  // Dernières valeurs valides (non nulles, non aberrantes) pour gestion fallback
  uint16_t _lastValidWlAqua{0};
  uint16_t _lastValidWlTank{0};
  
  // Historique wlAqua pour calcul du diff ~10s
  static constexpr uint8_t AQUA_HIST_SIZE = 16;
  uint16_t _aquaHist[AQUA_HIST_SIZE]{};
  uint32_t _aquaHistTime[AQUA_HIST_SIZE]{};
  uint8_t _aquaHistCount{0};
  uint8_t _aquaHistHead{0};
  uint32_t _tideWindowMs{15000};
  void pushAquaHist(uint16_t value, uint32_t nowMs);
  int diffMaree10s(uint16_t currentAqua, uint32_t nowMs) const;
}; 