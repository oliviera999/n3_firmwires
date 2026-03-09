#include "n3_display.h"

bool n3DisplayInit(Adafruit_SSD1306& display, uint8_t addr) {
  Wire.begin();
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() != 0) {
    Serial.println("OLED non detecte sur I2C");
    return false;
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, addr)) {
    Serial.println("OLED init echouee");
    return false;
  }
  return true;
}
