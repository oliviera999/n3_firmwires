#pragma once
// =============================================================================
// n3_log — macros de log série timestampées (Arduino)
// =============================================================================
// Extrait verbatim du formatage de ffp5cs `include/log.h` (_LOG_IMPL), découplé
// de config.h : préfixe `[HH:MM:SS][NIVEAU][TAG] message` quand l'horloge est
// posée (localtime_r), sinon repli uptime `[secondes][NIVEAU][TAG] message`.
//
// Configuration par macros de build (aucune dépendance à une config firmware) :
//   - N3_LOG_LEVEL   : niveau max émis (constante N3LOG_* ou expression du
//                      firmware). Défaut : CORE_DEBUG_LEVEL si défini (même
//                      échelle 0..5), sinon N3LOG_INFO.
//   - N3_LOG_ENABLED : 0 pour tout désactiver à la compilation (défaut 1).
//                      Peut être une expression constexpr du firmware (ex.
//                      LogConfig::SERIAL_ENABLED côté ffp5cs).
//
// NB : `Serial` est résolu au POINT D'EXPANSION des macros — un firmware qui
// redirige Serial vers un stub (ffp5cs NullSerial en prod) garde ce bénéfice.
// =============================================================================

#include <Arduino.h>
#include <time.h>

#include "n3_log_core.h"

#ifndef N3_LOG_LEVEL
  #if defined(CORE_DEBUG_LEVEL)
    #define N3_LOG_LEVEL (CORE_DEBUG_LEVEL)
  #else
    #define N3_LOG_LEVEL N3LOG_INFO
  #endif
#endif

#ifndef N3_LOG_ENABLED
  #define N3_LOG_ENABLED 1
#endif

// Macro générique avec tag littéral concaténé (ex. "[NTP]") — formatage
// identique octet pour octet à ffp5cs _LOG_IMPL.
#define N3_LOG_TAGGED(tag, level, fmt, ...) do { \
    if ((N3_LOG_ENABLED) && (level) <= (N3_LOG_LEVEL)) { \
      time_t _n3log_now = time(nullptr); \
      struct tm _n3log_tm; \
      if (localtime_r(&_n3log_now, &_n3log_tm)) { \
        Serial.printf("[%02d:%02d:%02d][%s]" tag " " fmt "\n", \
                      _n3log_tm.tm_hour, _n3log_tm.tm_min, _n3log_tm.tm_sec, \
                      n3LogLevelStr((N3LogLevel)(level)), ##__VA_ARGS__); \
      } else { \
        Serial.printf("[%lu][%s]" tag " " fmt "\n", millis() / 1000, \
                      n3LogLevelStr((N3LogLevel)(level)), ##__VA_ARGS__); \
      } \
    } \
  } while (0)

// Macro principale sans tag.
#define N3_LOG(level, fmt, ...) N3_LOG_TAGGED("", level, fmt, ##__VA_ARGS__)

// Raccourcis par niveau.
#define N3_LOGE(fmt, ...) N3_LOG(N3LOG_ERROR, fmt, ##__VA_ARGS__)
#define N3_LOGW(fmt, ...) N3_LOG(N3LOG_WARN, fmt, ##__VA_ARGS__)
#define N3_LOGI(fmt, ...) N3_LOG(N3LOG_INFO, fmt, ##__VA_ARGS__)
#define N3_LOGD(fmt, ...) N3_LOG(N3LOG_DEBUG, fmt, ##__VA_ARGS__)
#define N3_LOGV(fmt, ...) N3_LOG(N3LOG_VERBOSE, fmt, ##__VA_ARGS__)
