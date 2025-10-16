#pragma once

#include <Arduino.h>
#include "system_actuators.h"
#include "config_manager.h"

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
    
    // === SYNCHRONISATION SERVEUR ===
    // Marquer pour synchronisation serveur au prochain cycle
    void markForSync(const char* payload);
    
    // Vérifier si sync nécessaire
    bool needsSync() const { return _needsServerSync; }
    
    // Récupérer et consommer le payload de sync
    String consumePendingSyncPayload();
    
private:
    SystemActuators& _acts;
    ConfigManager& _config;
    
    // Système de synchronisation serveur différée
    bool _needsServerSync = false;
    String _pendingSyncPayload = "";
};


