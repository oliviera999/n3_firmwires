#pragma once

#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

class PglAudio {
 public:
  void begin(HardwareSerial& serialPort);
  void playThanks();
  bool isAvailable() const;

 private:
  DFRobotDFPlayerMini player_;
  bool available_ = false;
  uint16_t lastTrack_ = 0;
};
