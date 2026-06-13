#pragma once

// Stub FreeRTOS minimal (voir freertos/FreeRTOS.h). Fournit TaskHandle_t,
// suffisant pour les constexpr/typedefs de config_tasks.h en test natif.

#include "freertos/FreeRTOS.h"

typedef void* TaskHandle_t;
