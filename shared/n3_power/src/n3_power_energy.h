/**
 * n3_power_energy — Accumulation par voie de mesure (tension / courant / puissance).
 *
 * Logique pure (aucune dependance Arduino), testee en natif (test_power_energy) :
 *  - statistiques de fenetre (moyenne, min, max) pour l'envoi periodique ;
 *  - integration trapezoidale de la puissance (Wh) et du courant (Ah), signee
 *    (batterie : I > 0 = charge, I < 0 = decharge) ;
 *  - robustesse : horodatage en millisecondes 32 bits (debordement de millis()
 *    gere par soustraction non signee), intervalle nul ignore, trou de mesure
 *    > maxGap non integre (on n'invente pas d'energie) et compte, mesure
 *    invalide = rupture de continuite (invalidate()).
 *
 * Le firmware appelle add() a chaque mesure (1 Hz sur le banc energie), puis
 * take() a chaque envoi (10 s) : la fenetre repart a zero mais la continuite de
 * l'integration et les cumuls sont conserves.
 */
#pragma once

#include <stdint.h>

namespace N3Power {

/** Increment produit par une mesure (pour alimenter d'autres compteurs, ex. SoC). */
struct Increment {
  bool integrated;  // false : premiere mesure, dt nul ou trou > maxGap
  float dtSeconds;  // intervalle integre (0 si non integre)
  double wh;        // energie du pas (Wh, signee)
  double ah;        // charge du pas (Ah, signee)
};

/** Instantane d'une fenetre d'envoi. Champs nuls si samples == 0. */
struct WindowSnapshot {
  uint32_t samples;
  float avgV, minV, maxV;
  float avgI, minI, maxI;
  float avgP, minP, maxP;
  double energyWh;  // energie integree pendant la fenetre (signee)
  double chargeAh;  // charge integree pendant la fenetre (signee)
  uint32_t gaps;    // trous de mesure non integres pendant la fenetre
};

class ChannelAccumulator {
 public:
  /** maxGapMs : au-dela, l'intervalle n'est pas integre (defaut 5 s). */
  explicit ChannelAccumulator(uint32_t maxGapMs = 5000);

  /** Ajoute une mesure valide prise a nowMs (millis()). */
  Increment add(uint32_t nowMs, float voltageV, float currentA, float powerW);

  /** Mesure manquante / invalide : la prochaine add() ne s'integre pas au trou. */
  void invalidate();

  /** Instantane de la fenetre courante sans la remettre a zero. */
  WindowSnapshot peek() const;
  /** Instantane puis remise a zero de la fenetre (cumuls et continuite conserves). */
  WindowSnapshot take();

  /** Cumuls depuis le dernier resetTotals() (Wh / Ah signes). */
  double totalEnergyWh() const { return totalWh_; }
  double totalChargeAh() const { return totalAh_; }
  void resetTotals();

 private:
  void resetWindow();

  uint32_t maxGapMs_;
  // Continuite de l'integration
  bool hasLast_ = false;
  uint32_t lastMs_ = 0;
  float lastI_ = 0.0f;
  float lastP_ = 0.0f;
  // Fenetre
  uint32_t samples_ = 0;
  double sumV_ = 0.0, sumI_ = 0.0, sumP_ = 0.0;
  float minV_ = 0.0f, maxV_ = 0.0f;
  float minI_ = 0.0f, maxI_ = 0.0f;
  float minP_ = 0.0f, maxP_ = 0.0f;
  double windowWh_ = 0.0, windowAh_ = 0.0;
  uint32_t windowGaps_ = 0;
  // Cumuls
  double totalWh_ = 0.0, totalAh_ = 0.0;
};

}  // namespace N3Power
