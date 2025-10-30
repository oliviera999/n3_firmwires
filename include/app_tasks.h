#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "app_context.h"

namespace AppTasks {

struct Handles {
  TaskHandle_t sensor;
  TaskHandle_t web;
  TaskHandle_t automation;
  TaskHandle_t display;
};

/**
 * Initialise la queue capteurs et crée les tâches FreeRTOS dédiées.
 * Retourne true si toutes les ressources critiques ont été créées.
 */
bool start(AppContext& ctx);

/**
 * Retourne les handles courants des tâches FreeRTOS (nullptr si non créées).
 */
Handles getHandles();

/**
 * Retourne la queue utilisée pour transmettre les mesures capteurs.
 */
QueueHandle_t getSensorQueue();

}  // namespace AppTasks


