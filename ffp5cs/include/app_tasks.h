#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "app_context.h"
#include "config.h"
#include "post_category.h"
#include <ArduinoJson.h>

class Diagnostics;

namespace AppTasks {

struct Handles {
  TaskHandle_t sensor;
  TaskHandle_t web;
  TaskHandle_t automation;
  TaskHandle_t display;
  TaskHandle_t net;  // TLS/HTTP, stack 12 KB
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
// v11.178: Defaults 5s (règle offline-first). POST: dérogation 8s (HTTP_POST_TIMEOUT_MS).
// outFromNVSFallback: si non null, reçoit true quand la config vient du cache NVS (v11.193: ne pas appeler processFetchedRemoteConfig dans ce cas)
bool netFetchRemoteState(ArduinoJson::JsonDocument& doc, uint32_t timeoutMs = 5000, bool* outFromNVSFallback = nullptr);

/** Raison d'échec de netPostRaw (pour logs différenciés). */
enum class NetPostFailureReason { None, PoolFull, TimeoutRpc, HttpError };

bool netPostRaw(const char* payload, uint32_t timeoutMs = NetworkConfig::HTTP_POST_TIMEOUT_MS,
                PostCategory category = PostCategory::Periodic, NetPostFailureReason* outFailure = nullptr);
bool netSendHeartbeat(const Diagnostics& diag, uint32_t timeoutMs = 5000);

/** Demande une vérification OTA à la tâche dédiée otaTask (fire-and-forget). Utilisé par le boot, le timer 2h ou le serveur distant (triggerOtaCheck). */
void netRequestOtaCheck();

/** Nombre de slots actuellement utilisés dans le pool netRPC (indicateur pour throttle). */
size_t netRequestPoolUsedCount();
/** Taille du pool netRPC (10 WROOM, 16 S3). Un slot est réservé à l’OTA (netRequestOtaCheck). */
size_t netRequestPoolSize();
/** Seuil pool used pour throttle POST (quand tous les slots POST sont occupés). */
size_t netRequestPoolPostSlotsFullThreshold();

#if FEATURE_MAIL
/** Réserve un bloc 32 KB pour SMTP au boot (heap peu fragmenté). Libéré au moment de l'envoi pour créer un bloc contigu pour TLS. */
void reserveMailBlockAtBoot();
#endif

/** Attend files réseau vides puis acquiert le mutex HTTP (avant goToLightSleep). */
bool quiesceHttpBeforeLightSleep(uint32_t timeoutMs = NetworkConfig::LIGHT_SLEEP_HTTP_QUIESCE_TIMEOUT_MS);
/** Relâche le mutex acquis par quiesceHttpBeforeLightSleep (appeler juste après goToLightSleep). */
void releaseHttpAfterLightSleep();

}  // namespace AppTasks


