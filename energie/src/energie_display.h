#pragma once
/** Banc energie — ecran OLED SSD1306 (n3_display), optionnel. */

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

extern Adafruit_SSD1306 g_display;
extern bool g_displayOk;

void displayBegin();
/** Rafraichit l'ecran (V/I/P des 3 voies, SoC, WiFi, dernier POST). */
void displayUpdate();
