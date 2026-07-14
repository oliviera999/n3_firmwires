#include "n3_time.h"

#include <ESP32Time.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include <time.h>

static bool n3TimeApplyEpochToSystem(unsigned long epoch) {
  if (epoch <= N3_TIME_MIN_VALID_EPOCH) {
    return false;
  }
  struct timeval tv = {};
  tv.tv_sec = static_cast<time_t>(epoch);
  tv.tv_usec = 0;
  return settimeofday(&tv, nullptr) == 0;
}

static void n3TimeSyncRtcFromSystem(ESP32Time& rtc) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    return;
  }
  rtc.setTime(timeinfo.tm_sec,
              timeinfo.tm_min,
              timeinfo.tm_hour,
              timeinfo.tm_mday,
              timeinfo.tm_mon + 1,
              timeinfo.tm_year + 1900);
}

void n3TimeSaveToFlash(ESP32Time& rtc, Preferences& prefs) {
  prefs.begin("rtc", false);
  prefs.putULong("epoch", rtc.getEpoch());
  prefs.end();
}

void n3TimeLoadFromFlash(Preferences& prefs, ESP32Time& rtc) {
  prefs.begin("rtc", true);
  unsigned long epoch = prefs.getULong("epoch", 0);
  if (epoch != 0) {
    prefs.end();
    rtc.setTime(epoch);
    return;
  }
  int heure = prefs.getInt("heure", 12);
  int minute = prefs.getInt("minute", 0);
  int seconde = prefs.getInt("seconde", 0);
  int jour = prefs.getInt("jour", 1);
  int mois = prefs.getInt("mois", 1);
  int annee = prefs.getInt("annee", 2023);
  prefs.end();
  rtc.setTime(seconde, minute, heure, jour, mois, annee);
}

bool n3TimeLoadAndApplyToSystem(Preferences& prefs, ESP32Time& rtc) {
  n3TimeLoadFromFlash(prefs, rtc);
  const unsigned long epoch = rtc.getEpoch();
  if (epoch <= N3_TIME_MIN_VALID_EPOCH) {
    Serial.printf("[TIME] NVS epoch non fiable (%lu), horloge systeme non amorcee\n",
                  epoch);
    return false;
  }
  if (!n3TimeApplyEpochToSystem(epoch)) {
    Serial.println("[TIME][WARN] settimeofday(NVS) a echoue");
    return false;
  }
  Serial.printf("[TIME] Horloge amorcee depuis NVS epoch=%lu\n", epoch);
  return true;
}

bool n3TimeSyncNtp(ESP32Time& rtc,
                   long gmtOffsetSec,
                   int daylightOffsetSec,
                   const char* ntpServer,
                   uint32_t timeoutMs) {
  if (!ntpServer || ntpServer[0] == '\0') {
    return false;
  }

  configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);

  const uint32_t startMs = millis();
  struct tm timeinfo;
  while ((millis() - startMs) < timeoutMs) {
    if (getLocalTime(&timeinfo, 200)) {
      n3TimeSyncRtcFromSystem(rtc);
      const time_t epoch = time(nullptr);
      Serial.printf("[TIME] NTP ok epoch=%ld\n", static_cast<long>(epoch));
      return true;
    }
    delay(50);
  }

  Serial.printf("[TIME][WARN] NTP KO apres %lu ms (serveur=%s)\n",
                static_cast<unsigned long>(timeoutMs),
                ntpServer);
  return false;
}

bool n3TimeHasPlausibleEpoch(void) {
  return static_cast<unsigned long>(time(nullptr)) > N3_TIME_MIN_VALID_EPOCH;
}

void n3PrintWakeupReason(Preferences& prefs, ESP32Time& rtc) {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wakeup caused by external signal using RTC_IO");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wakeup caused by external signal using RTC_CNTL");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wakeup caused by timer");
      n3TimeLoadFromFlash(prefs, rtc);
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("Wakeup caused by touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("Wakeup caused by ULP program");
      break;
    default:
      Serial.printf("Wakeup was not caused by deep sleep: %d\n", cause);
      n3TimeLoadFromFlash(prefs, rtc);
      break;
  }
}

// Resync des composantes calendaires du firmware depuis le RTC — mutualisé
// depuis n3pp/msp (bloc identique dupliqué 3× : print_wakeup_reason des deux
// firmwares + HeureSansWifi n3pp). Mêmes conversions que l'original.
void n3TimeSyncBrokenDown(ESP32Time& rtc, int& seconde, int& minute, int& heure,
                          int& jour, int& mois, int& annee) {
  seconde = rtc.getTime("%S").toInt();
  minute = rtc.getTime("%M").toInt();
  heure = rtc.getTime("%H").toInt();
  jour = rtc.getTime("%d").toInt();
  mois = rtc.getTime("%m").toInt();
  annee = rtc.getTime("%Y").toInt();
}
