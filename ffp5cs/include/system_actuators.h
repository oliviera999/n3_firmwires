#pragma once
#include "actuators.h"
#include "log.h" // Ajout pour permettre l'utilisation du macro LOG

struct SystemActuators {
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
  void startTankPump();
  
  void stopTankPump(uint32_t startTime = 0);
  
  void startAquaPump();
  void stopAquaPump();
  
  // Méthodes pour le chauffage
  void startHeater();
  void stopHeater();
  
  // Méthodes pour la lumière
  void startLight();
  void stopLight();
  
  // Méthodes pour la nourriture
  // durationSec : durée pendant laquelle le servo reste dans la position de distribution.
  // Valeur par défaut : 10 s pour conserver la compatibilité avec les appels existants.
  void feedBigFish(uint16_t durationSec = 10);
  void feedSmallFish(uint16_t durationSec = 10);
  
  // Nourrissage séquentiel pour éviter les conflits de puissance.
  // Retourne false uniquement si AUCUNE distribution n'a eu lieu (séquence déjà en cours).
  // Un échec de planification de la phase 2 renvoie true : les gros poissons ont déjà été
  // nourris, re-déclencher provoquerait un surdosage.
  bool feedSequential(uint16_t bigDurationSec = 10, uint16_t smallDurationSec = 10, uint16_t delayBetweenSec = 2);

  // true tant qu'une séquence gros->petits est planifiée (timer FreeRTOS des petits armé).
  // Source de vérité matérielle de l'anti-double-nourrissage (cf. feedSequential).
  bool isSequentialFeedInProgress() const;

  // Getters pour l'état
  bool isTankPumpRunning() const;
  bool isAquaPumpRunning() const;
  bool isHeaterOn() const;
  bool isLightOn() const;
  
  // Méthodes pour obtenir les statistiques de la pompe de réserve
  unsigned long getTankPumpCurrentRuntime() const;
  unsigned long getTankPumpTotalRuntime() const;
  unsigned long getTankPumpTotalStops() const;
  unsigned long getTankPumpLastStopTime() const;
};
