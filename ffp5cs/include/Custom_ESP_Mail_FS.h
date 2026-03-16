/**
 * FFP5CS: FS pour ESP Mail Client.
 * Documenté dans ESP_Mail_FS.h : __has_include("Custom_ESP_Mail_FS.h")
 * - ESP32-S3 (wroom-s3-*) : LittleFS (inclut disponible via script / build).
 * - ESP32 WROOM (wroom-prod, wroom-test, wroom-beta) : SPIFFS, car avec pioarduino
 *   le chemin LittleFS du framework n'est pas propagé aux libs → 'LittleFS' was not declared.
 */
#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#  include <LittleFS.h>
#  define ESP_MAIL_DEFAULT_FLASH_FS LittleFS
#else
#  include <SPIFFS.h>
#  define ESP_MAIL_DEFAULT_FLASH_FS SPIFFS
#endif
