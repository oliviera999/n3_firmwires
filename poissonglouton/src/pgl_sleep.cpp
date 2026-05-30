#include "pgl_sleep.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "config.h"
#include "pgl_log.h"
#include "n3_sleep.h"

void PglSleep::configure(bool useIrWakeup, unsigned long timerSeconds) {
  const unsigned long safeTimer = (timerSeconds == 0) ? 1 : timerSeconds;
  if (useIrWakeup) {
    PGL_LOG_V("Sleep config: EXT0 GPIO%d niveau=0 + timer %lus", PGL_IR_PIN, safeTimer);
    N3SleepConfig cfg{static_cast<gpio_num_t>(PGL_IR_PIN), 0, safeTimer};
    n3SleepConfigure(cfg);
  } else {
    PGL_LOG_V("Sleep config: timer seul %lus (IR absent)", safeTimer);
    esp_sleep_enable_timer_wakeup(safeTimer * N3_US_TO_S_FACTOR);
  }
}

void PglSleep::start() {
  PGL_LOG("Sleep: entree deep sleep (flush serie...)");
  Serial.flush();
  n3SleepStart();
}
