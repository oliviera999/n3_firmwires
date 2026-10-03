#include "energie_board.h"

#include <Wire.h>

#include "energie_config.h"

namespace {
const char* guessDevice(uint8_t addr) {
  if (addr >= 0x40 && addr <= 0x4F) return "INA2xx (INA226 ?)";
  switch (addr) {
    case 0x3C:
    case 0x3D:
      return "OLED SSD1306";
    case 0x57:
      return "EEPROM AT24C32 (module DS3231)";
    case 0x68:
      return "RTC DS3231";
    case 0x76:
    case 0x77:
      return "BME280";
    case 0x20:
    case 0x27:
      return "PCF8574";
    default:
      return "?";
  }
}
}  // namespace

void boardBegin() {
  static const int kSafeLow[] = ENERGIE_SAFE_LOW_PINS;
  for (int pin : kSafeLow) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
#if ENERGIE_PIN_RAIL_GATE >= 0
  pinMode(ENERGIE_PIN_RAIL_GATE, OUTPUT);
  digitalWrite(ENERGIE_PIN_RAIL_GATE, HIGH);
  delay(ENERGIE_RAIL_SETTLE_MS);
#endif
#if ENERGIE_PIN_INA_ALERT >= 0
  pinMode(ENERGIE_PIN_INA_ALERT, INPUT_PULLUP);  // ALERT open-drain
#endif
  Wire.begin(ENERGIE_PIN_SDA, ENERGIE_PIN_SCL, ENERGIE_I2C_HZ);
  Serial.printf("[I2C] bus SDA=%d SCL=%d %lu Hz\n", ENERGIE_PIN_SDA, ENERGIE_PIN_SCL,
                static_cast<unsigned long>(ENERGIE_I2C_HZ));
}

void boardI2cScan() {
  Serial.println("[I2C] scan 0x08..0x77");
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C]   0x%02X  %s\n", addr, guessDevice(addr));
      ++found;
    }
  }
  Serial.printf("[I2C] %u peripherique(s)\n", found);
}
