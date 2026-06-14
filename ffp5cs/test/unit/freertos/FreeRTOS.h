#pragma once

// Stub FreeRTOS minimal pour les tests natifs (hôte).
//
// config.h -> config_tasks.h inclut <freertos/FreeRTOS.h> et <freertos/task.h>
// uniquement pour les TYPES (UBaseType_t / BaseType_t) de constexpr de priorité
// et de cœur. Ce stub fournit ce minimum afin que config.h (et les modules qui
// en dépendent, ex. ota_config.h) compilent sans la toolchain ESP32.
//
// Récupéré via build_flags: -Itest/unit. N'affecte JAMAIS le build firmware
// (platformio.ini n'inclut pas test/unit/). Ne pas utiliser pour tester du
// vrai comportement FreeRTOS — uniquement de la logique pure dépendante de
// config.h.

#include <cstdint>

typedef unsigned int UBaseType_t;
typedef int          BaseType_t;
typedef uint32_t     TickType_t;

#ifndef pdTRUE
#define pdTRUE  1
#define pdFALSE 0
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#endif

#ifndef portMAX_DELAY
#define portMAX_DELAY ((TickType_t)0xffffffffU)
#endif
