#pragma once

#include <Arduino.h>
#include "system_actuators.h"
#include "config_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * Module Actuators pour Automatism
 * 
 * Responsabilité: Contrôle manuel des actionneurs (pompes, chauffage, lumière)
 * avec synchronisation automatique serveur distant et feedback WebSocket
 * 
 * Pattern: Action immédiate → WebSocket broadcast → Sync serveur async
 */
class AutomatismActuators {
public:
    /**
     * Constructeur
     * @param acts Référence SystemActuators
     * @param cfg Référence ConfigManager
     */
    AutomatismActuators(SystemActuators& acts, ConfigManager& cfg);
    
    // === POMPE AQUARIUM ===
    void startAquaPumpManual();
    void stopAquaPumpManual();
    
    // === POMPE RÉSERVE ===
    void startTankPumpManual();
    void stopTankPumpManual();
    
    // === CHAUFFAGE ===
    void startHeaterManual();
    void stopHeaterManual();
    
    // === LUMIÈRE ===
    void startLightManual();
    void stopLightManual();
    
    // === CONFIGURATION ===
    void toggleEmailNotifications();
    void toggleForceWakeup();
    
private:
    SystemActuators& _acts;
    ConfigManager& _config;
    
    // Task handles pour synchronisation async
    TaskHandle_t _syncAquaStartHandle = nullptr;
    TaskHandle_t _syncAquaStopHandle = nullptr;
    TaskHandle_t _syncTankStartHandle = nullptr;
    TaskHandle_t _syncTankStopHandle = nullptr;
    TaskHandle_t _syncHeaterStartHandle = nullptr;
    TaskHandle_t _syncHeaterStopHandle = nullptr;
    TaskHandle_t _syncLightStartHandle = nullptr;
    TaskHandle_t _syncLightStopHandle = nullptr;
    
    /**
     * Helper pour synchronisation asynchrone avec serveur distant
     * Factorisation du code dupliqué 8x
     * 
     * @param taskName Nom de la tâche FreeRTOS
     * @param extraParams Paramètres pour sendFullUpdate() (ex: "etatPompeAqua=1")
     * @param taskHandle Référence au handle de tâche
     * @param actionDesc Description de l'action (pour logs)
     */
    void syncActuatorStateAsync(
        const char* taskName,
        const char* extraParams,
        TaskHandle_t& taskHandle,
        const char* actionDesc
    );
};

