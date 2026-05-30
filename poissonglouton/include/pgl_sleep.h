#pragma once

class PglSleep {
 public:
  void configure(bool useIrWakeup, unsigned long timerSeconds);
  void start();
};
