#pragma once
// =============================================================================
// n3pp — Gestionnaire de défaillances capteurs : point d'inclusion unique
// =============================================================================
// v4.61 (adoption T2) : n3pp adopte la machine d'état SensorFailureManager de
// shared/n3_analog_sensors pour la détection de panne DURABLE des 4 capteurs
// d'humidité du sol (débranchement), avec persistance en RTC_DATA_ATTR à
// travers le deep sleep (cf. n3pp_sensors.cpp).
//
// Ce header est l'UNIQUE point d'inclusion côté n3pp : il mappe la macro de log
// de la lib sur Serial.printf AVANT l'include, de sorte que la macro soit LA
// MÊME dans toutes les TU (pas d'ODR divergent). n3pp n'a pas de macro de log
// gatée par profil comme ffp5cs -> Serial.printf direct.
// =============================================================================

#include <Arduino.h>

#define N3_SENSOR_LOG_PRINTF Serial.printf
#include "n3_sensor_failure_manager.h"
