#pragma once

#include <stdint.h>

class DisplayView;
class Adafruit_SSD1306;

struct StatusBarParams {
  int8_t sendState;
  int8_t recvState;
  int8_t rssi;
  bool mailBlink;
  int8_t tideDir;
  int diffValue;
  bool otaOverlayActive;
  uint8_t otaPercent;
};

class StatusBarRenderer {
 public:
  static void draw(DisplayView& view, Adafruit_SSD1306& display, const StatusBarParams& params);
};












