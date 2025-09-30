#pragma once

// =============================================================================
// SYSTÈME DE LOGS UNIFIÉ
// =============================================================================
// Utilise la configuration centralisée de project_config.h
// =============================================================================

#include "project_config.h"

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

// Macro de log principale (compatibilité avec l'ancien système)
#define LOG(level, fmt, ...) do { \
    if((level) <= LOG_LEVEL && LogConfig::SERIAL_ENABLED) { \
        Serial.printf("[%s] " fmt "\n", logLevelStr((LogLevel)(level)), ##__VA_ARGS__); \
    } \
} while(0)

// Macros de log par niveau (nouveau système préféré)
// Déjà définies dans project_config.h :
// LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG, LOG_VERBOSE