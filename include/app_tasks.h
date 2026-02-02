#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "app_context.h"
#include <ArduinoJson.h>

class Diagnostics;

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

// ============================================================================
// Point 2: Dispatcher réseau/TLS (exécuté uniquement dans netTask)
// Objectif: plus aucun TLS/HTTP depuis loopTask/autoTask → éviter panics TLS.
// Ces appels sont "RPC" via queue + task notification (pas d'allocation).
// ============================================================================

// Note: ces appels bloquent le caller jusqu'à la fin de l'opération réseau
// (sinon risque de corruption si le caller retourne avant fin TLS).
// v11.178: Defaults réduits à 5s (règle offline-first: max 5s - audit)
bool netFetchRemoteState(ArduinoJson::JsonDocument& doc, uint32_t timeoutMs = 5000);
bool netPostRaw(const char* payload, uint32_t timeoutMs = 5000);
bool netSendHeartbeat(const Diagnostics& diag, uint32_t timeoutMs = 5000);

}  // namespace AppTasks


