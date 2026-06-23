#pragma once
#include "actuators.h"
#include "gpio_mapping.h"
#include "iactuators.h"  // C4: interface d'actionneurs (inversion de dépendance)
#include "log.h" // Ajout pour permettre l'utilisation du macro LOG

struct SystemActuators : public IActuators {
  PumpController pumpAqua{Pins::POMPE_AQUA};
  PumpController pumpTank{Pins::POMPE_RESERV};
  HeaterController heater{Pins::RADIATEURS};
  LightController light{Pins::LUMIERE};
  Feeder feederBig{Pins::SERVO_GROS};
  Feeder feederSmall{Pins::SERVO_PETITS};

  // Variables pour le tracking du timing de la pompe de réserve (ms)
  uint32_t tankPumpStartTime = 0;
  uint32_t tankPumpLastStopTime = 0;
  uint32_t tankPumpTotalRuntime = 0;
  uint32_t tankPumpTotalStops = 0;

  void begin();

  // Méthodes pour la gestion des pompes avec logs détaillés
  // (override IActuators — voir iactuators.h pour le contrat)
  void startTankPump() override;

  void stopTankPump(uint32_t startTime = 0) override;

  void startAquaPump() override;
  void stopAquaPump() override;

  // Méthodes pour le chauffage
  void startHeater() override;
  void stopHeater() override;

  // Méthodes pour la lumière
  void startLight() override;
  void stopLight() override;

  // Méthodes pour la nourriture
  // durationSec : durée pendant laquelle le servo reste dans la position de distribution.
  void feedBigFish(uint16_t durationSec = 10) override;
  void feedSmallFish(uint16_t durationSec = 10) override;

  void setServoAngles(bool big, int rest, int feed, int inter);
  void setServoGrosRest(int angle) override;
  void setServoGrosFeed(int angle) override;
  void setServoGrosInter(int angle) override;
  void setServoPetitsRest(int angle) override;
  void setServoPetitsFeed(int angle) override;
  void setServoPetitsInter(int angle) override;

  // Nourrissage séquentiel pour éviter les conflits de puissance.
  // Retourne false uniquement si AUCUNE distribution n'a eu lieu (séquence déjà en cours).
  // Un échec de planification de la phase 2 renvoie true : les gros poissons ont déjà été
  // nourris, re-déclencher provoquerait un surdosage.
  bool feedSequential(uint16_t bigDurationSec = 10, uint16_t smallDurationSec = 10, uint16_t delayBetweenSec = 2) override;

  // true tant qu'une séquence gros->petits est planifiée (timer FreeRTOS des petits armé).
  // Source de vérité matérielle de l'anti-double-nourrissage (cf. feedSequential).
  bool isSequentialFeedInProgress() const override;

  // Getters pour l'état
  bool isTankPumpRunning() const override;
  bool isAquaPumpRunning() const override;
  bool isHeaterOn() const override;
  bool isLightOn() const override;
  
  // Méthodes pour obtenir les statistiques de la pompe de réserve
  unsigned long getTankPumpCurrentRuntime() const;
  unsigned long getTankPumpTotalRuntime() const;
  unsigned long getTankPumpTotalStops() const;
  unsigned long getTankPumpLastStopTime() const;
  
 private:
  static int clampServoAngle(int angle) {
    if (angle < 0) return 0;
    if (angle > 180) return 180;
    return angle;
  }

  void applyServoConfigToFeeders();

  int _servoGrosRest{GPIODefaults::SERVO_REST_ANGLE};
  int _servoGrosFeed{GPIODefaults::SERVO_FEED_ANGLE};
  int _servoGrosInter{GPIODefaults::SERVO_INTER_ANGLE};
  int _servoPetitsRest{GPIODefaults::SERVO_REST_ANGLE};
  int _servoPetitsFeed{GPIODefaults::SERVO_FEED_ANGLE};
  int _servoPetitsInter{GPIODefaults::SERVO_INTER_ANGLE};
};
