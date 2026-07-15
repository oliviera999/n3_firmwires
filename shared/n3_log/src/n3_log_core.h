#pragma once
// =============================================================================
// n3_log — cœur pur (niveaux + libellés), sans dépendance Arduino
// =============================================================================
// Mutualisé depuis ffp5cs `include/log.h` / `config_logging.h` (tranche T5 du
// chantier core shared), découplé de toute config firmware. L'échelle numérique
// est IDENTIQUE à celle de ffp5cs (LogConfig::LogLevel) ET à celle de
// CORE_DEBUG_LEVEL / ARDUHAL_LOG_LEVEL_* d'arduino-esp32 (0=None … 5=Verbose),
// ce qui permet de piloter le niveau via le flag de build standard.
// Testé en natif (test_log).
// =============================================================================

#include <stdint.h>

enum N3LogLevel : uint8_t {
  N3LOG_NONE = 0,
  N3LOG_ERROR = 1,
  N3LOG_WARN = 2,
  N3LOG_INFO = 3,
  N3LOG_DEBUG = 4,
  N3LOG_VERBOSE = 5,
};

// Libellé court du niveau — chaînes identiques à ffp5cs logLevelStr()
// (parité de logs exigée par le câblage sans changement observable).
inline const char* n3LogLevelStr(N3LogLevel level) {
  switch (level) {
    case N3LOG_ERROR: return "ERROR";
    case N3LOG_WARN:  return "WARN";
    case N3LOG_INFO:  return "INFO";
    case N3LOG_DEBUG: return "DEBUG";
    case N3LOG_VERBOSE: return "VERB";
    default: return "NONE";
  }
}
