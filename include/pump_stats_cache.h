#pragma once

#include <Arduino.h>
#include "system_actuators.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Cache des statistiques de pompes pour optimiser les calculs
 * Évite les calculs redondants sur des données qui changent peu
 */
class PumpStatsCache {
private:
    struct PumpStatsData {
        bool isRunning;
        uint32_t currentRuntime;
        uint32_t currentRuntimeSec;
        uint32_t totalRuntime;
        uint32_t totalRuntimeSec;
        uint32_t totalStops;
        uint32_t lastStopTime;
        uint32_t timeSinceLastStop;
        uint32_t timeSinceLastStopSec;
        unsigned long lastUpdate;
    };
    
    PumpStatsData cachedStats;
    SemaphoreHandle_t mutex;
    
    // Durée de cache selon le type de board
    // Optimisé pour réduire charge CPU - cache valide pendant 25% du cycle sensor (4000ms)
    static constexpr unsigned long CACHE_DURATION_MS = 
        #ifdef BOARD_S3
        1000;  // ESP32-S3 : cache optimisé (1000ms)
        #else
        1000;  // ESP32-WROOM : cache optimisé (1000ms)
        #endif
    
public:
    PumpStatsCache() : mutex(xSemaphoreCreateMutex()) {
        // Initialiser avec des valeurs par défaut
        cachedStats.isRunning = false;
        cachedStats.currentRuntime = 0;
        cachedStats.currentRuntimeSec = 0;
        cachedStats.totalRuntime = 0;
        cachedStats.totalRuntimeSec = 0;
        cachedStats.totalStops = 0;
        cachedStats.lastStopTime = 0;
        cachedStats.timeSinceLastStop = 0;
        cachedStats.timeSinceLastStopSec = 0;
        cachedStats.lastUpdate = 0;
    }
    
    ~PumpStatsCache() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Met à jour les statistiques de pompe
     * @param acts Référence vers le système d'actionneurs
     */
    void updateStats(SystemActuators& acts) {
        if (!mutex) return;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        unsigned long now = millis();
        
        // Vérifier si la mise à jour est nécessaire
        if (now - cachedStats.lastUpdate > CACHE_DURATION_MS) {
            cachedStats.isRunning = acts.isTankPumpRunning();
            cachedStats.currentRuntime = acts.getTankPumpCurrentRuntime();
            cachedStats.currentRuntimeSec = cachedStats.currentRuntime / 1000;
            cachedStats.totalRuntime = acts.getTankPumpTotalRuntime();
            cachedStats.totalRuntimeSec = cachedStats.totalRuntime / 1000;
            cachedStats.totalStops = acts.getTankPumpTotalStops();
            cachedStats.lastStopTime = acts.getTankPumpLastStopTime();
            
            // Calculer le temps depuis le dernier arrêt
            if (cachedStats.lastStopTime > 0) {
                cachedStats.timeSinceLastStop = now - cachedStats.lastStopTime;
                cachedStats.timeSinceLastStopSec = cachedStats.timeSinceLastStop / 1000;
            } else {
                cachedStats.timeSinceLastStop = 0;
                cachedStats.timeSinceLastStopSec = 0;
            }
            
            cachedStats.lastUpdate = now;
        }
        
        xSemaphoreGive(mutex);
    }
    
    /**
     * Obtient les statistiques mises en cache
     * @param acts Référence vers le système d'actionneurs
     * @return Statistiques de pompe
     */
    PumpStatsData getStats(SystemActuators& acts) {
        updateStats(acts); // Mettre à jour si nécessaire
        
        PumpStatsData result;
        if (mutex) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            result = cachedStats;
            xSemaphoreGive(mutex);
        } else {
            // Fallback vers calcul direct
            result.isRunning = acts.isTankPumpRunning();
            result.currentRuntime = acts.getTankPumpCurrentRuntime();
            result.currentRuntimeSec = result.currentRuntime / 1000;
            result.totalRuntime = acts.getTankPumpTotalRuntime();
            result.totalRuntimeSec = result.totalRuntime / 1000;
            result.totalStops = acts.getTankPumpTotalStops();
            result.lastStopTime = acts.getTankPumpLastStopTime();
            
            unsigned long now = millis();
            if (result.lastStopTime > 0) {
                result.timeSinceLastStop = now - result.lastStopTime;
                result.timeSinceLastStopSec = result.timeSinceLastStop / 1000;
            } else {
                result.timeSinceLastStop = 0;
                result.timeSinceLastStopSec = 0;
            }
            result.lastUpdate = now;
        }
        
        return result;
    }
    
    /**
     * Force la mise à jour du cache
     * @param acts Référence vers le système d'actionneurs
     */
    void forceUpdate(SystemActuators& acts) {
        if (!mutex) return;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        cachedStats.lastUpdate = 0; // Force la mise à jour
        xSemaphoreGive(mutex);
        
        updateStats(acts);
    }
    
    /**
     * Obtient l'âge du cache en millisecondes
     */
    unsigned long getCacheAge() const {
        if (!mutex) return 0;
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        unsigned long age = millis() - cachedStats.lastUpdate;
        xSemaphoreGive(mutex);
        
        return age;
    }
};

// Instance globale du cache de statistiques de pompes
extern PumpStatsCache pumpStatsCache;
