#pragma once

#include <Arduino.h>

struct N3BatteryConfig {
  int pin;
  float r1;
  float r2;
  float vRef;
  int numSamples;
};

struct N3BatteryResult {
  int rawAvg;
  float measuredVoltage;
  float batteryVoltage;
};

/**
 * Lit le pont diviseur, met a jour la moyenne mobile,
 * calcule la tension mesuree et la tension batterie.
 * samples[] doit etre un tableau de taille config.numSamples, initialise a 0.
 * sampleIndex et sampleTotal sont mis a jour par la fonction.
 */
N3BatteryResult n3BatteryRead(const N3BatteryConfig& config,
                              int* samples, int* sampleIndex, int* sampleTotal);
