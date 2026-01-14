#pragma once

// =============================================================================
// CONFIGURATION MÉMOIRE, BUFFERS ET AFFICHAGE
// =============================================================================
// Module: Configuration des buffers, gestion mémoire et affichage OLED
// Taille: ~77 lignes (BufferConfig + DisplayConfig)
// =============================================================================

#include <Arduino.h>

// =============================================================================
// CONFIGURATION BUFFERS ET MÉMOIRE
// =============================================================================
namespace BufferConfig {
    // Tailles de buffers HTTP
    constexpr uint32_t HTTP_BUFFER_SIZE = 4096;
    constexpr uint32_t HTTP_TX_BUFFER_SIZE = 4096;
    
    // Tailles de documents JSON
    constexpr uint32_t JSON_DOCUMENT_SIZE = 4096;
    
    // Limites de taille pour emails
    constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 6000;
    constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 5000;
    constexpr uint32_t JSON_PREVIEW_MAX_SIZE = 200;
    
    // Seuils de mémoire
    constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 5000;
    constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 10000;
}

// =============================================================================
// CONFIGURATION DISPLAY ÉTENDUE
// =============================================================================
namespace DisplayConfig {
    constexpr uint8_t OLED_WIDTH = 128;
    constexpr uint8_t OLED_HEIGHT = 64;
    constexpr uint8_t OLED_I2C_ADDR = 0x3C;
    
    // Configuration overlay OTA
    constexpr int OTA_OVERLAY_X_POS = 100;
    constexpr int OTA_OVERLAY_Y_POS = 0;
    constexpr int OTA_OVERLAY_WIDTH = 28;
    constexpr int OTA_OVERLAY_HEIGHT = 8;
    
    // Durée splash screen
    constexpr uint32_t SPLASH_DURATION_MS = 2000;
    
    // Limites pourcentage
    constexpr int PERCENTAGE_MAX = 100;

    // ========================================
    // BARRES DE PROGRESSION
    // ========================================
    constexpr int PROGRESS_BAR_X = 4;
    constexpr int PROGRESS_BAR_Y = 48;
    constexpr int PROGRESS_BAR_WIDTH = 120;
    constexpr int PROGRESS_BAR_HEIGHT = 10;
    constexpr int PROGRESS_BAR_ALT_Y = 40;

    // ========================================
    // ROTATION D'ÉCRAN
    // ========================================
    constexpr uint32_t SCREEN_SWITCH_INTERVAL_MS = 4000;    // Intervalle changement écran (4s)

    // ========================================
    // PARAMÈTRES OLED BAS NIVEAU
    // ========================================
    constexpr uint8_t DISPLAY_WHITE = 1;
    constexpr uint8_t DISPLAY_BLACK = 0;
    constexpr uint8_t SSD1306_SWITCHCAPVCC = 0;
}
