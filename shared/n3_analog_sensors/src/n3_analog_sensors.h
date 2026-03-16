/**
 * n3_analog_sensors — Lecture ADC filtrée commune (luminosité, pont diviseur, humidité sol).
 * Multi-échantillonnage, médiane, rejet d'outliers, EMA optionnel.
 * Utilisable par ffp5cs, n3pp, msp.
 */
#pragma once

#include <Arduino.h>

namespace N3Analog {

/** Mode de fusion des N échantillons */
enum FilterMode {
  MOYENNE,              /**< Moyenne arithmétique (comportement ffp5cs luminosité) */
  MEDIANE,              /**< Médiane (robuste aux pics) */
  MEDIANE_PUIS_MOYENNE  /**< Rejet outliers puis moyenne des restants */
};

/** Configuration lecture ADC filtrée (luminosité, humidité sol, etc.) */
struct AnalogConfig {
  uint8_t  pin;           /**< Broche ADC (ex. 34, 35) */
  uint8_t  numSamples;    /**< Nombre d'échantillons (recommandé 8–12) */
  uint8_t  delayMs;       /**< Délai en ms entre deux lectures */
  FilterMode filterMode;  /**< Moyenne, médiane ou médiane puis moyenne */
  uint16_t outlierMax;    /**< Écart max à la médiane pour rejet (0 = pas de rejet) */
  uint16_t minValid;      /**< Borne min valide (ex. 0) */
  uint16_t maxValid;      /**< Borne max valide (ex. 4095 pour ESP32 12-bit) */
  uint16_t fallback;      /**< Valeur retournée si invalide */
  float    emaAlpha;     /**< Coefficient EMA (0 = désactivé), ex. 0.3 */
};

/** Résultat d'une lecture ADC filtrée */
struct AnalogResult {
  uint16_t value;  /**< Valeur brute filtrée (0–4095) */
  bool     valid;  /**< true si dans [minValid, maxValid] */
};

/** Configuration pont diviseur (batterie) — compatible n3pp/msp */
struct DividerConfig {
  uint8_t  pin;           /**< Broche ADC */
  uint32_t R1;            /**< Résistance haute (ohm) */
  uint32_t R2;            /**< Résistance basse (ohm) */
  float    Vref;          /**< Référence ADC (V), ex. 3.3f */
  uint16_t adcMax;        /**< Max ADC (4095 pour ESP32 12-bit) */
  uint8_t  numSamples;    /**< Nombre d'échantillons */
  uint8_t  delayMs;       /**< Délai entre lectures (ms) */
  /** Mapping linéaire pour % batterie : raw 2100 -> 100%, formule 100 - (2100 - raw)*0.2 */
  uint16_t rawAt100Percent;      /**< Raw correspondant à 100 % (ex. 2100) */
  float    voltageAtFullScale;   /**< Tension à 100 % (ex. 4.2f) */
};

/** Résultat lecture batterie (remplace N3BatteryResult pour n3_battery) */
struct BatteryResult {
  uint16_t rawAvg;         /**< Moyenne raw ADC */
  float    measuredVoltage; /**< Tension au diviseur (V) depuis formule R1/R2 */
  float    batteryVoltage; /**< Tension batterie (V), ex. rawAvg * 4.2f / 2100 */
};

/**
 * Lecture ADC multi-échantillons avec filtrage (médiane/moyenne, rejet outliers).
 * @param config  Configuration (pin, N, délai, mode, plage, fallback).
 * @param emaPrev Pointeur vers float pour EMA entre appels (optionnel). Si non null, utilise et met à jour.
 * @return        AnalogResult (value, valid).
 */
AnalogResult readFilteredAnalog(const AnalogConfig& config, float* emaPrev = nullptr);

/**
 * Lecture pont diviseur (batterie) : raw filtré + tensions.
 * Remplace n3BatteryRead() pour n3pp et msp.
 */
BatteryResult readBattery(const DividerConfig& config);

/** Tri par insertion pour médiane (petit tableau) */
void sortSamples(uint16_t* samples, uint8_t n);

} // namespace N3Analog
