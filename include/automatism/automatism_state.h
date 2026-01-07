#pragma once

#include <Arduino.h>
#include "system_sensors.h"

// Forward declaration
class Automatism;

// Contexte d'exécution passé aux contrôleurs
struct AutomatismRuntimeContext {
  // Suppression des références pour permettre l'initialisation par défaut
  // Les références nécessitent une initialisation immédiate, ce qui complique l'utilisation dans Automatism::update
  // On passe à des pointeurs ou des valeurs copies pour simplifier
  
  SensorReadings readings;
  uint32_t nowMs;
  
  // Constructeur par défaut
  AutomatismRuntimeContext() : nowMs(0) {
      // readings initialisé par défaut
  }
  
  // Constructeur complet
  AutomatismRuntimeContext(const SensorReadings& r, uint32_t now) : readings(r), nowMs(now) {}
};
