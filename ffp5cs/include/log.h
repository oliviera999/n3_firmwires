#pragma once

// =============================================================================
// SYSTÈME DE LOGS UNIFIÉ
// =============================================================================
// Câblage T5 (chantier core shared) : le formatage timestampé historique
// (_LOG_IMPL) est mutualisé dans `shared/n3_log` (macro N3_LOG_TAGGED,
// extraite verbatim d'ici) — même échelle de niveaux 0..5, mêmes libellés,
// mêmes octets émis. Le niveau (LOG_LEVEL) et la coupure série
// (LogConfig::SERIAL_ENABLED, incl. NullSerial en prod) restent pilotés par
// la configuration centralisée de config.h.
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

// Délégation à shared/n3_log : niveau et coupure série injectés depuis la
// config locale AVANT l'include (n3_log ne fixe ces macros que par défaut).
// Serial est résolu au point d'expansion des macros -> le stub NullSerial
// de config_logging.h reste effectif en prod.
#define N3_LOG_LEVEL LOG_LEVEL
#define N3_LOG_ENABLED (LogConfig::SERIAL_ENABLED)
#include "n3_log.h"

// Helper pour obtenir le nom du niveau de log (délègue au cœur pur partagé —
// mêmes libellés, même repli "NONE").
inline const char* logLevelStr(LogLevel level) {
    return n3LogLevelStr(static_cast<N3LogLevel>(level));
}

// =============================================================================
// MACRO INTERNE — délègue le formatage timestamp à shared/n3_log
// =============================================================================
// Le paramètre 'tag' permet d'ajouter un suffixe optionnel (ex: "[TIME]", "[NTP]").
// =============================================================================
#define _LOG_IMPL(tag, level, fmt, ...) N3_LOG_TAGGED(tag, level, fmt, ##__VA_ARGS__)

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
