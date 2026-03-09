#pragma once

#include <Arduino.h>

#define N3_US_TO_S_FACTOR 1000000ULL

struct N3SleepConfig {
  gpio_num_t wakeupGpio;   // GPIO pour reveil externe (ex: GPIO_NUM_4)
  int wakeupLevel;         // niveau pour reveil (HIGH ou LOW)
  unsigned long sleepSeconds;
};

/**
 * Configure le timer deep sleep et le reveil par GPIO externe.
 * A appeler dans setup() apres le calcul de sleepSeconds.
 */
void n3SleepConfigure(const N3SleepConfig& config);

/**
 * Entre en deep sleep immediatement.
 * Flush le Serial avant de dormir.
 */
void n3SleepStart();
