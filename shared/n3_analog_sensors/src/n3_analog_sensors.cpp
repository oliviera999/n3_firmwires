/**
 * n3_analog_sensors — Implémentation lecture ADC filtrée.
 */
#include "n3_analog_sensors.h"

#define N3_ANALOG_MAX_SAMPLES 16

namespace N3Analog {

void sortSamples(uint16_t* samples, uint8_t n) {
  for (uint8_t i = 1; i < n; ++i) {
    uint16_t v = samples[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && samples[j] > v) {
      samples[j + 1] = samples[j];
      --j;
    }
    samples[j + 1] = v;
  }
}

AnalogResult readFilteredAnalog(const AnalogConfig& config, float* emaPrev) {
  AnalogResult res = { config.fallback, false };
  const uint8_t n = (config.numSamples <= N3_ANALOG_MAX_SAMPLES) ? config.numSamples : N3_ANALOG_MAX_SAMPLES;
  if (n == 0) return res;

  uint16_t samples[N3_ANALOG_MAX_SAMPLES];
  for (uint8_t i = 0; i < n; ++i) {
    samples[i] = (uint16_t)analogRead(config.pin);
    if (config.delayMs > 0 && i < n - 1) {
      delay(config.delayMs);
    }
  }

  float fused = 0.0f;
  if (config.filterMode == MOYENNE) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < n; ++i) sum += samples[i];
    fused = (float)(sum / n);
  } else {
    sortSamples(samples, n);
    uint16_t median = samples[n / 2];
    if (config.filterMode == MEDIANE) {
      fused = (float)median;
    } else {
      // MEDIANE_PUIS_MOYENNE : rejet outliers puis moyenne
      uint32_t sum = 0;
      uint8_t count = 0;
      const uint16_t limit = config.outlierMax > 0 ? config.outlierMax : 255;
      for (uint8_t i = 0; i < n; ++i) {
        if ((uint16_t)abs((int)samples[i] - (int)median) <= limit) {
          sum += samples[i];
          ++count;
        }
      }
      fused = (count > 0) ? (float)(sum / count) : (float)median;
    }
  }

  uint16_t raw = (uint16_t)(fused + 0.5f);
  if (emaPrev && config.emaAlpha > 0.0f && config.emaAlpha <= 1.0f) {
    float prev = *emaPrev;
    if (prev < 0) prev = (float)raw;
    float ema = config.emaAlpha * (float)raw + (1.0f - config.emaAlpha) * prev;
    *emaPrev = ema;
    raw = (uint16_t)(ema + 0.5f);
  }

  res.value = raw;
  res.valid = (raw >= config.minValid && raw <= config.maxValid);
  if (!res.valid) res.value = config.fallback;
  return res;
}

BatteryResult readBattery(const DividerConfig& config) {
  BatteryResult res = { 0, 0.0f, 0.0f };
  const uint8_t n = (config.numSamples <= N3_ANALOG_MAX_SAMPLES) ? config.numSamples : N3_ANALOG_MAX_SAMPLES;
  if (n == 0) return res;

  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; ++i) {
    sum += (uint32_t)analogRead(config.pin);
    if (config.delayMs > 0 && i < n - 1) delay(config.delayMs);
  }
  uint16_t rawAvg = (uint16_t)(sum / n);
  res.rawAvg = rawAvg;

  float vAdc = (float)rawAvg * (config.Vref / (float)config.adcMax);
  res.measuredVoltage = vAdc * ((float)(config.R1 + config.R2) / (float)config.R2);
  res.batteryVoltage = (float)rawAvg * config.voltageAtFullScale / (float)config.rawAt100Percent;
  return res;
}

} // namespace N3Analog
