#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TaskMonitor {

  // Structure simplifiée
  struct TaskStats {
    const char* name;
    uint32_t highWaterMark; // Mots
    eTaskState state;
  };

  // Snapshot global simplifié
  struct Snapshot {
    TaskStats sensor;
    TaskStats web;
    TaskStats automation;
    TaskStats display;
  };

  // Capture l'état actuel des tâches principales
  Snapshot collectSnapshot();

  // Log simple de l'état
  void logSnapshot(const Snapshot& snapshot, const char* stage);

  // Comparaison simple (avant/après sleep par exemple)
  void logDiff(const Snapshot& before, const Snapshot& after, const char* stage);

  // Détection basique d'anomalies (stack overflow imminent)
  bool detectAnomalies(const Snapshot& snapshot, const char* stage);

} // namespace TaskMonitor
