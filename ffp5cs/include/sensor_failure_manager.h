#pragma once
// =============================================================================
// FFP5CS — Gestionnaire de défaillances capteurs : wrapper vers shared/
// =============================================================================
// v15.16 (mutualisation T2) : la logique vit désormais dans
// shared/n3_analog_sensors/src/n3_sensor_failure_manager.h (classe
// SensorFailureManager identique, header-only). Ce wrapper est l'UNIQUE point
// d'inclusion côté ffp5cs : il mappe la macro de log de la lib sur le
// SENSOR_LOG_PRINTF du projet (gated par profil, cf. config_logging.h) AVANT
// l'include — même macro dans toutes les TU du binaire (pas d'ODR divergent).
// =============================================================================

#include "config.h"  // SENSOR_LOG_PRINTF (gate par profil via config_logging.h)

#define N3_SENSOR_LOG_PRINTF SENSOR_LOG_PRINTF
#include "n3_sensor_failure_manager.h"
