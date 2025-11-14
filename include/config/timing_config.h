#pragma once

// =============================================================================
// CONFIGURATION TEMPORELLE ET RETRY
// =============================================================================
// Module: Intervalles de temps, délais et configuration des tentatives
// Taille: ~108 lignes (TimingConfig + RetryConfig)
// =============================================================================

#include <Arduino.h>

// =============================================================================
// CONFIGURATION TEMPORELLE
// =============================================================================
namespace TimingConfig {
    // Intervalles de base optimisés
    constexpr uint32_t SENSOR_READ_INTERVAL_MS = 4000;  // Optimisé pour stabilité et économie d'énergie
    constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 200; // Augmenté à 5 FPS
    constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;   // 30 secondes (inchangé)
    
    // WiFi et réseau
    constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000; // 12 secondes
    constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 60000;  // 1 minute
    
    // OTA et mises à jour
    constexpr uint32_t OTA_CHECK_INTERVAL_MS = 7200000; // 2 heures
    constexpr uint32_t OTA_PROGRESS_UPDATE_INTERVAL_MS = 2000; // 2 secondes
    
    // Watchdog
    constexpr uint32_t WATCHDOG_TIMEOUT_MS = 300000;    // 5 minutes
    
    // Force wake-up
    constexpr uint32_t WEB_ACTIVITY_TIMEOUT_MS = 600000; // 10 minutes
    
    // Intervalles d'affichage et logging
    constexpr uint32_t BOUFFE_DISPLAY_INTERVAL_MS = 120000;  // 2 minutes
    constexpr uint32_t PUMP_STATS_DISPLAY_INTERVAL_MS = 600000; // 10 minutes
    constexpr uint32_t DRIFT_DISPLAY_INTERVAL_MS = 1800000;   // 30 minutes
    constexpr uint32_t DIGEST_INTERVAL_MS = 900000;          // 15 minutes
    
    // Protection wake-up
    constexpr uint32_t WAKEUP_PROTECTION_DURATION_MS = 5000;  // 5 secondes
    
    // Délais minimums
    constexpr uint32_t MIN_DISPLAY_INTERVAL_MS = 100;        // 100ms minimum

    // ========================================
    // DÉLAIS CAPTEURS ET MESURES (étendus)
    // ========================================
    constexpr uint32_t ULTRASONIC_PRE_TRIGGER_DELAY_US = 2;     // Délai avant déclenchement pulse ultrasonique
    constexpr uint32_t ULTRASONIC_PULSE_DURATION_US = 10;       // Durée du pulse ultrasonique (µs)
    constexpr uint32_t SENSOR_READ_DELAY_MS = 50;              // Délai entre lectures capteurs individuels
    constexpr uint32_t SENSOR_RESET_DELAY_MS = 500;            // Délai après reset d'un capteur
    constexpr uint32_t SENSOR_STABILIZATION_DELAY_MS = 1;      // Délai entre échantillons pour stabilisation

    // ========================================
    // DÉLAIS SYSTÈME, WIFI ET ACTIONNEURS
    // ========================================
    constexpr uint32_t SYSTEM_INIT_SAFETY_DELAY_MS = 3000;     // Délai de sécurité après initialisation système
    constexpr uint32_t TASK_START_LATENCY_MS = 50;             // Latence pour laisser démarrer les tâches
    constexpr uint32_t CPU_LOAD_REDUCTION_DELAY_MS = 500;     // Délai pour réduire la charge CPU
    constexpr uint32_t WIFI_INIT_DELAY_MS = 50;                // Délai initialisation WiFi
    constexpr uint32_t WIFI_RECONNECT_DELAY_FAST_MS = 100;    // Délai optimisé pour reconnexion WiFi
    constexpr uint32_t WIFI_CONNECTION_TIMEOUT_MS = 10000;    // Timeout connexion WiFi (10 secondes)
    constexpr uint32_t SERVO_ACTION_DELAY_MS = 200;           // Délai entre actions servo
    constexpr uint32_t ACTUATOR_SHORT_DELAY_MS = 50;          // Délai court entre actions actionneurs
    constexpr uint32_t ACTUATOR_MEDIUM_DELAY_MS = 100;        // Délai moyen entre actions actionneurs
    constexpr uint32_t ACTUATOR_LONG_DELAY_MS = 150;          // Délai long entre actions actionneurs

