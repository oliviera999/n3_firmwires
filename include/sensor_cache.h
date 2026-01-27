#pragma once

#include <Arduino.h>
#include "system_sensors.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Cache simple des données de capteurs pour optimiser les performances
 * Version simplifiée - réduit l'overhead mutex
 */
class SensorCache {
private:
    SensorReadings cachedData;
    unsigned long lastUpdate = 0;
    SemaphoreHandle_t mutex;
    
    static constexpr unsigned long CACHE_DURATION_MS = 1000; // 1 seconde
    
public:
    SensorCache() : mutex(xSemaphoreCreateMutex()) {
        cachedData.tempWater = NAN;
        cachedData.tempAir = NAN;
        cachedData.humidity = NAN;
        cachedData.wlAqua = 0;
        cachedData.wlTank = 0;
        cachedData.wlPota = 0;
        cachedData.luminosite = 0;
    }
    
    ~SensorCache() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Obtient les données des capteurs (cache ou lecture fraîche)
     */
    SensorReadings getReadings(SystemSensors& sensors) {
        if (!mutex) {
            return sensors.read();
        }
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        unsigned long now = millis();
        if (now - lastUpdate > CACHE_DURATION_MS) {
            cachedData = sensors.read();
            lastUpdate = now;
        }
        
        SensorReadings result = cachedData;
        xSemaphoreGive(mutex);
        return result;
    }
    
    /**
     * Force la mise à jour du cache
     */
    void forceUpdate(SystemSensors& sensors) {
        if (!mutex) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        cachedData = sensors.read();
        lastUpdate = millis();
        xSemaphoreGive(mutex);
    }
    
    /**
     * Invalide le cache
     */
    void invalidate() {
        if (!mutex) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        lastUpdate = 0;
        xSemaphoreGive(mutex);
    }
};

// Instance globale du cache de capteurs
extern SensorCache sensorCache;
