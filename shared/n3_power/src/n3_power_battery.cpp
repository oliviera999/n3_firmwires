#include "n3_power_battery.h"

#include <math.h>

namespace N3Power {

// AGM 12 V, tension de repos (>= quelques heures sans courant) a ~25 °C.
const OcvPoint kAgm12vOcvTable[] = {
    {11.80f, 0.0f},  {11.95f, 10.0f}, {12.05f, 20.0f}, {12.15f, 30.0f},
    {12.25f, 40.0f}, {12.35f, 50.0f}, {12.45f, 60.0f}, {12.55f, 70.0f},
    {12.65f, 80.0f}, {12.75f, 90.0f}, {12.85f, 100.0f},
};
const size_t kAgm12vOcvTableSize = sizeof(kAgm12vOcvTable) / sizeof(kAgm12vOcvTable[0]);

namespace {
float clampSoc(float soc) {
  if (!(soc >= 0.0f)) return 0.0f;  // NaN -> 0
  return soc > 100.0f ? 100.0f : soc;
}
}  // namespace

float ocvToSoc(float volts, const OcvPoint* table, size_t size) {
  if (table == nullptr || size == 0) return -1.0f;
  if (volts <= table[0].volts) return table[0].socPercent;
  if (volts >= table[size - 1].volts) return table[size - 1].socPercent;
  for (size_t i = 1; i < size; ++i) {
    if (volts <= table[i].volts) {
      const float v0 = table[i - 1].volts;
      const float v1 = table[i].volts;
      const float s0 = table[i - 1].socPercent;
      const float s1 = table[i].socPercent;
      if (v1 <= v0) return s1;  // table degeneree : point suivant
      return s0 + (s1 - s0) * (volts - v0) / (v1 - v0);
    }
  }
  return table[size - 1].socPercent;
}

LeadAcidSoc::LeadAcidSoc(const LeadAcidConfig& config) : cfg_(config) {}

void LeadAcidSoc::seed(float socPercent) {
  soc_ = clampSoc(socPercent);
  seeded_ = true;
  seededFromOcv_ = false;
}

void LeadAcidSoc::update(float voltageV, float currentA, double dAh, float dtSeconds) {
  if (!seeded_) {
    const float s = ocvToSoc(voltageV, cfg_.ocvTable, cfg_.ocvTableSize);
    if (s < 0.0f) return;
    soc_ = clampSoc(s);
    seeded_ = true;
    seededFromOcv_ = true;
  }

  // Comptage coulometrique (rendement applique a la seule charge)
  netAh_ += dAh;
  if (cfg_.capacityAh > 0.0f) {
    const double eff = dAh > 0.0 ? static_cast<double>(cfg_.chargeEfficiency) : 1.0;
    soc_ = clampSoc(soc_ + static_cast<float>(100.0 * dAh * eff / cfg_.capacityAh));
  }

  // Detection de repos puis recalage OCV (une fois par periode de repos)
  if (fabsf(currentA) < cfg_.restCurrentA) {
    if (dtSeconds > 0.0f) restAccumS_ += dtSeconds;
  } else {
    restAccumS_ = 0.0f;
    resyncedThisRest_ = false;
  }
  if (atRest() && !resyncedThisRest_) {
    const float s = ocvToSoc(voltageV, cfg_.ocvTable, cfg_.ocvTableSize);
    if (s >= 0.0f) {
      soc_ = clampSoc(s);
      seededFromOcv_ = false;  // OCV au repos = valeur fiable
      resyncedThisRest_ = true;
      ++resyncCount_;
    }
  }
}

LowBatteryAlert::LowBatteryAlert(const LowBatteryAlertConfig& config) : cfg_(config) {}

bool LowBatteryAlert::update(uint32_t nowMs, float voltageV, float currentA) {
  if (voltageV >= cfg_.rearmV) {
    armed_ = true;
    below_ = false;
    return false;
  }
  const bool charging = currentA > cfg_.chargingCurrentA;
  if (voltageV >= cfg_.thresholdV || charging) {
    below_ = false;  // remontee ou charge : la duree de maintien repart a zero
    return false;
  }
  if (!below_) {
    below_ = true;
    belowSinceMs_ = nowMs;
  }
  if (!armed_) return false;
  if (nowMs - belowSinceMs_ < cfg_.holdSeconds * 1000UL) return false;
  if (hasAlerted_ && nowMs - lastAlertMs_ < cfg_.cooldownSeconds * 1000UL) return false;
  armed_ = false;
  hasAlerted_ = true;
  lastAlertMs_ = nowMs;
  ++alertCount_;
  return true;
}

}  // namespace N3Power
