#pragma once
// config_display.h — géométrie OLED, barre d'état, overlay OTA, splash.
// Extrait de config.h (découpe du god-header).
#include <Arduino.h>

// -----------------------------------------------------------------------------
// 7. DISPLAY
// -----------------------------------------------------------------------------
namespace DisplayConfig {
    inline constexpr uint8_t OLED_WIDTH = 128;
    inline constexpr uint8_t OLED_HEIGHT = 64;
    // Adresse I2C OLED : source unique dans include/pins.h (Pins::OLED_ADDR)

    inline constexpr int PERCENTAGE_MAX = 100;
    
    // Intervalle de rafraîchissement OLED pour automatismes
    inline constexpr uint32_t OLED_INTERVAL_MS = 80;
    inline constexpr uint32_t OLED_COUNTDOWN_INTERVAL_MS = 250;

    // Status bar layout (barre d'état sur la dernière ligne de l'écran)
    inline constexpr int STATUS_BAR_HEIGHT = 8;
    inline constexpr int STATUS_BAR_Y = OLED_HEIGHT - STATUS_BAR_HEIGHT;  // 56 : dernière ligne
    inline constexpr int STATUS_BAR_WIFI_X = 0;       // Position indicateur WiFi
    inline constexpr int STATUS_BAR_SENDRECV_X = 60;  // Position indicateurs S/R
    inline constexpr int STATUS_BAR_TIDE_X = 80;      // Position indicateur marée
    inline constexpr int STATUS_BAR_MAIL_X = 90;      // Position indicateur mail

    // OTA overlay position
    inline constexpr int OTA_OVERLAY_X_POS = 100;
    inline constexpr int OTA_OVERLAY_Y_POS = 0;
    inline constexpr int OTA_OVERLAY_WIDTH = 28;
    inline constexpr int OTA_OVERLAY_HEIGHT = 8;
    
    inline constexpr uint32_t SPLASH_DURATION_MS = 3000;  // Durée du splash screen (3 secondes)
    inline constexpr uint32_t SCREEN_SWITCH_INTERVAL_MS = 6000;
    
    inline constexpr uint8_t DISPLAY_WHITE = 1;
    inline constexpr uint8_t DISPLAY_BLACK = 0;
    // VCC OLED : utiliser la macro SSD1306_SWITCHCAPVCC de la lib Adafruit (0x02) dans display_view.cpp
}
