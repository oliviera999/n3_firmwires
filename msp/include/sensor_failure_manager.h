#pragma once
// =============================================================================
// msp — Gestionnaire de défaillances capteurs : point d'inclusion unique
// =============================================================================
// v2.64 (adoption T2) : msp adopte la machine d'état SensorFailureManager de
// shared/n3_analog_sensors pour la détection de panne DURABLE du capteur de
// température sol DS18B20 (débranchement), avec persistance en RTC_DATA_ATTR à
// travers le deep sleep (cf. msp_sensors.cpp).
//
// Ce header est l'UNIQUE point d'inclusion côté msp : il mappe la macro de log
// de la lib sur Serial.printf AVANT l'include, de sorte que la macro soit LA
// MÊME dans toutes les TU (pas d'ODR divergent). msp n'a pas de macro de log
// gatée par profil comme ffp5cs -> Serial.printf direct.
// =============================================================================

#include <Arduino.h>

#define N3_SENSOR_LOG_PRINTF Serial.printf
#include "n3_sensor_failure_manager.h"