    // ========================================
    // DÉLAIS OTA ET MONITORING
    // ========================================
    constexpr uint32_t OTA_MICRO_DELAY_MS = 1;                // Délai micro entre opérations OTA
    constexpr uint32_t OTA_FAILURE_DELAY_MS = 3000;           // Délai après échec OTA
    constexpr uint32_t OTA_RETRY_PAUSE_MS = 500;             // Pause entre tentatives OTA
    constexpr uint32_t OTA_SHORT_PAUSE_MS = 5;                // Pause courte entre opérations OTA
    constexpr uint32_t OTA_CHUNK_PAUSE_MS = 10;              // Pause entre chunks OTA
    constexpr uint32_t OTA_SPECIFIC_DELAY_MS = 100;          // Délai spécifique OTA
    constexpr uint32_t TIME_DRIFT_RETRY_DELAY_MS = 1000;     // Délai entre tentatives correction drift
    constexpr uint32_t DEBUG_LOG_INTERVAL_MS = 10000;        // Intervalle log debug (10 secondes)
    constexpr uint32_t LOG_PER_SECOND_INTERVAL_MS = 1000;    // Intervalle log toutes les secondes
}

// =============================================================================
// CONFIGURATION RETRY ET TENTATIVES
// =============================================================================
namespace RetryConfig {
    // ========================================
    // TENTATIVES CAPTEURS
    // ========================================
    
    // Tentatives capteurs système
    constexpr uint8_t SYSTEM_SENSORS_MAX_RETRIES = 3;         // Nombre max de tentatives pour capteurs système
    
    // Tentatives capteurs individuels
    constexpr uint8_t SENSOR_RESET_MAX_RETRIES = 3;          // Tentatives reset capteur individuel
    constexpr uint8_t DS18B20_CONVERSION_RETRIES = 3;        // Tentatives conversion température DS18B20
    
    // ========================================
    // TENTATIVES RÉSEAU ET WEB
    // ========================================
    
    // Tentatives connexions web
    constexpr uint8_t WEB_CLIENT_MAX_ATTEMPTS = 3;           // Tentatives connexion web client
    constexpr uint8_t HTTP_REQUEST_MAX_RETRIES = 3;         // Tentatives requêtes HTTP
    
    // Tentatives OTA
    constexpr uint8_t OTA_MAX_RETRIES = 3;                   // Nombre max de tentatives OTA
    constexpr uint32_t OTA_MICRO_CHUNK_TIMEOUT_MS = 8000;   // Timeout chunks OTA micro (8 secondes)
    constexpr uint32_t OTA_PAUSE_BETWEEN_CHUNKS_MS = 10;    // Pause entre chunks OTA
    
    // ========================================
    // TENTATIVES ACTIONNEURS
    // ========================================
    
    // Tentatives pompes et actionneurs
    constexpr uint8_t PUMP_MAX_RETRIES = 5;                  // Tentatives pompe (augmenté de 3 à 5 pour robustesse)
    constexpr uint8_t SERVO_MAX_RETRIES = 3;                 // Tentatives servo
    constexpr uint8_t ACTUATOR_MAX_RETRIES = 3;              // Tentatives actionneurs génériques
    
    // ========================================
    // TENTATIVES MONITORING
    // ========================================
    
    // Tentatives monitoring temps
    constexpr uint8_t TIME_DRIFT_MAX_ATTEMPTS = 10;         // Tentatives correction drift temps
    constexpr uint8_t NTP_SYNC_MAX_RETRIES = 3;             // Tentatives synchronisation NTP
}
