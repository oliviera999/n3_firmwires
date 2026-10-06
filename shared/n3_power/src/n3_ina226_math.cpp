#include "n3_ina226_math.h"

#include <math.h>

namespace N3Ina226 {

namespace {
constexpr uint16_t kAvgSamples[8] = {1, 4, 16, 64, 128, 256, 512, 1024};
constexpr uint32_t kConvUs[8] = {140, 204, 332, 588, 1100, 2116, 4156, 8244};
constexpr float kRegisterSpan = 32768.0f;  // 2^15 (registres signes 16 bits)
}  // namespace

uint16_t avgSamplesForCode(uint8_t code) { return kAvgSamples[code & 0x07]; }

uint8_t avgCodeForSamples(uint16_t samples) {
  uint8_t code = 0;
  for (uint8_t c = 0; c < 8; ++c) {
    if (kAvgSamples[c] <= samples) code = c;
  }
  return code;
}

uint32_t conversionTimeUs(uint8_t code) { return kConvUs[code & 0x07]; }

uint8_t conversionCodeForUs(uint32_t us) {
  uint8_t code = 0;
  for (uint8_t c = 0; c < 8; ++c) {
    if (kConvUs[c] <= us) code = c;
  }
  return code;
}

uint16_t encodeConfig(uint8_t avgCode, uint8_t busCtCode, uint8_t shuntCtCode, uint8_t mode) {
  return static_cast<uint16_t>(kConfigReservedBits | ((avgCode & 0x07u) << 9) |
                               ((busCtCode & 0x07u) << 6) | ((shuntCtCode & 0x07u) << 3) |
                               (mode & 0x07u));
}

ConfigFields decodeConfig(uint16_t config) {
  ConfigFields f;
  f.avgCode = static_cast<uint8_t>((config >> 9) & 0x07);
  f.busCtCode = static_cast<uint8_t>((config >> 6) & 0x07);
  f.shuntCtCode = static_cast<uint8_t>((config >> 3) & 0x07);
  f.mode = static_cast<uint8_t>(config & 0x07);
  return f;
}

uint32_t cycleTimeUs(uint16_t config) {
  const ConfigFields f = decodeConfig(config);
  return static_cast<uint32_t>(avgSamplesForCode(f.avgCode)) *
         (conversionTimeUs(f.busCtCode) + conversionTimeUs(f.shuntCtCode));
}

Calibration computeCalibration(float shuntOhm, float maxCurrentA) {
  Calibration c = {};
  if (!(shuntOhm > 0.0f) || !(maxCurrentA > 0.0f)) {
    return c;  // ok=false
  }
  c.shuntLimitA = kShuntFullScaleV / shuntOhm;
  c.imaxBeyondShunt = maxCurrentA > c.shuntLimitA;

  // LSB minimal (resolution maximale) pour couvrir Imax sur 15 bits.
  double lsb = static_cast<double>(maxCurrentA) / kRegisterSpan;
  double calExact = static_cast<double>(kCalibrationConstant) / (lsb * shuntOhm);
  if (calExact > kCalibrationMax) {
    // Shunt tres faible : CAL deborderait. On elargit le LSB pour tenir dans
    // 15 bits (la resolution est alors limitee par le shunt, pas par Imax).
    calExact = kCalibrationMax;
    c.calClamped = true;
  }
  const uint32_t cal = static_cast<uint32_t>(floor(calExact));
  if (cal == 0) {
    return c;  // Imax * R absurde (> ~168 V) : inexploitable
  }
  c.cal = static_cast<uint16_t>(cal);
  // LSB effectif : celui que la puce applique reellement avec le CAL tronque.
  lsb = static_cast<double>(kCalibrationConstant) / (static_cast<double>(cal) * shuntOhm);
  c.currentLsbA = static_cast<float>(lsb);
  c.powerLsbW = kPowerLsbFactor * c.currentLsbA;
  const float registerMaxA = 32767.0f * c.currentLsbA;
  c.measurableMaxA = registerMaxA < c.shuntLimitA ? registerMaxA : c.shuntLimitA;
  c.ok = true;
  return c;
}

float busVoltageV(uint16_t raw) { return static_cast<float>(raw & 0x7FFF) * kBusLsbV; }

float shuntVoltageMv(int16_t raw) { return static_cast<float>(raw) * kShuntLsbV * 1000.0f; }

float currentFromRegisterA(int16_t raw, float currentLsbA) {
  return static_cast<float>(raw) * currentLsbA;
}

float currentFromShuntA(float shuntMv, float shuntOhm) {
  if (!(shuntOhm > 0.0f)) return 0.0f;
  return (shuntMv / 1000.0f) / shuntOhm;
}

float powerFromRegisterW(uint16_t raw, float currentLsbA) {
  return static_cast<float>(raw) * kPowerLsbFactor * currentLsbA;
}

bool isShuntSaturated(int16_t shuntRaw) {
  return shuntRaw >= kShuntSaturationRaw || shuntRaw <= -kShuntSaturationRaw;
}

float applyCurrentCorrection(float currentA, float gain, bool invert) {
  const float corrected = currentA * gain;
  return invert ? -corrected : corrected;
}

float computeGainCorrection(float currentGain, float measuredA, float referenceA,
                            float minMeasuredA, float minGain, float maxGain, bool* ok) {
  if (ok != nullptr) *ok = false;
  if (fabsf(measuredA) < minMeasuredA) return currentGain;
  if ((measuredA > 0.0f) != (referenceA > 0.0f)) return currentGain;  // sens incoherent
  const float g = currentGain * (referenceA / measuredA);
  if (!(g >= minGain) || !(g <= maxGain)) return currentGain;
  if (ok != nullptr) *ok = true;
  return g;
}

bool isValidAddress(uint8_t addr) { return addr >= 0x40 && addr <= 0x4F; }

}  // namespace N3Ina226
