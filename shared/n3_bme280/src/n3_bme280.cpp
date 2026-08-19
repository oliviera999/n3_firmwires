#include "n3_bme280.h"

#include <Wire.h>

namespace {
constexpr float TEMP_MIN_C = -40.0f;
constexpr float TEMP_MAX_C = 85.0f;
constexpr float HUM_MIN_PCT = 0.0f;
constexpr float HUM_MAX_PCT = 100.0f;
constexpr float PRESS_MIN_HPA = 300.0f;
constexpr float PRESS_MAX_HPA = 1100.0f;
// Comme ffp5cs (SensorConfig::BME280::INIT_STABILIZATION_DELAY_MS) : courte
// stabilisation après l'init — le rail capteurs vient d'être mis sous tension.
constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 100;
}  // namespace

bool N3Bme280::begin(uint8_t i2cAddress) {
  Wire.begin();  // idempotent (déjà fait par l'OLED le cas échéant)
  _ok = _bme.begin(i2cAddress, &Wire);
  if (_ok) {
    delay(INIT_STABILIZATION_DELAY_MS);
  }
  return _ok;
}

bool N3Bme280::read(float& tempC, float& humidityPct, float& pressureHpa) {
  if (!_ok) {
    return false;
  }
  const float t = _bme.readTemperature();
  const float h = _bme.readHumidity();
  const float p = _bme.readPressure() / 100.0f;  // Pa -> hPa
  if (isnan(t) || t < TEMP_MIN_C || t > TEMP_MAX_C ||
      isnan(h) || h < HUM_MIN_PCT || h > HUM_MAX_PCT ||
      isnan(p) || p < PRESS_MIN_HPA || p > PRESS_MAX_HPA) {
    return false;
  }
  tempC = t;
  humidityPct = h;
  pressureHpa = p;
  return true;
}
