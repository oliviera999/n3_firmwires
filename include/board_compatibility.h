#pragma once

#include <Arduino.h>

/**
 * Configuration de compatibilité entre ESP32 et ESP32-S3
 * Définit les paramètres optimaux selon le type de board détecté
 */
class BoardCompatibility {
public:
    // Détection automatique du type de board
    #ifdef CONFIG_IDF_TARGET_ESP32S3
        #define BOARD_S3
        static constexpr bool IS_ESP32_S3 = true;
        static constexpr bool IS_ESP32_WROOM = false;
    #elif defined(CONFIG_IDF_TARGET_ESP32)
        #define BOARD_WROOM
        static constexpr bool IS_ESP32_S3 = false;
        static constexpr bool IS_ESP32_WROOM = true;
    #else
        #define BOARD_UNKNOWN
        static constexpr bool IS_ESP32_S3 = false;
        static constexpr bool IS_ESP32_WROOM = false;
    #endif
    
    /**
     * Configuration des tailles de buffers selon le board
     */
    struct BufferSizes {
        static constexpr size_t JSON_POOL_SIZE = 
            IS_ESP32_S3 ? 8 : 6;  // Plus de documents en pool pour S3
        
        static constexpr size_t JSON_DOC_SMALL = 
            IS_ESP32_S3 ? 512 : 256;
        
        static constexpr size_t JSON_DOC_MEDIUM = 
            IS_ESP32_S3 ? 2048 : 1024;
        
        static constexpr size_t JSON_DOC_LARGE = 
            IS_ESP32_S3 ? 8192 : 4096;
        
        static constexpr size_t JSON_DOC_XLARGE = 
            IS_ESP32_S3 ? 16384 : 8192;
        
        static constexpr size_t HTTP_BUFFER_SIZE = 
            IS_ESP32_S3 ? 8192 : 4096;
        
        static constexpr size_t WEBSOCKET_BUFFER_SIZE = 
            IS_ESP32_S3 ? 2048 : 1024;
        
        static constexpr size_t MAX_COMPRESSION_SIZE = 
            IS_ESP32_S3 ? 16384 : 8192;
    };
    
    /**
     * Configuration des intervalles de timing
     */
    struct TimingConfig {
        static constexpr unsigned long SENSOR_CACHE_DURATION_MS = 
            IS_ESP32_S3 ? 300 : 1000;  // Cache plus agressif pour S3
        
        static constexpr unsigned long PUMP_STATS_CACHE_DURATION_MS = 
            IS_ESP32_S3 ? 200 : 500;
        
        static constexpr unsigned long WEBSOCKET_BROADCAST_INTERVAL_MS = 
            IS_ESP32_S3 ? 250 : 1000;  // Mises à jour plus fréquentes pour S3
        
        static constexpr unsigned long HTTP_POLLING_INTERVAL_MS = 
            IS_ESP32_S3 ? 2000 : 5000;  // Polling moins fréquent pour S3 (WebSocket prioritaire)
    };
    
    /**
     * Configuration des connexions simultanées
     */
    struct ConnectionLimits {
        static constexpr uint8_t MAX_WEBSOCKET_CLIENTS = 
            IS_ESP32_S3 ? 8 : 4;  // Plus de connexions simultanées pour S3
        
        static constexpr uint8_t MAX_HTTP_CONNECTIONS = 
            IS_ESP32_S3 ? 16 : 8;
        
        static constexpr uint8_t MAX_ASYNC_TASKS = 
            IS_ESP32_S3 ? 12 : 6;
    };
    
    /**
     * Configuration de la mémoire
     */
    struct MemoryConfig {
        static constexpr size_t LOW_MEMORY_THRESHOLD = 
            IS_ESP32_S3 ? 50000 : 10000;  // Seuils plus élevés pour S3
        
        static constexpr size_t CRITICAL_MEMORY_THRESHOLD = 
            IS_ESP32_S3 ? 20000 : 5000;
        
        static constexpr size_t PSRAM_USAGE_THRESHOLD = 
            IS_ESP32_S3 ? 4096 : 0;  // Seuil pour utiliser PSRAM (S3 seulement)
        
        static constexpr bool ENABLE_PSRAM_OPTIMIZATION = 
            IS_ESP32_S3 ? true : false;
    };
    
    /**
     * Configuration des performances
     */
    struct PerformanceConfig {
        static constexpr bool ENABLE_AGGRESSIVE_CACHING = 
            IS_ESP32_S3 ? true : false;
        
        static constexpr bool ENABLE_COMPRESSION_OPTIMIZATION = 
            IS_ESP32_S3 ? true : false;
        
        static constexpr bool ENABLE_WEBSOCKET_PRIORITY = 
            IS_ESP32_S3 ? true : false;
        
        static constexpr uint8_t JSON_POOL_PRIORITY = 
            IS_ESP32_S3 ? 2 : 1;  // Priorité plus élevée pour S3
    };
    
    /**
     * Obtient le nom du board détecté
     */
    static const char* getBoardName() {
        #ifdef BOARD_S3
        return "ESP32-S3";
        #elif defined(BOARD_WROOM)
        return "ESP32-WROOM";
        #else
        return "ESP32-Unknown";
        #endif
    }
    
