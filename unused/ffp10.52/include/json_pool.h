#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Pool d'objets JSON réutilisables pour optimiser la mémoire
 * Compatible ESP32 et ESP32-S3 avec support PSRAM optionnel
 */
class JsonPool {
private:
    static constexpr size_t POOL_SIZE = 6;
    
    // Pool de documents JSON avec différentes tailles
    ArduinoJson::DynamicJsonDocument* pool[POOL_SIZE];
    bool inUse[POOL_SIZE];
    SemaphoreHandle_t mutex;
    
    // Configuration des tailles selon le type de board
    size_t getPoolSize(size_t index) const {
        #ifdef BOARD_S3
        // ESP32-S3 : tailles plus importantes avec PSRAM
        const size_t sizes[] = {512, 1024, 2048, 4096, 8192, 16384};
        #else
        // ESP32-WROOM : tailles conservatrices
        const size_t sizes[] = {256, 512, 1024, 2048, 4096, 8192};
        #endif
        return sizes[index];
    }
    
public:
    JsonPool() : mutex(xSemaphoreCreateMutex()) {
        // Initialiser le pool avec les bonnes tailles
        for (size_t i = 0; i < POOL_SIZE; i++) {
            pool[i] = new ArduinoJson::DynamicJsonDocument(getPoolSize(i));
            inUse[i] = false;
        }
    }
    
    ~JsonPool() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
        // Libérer les documents JSON
        for (size_t i = 0; i < POOL_SIZE; i++) {
            if (pool[i]) {
                delete pool[i];
                pool[i] = nullptr;
            }
        }
    }
    
    /**
     * Acquiert un document JSON du pool
     * @param minSize Taille minimale requise
     * @return Pointeur vers le document ou nullptr si aucun disponible
     */
    ArduinoJson::DynamicJsonDocument* acquire(size_t minSize) {
        if (!mutex) return nullptr;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        // Chercher le plus petit document disponible qui convient
        int bestIndex = -1;
        size_t bestSize = SIZE_MAX;
        
        for (size_t i = 0; i < POOL_SIZE; i++) {
            if (!inUse[i] && pool[i] && pool[i]->capacity() >= minSize) {
                if (pool[i]->capacity() < bestSize) {
                    bestSize = pool[i]->capacity();
                    bestIndex = i;
                }
            }
        }
        
        if (bestIndex != -1) {
            inUse[bestIndex] = true;
            pool[bestIndex]->clear(); // Nettoyer le document
            xSemaphoreGive(mutex);
            return pool[bestIndex];
        }
        
        xSemaphoreGive(mutex);
        return nullptr; // Aucun document disponible, utiliser allocation dynamique
    }
    
    /**
     * Libère un document JSON vers le pool
     * @param doc Document à libérer
     */
    void release(ArduinoJson::DynamicJsonDocument* doc) {
        if (!mutex || !doc) return;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        for (size_t i = 0; i < POOL_SIZE; i++) {
            if (pool[i] == doc) {
                inUse[i] = false;
                pool[i]->clear(); // Nettoyer le document
                break;
            }
        }
        
        xSemaphoreGive(mutex);
    }
    
    /**
     * Statistiques du pool
     */
    struct PoolStats {
        size_t totalDocuments;
        size_t usedDocuments;
        size_t availableDocuments;
        size_t totalCapacity;
        size_t usedCapacity;
    };
    
    PoolStats getStats() const {
        PoolStats stats = {0, 0, 0, 0, 0};
        
        if (!mutex) return stats;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        stats.totalDocuments = POOL_SIZE;
        for (size_t i = 0; i < POOL_SIZE; i++) {
            if (pool[i]) {
                size_t capacity = pool[i]->capacity();
                stats.totalCapacity += capacity;
                
                if (inUse[i]) {
                    stats.usedDocuments++;
                    stats.usedCapacity += capacity;
                } else {
                    stats.availableDocuments++;
                }
            }
        }
        
        xSemaphoreGive(mutex);
        return stats;
    }
};

// Instance globale du pool
extern JsonPool jsonPool;
