#pragma once
/**
 * Log boot / OTA / chemins chauds : un seul #if centralisé.
 * - S3 PSRAM : ets_printf (évite blocage si buffer UART plein).
 * - wroom-prod (Serial désactivé) : ets_printf pour diagnostic UART sans Serial.begin.
 * - Autres envs : Serial.printf.
 */
#include <Arduino.h>
#if (defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)) \
 || (defined(BOARD_WROOM) && defined(PROFILE_PROD) \
     && (!defined(ENABLE_SERIAL_MONITOR) || (ENABLE_SERIAL_MONITOR == 0)))
#include "rom/ets_sys.h"
#define BOOT_LOG(fmt, ...) ets_printf(fmt, ##__VA_ARGS__)
#define OTA_LOG(fmt, ...)  ets_printf("[OTA] " fmt "\n", ##__VA_ARGS__)
#else
#define BOOT_LOG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#define OTA_LOG(fmt, ...)  Serial.printf("[OTA] " fmt "\n", ##__VA_ARGS__)
#endif
