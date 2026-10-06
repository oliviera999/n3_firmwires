/**
 * n3_ina226 — Driver I2C minimal du moniteur de puissance INA226 (Arduino TwoWire).
 *
 * Fin : les acces bus sont ici, toute la logique (encodage CONFIG,
 * calibration, conversions, saturation, correction de gain) est dans
 * n3_ina226_math (pur, teste en natif).
 *
 * Robustesse banc / carte n3-universal :
 *  - probe() verifie Manufacturer ID 0x5449 et Die ID 0x226x (evite de
 *    confondre avec un autre composant a la meme adresse) ;
 *  - configLost() relit CONFIG et CAL : si le module a ete de-alimente
 *    (rail +3V3_SW coupe, faux contact), CAL est revenu a 0 et courant /
 *    puissance liraient 0 -> le firmware appelle restore() ;
 *  - P est calcule en V_bus * I (signe), le registre POWER de la puce etant
 *    non signe ; il reste lisible via readRegister() pour diagnostic.
 *
 * Reference : TI INA226 datasheet SBOS547A — https://www.ti.com/lit/ds/symlink/ina226.pdf
 */
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "n3_ina226_math.h"

struct N3Ina226Reading {
  bool ok;            // lecture I2C complete (bus + shunt + courant)
  float busV;         // tension bus (V)
  float shuntMv;      // tension shunt (mV, signee)
  float currentA;     // courant (A), gain et sens appliques
  float powerW;       // busV * currentA (W, signe)
  int16_t shuntRaw;   // registre brut 0x01 (diagnostic)
  int16_t currentRaw; // registre brut 0x04 (diagnostic)
  bool saturated;     // shunt proche de la pleine echelle (+/-81,92 mV)
  bool overflow;      // bit OVF (debordement du calcul courant/puissance)
};

class N3Ina226Device {
 public:
  /** Associe le bus et l'adresse (0x40..0x4F). Aucun acces I2C. */
  void begin(TwoWire& wire, uint8_t address);

  /** Presence + identite (Manufacturer 0x5449, Die 0x226x). */
  bool probe();

  /**
   * Applique moyennage / temps de conversion (mode continu bus+shunt) et la
   * calibration shunt. Memorise les reglages pour restore().
   */
  bool setup(uint16_t avgSamples, uint32_t conversionUs, float shuntOhm, float maxCurrentA);

  /** Reecrit CONFIG + CAL memorises (apres perte d'alimentation du module). */
  bool restore();

  /** true si CONFIG ou CAL relus different de ceux attendus (ou I2C KO). */
  bool configLost();

  /** Lecture complete d'une mesure (dernier resultat converti par la puce). */
  bool read(N3Ina226Reading& out);

  /** Reset logiciel (bit RST) : la puce reprend ses valeurs par defaut. */
  bool softReset();

  /** Alerte materielle sous-tension bus (broche ALERT, si cablee). */
  bool setBusUnderVoltageAlert(float volts);

  bool readRegister(uint8_t reg, uint16_t& value);
  bool writeRegister(uint8_t reg, uint16_t value);

  void setGain(float gain) { gain_ = gain; }
  float gain() const { return gain_; }
  void setInvert(bool invert) { invert_ = invert; }
  bool invert() const { return invert_; }

  uint8_t address() const { return address_; }
  uint16_t configWord() const { return config_; }
  const N3Ina226::Calibration& calibration() const { return calibration_; }
  float shuntOhm() const { return shuntOhm_; }
  float maxCurrentA() const { return maxCurrentA_; }
  uint32_t i2cErrors() const { return i2cErrors_; }

 private:
  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0x40;
  uint16_t config_ = N3Ina226::kConfigDefault;
  N3Ina226::Calibration calibration_ = {};
  float shuntOhm_ = 0.1f;
  float maxCurrentA_ = 0.8f;
  float gain_ = 1.0f;
  bool invert_ = false;
  uint32_t i2cErrors_ = 0;
};
