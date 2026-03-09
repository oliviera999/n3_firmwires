#include "n3_battery.h"

static const float ADC_MAX = 4095.0f;

N3BatteryResult n3BatteryRead(const N3BatteryConfig& config,
                              int* samples, int* sampleIndex, int* sampleTotal) {
  N3BatteryResult result = {};

  *sampleTotal -= samples[*sampleIndex];
  samples[*sampleIndex] = analogRead(config.pin);
  *sampleTotal += samples[*sampleIndex];
  *sampleIndex = (*sampleIndex + 1) % config.numSamples;

  result.rawAvg = *sampleTotal / config.numSamples;
  result.measuredVoltage = (result.rawAvg / ADC_MAX) * config.vRef;
  result.batteryVoltage = result.measuredVoltage * ((config.r1 + config.r2) / config.r2);

  return result;
}
