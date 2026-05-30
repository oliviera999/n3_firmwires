#pragma once

#include <Arduino.h>

class PglDisplay {
 public:
  void begin();
  void update();
  void onBottleCount(uint32_t totalCount, uint32_t todayCount);
  void showIdle();
  void sleepBacklight();
  bool adminUnlocked() const;
};
