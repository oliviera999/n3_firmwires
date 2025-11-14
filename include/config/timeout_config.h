#pragma once

// =============================================================================
// CONFIGURATION DES TIMEOUTS ET VALEURS PAR DÉFAUT
// =============================================================================
// Module: Timeouts globaux non-bloquants et valeurs par défaut
// Taille: ~34 lignes
// =============================================================================

#include <Arduino.h>

// =============================================================================
// TIMEOUTS GLOBAUX NON-BLOQUANTS (v11.50)
// =============================================================================
namespace GlobalTimeouts {
    // Timeouts stricts pour éviter tout blocage système
    const uint32_t SENSOR_MAX_MS = 2000;        // 2s MAX pour capteurs
    const uint32_t HTTP_MAX_MS = 5000;          // 5s MAX pour HTTP
    const uint32_t FILE_MAX_MS = 1000;          // 1s MAX pour fichiers
    const uint32_t I2C_MAX_MS = 500;            // 500ms MAX pour I2C
    const uint32_t NVS_MAX_MS = 2000;           // 2s MAX pour NVS
    const uint32_t JSON_MAX_MS = 1000;          // 1s MAX pour JSON
    const uint32_t GLOBAL_MAX_MS = 10000;       // 10s MAX total système
    
    // Timeouts spécifiques par capteur
    const uint32_t DS18B20_MAX_MS = 1000;       // 1s MAX pour DS18B20
    const uint32_t DHT22_MAX_MS = 2000;         // 2s MAX pour DHT22
    const uint32_t ULTRASONIC_MAX_MS = 500;     // 500ms MAX pour ultrasoniques
    const uint32_t LIGHT_MAX_MS = 100;          // 100ms MAX pour luminosité
}

// =============================================================================
// VALEURS PAR DÉFAUT POUR FALLBACK IMMÉDIAT
// =============================================================================
namespace DefaultValues {
    const float WATER_TEMP = 25.0f;    // Température eau par défaut
    const float AIR_TEMP = 22.0f;      // Température air par défaut
    const float HUMIDITY = 60.0f;      // Humidité par défaut
    const uint16_t WATER_LEVEL = 200;  // Niveau eau par défaut
    const uint16_t LIGHT_LEVEL = 500;  // Luminosité par défaut
}
