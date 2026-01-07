#pragma once

#include "system_actuators.h"
#include "config_manager.h"

class AutomatismFeeding {
public:
    AutomatismFeeding(SystemActuators& acts, ConfigManager& cfg);
    
    // Commandes manuelles
    void feedSmall();
    void feedBig();
    
    // Accesseurs
    uint16_t getBigDuration() const;
    uint16_t getSmallDuration() const;

private:
    SystemActuators& _acts;
    ConfigManager& _cfg;
    uint16_t _tempsGros;
    uint16_t _tempsPetits;
};
