#include "pgl_log.h"

#include "config.h"

#ifndef PGL_HEADLESS
#define PGL_HEADLESS 1
#endif
#ifndef PGL_DEBUG_NO_SLEEP
#define PGL_DEBUG_NO_SLEEP 0
#endif

void pglLogBootBanner() {
  PGL_LOG("========================================");
  PGL_LOG("Poisson Glouton v%s (%s)", PGL_FIRMWARE_VERSION, PGL_SENSOR_LOCATION);
#if PGL_HEADLESS
  PGL_LOG("Build: headless");
#else
  PGL_LOG("Build: display JC4827W543");
#endif
#if PGL_DEBUG_NO_SLEEP
  PGL_LOG("Mode test: veille DESACTIVEE (PGL_DEBUG_NO_SLEEP)");
#endif
#if PGL_VERBOSE_LOG
  PGL_LOG("Mode test: logs VERBOSE actifs");
#endif
  PGL_LOG("========================================");
}