    /**
     * Obtient les informations détaillées du board
     */
    struct BoardInfo {
        const char* name;
        bool isS3;
        bool isWroom;
        size_t heapSize;
        size_t freeHeap;
        size_t psramSize;
        size_t freePsram;
        uint8_t cpuFreq;
        uint8_t flashSize;
    };
    
    static BoardInfo getBoardInfo() {
        BoardInfo info;
        info.name = getBoardName();
        info.isS3 = IS_ESP32_S3;
        info.isWroom = IS_ESP32_WROOM;
        info.heapSize = ESP.getHeapSize();
        info.freeHeap = ESP.getFreeHeap();
        
        #ifdef BOARD_S3
        info.psramSize = ESP.getPsramSize();
        info.freePsram = ESP.getFreePsram();
        #else
        info.psramSize = 0;
        info.freePsram = 0;
        #endif
        
        info.cpuFreq = ESP.getCpuFreqMHz();
        info.flashSize = ESP.getFlashChipSize() / (1024 * 1024); // En MB
        
        return info;
    }
    
    /**
     * Log les informations de compatibilité au démarrage
     */
    static void logCompatibilityInfo() {
        BoardInfo info = getBoardInfo();
        
        Serial.printf("[Board] Type: %s\n", info.name);
        Serial.printf("[Board] Heap: %u bytes (libre: %u bytes)\n", info.heapSize, info.freeHeap);
        
        if (info.isS3) {
            Serial.printf("[Board] PSRAM: %u bytes (libre: %u bytes)\n", info.psramSize, info.freePsram);
        }
        
        Serial.printf("[Board] CPU: %u MHz\n", info.cpuFreq);
        Serial.printf("[Board] Flash: %u MB\n", info.flashSize);
        
        // Configuration appliquée
        Serial.printf("[Config] JSON Pool: %u documents\n", BufferSizes::JSON_POOL_SIZE);
        Serial.printf("[Config] WebSocket clients max: %u\n", ConnectionLimits::MAX_WEBSOCKET_CLIENTS);
        Serial.printf("[Config] Cache capteurs: %lu ms\n", TimingConfig::SENSOR_CACHE_DURATION_MS);
        Serial.printf("[Config] Cache pompe: %lu ms\n", TimingConfig::PUMP_STATS_CACHE_DURATION_MS);
        Serial.printf("[Config] WebSocket broadcast: %lu ms\n", TimingConfig::WEBSOCKET_BROADCAST_INTERVAL_MS);
        
        if (PerformanceConfig::ENABLE_AGGRESSIVE_CACHING) {
            Serial.println("[Config] Cache agressif: Activé");
        }
        
        if (PerformanceConfig::ENABLE_PSRAM_OPTIMIZATION) {
            Serial.println("[Config] Optimisation PSRAM: Activée");
        }
    }
    
    /**
     * Vérifie si une optimisation est recommandée
     */
    static bool isOptimizationRecommended(const char* optimization) {
        if (strcmp(optimization, "aggressive_caching") == 0) {
            return PerformanceConfig::ENABLE_AGGRESSIVE_CACHING;
        }
        
        if (strcmp(optimization, "psram_usage") == 0) {
            return PerformanceConfig::ENABLE_PSRAM_OPTIMIZATION;
        }
        
        if (strcmp(optimization, "websocket_priority") == 0) {
            return PerformanceConfig::ENABLE_WEBSOCKET_PRIORITY;
        }
        
        if (strcmp(optimization, "compression") == 0) {
            return PerformanceConfig::ENABLE_COMPRESSION_OPTIMIZATION;
        }
        
        return false;
    }
    
    /**
     * Obtient une recommandation de taille pour un type de buffer
     */
    static size_t getRecommendedBufferSize(const char* bufferType, size_t requestedSize) {
        if (strcmp(bufferType, "json_small") == 0) {
            return BufferSizes::JSON_DOC_SMALL;
        }
        
        if (strcmp(bufferType, "json_medium") == 0) {
            return BufferSizes::JSON_DOC_MEDIUM;
        }
        
        if (strcmp(bufferType, "json_large") == 0) {
            return BufferSizes::JSON_DOC_LARGE;
        }
        
        if (strcmp(bufferType, "json_xlarge") == 0) {
            return BufferSizes::JSON_DOC_XLARGE;
        }
        
        if (strcmp(bufferType, "http") == 0) {
            return BufferSizes::HTTP_BUFFER_SIZE;
        }
        
        if (strcmp(bufferType, "websocket") == 0) {
            return BufferSizes::WEBSOCKET_BUFFER_SIZE;
        }
        
        // Fallback vers la taille demandée avec limites
        if (requestedSize <= BufferSizes::JSON_DOC_SMALL) {
            return BufferSizes::JSON_DOC_SMALL;
        } else if (requestedSize <= BufferSizes::JSON_DOC_MEDIUM) {
            return BufferSizes::JSON_DOC_MEDIUM;
        } else if (requestedSize <= BufferSizes::JSON_DOC_LARGE) {
            return BufferSizes::JSON_DOC_LARGE;
        } else {
            return BufferSizes::JSON_DOC_XLARGE;
        }
    }
};
