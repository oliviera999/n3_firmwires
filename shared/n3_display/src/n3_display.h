#pragma once

#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define N3_OLED_WIDTH  128
#define N3_OLED_HEIGHT 64
#define N3_OLED_ADDR   0x3C

/**
 * Detecte l'ecran OLED via I2C et l'initialise si present.
 * Retourne true si l'ecran est operationnel, false sinon.
 */
bool n3DisplayInit(Adafruit_SSD1306& display, uint8_t addr = N3_OLED_ADDR);
