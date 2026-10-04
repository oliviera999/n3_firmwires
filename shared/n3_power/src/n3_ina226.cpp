#include "n3_ina226.h"

using namespace N3Ina226;

void N3Ina226Device::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
}

bool N3Ina226Device::writeRegister(uint8_t reg, uint16_t value) {
  if (wire_ == nullptr) return false;
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(static_cast<uint8_t>(value >> 8));
  wire_->write(static_cast<uint8_t>(value & 0xFF));
  if (wire_->endTransmission() != 0) {
    ++i2cErrors_;
    return false;
  }
  return true;
}

bool N3Ina226Device::readRegister(uint8_t reg, uint16_t& value) {
  if (wire_ == nullptr) return false;
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission() != 0) {
    ++i2cErrors_;
    return false;
  }
  if (wire_->requestFrom(address_, static_cast<uint8_t>(2)) != 2) {
    ++i2cErrors_;
    return false;
  }
  const uint8_t hi = static_cast<uint8_t>(wire_->read());
  const uint8_t lo = static_cast<uint8_t>(wire_->read());
  value = static_cast<uint16_t>((hi << 8) | lo);
  return true;
}

bool N3Ina226Device::probe() {
  uint16_t manufacturer = 0;
  uint16_t die = 0;
  if (!readRegister(kRegManufacturerId, manufacturer)) return false;
  if (!readRegister(kRegDieId, die)) return false;
  return manufacturer == kManufacturerIdTi && (die & kDieIdMask) == kDieIdIna226;
}

bool N3Ina226Device::setup(uint16_t avgSamples, uint32_t conversionUs, float shuntOhm,
                           float maxCurrentA) {
  const uint8_t ct = conversionCodeForUs(conversionUs);
  config_ = encodeConfig(avgCodeForSamples(avgSamples), ct, ct, kModeShuntBusContinuous);
  shuntOhm_ = shuntOhm;
  maxCurrentA_ = maxCurrentA;
  calibration_ = computeCalibration(shuntOhm, maxCurrentA);
  if (!calibration_.ok) return false;
  return restore();
}

bool N3Ina226Device::restore() {
  if (!calibration_.ok) return false;
  const bool cfgOk = writeRegister(kRegConfig, config_);
  const bool calOk = writeRegister(kRegCalibration, calibration_.cal);
  return cfgOk && calOk;
}

bool N3Ina226Device::configLost() {
  uint16_t cfg = 0;
  uint16_t cal = 0;
  if (!readRegister(kRegConfig, cfg)) return true;
  if (!readRegister(kRegCalibration, cal)) return true;
  // Bit RST toujours relu a 0 : comparaison directe du mot complet.
  return cfg != config_ || cal != calibration_.cal;
}

bool N3Ina226Device::read(N3Ina226Reading& out) {
  out = {};
  uint16_t shunt = 0;
  uint16_t bus = 0;
  uint16_t current = 0;
  uint16_t mask = 0;
  if (!readRegister(kRegShuntVoltage, shunt) || !readRegister(kRegBusVoltage, bus) ||
      !readRegister(kRegCurrent, current)) {
    return false;
  }
  // MASK/ENABLE : facultatif (la lecture acquitte aussi CVRF).
  if (readRegister(kRegMaskEnable, mask)) {
    out.overflow = (mask & kMaskMathOverflow) != 0;
  }
  out.shuntRaw = static_cast<int16_t>(shunt);
  out.currentRaw = static_cast<int16_t>(current);
  out.busV = busVoltageV(bus);
  out.shuntMv = shuntVoltageMv(out.shuntRaw);
  const float rawCurrent = currentFromRegisterA(out.currentRaw, calibration_.currentLsbA);
  out.currentA = applyCurrentCorrection(rawCurrent, gain_, invert_);
  out.powerW = out.busV * out.currentA;
  out.saturated = isShuntSaturated(out.shuntRaw);
  out.ok = true;
  return true;
}

bool N3Ina226Device::softReset() { return writeRegister(kRegConfig, kConfigResetBit); }

bool N3Ina226Device::setBusUnderVoltageAlert(float volts) {
  if (!(volts > 0.0f)) return writeRegister(kRegMaskEnable, 0);
  const float limit = volts / kBusLsbV;
  const uint16_t raw = static_cast<uint16_t>(limit > 32767.0f ? 32767.0f : limit);
  return writeRegister(kRegAlertLimit, raw) && writeRegister(kRegMaskEnable, kMaskBusUnderVoltage);
}
