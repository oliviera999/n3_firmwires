/**
 * FFP5CS: abstraction FS pour WROOM (SPIFFS) vs S3 (LittleFS).
 * Inclure ce fichier au lieu de <LittleFS.h> / <SPIFFS.h> et utiliser FFP5CS_FS.
 */
#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#  include <LittleFS.h>
#  define FFP5CS_FS         LittleFS
#  define FFP5CS_FS_NAME   "LittleFS"
#else
#  include <SPIFFS.h>
#  define FFP5CS_FS         SPIFFS
#  define FFP5CS_FS_NAME   "SPIFFS"
#endif
