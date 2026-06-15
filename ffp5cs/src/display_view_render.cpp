// display_view_render.cpp — formatage + rendu bas-niveau OLED de DisplayView.
// Extrait de display_view.cpp (audit : découpe). Méthodes membres, comportement identique.
#include "display_view.h"
#include "config.h"
#include "pins.h"
#include "i2c_bus.h"
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <algorithm>
#include <string.h>

// Helpers de formatage
void DisplayView::formatLevel(uint16_t value, char* buffer, size_t size) {
  if (value == 0) {
    strncpy(buffer, "--", size);
    buffer[size - 1] = '\0';
  } else {
    snprintf(buffer, size, "%u", value);
  }
}

void DisplayView::formatTemp(float value, char* buffer, size_t size) {
  if (isnan(value)) {
    strncpy(buffer, "--.-", size);
    buffer[size - 1] = '\0';
    return;
  }
  snprintf(buffer, size, "%.1f", value);
}

void DisplayView::formatHumidity(float value, char* buffer, size_t size) {
  if (isnan(value)) {
    strncpy(buffer, "--.-", size);
    buffer[size - 1] = '\0';
    return;
  }
  snprintf(buffer, size, "%.1f", value);
}

const char* DisplayView::onOffLabel(bool value) {
  return value ? "ON" : "OFF";
}

// Méthodes de rendu
void DisplayView::renderMainScreen(float tempEau, float tempAir, float humidite,
                                    uint16_t aquaLvl, uint16_t tankLvl, uint16_t potaLvl,
                                    uint16_t lumi, const char* timeStr, bool wifiConnected,
                                    const char* stationSsid, const char* stationIp,
                                    const char* apSsid, const char* apIp) {
  _disp.setTextSize(1);

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "FFP5CS v%s %s", ProjectConfig::VERSION, ProjectConfig::PROFILE_TYPE);
    printClipped(0, 0, buf, 1);
  }

  if (wifiConnected) {
    printClipped(0, 8, stationSsid, 1);
    printClipped(0, 16, stationIp, 1);
  } else {
    printClipped(0, 8, apSsid, 1);
    printClipped(0, 16, apIp, 1);
  }

  {
    char buf[32];
    char tempEauBuf[8];
    char tempAirBuf[8];
    formatTemp(tempEau, tempEauBuf, sizeof(tempEauBuf));
    formatTemp(tempAir, tempAirBuf, sizeof(tempAirBuf));
    snprintf(buf, sizeof(buf), "Eau:%sC Air:%sC", tempEauBuf, tempAirBuf);
    printClipped(0, 24, buf, 1);
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
    printClipped(0, 32, buf, 1);
  }

  {
    char buf[32];
    char humBuf[8];
    formatHumidity(humidite, humBuf, sizeof(humBuf));
    snprintf(buf, sizeof(buf), "Hum:%s%% Lum:%u", humBuf, lumi);
    printClipped(0, 40, buf, 1);
  }

  printClipped(0, 48, timeStr, 1);

  if (::MonitoringConfig::ENABLE_DRIFT_VISUAL_INDICATOR) {
    static unsigned long lastDriftCheck = 0;
    unsigned long now = millis();
    if (now - lastDriftCheck > ::MonitoringConfig::DRIFT_CHECK_INTERVAL_MS) {
      lastDriftCheck = now;
      if (!wifiConnected) {
        _disp.drawPixel(127, 0, WHITE);
      } else {
        _disp.drawPixel(127, 0, BLACK);
      }
    }
  }
}

void DisplayView::renderCountdown(const char* label, uint16_t secondsLeft, bool isManual) {
  printClipped(0, 0, label, 2);

  _disp.setTextSize(3);
  _disp.setCursor(0, 24);
  if (secondsLeft == 0) {
    _disp.setTextSize(2);
    _disp.print("Terminé");
  } else {
    _disp.printf("%u", secondsLeft);
  }

  _disp.setTextSize(1);
  if (isManual) {
    _disp.drawChar(110, 0, 'M', WHITE, BLACK, 1);
  } else {
    _disp.drawChar(110, 0, 'A', WHITE, BLACK, 1);
  }
}

void DisplayView::renderFeedingCountdown(const char* fishType, const char* phase,
                                         uint16_t secondsLeft, bool isManual) {
  printClipped(0, 0, "Nourriture", 1);
  printClipped(0, 10, fishType, 2);
  char tempsBuf[64];
  snprintf(tempsBuf, sizeof(tempsBuf), "Temps %s", phase);
  printClipped(0, 26, tempsBuf, 1);

  _disp.setTextSize(2);
  _disp.setCursor(0, 36);
  if (secondsLeft == 0) {
    _disp.print("Terminé");
  } else {
    _disp.printf("%u", secondsLeft);
  }

  _disp.setTextSize(1);
  if (isManual) {
    _disp.drawChar(110, 0, 'M', WHITE, BLACK, 1);
  } else {
    _disp.drawChar(110, 0, 'A', WHITE, BLACK, 1);
  }
}

