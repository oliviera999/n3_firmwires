#pragma once

#include <Wire.h>
#include <TouchLib.h>

#define TOUCH_MODULES_GT911
#define TOUCH_MODULE_ADDR GT911_SLAVE_ADDRESS1
#define TOUCH_SCL 4
#define TOUCH_SDA 8
#define TOUCH_RES 38

static bool touch_swap_xy = false;
static int16_t touch_map_x1 = -1;
static int16_t touch_map_x2 = -1;
static int16_t touch_map_y1 = -1;
static int16_t touch_map_y2 = -1;

static int16_t touch_max_x = 0;
static int16_t touch_max_y = 0;
static int16_t touch_raw_x = 0;
static int16_t touch_raw_y = 0;
static int16_t touch_last_x = 0;
static int16_t touch_last_y = 0;

static TouchLib touch(Wire, TOUCH_SDA, TOUCH_SCL, TOUCH_MODULE_ADDR);

static void touch_translate_raw() {
  if (touch_swap_xy) {
    touch_last_x = map(touch_raw_y, touch_map_x1, touch_map_x2, 0, touch_max_x);
    touch_last_y = map(touch_raw_x, touch_map_y1, touch_map_y2, 0, touch_max_y);
  } else {
    touch_last_x = map(touch_raw_x, touch_map_x1, touch_map_x2, 0, touch_max_x);
    touch_last_y = map(touch_raw_y, touch_map_y1, touch_map_y2, 0, touch_max_y);
  }
}

static void touch_init(int16_t w, int16_t h, uint8_t rotation) {
  touch_max_x = w - 1;
  touch_max_y = h - 1;
  if (touch_map_x1 == -1) {
    switch (rotation) {
      case 3:
        touch_swap_xy = true;
        touch_map_x1 = touch_max_x;
        touch_map_x2 = 0;
        touch_map_y1 = 0;
        touch_map_y2 = touch_max_y;
        break;
      case 2:
        touch_swap_xy = false;
        touch_map_x1 = touch_max_x;
        touch_map_x2 = 0;
        touch_map_y1 = touch_max_y;
        touch_map_y2 = 0;
        break;
      case 1:
        touch_swap_xy = true;
        touch_map_x1 = 0;
        touch_map_x2 = touch_max_x;
        touch_map_y1 = touch_max_y;
        touch_map_y2 = 0;
        break;
      default:
        touch_swap_xy = false;
        touch_map_x1 = 0;
        touch_map_x2 = touch_max_x;
        touch_map_y1 = 0;
        touch_map_y2 = touch_max_y;
        break;
    }
  }

  pinMode(TOUCH_RES, OUTPUT);
  digitalWrite(TOUCH_RES, LOW);
  delay(100);
  digitalWrite(TOUCH_RES, HIGH);
  delay(100);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  touch.init();
}

static bool touch_touched() {
  if (!touch.read()) return false;
  TP_Point t = touch.getPoint(0);
  touch_raw_x = t.x;
  touch_raw_y = t.y;
  touch_translate_raw();
  return true;
}
