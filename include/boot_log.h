#pragma once
/**
 * Log boot / chemins chauds : un seul #if centralisé.
 * S3 PSRAM : ets_printf (évite blocage si buffer UART plein).
 * Autres envs : Serial.printf.
 */
#include <Arduino.h>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#define BOOT_LOG(fmt, ...) ets_printf(fmt, ##__VA_ARGS__)
#else
#define BOOT_LOG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#endif