void DisplayView::renderVariables(bool pumpAqua, bool pumpTank, bool heater, bool light,
                                  uint8_t hMat, uint8_t hMid, uint8_t hSoir,
                                  uint16_t tPetits, uint16_t tGros,
                                  uint16_t thAq, uint16_t thTank, float thHeat,
                                  uint16_t limFlood) {
  _disp.setTextSize(1);
  printClipped(0, 0, "Relais+Cfg:", 1);

  char buf[32];
  snprintf(buf, sizeof(buf), "PomAq:%s PomTk:%s", onOffLabel(pumpAqua), onOffLabel(pumpTank));
  printClipped(0, 8, buf, 1);

  snprintf(buf, sizeof(buf), "Chauff:%s Lumi:%s", onOffLabel(heater), onOffLabel(light));
  printClipped(0, 16, buf, 1);

  snprintf(buf, sizeof(buf), "Feed h:%02u %02u %02u", hMat, hMid, hSoir);
  printClipped(0, 24, buf, 1);

  snprintf(buf, sizeof(buf), "Tps P:%us G:%us", tPetits, tGros);
  printClipped(0, 32, buf, 1);

  snprintf(buf, sizeof(buf), "Seuil Aq:%u Ta:%u", thAq, thTank);
  printClipped(0, 40, buf, 1);

  snprintf(buf, sizeof(buf), "Ch:%.1fC F:%ucm", thHeat, limFlood);
  printClipped(0, 48, buf, 1);
}

void DisplayView::renderServerVars(bool pumpAqua, bool pumpTank, bool heater, bool light,
                                   uint8_t hMat, uint8_t hMid, uint8_t hSoir,
                                   uint16_t tPetits, uint16_t tGros,
                                   uint16_t thAq, uint16_t thTank, float thHeat,
                                   uint16_t tRemp, uint16_t limFlood,
                                   bool wakeUp, uint16_t freqWake) {
  _disp.setTextSize(1);
  printClipped(0, 0, "Vars:", 1);

  char buf[64];
  snprintf(buf, sizeof(buf), "Paq:%d Pta:%d R:%d L:%d", pumpAqua, pumpTank, heater, light);
  printClipped(0, 8, buf, 1);

  snprintf(buf, sizeof(buf), "Feed h:%u %u %u", hMat, hMid, hSoir);
  printClipped(0, 16, buf, 1);

  snprintf(buf, sizeof(buf), "Tps P:%u G:%u", tPetits, tGros);
  printClipped(0, 24, buf, 1);

  snprintf(buf, sizeof(buf), "Lim Aq:%u Ta:%u", thAq, thTank);
  printClipped(0, 32, buf, 1);

  snprintf(buf, sizeof(buf), "Ch:%.1f R:%u F:%u", thHeat, tRemp, limFlood);
  printClipped(0, 40, buf, 1);

  snprintf(buf, sizeof(buf), "W:%d Fq:%us", wakeUp ? 1 : 0, freqWake);
  printClipped(0, 48, buf, 1);
}

void DisplayView::renderStatusBar(const StatusBarParams& params) {
  // Dessiner la barre d'état sur la dernière ligne de l'écran (8 pixels de hauteur)
  const int barY = DisplayConfig::STATUS_BAR_Y;

  // Effacer la zone de la barre d'état
  _disp.fillRect(0, barY, 128, DisplayConfig::STATUS_BAR_HEIGHT, BLACK);

  // WiFi RSSI indicator (gauche)
  _disp.setCursor(0, barY);
  _disp.setTextSize(1);
  _disp.setTextColor(WHITE);

  if (params.rssi >= -50) {
    _disp.print(F("WiFi +++"));
  } else if (params.rssi >= -70) {
    _disp.print(F("WiFi ++ "));
  } else if (params.rssi >= -85) {
    _disp.print(F("WiFi +  "));
  } else if (params.rssi > -100) {
    _disp.print(F("WiFi -  "));
  } else {
    _disp.print(F("WiFi X  "));
  }

  // Indicateur send/recv (milieu)
  _disp.setCursor(60, barY);
  if (params.sendState == 1) {
    _disp.print(F("S"));
  } else if (params.sendState == -1) {
    _disp.print(F("!"));
  } else {
    _disp.print(F(" "));
  }

  if (params.recvState == 1) {
    _disp.print(F("R"));
  } else if (params.recvState == -1) {
    _disp.print(F("!"));
  } else {
    _disp.print(F(" "));
  }

  // Indicateur marée (droite)
  _disp.setCursor(80, barY);
  if (params.tideDir > 0) {
    _disp.print(F("^")); // Marée montante
  } else if (params.tideDir < 0) {
    _disp.print(F("v")); // Marée descendante
  } else {
    _disp.print(F("-")); // Stable
  }

  // Mail blink indicator
  if (params.mailBlink) {
    _disp.setCursor(90, barY);
    _disp.print(F("@"));
  }

  // OTA progress overlay (si actif)
  if (params.otaOverlayActive) {
    _disp.setCursor(100, barY);
    _disp.printf("%d%%", params.otaPercent);
  }
}

void DisplayView::appendDiagnosticLine(const char* line, uint8_t lineIndex) {
  _disp.setTextSize(1);
  printClipped(0, lineIndex * 8, line, 1);
}