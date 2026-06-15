#pragma once
// =============================================================================
// FFP5CS — Interface d'actionneurs (inversion de dépendance, chantier C4)
// =============================================================================
// `IActuators` abstrait la surface d'actionneurs consommée par `Automatism`
// (et ses sous-modules sync/sleep/display). `SystemActuators` l'implémente.
//
// But : casser le couplage god-class -> matériel concret. `Automatism` dépend
// désormais de l'interface, ce qui permet d'injecter un FAUX acteur dans les
// tests natifs (cf. test/test_iactuators/) au lieu du matériel réel.
//
// Surface MINIMALE : seules les méthodes effectivement appelées par le module
// `automatism` figurent ici (vérifié par grep exhaustif des `_acts.`/`acts.`).
// Les statistiques pompe (getTankPump*) ne sont pas consommées par Automatism
// et restent hors interface (sur le concret `SystemActuators`).
//
// Les valeurs par défaut des arguments reproduisent celles de `SystemActuators`
// pour préserver à l'identique les sites d'appel (ex. `stopTankPump()`).
// =============================================================================

#include <cstdint>

struct IActuators {
  virtual ~IActuators() = default;

  // Pompe réservoir
  virtual void startTankPump() = 0;
  virtual void stopTankPump(uint32_t startTime = 0) = 0;

  // Pompe aquarium
  virtual void startAquaPump() = 0;
  virtual void stopAquaPump() = 0;

  // Chauffage
  virtual void startHeater() = 0;
  virtual void stopHeater() = 0;

  // Lumière
  virtual void startLight() = 0;
  virtual void stopLight() = 0;

  // Nourrissage
  virtual void feedBigFish(uint16_t durationSec = 10) = 0;
  virtual void feedSmallFish(uint16_t durationSec = 10) = 0;
  virtual bool feedSequential(uint16_t bigDurationSec = 10, uint16_t smallDurationSec = 10,
                              uint16_t delayBetweenSec = 2) = 0;

  // États (lecture seule)
  virtual bool isSequentialFeedInProgress() const = 0;
  virtual bool isTankPumpRunning() const = 0;
  virtual bool isAquaPumpRunning() const = 0;
  virtual bool isHeaterOn() const = 0;
  virtual bool isLightOn() const = 0;
};
