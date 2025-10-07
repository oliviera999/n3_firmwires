#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * Optimiseur PSRAM pour ESP32-S3
 * Utilise la PSRAM pour les gros buffers quand disponible
 */
class PSRAMOptimizer {
private:
    static bool psramAvailable;
    static size_t psramFree;
    
public:
    /**
     * Initialise l'optimiseur PSRAM
     */
    static void init() {
        #ifdef BOARD_S3
        psramAvailable = (ESP.getFreePsram() > 0);
        if (psramAvailable) {
            psramFree = ESP.getFreePsram();
            Serial.printf("[PSRAM] PSRAM disponible: %u bytes\n", psramFree);
        } else {
            Serial.println("[PSRAM] PSRAM non disponible");
        }
        #else
        psramAvailable = false;
        psramFree = 0;
        #endif
    }
    
    /**
     * Crée un document JSON optimisé pour la taille
     * @param minSize Taille minimale requise
     * @return Document JSON ou nullptr si échec
     */
    static ArduinoJson::DynamicJsonDocument* createOptimizedJsonDocument(size_t minSize) {
        #ifdef BOARD_S3
        if (psramAvailable && minSize > 4096) {
            // Utiliser PSRAM pour les gros documents
            if (ESP.getFreePsram() > minSize * 2) { // Marge de sécurité
                return new ArduinoJson::DynamicJsonDocument(minSize);
            }
        }
        #endif
        
        // Fallback vers RAM interne
        if (ESP.getFreeHeap() > minSize * 2) {
            return new ArduinoJson::DynamicJsonDocument(minSize);
        }
        
        return nullptr; // Pas assez de mémoire
    }
    
    /**
     * Vérifie si PSRAM est disponible
     */
    static bool isPSRAMAvailable() {
        return psramAvailable;
    }
    
    /**
     * Obtient la quantité de PSRAM libre
     */
    static size_t getFreePSRAM() {
        #ifdef BOARD_S3
        return ESP.getFreePsram();
        #else
        return 0;
        #endif
    }
    
    /**
     * Obtient la quantité de PSRAM totale
     */
    static size_t getTotalPSRAM() {
        #ifdef BOARD_S3
        return ESP.getPsramSize();
        #else
        return 0;
        #endif
    }
    
    /**
     * Obtient les statistiques PSRAM
     */
    struct PSRAMStats {
        bool available;
        size_t total;
        size_t free;
        size_t used;
        float usagePercent;
    };
    
    static PSRAMStats getStats() {
        PSRAMStats stats = {false, 0, 0, 0, 0.0f};
        
        #ifdef BOARD_S3
        stats.available = psramAvailable;
        if (psramAvailable) {
            stats.total = ESP.getPsramSize();
            stats.free = ESP.getFreePsram();
            stats.used = stats.total - stats.free;
            if (stats.total > 0) {
                stats.usagePercent = (float)stats.used / stats.total * 100.0f;
            }
        }
        #endif
        
        return stats;
    }
    
    /**
     * Recommande la taille optimale pour un buffer
     * @param requestedSize Taille demandée
     * @return Taille recommandée
     */
    static size_t getRecommendedBufferSize(size_t requestedSize) {
        #ifdef BOARD_S3
        if (psramAvailable) {
            // PSRAM disponible : autoriser des tailles plus importantes
            if (requestedSize <= 4096) {
                return requestedSize; // Petits buffers en RAM interne
            } else if (requestedSize <= 16384) {
                return 16384; // Buffers moyens en PSRAM
            } else {
                return 32768; // Gros buffers en PSRAM (limité)
            }
        }
        #endif
        
        // ESP32-WROOM : tailles conservatrices
        if (requestedSize <= 1024) {
            return 1024;
        } else if (requestedSize <= 2048) {
            return 2048;
        } else if (requestedSize <= 4096) {
            return 4096;
        } else {
            return 8192; // Maximum pour ESP32-WROOM
        }
    }
    
    /**
     * Alloue de la mémoire optimisée selon le type de board
     * @param size Taille requise
     * @return Pointeur vers la mémoire allouée ou nullptr
     */
    static void* allocateOptimized(size_t size) {
        #ifdef BOARD_S3
        if (psramAvailable && size > 1024) {
            // Utiliser PSRAM pour les allocations importantes
            if (ESP.getFreePsram() > size) {
                return ps_malloc(size);
            }
        }
        #endif
        
        // Fallback vers heap normal
        if (ESP.getFreeHeap() > size) {
            return malloc(size);
        }
        
        return nullptr;
    }
    
    /**
     * Libère la mémoire allouée
     * @param ptr Pointeur vers la mémoire à libérer
     */
    static void freeOptimized(void* ptr) {
        if (ptr) {
            free(ptr);
        }
    }
    
    /**
     * Log les statistiques PSRAM
     */
    static void logStats() {
        #ifdef BOARD_S3
        if (psramAvailable) {
            PSRAMStats stats = getStats();
            Serial.printf("[PSRAM] Total: %u bytes, Libre: %u bytes, Utilisé: %u bytes (%.1f%%)\n",
                         stats.total, stats.free, stats.used, stats.usagePercent);
        } else {
            Serial.println("[PSRAM] PSRAM non disponible");
        }
        #else
        Serial.println("[PSRAM] PSRAM non supporté sur cette plateforme");
        #endif
    }
};
