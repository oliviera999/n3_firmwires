#pragma once

// =============================================================================
// CONFIGURATION MÉMOIRE, BUFFERS ET AFFICHAGE - VERSION OPTIMISÉE
// =============================================================================
// Module: Configuration des buffers optimisée selon le type de board
// Taille: ~120 lignes (BufferConfig + JsonSize + DisplayConfig)
// =============================================================================

#include <Arduino.h>

// =============================================================================
// CONFIGURATION BUFFERS ET MÉMOIRE - OPTIMISÉE
// =============================================================================
namespace BufferConfig {
    // Configuration adaptative selon le type de board
    #if defined(BOARD_S3)
        // ESP32-S3: Plus de RAM disponible, buffers confortables
        constexpr uint32_t HTTP_BUFFER_SIZE = 4096;
        constexpr uint32_t HTTP_TX_BUFFER_SIZE = 4096;
        constexpr uint32_t JSON_DOCUMENT_SIZE = 4096;
        constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 6000;
        constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 5000;
    #else
        // ESP32-WROOM: Optimisé pour économie mémoire (-50%)
        constexpr uint32_t HTTP_BUFFER_SIZE = 2048;
        constexpr uint32_t HTTP_TX_BUFFER_SIZE = 2048;
        constexpr uint32_t JSON_DOCUMENT_SIZE = 2048;
        constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 3000;
        constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 2500;
    #endif
    
    // Tailles communes aux deux boards
    constexpr uint32_t JSON_PREVIEW_MAX_SIZE = 200;
    
    // Seuils de mémoire ajustés
    #if defined(BOARD_S3)
        constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 10000;
        constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 20000;
    #else
        constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 8000;
        constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 15000;
    #endif
}

// =============================================================================
// TAILLES JSON STANDARDISÉES
// =============================================================================
namespace JsonSize {
    // Petits JSON: status, commandes simples, réponses courtes
    constexpr size_t SMALL = 512;
    
    // JSON moyens: configuration, états capteurs, paramètres
    constexpr size_t MEDIUM = 1536;
    
    // Grands JSON: données complètes, OTA metadata, logs
    #if defined(BOARD_S3)
        constexpr size_t LARGE = 4096;
        constexpr size_t XLARGE = 8192;  // Disponible uniquement sur S3
    #else
        constexpr size_t LARGE = 3072;   // Réduit pour WROOM
    #endif
    
    // Helper pour sélectionner la taille appropriée
    constexpr size_t getOptimalSize(size_t expectedSize) {
        if (expectedSize <= SMALL) return SMALL;
        if (expectedSize <= MEDIUM) return MEDIUM;
        return LARGE;
    }
}

// =============================================================================
// MONITORING MÉMOIRE
// =============================================================================
namespace MemoryMonitor {
    // Structure pour stocker les statistiques
    struct Stats {
        uint32_t freeHeap;
        uint32_t largestBlock;
        uint32_t minFreeHeap;
        uint8_t fragmentation;  // Pourcentage
        
        void update() {
            freeHeap = ESP.getFreeHeap();
            largestBlock = ESP.getMaxAllocHeap();
            minFreeHeap = ESP.getMinFreeHeap();
            fragmentation = (freeHeap > 0) ? 
                100 - (100 * largestBlock / freeHeap) : 100;
        }
        
        bool isLowMemory() const {
            return freeHeap < BufferConfig::LOW_MEMORY_THRESHOLD_BYTES;
        }
        
        bool isCriticalMemory() const {
            return freeHeap < BufferConfig::CRITICAL_MEMORY_THRESHOLD_BYTES;
        }
    };
    
    // Logging helper
    inline void logStats(const char* context) {
        Stats stats;
        stats.update();
        
        const char* level = stats.isCriticalMemory() ? "[CRIT]" :
                           stats.isLowMemory() ? "[WARN]" : "[INFO]";
        
        // Note: Utiliser LOG_INFO du système de logs si disponible
        #if !defined(PROFILE_PROD) || (defined(ENABLE_SERIAL_MONITOR) && ENABLE_SERIAL_MONITOR == 1)
        Serial.printf("%s [MEM] %s: Free=%uKB (min=%uKB), Largest=%uKB, Frag=%u%%\n",
                     level, context, 
                     stats.freeHeap/1024, stats.minFreeHeap/1024,
                     stats.largestBlock/1024, stats.fragmentation);
        #endif
    }
}

// =============================================================================
// CONFIGURATION DISPLAY (INCHANGÉE)
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

    // Barres de progression
    constexpr int PROGRESS_BAR_X = 4;
    constexpr int PROGRESS_BAR_Y = 48;
    constexpr int PROGRESS_BAR_WIDTH = 120;
    constexpr int PROGRESS_BAR_HEIGHT = 10;
    constexpr int PROGRESS_BAR_ALT_Y = 40;

    // Rotation d'écran
    constexpr uint32_t SCREEN_SWITCH_INTERVAL_MS = 4000;

    // Paramètres OLED bas niveau
    constexpr uint8_t DISPLAY_WHITE = 1;
    constexpr uint8_t DISPLAY_BLACK = 0;
    constexpr uint8_t SSD1306_SWITCHCAPVCC = 0;
}
