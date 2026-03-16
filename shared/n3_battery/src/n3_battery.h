/**
 * n3_battery — API batterie pont diviseur (compatible n3pp, msp).
 * Implémentation déléguée à n3_analog_sensors.
 */
#pragma once

#include <Arduino.h>

struct N3BatteryConfig {
  uint8_t  pin;        /**< Broche ADC (ex. pontdiv) */
  uint32_t R1;         /**< Résistance haute (ohm) */
  uint32_t R2;         /**< Résistance basse (ohm) */
  float    Vref;       /**< Référence ADC (V) */
  uint8_t  numSamples; /**< Nombre d'échantillons */
};

struct N3BatteryResult {
  uint16_t rawAvg;         /**< Moyenne raw ADC */
  float    measuredVoltage; /**< Tension au diviseur (V) */
  float    batteryVoltage; /**< Tension batterie (V) */
};

/**
 * Lecture batterie filtrée (multi-échantillons).
 * samples, sampleIndex, sampleTotal conservés pour compatibilité API (non utilisés).
 */
N3BatteryResult n3BatteryRead(const N3BatteryConfig& config,
                              void* samples,
                              void* sampleIndex,
                              void* sampleTotal);
