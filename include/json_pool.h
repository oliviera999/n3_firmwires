#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Pool d'objets JSON réutilisables pour optimiser la mémoire
 * Version simplifiée - garde la fonctionnalité essentielle
 */
class JsonPool {
private:
    static constexpr size_t POOL_SIZE = 6;
    
    ArduinoJson::DynamicJsonDocument* pool[POOL_SIZE];
    bool inUse[POOL_SIZE];
    SemaphoreHandle_t mutex;
    
    // Tailles fixes selon le type de board
    #ifdef BOARD_S3
    static constexpr size_t sizes[POOL_SIZE] = {512, 1024, 2048, 4096, 8192, 16384};
    #else
    static constexpr size_t sizes[POOL_SIZE] = {256, 512, 1024, 2048, 4096, 8192};
    #endif
    
public:
    JsonPool() : mutex(xSemaphoreCreateMutex()) {
        for (size_t i = 0; i < POOL_SIZE; i++) {
            pool[i] = new ArduinoJson::DynamicJsonDocument(sizes[i]);
            inUse[i] = false;
        }
    }
    
    ~JsonPool() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
        for (size_t i = 0; i < POOL_SIZE; i++) {
            if (pool[i]) {
                delete pool[i];
            }
        }
    }
    
    /**
     * Acquiert un document JSON du pool
     */
    ArduinoJson::DynamicJsonDocument* acquire(size_t minSize) {
        if (!mutex) return nullptr;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        // Chercher le plus petit document disponible
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
            pool[bestIndex]->clear();
            xSemaphoreGive(mutex);
            return pool[bestIndex];
        }
        
        xSemaphoreGive(mutex);
        return nullptr;
    }
    
    /**
     * Libère un document JSON vers le pool
     */
    void release(ArduinoJson::DynamicJsonDocument* doc) {
        if (!mutex || !doc) return;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        for (size_t i = 0; i < POOL_SIZE; i++) {
            if (pool[i] == doc) {
                inUse[i] = false;
                pool[i]->clear();
                break;
            }
        }
        xSemaphoreGive(mutex);
    }
    
    /**
     * Statistiques du pool (simplifiées)
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
