#include "n3_sleep.h"
#include <esp_sleep.h>

void n3SleepConfigure(const N3SleepConfig& config) {
  esp_sleep_enable_timer_wakeup(config.sleepSeconds * N3_US_TO_S_FACTOR);
  esp_sleep_enable_ext0_wakeup(config.wakeupGpio, config.wakeupLevel);
}

void n3SleepStart() {
  Serial.flush();
  esp_deep_sleep_start();
}
