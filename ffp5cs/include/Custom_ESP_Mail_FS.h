/**
 * FFP5CS: Forcer LittleFS pour ESP Mail Client (au lieu de SPIFFS).
 * Documenté dans ESP_Mail_FS.h : __has_include("Custom_ESP_Mail_FS.h")
 * Évite l'erreur "LittleFS was not declared" sur certains envs (ex. S3 / pioarduino).
 */
#pragma once

#include <LittleFS.h>
#define ESP_MAIL_DEFAULT_FLASH_FS LittleFS
