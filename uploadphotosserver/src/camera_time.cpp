#include "camera_time.h"

#include "config.h"
#include "n3_log.h"
#include "n3_time.h"

#include <ESP32Time.h>
#include <Preferences.h>
#include <sys/time.h>

void n3CamSyncClock(Preferences& prefs, ESP32Time& rtc, bool wifiOk) {
  /* A3 (audit 2026-07-05) : le fuseau (POSIX "<+01>-1" = UTC+1 Maroc, sans DST) est appliqué
     INCONDITIONNELLEMENT et tôt. La newlib de l'ESP32 n'a pas la base IANA : un nom comme
     "Africa/Casablanca" tombait en UTC (décalage 1 h sur le créneau et les horodatages). L'appliquer
     ici garantit une heure murale correcte même sur un réveil deep sleep SANS WiFi (aucun configTime
     alors). L'epoch système/NVS reste en UTC ; seul localtime applique l'offset. */
#if defined(NTP_TZ_STRING)
  setenv("TZ", NTP_TZ_STRING, 1);
  tzset();
#endif

  const bool nvsOk = n3TimeLoadAndApplyToSystem(prefs, rtc);

  if (!wifiOk) {
    if (nvsOk) {
      N3_LOGI("[TIME] WiFi KO, horloge NVS epoch=%lu",
              static_cast<unsigned long>(rtc.getEpoch()));
    } else {
      N3_LOGW("[TIME] WiFi KO et pas d'horloge NVS fiable");
    }
    return;
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  const bool ntpOk =
      n3TimeSyncNtp(rtc, GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER, NTP_SYNC_TIMEOUT_MS);
#if defined(NTP_TZ_STRING)
  /* configTime()/n3TimeSyncNtp() reposent un TZ dérivé de l'offset (format à règle DST parasite) :
     on ré-applique le TZ POSIX canonique pour que localtime reste déterministe (UTC+1, aucune DST). */
  setenv("TZ", NTP_TZ_STRING, 1);
  tzset();
#endif
  if (ntpOk) {
    if (rtc.getEpoch() > N3_TIME_MIN_VALID_EPOCH) {
      n3TimeSaveToFlash(rtc, prefs);
    }
    return;
  }

  if (nvsOk) {
    N3_LOGW("[TIME] NTP KO, horloge NVS conservee epoch=%lu",
            static_cast<unsigned long>(rtc.getEpoch()));
  } else {
    N3_LOGW("[TIME] NTP KO et pas d'horloge NVS fiable");
  }
}
