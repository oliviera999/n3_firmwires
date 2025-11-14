#pragma once

// =============================================================================
// CONFIGURATION SYSTÈME, ACTIONNEURS ET MONITORING
// =============================================================================
// Module: Configuration système, actionneurs et monitoring drift
// Taille: ~54 lignes (SystemConfig + ActuatorConfig + MonitoringConfig)
// =============================================================================

#include <Arduino.h>

// =============================================================================
// CONFIGURATION SYSTÈME
// =============================================================================
namespace SystemConfig {
    // Communication série
    constexpr uint32_t SERIAL_BAUD_RATE = 115200;
    
    // Configuration NTP - Maroc (UTC+1 fixe, pas de changement d'heure)
    constexpr int32_t NTP_GMT_OFFSET_SEC = 3600;  // 1 heure (UTC+1)
    constexpr int32_t NTP_DAYLIGHT_OFFSET_SEC = 0;  // Pas de changement d'heure au Maroc
    constexpr const char* NTP_SERVER = "pool.ntp.org";
    
    // Configuration OTA
    constexpr uint16_t ARDUINO_OTA_PORT = 3232;
    
    // Délais système
    constexpr uint32_t INITIAL_DELAY_MS = 200;
    constexpr uint32_t FINAL_INIT_DELAY_MS = 1000;
    
    // Hostname
    constexpr const char* HOSTNAME_PREFIX = "ffp5";
    constexpr size_t HOSTNAME_BUFFER_SIZE = 20;
}

// =============================================================================
// CONFIGURATION ACTIONNEURS
// =============================================================================
namespace ActuatorConfig {
    // Seuils par défaut
    namespace Default {
        constexpr int AQUA_LEVEL_CM = 18;
        constexpr int TANK_LEVEL_CM = 80;
        constexpr float HEATER_THRESHOLD_C = 18.0f;
        
        // Durées de nourrissage par défaut (synchronisées avec BDD distante)
        constexpr uint16_t FEED_BIG_DURATION_SEC = 3;      // Correspond à GPIO 111 (Temps Gros)
        constexpr uint16_t FEED_SMALL_DURATION_SEC = 2;    // Correspond à GPIO 112 (Temps Petits)
    }
    
    // Servo
    constexpr int SERVO_MIN_ANGLE = 0;
    constexpr int SERVO_MAX_ANGLE = 180;
}

// =============================================================================
// CONFIGURATION MONITORING ET DRIFT
// =============================================================================
namespace MonitoringConfig {
    // ========================================
    // CONFIGURATION DRIFT TEMPS
    // ========================================
    
    // Seuils drift temps
    constexpr float DEFAULT_DRIFT_THRESHOLD_PPM = 100.0f;   // Seuil drift par défaut (100 PPM = 0.1%)
    
    // Indicateur visuel de dérive sur OLED
    constexpr bool ENABLE_DRIFT_VISUAL_INDICATOR = true;    // Activer l'indicateur visuel de dérive
    constexpr uint32_t DRIFT_CHECK_INTERVAL_MS = 10000;     // Intervalle de vérification (10 secondes)
    
    // ========================================
    // CONFIGURATION LOGGING
    // ========================================
    
    // Intervalles de logging
    constexpr uint32_t DEBUG_LOG_INTERVAL_MS = 10000;        // Intervalle log debug (10 secondes)
    constexpr uint32_t PER_SECOND_LOG_INTERVAL_MS = 1000;   // Intervalle log toutes les secondes
}
