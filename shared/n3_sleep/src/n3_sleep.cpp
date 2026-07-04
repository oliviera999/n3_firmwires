#include "n3_sleep.h"
#include <esp_sleep.h>

void n3SleepConfigure(const N3SleepConfig& config) {
  // sleepSeconds == 0 signifie "reveil GPIO uniquement" (mode urgence batterie).
  // esp_sleep_enable_timer_wakeup(0) n'est PAS traite comme "timer desactive" par
  // l'ESP-IDF : l'alarme se declenche a now+0 -> reveil immediat et boucle de
  // reboot qui vide la batterie au pire moment. Il faut donc desactiver
  // explicitement la source timer dans ce cas.
  if (config.sleepSeconds == 0) {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  } else {
    esp_sleep_enable_timer_wakeup(config.sleepSeconds * N3_US_TO_S_FACTOR);
  }
  esp_sleep_enable_ext0_wakeup(config.wakeupGpio, config.wakeupLevel);
}

void n3SleepStart() {
  Serial.flush();
  esp_deep_sleep_start();
}
