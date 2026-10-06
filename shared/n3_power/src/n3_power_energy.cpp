#include "n3_power_energy.h"

namespace N3Power {

namespace {
constexpr double kMsPerHour = 3600000.0;
}  // namespace

ChannelAccumulator::ChannelAccumulator(uint32_t maxGapMs) : maxGapMs_(maxGapMs) {}

Increment ChannelAccumulator::add(uint32_t nowMs, float voltageV, float currentA, float powerW) {
  Increment inc = {};

  // Statistiques de fenetre
  if (samples_ == 0) {
    minV_ = maxV_ = voltageV;
    minI_ = maxI_ = currentA;
    minP_ = maxP_ = powerW;
  } else {
    if (voltageV < minV_) minV_ = voltageV;
    if (voltageV > maxV_) maxV_ = voltageV;
    if (currentA < minI_) minI_ = currentA;
    if (currentA > maxI_) maxI_ = currentA;
    if (powerW < minP_) minP_ = powerW;
    if (powerW > maxP_) maxP_ = powerW;
  }
  ++samples_;
  sumV_ += voltageV;
  sumI_ += currentA;
  sumP_ += powerW;

  // Integration trapezoidale avec la mesure precedente
  if (hasLast_) {
    const uint32_t dtMs = nowMs - lastMs_;  // soustraction non signee : debordement millis() OK
    if (dtMs > 0 && dtMs <= maxGapMs_) {
      const double hours = static_cast<double>(dtMs) / kMsPerHour;
      inc.integrated = true;
      inc.dtSeconds = static_cast<float>(dtMs) / 1000.0f;
      inc.wh = 0.5 * (static_cast<double>(lastP_) + powerW) * hours;
      inc.ah = 0.5 * (static_cast<double>(lastI_) + currentA) * hours;
      windowWh_ += inc.wh;
      windowAh_ += inc.ah;
      totalWh_ += inc.wh;
      totalAh_ += inc.ah;
    } else if (dtMs > maxGapMs_) {
      ++windowGaps_;
    }
  }
  hasLast_ = true;
  lastMs_ = nowMs;
  lastI_ = currentA;
  lastP_ = powerW;
  return inc;
}

void ChannelAccumulator::invalidate() { hasLast_ = false; }

WindowSnapshot ChannelAccumulator::peek() const {
  WindowSnapshot s = {};
  s.samples = samples_;
  s.gaps = windowGaps_;
  s.energyWh = windowWh_;
  s.chargeAh = windowAh_;
  if (samples_ == 0) return s;
  const double n = static_cast<double>(samples_);
  s.avgV = static_cast<float>(sumV_ / n);
  s.avgI = static_cast<float>(sumI_ / n);
  s.avgP = static_cast<float>(sumP_ / n);
  s.minV = minV_;
  s.maxV = maxV_;
  s.minI = minI_;
  s.maxI = maxI_;
  s.minP = minP_;
  s.maxP = maxP_;
  return s;
}

WindowSnapshot ChannelAccumulator::take() {
  const WindowSnapshot s = peek();
  resetWindow();
  return s;
}

void ChannelAccumulator::resetWindow() {
  samples_ = 0;
  sumV_ = sumI_ = sumP_ = 0.0;
  minV_ = maxV_ = minI_ = maxI_ = minP_ = maxP_ = 0.0f;
  windowWh_ = windowAh_ = 0.0;
  windowGaps_ = 0;
}

void ChannelAccumulator::resetTotals() { totalWh_ = totalAh_ = 0.0; }

}  // namespace N3Power
