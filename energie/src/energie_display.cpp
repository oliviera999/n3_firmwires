#include "energie_display.h"

#include <Wire.h>
#include <math.h>

#include "energie_config.h"
#include "energie_network.h"
#include "energie_sensors.h"
#include "n3_display.h"

Adafruit_SSD1306 g_display(N3_OLED_WIDTH, N3_OLED_HEIGHT, &Wire, -1);
bool g_displayOk = false;

void displayBegin() {
  // Wire est deja ouvert sur SDA 8 / SCL 9 (boardBegin) : le Wire.begin() sans
  // broches de n3DisplayInit est alors sans effet.
  g_displayOk = n3DisplayInit(g_display, ENERGIE_OLED_ADDR);
  if (!g_displayOk) {
    Serial.println("[OLED] absent (le banc fonctionne sans ecran)");
    return;
  }
  g_display.clearDisplay();
  g_display.setTextSize(1);
  g_display.setTextColor(SSD1306_WHITE);
  g_display.setCursor(0, 0);
  g_display.println("Banc energie INA226");
  g_display.print("v");
  g_display.println(FIRMWARE_VERSION);
  g_display.display();
}

void displayUpdate() {
  if (!g_displayOk) return;
  g_display.clearDisplay();
  g_display.setTextSize(1);
  g_display.setCursor(0, 0);
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) {
    const EnergieChannel& c = g_channels[i];
    if (!c.configured || !c.lastOk) {
      g_display.printf("%-3.3s  --- absent ---\n", c.name);
      continue;
    }
    g_display.printf("%-3.3s%5.2fV%+6.3fA\n", c.name, c.last.busV, c.last.currentA);
    g_display.printf("   %+7.2fW%s\n", c.last.powerW, c.last.saturated ? " SAT!" : "");
  }
  const EnergieNetStatus& net = networkStatus();
  if (g_batterySoc.seeded()) {
    g_display.printf("SoC %3.0f%% %+.3fAh\n", g_batterySoc.socPercent(), g_batterySoc.netAh());
  } else {
    g_display.println("SoC ?");
  }
  if (net.wifiConnected) {
    g_display.printf("%ddBm POST %d\n", net.rssi, net.lastPostCode);
  } else {
    g_display.println("WiFi --");
  }
  g_display.display();
}
