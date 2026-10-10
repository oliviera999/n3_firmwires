#include "camera_time.h"

#include "config.h"
#include "n3_log.h"
#include "n3_time.h"

#include <ESP32Time.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

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

  /* v2.77 — horloge système d'abord. ESP-IDF conserve l'heure système à travers le deep sleep
     (timer RTC) : au réveil timer elle est déjà juste, à la dérive de l'oscillateur RTC près.
     Avant 2.77, l'epoch NVS (sauvé au réveil précédent) était rechargé à CHAQUE réveil et
     RECULAIT l'horloge ; pire, le « NTP ok » qui suivait était immédiat (cf. NTP plus bas) et
     re-sauvait ce même epoch périmé : l'heure NVS restait figée à sa valeur du premier boot.
     Règle : l'epoch NVS ne sert qu'à amorcer une horloge absente (cold boot) ou en retard sur
     lui — jamais à reculer une horloge RTC valide. */
  const unsigned long sysEpoch = static_cast<unsigned long>(time(nullptr));
  const unsigned long nvsEpoch = n3TimeReadSavedEpoch(prefs);
  bool clockPlausible = sysEpoch > N3_TIME_MIN_VALID_EPOCH;
  if (nvsEpoch > N3_TIME_MIN_VALID_EPOCH && nvsEpoch > sysEpoch) {
    rtc.setTime(nvsEpoch);
    clockPlausible = true;
    N3_LOGI("[TIME] Horloge amorcee depuis NVS epoch=%lu (systeme=%lu)", nvsEpoch, sysEpoch);
  } else if (clockPlausible) {
    N3_LOGI("[TIME] Horloge systeme conservee (RTC) epoch=%lu", sysEpoch);
  }

  if (!wifiOk) {
    if (clockPlausible) {
      N3_LOGI("[TIME] WiFi KO, horloge locale epoch=%lu", static_cast<unsigned long>(time(nullptr)));
    } else {
      N3_LOGW("[TIME] WiFi KO et pas d'horloge fiable");
    }
    return;
  }

  /* NTP CONFIRMÉ (statut SNTP) : n3TimeSyncNtp() attendait getLocalTime(), qui réussit dès que
     l'horloge est plausible — donc instantanément ici, sans aucune réponse NTP. */
  const bool ntpOk =
      n3TimeSyncNtpConfirmed(rtc, GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER, NTP_SYNC_TIMEOUT_MS);
#if defined(NTP_TZ_STRING)
  /* configTime() repose un TZ dérivé de l'offset (format à règle DST parasite) :
     on ré-applique le TZ POSIX canonique pour que localtime reste déterministe (UTC+1, aucune DST). */
  setenv("TZ", NTP_TZ_STRING, 1);
  tzset();
#endif
  if (ntpOk) {
    if (rtc.getEpoch() > N3_TIME_MIN_VALID_EPOCH) {
      n3TimeSaveToFlash(rtc, prefs);  // amorce d'un futur cold boot (coupure d'alimentation)
    }
    return;
  }

  if (clockPlausible) {
    N3_LOGW("[TIME] NTP KO, horloge locale conservee epoch=%lu",
            static_cast<unsigned long>(time(nullptr)));
  } else {
    N3_LOGW("[TIME] NTP KO et pas d'horloge fiable");
  }
}
