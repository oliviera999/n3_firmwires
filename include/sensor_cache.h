#pragma once

#include <Arduino.h>
#include "system_sensors.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Cache intelligent des données de capteurs pour optimiser les performances
 * Évite les lectures répétées des capteurs sur des requêtes fréquentes
 */
class SensorCache {
private:
    SensorReadings cachedData;
    unsigned long lastUpdate = 0;
    SemaphoreHandle_t mutex;
    
    // Durée de cache configurable selon le type de board
    static constexpr unsigned long CACHE_DURATION_MS = 
        #ifdef BOARD_S3
        250;  // ESP32-S3 : cache optimisé (250ms)
        #else
        250; // ESP32-WROOM : cache optimisé (250ms)
        #endif
    
    // Seuil de mémoire pour activer le cache agressif
    static constexpr size_t LOW_MEMORY_THRESHOLD = 10000; // 10KB
    
public:
    SensorCache() : mutex(xSemaphoreCreateMutex()) {
        // Initialiser avec des valeurs par défaut
        cachedData.tempWater = NAN;
        cachedData.tempAir = NAN;
        cachedData.humidity = NAN;
        cachedData.wlAqua = 0;
        cachedData.wlTank = 0;
        cachedData.wlPota = 0;
        cachedData.luminosite = 0;
        cachedData.voltageMv = 0;
        // diffMaree n'est pas dans SensorReadings, il est calculé séparément
    }
    
    ~SensorCache() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Obtient les données des capteurs (cache ou lecture fraîche)
     * @param sensors Référence vers le système de capteurs
     * @return Données des capteurs (mises en cache ou fraîches)
     */
    SensorReadings getReadings(SystemSensors& sensors) {
        if (!mutex) {
            // Fallback vers lecture directe si pas de mutex
            return sensors.read();
        }
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        unsigned long now = millis();
        unsigned long cacheDuration = CACHE_DURATION_MS;
        
        // Cache plus agressif si mémoire faible
        if (ESP.getFreeHeap() < LOW_MEMORY_THRESHOLD) {
            cacheDuration = CACHE_DURATION_MS * 2; // Double la durée
        }
        
        // Vérifier si le cache est valide
        if (now - lastUpdate > cacheDuration) {
            // Cache expiré, lire les capteurs
            cachedData = sensors.read();
            lastUpdate = now;
        }
        
        SensorReadings result = cachedData;
        xSemaphoreGive(mutex);
        
        return result;
    }
    
    /**
     * Force la mise à jour du cache
     * @param sensors Référence vers le système de capteurs
     */
    void forceUpdate(SystemSensors& sensors) {
        if (!mutex) return;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        cachedData = sensors.read();
        lastUpdate = millis();
        xSemaphoreGive(mutex);
    }
    
    /**
     * Invalide le cache (force une prochaine lecture)
     */
    void invalidate() {
        if (!mutex) return;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        lastUpdate = 0; // Force la prochaine lecture
        xSemaphoreGive(mutex);
    }
    
    /**
     * Obtient les statistiques du cache
     */
    struct CacheStats {
        unsigned long lastUpdate;
        unsigned long cacheAge;
        unsigned long cacheDuration;
        bool isValid;
        size_t freeHeap;
    };
    
    CacheStats getStats() const {
        CacheStats stats = {0, 0, CACHE_DURATION_MS, false, 0};
        
        if (!mutex) return stats;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        unsigned long now = millis();
        stats.lastUpdate = lastUpdate;
        stats.cacheAge = now - lastUpdate;
        stats.isValid = (now - lastUpdate) <= CACHE_DURATION_MS;
        stats.freeHeap = ESP.getFreeHeap();
        
        xSemaphoreGive(mutex);
        return stats;
    }
    
    /**
     * Obtient l'âge du cache en millisecondes
     */
    unsigned long getCacheAge() const {
        if (!mutex) return 0;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        unsigned long age = millis() - lastUpdate;
        xSemaphoreGive(mutex);
        
        return age;
    }
};

// Instance globale du cache de capteurs
extern SensorCache sensorCache;
