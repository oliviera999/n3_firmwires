/**
 * n3_battery — Délègue à n3_analog_sensors pour la lecture ADC filtrée.
 */
#include "n3_battery.h"
#include "n3_analog_sensors.h"

static const uint16_t RAW_AT_100_PERCENT = 2100;
static const float    VOLTAGE_AT_FULL    = 4.2f;

N3BatteryResult n3BatteryRead(const N3BatteryConfig& config,
                              void* /*samples*/,
                              void* /*sampleIndex*/,
                              void* /*sampleTotal*/) {
  N3Analog::DividerConfig cfg = {};
  cfg.pin                 = config.pin;
  cfg.R1                   = config.R1;
  cfg.R2                   = config.R2;
  cfg.Vref                 = config.Vref;
  cfg.adcMax               = 4095;
  cfg.numSamples           = config.numSamples ? config.numSamples : (uint8_t)8;
  cfg.delayMs              = 1;
  cfg.rawAt100Percent      = RAW_AT_100_PERCENT;
  cfg.voltageAtFullScale    = VOLTAGE_AT_FULL;

  N3Analog::BatteryResult r = N3Analog::readBattery(cfg);
  N3BatteryResult res;
  res.rawAvg          = r.rawAvg;
  res.measuredVoltage = r.measuredVoltage;
  res.batteryVoltage = r.batteryVoltage;
  return res;
}
