/**
 * n3_power_battery — Etat de charge d'une batterie plomb 12 V (AGM / gel / ouverte)
 * et decision d'alerte batterie basse.
 *
 * Logique pure (aucune dependance Arduino), testee en natif (test_power_battery).
 *
 * Etat de charge (SoC) = combinaison classique :
 *  1. comptage coulometrique : SoC += 100 * dAh / capacite, la charge etant
 *     ponderee par un rendement (plomb : ~85-90 % en Ah) ;
 *  2. recalage sur la tension de repos (OCV) : apres un repos prolonge
 *     (|I| < seuil pendant restSeconds), la tension a vide est fiable et
 *     remplace l'estimation integree (corrige la derive du compteur).
 * Sans graine (NVS, commande), la premiere mesure initialise le SoC par l'OCV
 * (estimation grossiere hors repos, signalee par seededFromOcv()).
 *
 * Table OCV par defaut : valeurs typiques AGM 12 V a ~25 °C apres repos
 * (ordre de grandeur des tables fabricants, ex. Victron « Batteries &
 * Charging », Lifeline AGM technical manual). A ajuster selon la batterie
 * reelle (table injectable).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace N3Power {

struct OcvPoint {
  float volts;
  float socPercent;
};

/** Table OCV AGM 12 V par defaut (tensions croissantes). */
extern const OcvPoint kAgm12vOcvTable[];
extern const size_t kAgm12vOcvTableSize;

/**
 * SoC (%) par interpolation lineaire dans une table triee par tension
 * croissante ; borne a [premier, dernier] point. Retourne -1 si table vide.
 */
float ocvToSoc(float volts, const OcvPoint* table, size_t size);

struct LeadAcidConfig {
  float capacityAh = 12.0f;        // capacite nominale (C20)
  float chargeEfficiency = 0.90f;  // rendement de charge en Ah (0..1]
  float restCurrentA = 0.12f;      // |I| sous ce seuil = repos (~C/100)
  uint32_t restSeconds = 1800;     // duree de repos avant recalage OCV
  const OcvPoint* ocvTable = kAgm12vOcvTable;
  size_t ocvTableSize = kAgm12vOcvTableSize;
};

class LeadAcidSoc {
 public:
  explicit LeadAcidSoc(const LeadAcidConfig& config = LeadAcidConfig());

  /** Impose le SoC (restauration NVS, commande « soc »). Borne a [0,100]. */
  void seed(float socPercent);
  bool seeded() const { return seeded_; }
  /** true si le SoC courant provient d'une OCV prise hors repos (au boot). */
  bool seededFromOcv() const { return seededFromOcv_; }

  /**
   * Met a jour avec une mesure batterie : tension (V), courant (A, > 0 =
   * charge), dAh/dtSeconds = increment integre (cf. ChannelAccumulator::add).
   */
  void update(float voltageV, float currentA, double dAh, float dtSeconds);

  float socPercent() const { return soc_; }
  /** Charge nette integree (Ah, signee, avant rendement) depuis resetAh(). */
  double netAh() const { return netAh_; }
  void resetAh() { netAh_ = 0.0; }
  bool atRest() const { return restAccumS_ >= static_cast<float>(cfg_.restSeconds); }
  uint32_t ocvResyncCount() const { return resyncCount_; }
  const LeadAcidConfig& config() const { return cfg_; }

 private:
  LeadAcidConfig cfg_;
  float soc_ = 0.0f;
  bool seeded_ = false;
  bool seededFromOcv_ = false;
  double netAh_ = 0.0;
  float restAccumS_ = 0.0f;
  bool resyncedThisRest_ = false;
  uint32_t resyncCount_ = 0;
};

struct LowBatteryAlertConfig {
  float thresholdV = 11.8f;         // alerte sous ce seuil...
  uint32_t holdSeconds = 60;        // ...tenu au moins ce temps
  float rearmV = 12.4f;             // reamorcage (hysteresis) au-dessus
  uint32_t cooldownSeconds = 21600; // 6 h mini entre deux alertes
  float chargingCurrentA = 0.05f;   // I > seuil = batterie en charge -> pas d'alerte
};

/**
 * Decision d'alerte batterie basse : seuil + duree de maintien + hysteresis
 * de reamorcage + cooldown. Temps en millisecondes 32 bits (millis()),
 * debordement gere par soustraction non signee.
 */
class LowBatteryAlert {
 public:
  explicit LowBatteryAlert(const LowBatteryAlertConfig& config = LowBatteryAlertConfig());

  /** Retourne true quand une alerte doit etre emise maintenant. */
  bool update(uint32_t nowMs, float voltageV, float currentA);

  bool armed() const { return armed_; }
  uint32_t alertCount() const { return alertCount_; }
  const LowBatteryAlertConfig& config() const { return cfg_; }

 private:
  LowBatteryAlertConfig cfg_;
  bool armed_ = true;
  bool below_ = false;
  uint32_t belowSinceMs_ = 0;
  bool hasAlerted_ = false;
  uint32_t lastAlertMs_ = 0;
  uint32_t alertCount_ = 0;
};

}  // namespace N3Power
