#pragma once

// Tactile integre AXS15231B (JC3248W535) — I2C direct, pas GT911/TouchLib.

#include <Wire.h>

#define PGL_TOUCH_AXS_SDA 4
#define PGL_TOUCH_AXS_SCL 8
#define PGL_TOUCH_AXS_INT 3
#define PGL_TOUCH_AXS_ADDR 0x3B
#define PGL_TOUCH_AXS_I2C_CLOCK 400000

static int16_t touch_axs_max_x = 0;
static int16_t touch_axs_max_y = 0;
static int16_t touch_axs_last_x = 0;
static int16_t touch_axs_last_y = 0;
static bool touch_axs_pressed = false;

static bool touch_axs_read_raw(uint16_t* outX, uint16_t* outY) {
  const uint8_t cmd[] = {0xB5, 0xAB, 0xA5, 0x5A};
  Wire.beginTransmission(PGL_TOUCH_AXS_ADDR);
  Wire.write(cmd, sizeof(cmd));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(PGL_TOUCH_AXS_ADDR, static_cast<uint8_t>(8)) != 8) {
    return false;
  }
  const uint8_t b0 = Wire.read();
  const uint8_t b1 = Wire.read();
  const uint8_t b2 = Wire.read();
  const uint8_t b3 = Wire.read();
  Wire.read();
  Wire.read();
  Wire.read();
  Wire.read();
  if ((b0 & 0x80) == 0) {
    return false;
  }
  const uint16_t x = static_cast<uint16_t>(((b0 & 0x0F) << 8) | b1);
  const uint16_t y = static_cast<uint16_t>(((b2 & 0x0F) << 8) | b3);
  if (outX) {
    *outX = x;
  }
  if (outY) {
    *outY = y;
  }
  return true;
}

static void touch_axs_init(int16_t w, int16_t h, uint8_t /*rotation*/) {
  touch_axs_max_x = w - 1;
  touch_axs_max_y = h - 1;
  pinMode(PGL_TOUCH_AXS_INT, INPUT_PULLUP);
  Wire.begin(PGL_TOUCH_AXS_SDA, PGL_TOUCH_AXS_SCL);
  Wire.setClock(PGL_TOUCH_AXS_I2C_CLOCK);
  delay(50);
}

static bool touch_axs_has_signal() {
  return digitalRead(PGL_TOUCH_AXS_INT) == LOW;
}

static bool touch_axs_touched() {
  uint16_t rawX = 0;
  uint16_t rawY = 0;
  if (!touch_axs_read_raw(&rawX, &rawY)) {
    touch_axs_pressed = false;
    return false;
  }
  if (rawX > static_cast<uint16_t>(touch_axs_max_x)) {
    rawX = static_cast<uint16_t>(touch_axs_max_x);
  }
  if (rawY > static_cast<uint16_t>(touch_axs_max_y)) {
    rawY = static_cast<uint16_t>(touch_axs_max_y);
  }
  touch_axs_last_x = static_cast<int16_t>(rawX);
  touch_axs_last_y = static_cast<int16_t>(rawY);
  touch_axs_pressed = true;
  return true;
}

static bool touch_axs_released() {
  if (touch_axs_pressed && !touch_axs_has_signal()) {
    touch_axs_pressed = false;
    return true;
  }
  return false;
}
