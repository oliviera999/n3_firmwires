#include "camera_time.h"

#include "config.h"
#include "n3_time.h"

#include <ESP32Time.h>
#include <Preferences.h>
#include <sys/time.h>

void n3CamSyncClock(Preferences& prefs, ESP32Time& rtc, bool wifiOk) {
  const bool nvsOk = n3TimeLoadAndApplyToSystem(prefs, rtc);

  if (!wifiOk) {
    if (nvsOk) {
      Serial.printf("[TIME] WiFi KO, horloge NVS epoch=%lu\n",
                    static_cast<unsigned long>(rtc.getEpoch()));
    } else {
      Serial.println("[TIME][WARN] WiFi KO et pas d'horloge NVS fiable");
    }
    return;
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
#if defined(NTP_TZ_STRING)
  setenv("TZ", NTP_TZ_STRING, 1);
  tzset();
#endif

  const bool ntpOk =
      n3TimeSyncNtp(rtc, GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER, NTP_SYNC_TIMEOUT_MS);
  if (ntpOk) {
    if (rtc.getEpoch() > N3_TIME_MIN_VALID_EPOCH) {
      n3TimeSaveToFlash(rtc, prefs);
    }
    return;
  }

  if (nvsOk) {
    Serial.printf("[TIME][WARN] NTP KO, horloge NVS conservee epoch=%lu\n",
                  static_cast<unsigned long>(rtc.getEpoch()));
  } else {
    Serial.println("[TIME][WARN] NTP KO et pas d'horloge NVS fiable");
  }
}
