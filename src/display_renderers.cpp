#include "display_renderers.h"

#include "display_view.h"
#include "project_config.h"
#include <Adafruit_SSD1306.h>
#include <cstring>

namespace {

void formatLevel(uint16_t value, char* buffer, size_t size) {
  if (value == 0) {
    strncpy(buffer, "--", size);
    buffer[size - 1] = '\0';
  } else {
    snprintf(buffer, size, "%u", value);
  }
}

}  // namespace

void MainScreenRenderer::render(DisplayView& view,
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
                                const String& apIp) {
  display.setTextSize(1);

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "FFP3 v%s %s", ProjectConfig::VERSION, ProjectConfig::PROFILE_TYPE);
    view.printClipped(0, 0, buf, 1);
  }

  if (wifiConnected) {
    view.printClipped(0, 8, stationSsid, 1);
    view.printClipped(0, 16, stationIp, 1);
  } else {
    view.printClipped(0, 8, apSsid, 1);
    view.printClipped(0, 16, apIp, 1);
  }

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "Te:%.1f Ta:%.1f", tempEau, tempAir);
    view.printClipped(0, 24, buf, 1);
  }

  {
    char buf[32];
    char aqBuf[6];
    char tkBuf[6];
    char ptBuf[6];
    formatLevel(aquaLvl, aqBuf, sizeof(aqBuf));
    formatLevel(tankLvl, tkBuf, sizeof(tkBuf));
    formatLevel(potaLvl, ptBuf, sizeof(ptBuf));
    snprintf(buf, sizeof(buf), "Aq:%s Tk:%s Pt:%s", aqBuf, tkBuf, ptBuf);
    view.printClipped(0, 32, buf, 1);
  }

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "H:%.1f%%  L:%u", humidite, lumi);
    view.printClipped(0, 40, buf, 1);
  }

  view.printClipped(0, 48, timeStr, 1);

  if (::MonitoringConfig::ENABLE_DRIFT_VISUAL_INDICATOR) {
    static unsigned long lastDriftCheck = 0;
    unsigned long now = millis();
    if (now - lastDriftCheck > ::MonitoringConfig::DRIFT_CHECK_INTERVAL_MS) {
      lastDriftCheck = now;
      if (!wifiConnected) {
        display.drawPixel(127, 0, WHITE);
      } else {
        display.drawPixel(127, 0, BLACK);
      }
    }
  }
}

void CountdownRenderer::render(DisplayView& view,
                               Adafruit_SSD1306& display,
                               const char* label,
                               uint16_t secondsLeft,
                               bool isManual) {
  view.printClipped(0, 0, String(label), 2);

  display.setTextSize(3);
  display.setCursor(0, 24);
  if (secondsLeft == 0) {
    display.setTextSize(2);
    display.print("Terminé");
  } else {
    display.printf("%u", secondsLeft);
  }

  display.setTextSize(1);
  if (isManual) {
    display.drawChar(110, 0, 'M', WHITE, BLACK, 1);
  } else {
    display.drawChar(110, 0, 'A', WHITE, BLACK, 1);
  }
}

void CountdownRenderer::renderFeeding(DisplayView& view,
                                      Adafruit_SSD1306& display,
                                      const char* fishType,
                                      const char* phase,
                                      uint16_t secondsLeft,
                                      bool isManual) {
  view.printClipped(0, 0, "Nourriture", 1);
  view.printClipped(0, 10, String(fishType), 2);
  view.printClipped(0, 26, String("Temps ") + phase, 1);

  display.setTextSize(2);
  display.setCursor(0, 36);
  if (secondsLeft == 0) {
    display.print("Terminé");
  } else {
    display.printf("%u", secondsLeft);
  }

  display.setTextSize(1);
  if (isManual) {
    display.drawChar(110, 0, 'M', WHITE, BLACK, 1);
  } else {
    display.drawChar(110, 0, 'A', WHITE, BLACK, 1);
  }
}

void InfoScreenRenderer::renderVariables(DisplayView& view,
                                         Adafruit_SSD1306& display,
                                         bool pumpAqua,
                                         bool pumpTank,
                                         bool heater,
                                         bool light) {
  display.setTextSize(1);
  view.printClipped(0, 0, "Relais:", 1);

  char buf[32];
  snprintf(buf, sizeof(buf), "Paq:%d Pta:%d", pumpAqua, pumpTank);
  view.printClipped(0, 8, buf, 1);

  snprintf(buf, sizeof(buf), "Heat:%d Light:%d", heater, light);
  view.printClipped(0, 16, buf, 1);
}

void InfoScreenRenderer::renderServerVars(DisplayView& view,
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
                                          uint16_t freqWake) {
  display.setTextSize(1);
  view.printClipped(0, 0, "Vars:", 1);

  char buf[64];
  snprintf(buf, sizeof(buf), "Paq:%d Pta:%d R:%d L:%d", pumpAqua, pumpTank, heater, light);
  view.printClipped(0, 8, buf, 1);

  snprintf(buf, sizeof(buf), "Feed h:%u %u %u", hMat, hMid, hSoir);
  view.printClipped(0, 16, buf, 1);

  snprintf(buf, sizeof(buf), "Tps P:%u G:%u", tPetits, tGros);
  view.printClipped(0, 24, buf, 1);

  snprintf(buf, sizeof(buf), "Lim Aq:%u Ta:%u", thAq, thTank);
  view.printClipped(0, 32, buf, 1);

  snprintf(buf, sizeof(buf), "Ch:%.1f R:%u F:%u", thHeat, tRemp, limFlood);
  view.printClipped(0, 40, buf, 1);

  snprintf(buf, sizeof(buf), "W:%d Fq:%us", wakeUp ? 1 : 0, freqWake);
  view.printClipped(0, 48, buf, 1);
}

void InfoScreenRenderer::appendDiagnosticLine(DisplayView& view,
                                              Adafruit_SSD1306& display,
                                              const String& line,
                                              uint8_t lineIndex) {
  display.setTextSize(1);
  view.printClipped(0, lineIndex * 8, line, 1);
}





