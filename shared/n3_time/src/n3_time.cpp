#include "n3_time.h"
#include <ESP32Time.h>
#include <esp_sleep.h>

void n3TimeSaveToFlash(ESP32Time& rtc, Preferences& prefs) {
  // Une seule clé epoch (1 écriture NVS au lieu de 6 : usure flash -83 %,
  // pas de String temporaires getTime()/toInt()).
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
  // Migration : anciennes sauvegardes en 6 clés (heure/minute/...) si présentes.
  int heure = prefs.getInt("heure", 12);
  int minute = prefs.getInt("minute", 0);
  int seconde = prefs.getInt("seconde", 0);
  int jour = prefs.getInt("jour", 1);
  int mois = prefs.getInt("mois", 1);
  int annee = prefs.getInt("annee", 2023);
  prefs.end();
  rtc.setTime(seconde, minute, heure, jour, mois, annee);
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
