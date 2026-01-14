#pragma once

#include <Arduino.h>

class DisplayView;
class Adafruit_SSD1306;

class MainScreenRenderer {
 public:
  static void render(DisplayView& view,
                     Adafruit_SSD1306& display,
                     float tempEau,
                     float tempAir,
                     float humidite,
                     uint16_t aquaLvl,
                     uint16_t tankLvl,
                     uint16_t potaLvl,
                     uint16_t lumi,
                     const String& timeStr,
                     bool wifiConnected,
                     const String& stationSsid,
                     const String& stationIp,
                     const String& apSsid,
                     const String& apIp);
};

class CountdownRenderer {
 public:
  static void render(DisplayView& view,
                     Adafruit_SSD1306& display,
                     const char* label,
                     uint16_t secondsLeft,
                     bool isManual);

  static void renderFeeding(DisplayView& view,
                            Adafruit_SSD1306& display,
                            const char* fishType,
                            const char* phase,
                            uint16_t secondsLeft,
                            bool isManual);
};

class InfoScreenRenderer {
 public:
  static void renderVariables(DisplayView& view,
                              Adafruit_SSD1306& display,
                              bool pumpAqua,
                              bool pumpTank,
                              bool heater,
                              bool light,
                              uint8_t hMat,
                              uint8_t hMid,
                              uint8_t hSoir,
                              uint16_t tPetits,
                              uint16_t tGros,
                              uint16_t thAq,
                              uint16_t thTank,
                              float thHeat,
                              uint16_t limFlood);

  static void renderServerVars(DisplayView& view,
                               Adafruit_SSD1306& display,
                               bool pumpAqua,
                               bool pumpTank,
                               bool heater,
                               bool light,
                               uint8_t hMat,
                               uint8_t hMid,
                               uint8_t hSoir,
                               uint16_t tPetits,
                               uint16_t tGros,
                               uint16_t thAq,
                               uint16_t thTank,
                               float thHeat,
                               uint16_t tRemp,
                               uint16_t limFlood,
                               bool wakeUp,
                               uint16_t freqWake);

  static void appendDiagnosticLine(DisplayView& view,
                                   Adafruit_SSD1306& display,
                                   const String& line,
                                   uint8_t lineIndex);
};












