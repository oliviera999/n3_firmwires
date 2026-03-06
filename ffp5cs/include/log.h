#pragma once

// =============================================================================
// SYSTÈME DE LOGS UNIFIÉ
// =============================================================================
// Utilise la configuration centralisée de config.h
// =============================================================================

#include "config.h"

// Rétro-compatibilité avec l'ancien système
using LogLevel = LogConfig::LogLevel;

constexpr LogLevel LOG_NONE = LogConfig::LOG_NONE;
constexpr LogLevel LOG_ERROR = LogConfig::LOG_ERROR;
constexpr LogLevel LOG_WARN = LogConfig::LOG_WARN;
constexpr LogLevel LOG_INFO = LogConfig::LOG_INFO;
constexpr LogLevel LOG_DEBUG = LogConfig::LOG_DEBUG;
constexpr LogLevel LOG_VERBOSE = LogConfig::LOG_VERBOSE;

// Niveau de log par défaut
#ifndef LOG_LEVEL
    #define LOG_LEVEL LogConfig::DEFAULT_LEVEL
#endif

// Helper pour obtenir le nom du niveau de log
inline const char* logLevelStr(LogLevel level) {
    switch(level) {
        case LOG_ERROR: return "ERROR";
        case LOG_WARN:  return "WARN";
        case LOG_INFO:  return "INFO";
        case LOG_DEBUG: return "DEBUG";
        case LOG_VERBOSE: return "VERB";
        default: return "NONE";
    }
}

// =============================================================================
// MACRO INTERNE - IMPLÉMENTATION UNIQUE DU FORMATAGE TIMESTAMP
// =============================================================================
// Cette macro factorise le code de formatage pour éviter la duplication.
// Le paramètre 'tag' permet d'ajouter un suffixe optionnel (ex: "[TIME]", "[NTP]").
// =============================================================================
#define _LOG_IMPL(tag, level, fmt, ...) do { \
    if((level) <= LOG_LEVEL && LogConfig::SERIAL_ENABLED) { \
        time_t now = time(nullptr); \
        struct tm timeinfo; \
        if (localtime_r(&now, &timeinfo)) { \
            Serial.printf("[%02d:%02d:%02d][%s]" tag " " fmt "\n", \
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, \
                logLevelStr((LogLevel)(level)), ##__VA_ARGS__); \
        } else { \
            Serial.printf("[%lu][%s]" tag " " fmt "\n", millis()/1000, \
                logLevelStr((LogLevel)(level)), ##__VA_ARGS__); \
        } \
    } \
} while(0)

// =============================================================================
// MACROS DE LOG PUBLIQUES
// =============================================================================

// Macro de log principale avec timestamp (compatibilité avec l'ancien système)
#define LOG(level, fmt, ...) _LOG_IMPL("", level, fmt, ##__VA_ARGS__)

// Macros de log par niveau (nouveau système préféré)
// Déjà définies dans config.h : LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG

// Macros spécialisées pour les informations temporelles
#define LOG_TIME(level, fmt, ...) _LOG_IMPL("[TIME]", level, fmt, ##__VA_ARGS__)
#define LOG_NTP(level, fmt, ...) _LOG_IMPL("[NTP]", level, fmt, ##__VA_ARGS__)
#define LOG_RTC(level, fmt, ...) _LOG_IMPL("[RTC]", level, fmt, ##__VA_ARGS__)
#define LOG_DRIFT(level, fmt, ...) _LOG_IMPL("[DRIFT]", level, fmt, ##__VA_ARGS__)
