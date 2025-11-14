#pragma once

// =============================================================================
// CONFIGURATION CENTRALE DU PROJET FFP5CS
// =============================================================================
// Ce fichier centralise TOUTES les constantes et configurations du projet
// Pour changer d'environnement, utiliser les flags dans platformio.ini
// =============================================================================

#include <Arduino.h>

// Note: Les credentials email sont définis dans secrets.h
// Ils seront inclus via constants.h qui inclut secrets.h

// =============================================================================
// PROFILS D'ENVIRONNEMENT
// =============================================================================
// Définis dans platformio.ini via build_flags
// - PROFILE_DEV : Développement local (logs verbeux, debug actif)
// - PROFILE_TEST : Tests sur serveur de test (endpoints avec "2")
// - PROFILE_PROD : Production (logs minimaux, optimisations)

// =============================================================================
// VERSION ET IDENTIFICATION
// =============================================================================
namespace ProjectConfig {
  constexpr const char* VERSION = "11.117"; // v11.117: Finalisation migration FFP5CS - Toutes r\u00e9f\u00e9rences FFP3 corrig\u00e9es
    
    // Type d'environnement (dev, test, prod)
    #if defined(PROFILE_DEV)
        constexpr const char* PROFILE_TYPE = "dev";
    #elif defined(PROFILE_TEST)
        constexpr const char* PROFILE_TYPE = "test";
    #elif defined(PROFILE_PROD)
        constexpr const char* PROFILE_TYPE = "prod";
    #else
        constexpr const char* PROFILE_TYPE = "unknown";
    #endif
    
    // Identification matérielle
    #if defined(BOARD_S3)
        constexpr const char* BOARD_TYPE = "esp32-s3";
    #else
        constexpr const char* BOARD_TYPE = "esp32-wroom";
    #endif
}

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

// Valeurs par défaut pour fallback immédiat
namespace DefaultValues {
    const float WATER_TEMP = 25.0f;    // Température eau par défaut
    const float AIR_TEMP = 22.0f;      // Température air par défaut
    const float HUMIDITY = 60.0f;      // Humidité par défaut
    const uint16_t WATER_LEVEL = 200;  // Niveau eau par défaut
    const uint16_t LIGHT_LEVEL = 500;  // Luminosité par défaut
}

// =============================================================================
// CONFIGURATION SERVEUR
// =============================================================================
namespace ServerConfig {
    constexpr const char* BASE_URL = "https://iot.olution.info";
    
    // Endpoints selon le profil
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
        // Environnement de test: legacy POST route (DB write path)
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data-test";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs-test/state";
    #else
        // Production: legacy POST route (DB write path)
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs/state";
    #endif
    
    // URLs complètes - Utilisation de buffers statiques pour éviter la fragmentation mémoire
    inline void getPostDataUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s%s", BASE_URL, POST_DATA_ENDPOINT);
    }
    
    inline void getOutputUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s%s", BASE_URL, OUTPUT_ENDPOINT);
    }
    
    // Autres endpoints
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat-test";  // Slim controller (TEST)
    #else
    constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat";      // Slim controller (PROD)
    #endif
    constexpr const char* OTA_BASE_PATH = "/ffp3/ota/";
    
    // Timeouts optimisés pour fiabilité (augmentés v11.09 pour diagnostic HTTP)
    constexpr uint32_t CONNECTION_TIMEOUT_MS = 10000;     // 10s (augmenté de 5s pour stabilité)
    constexpr uint32_t REQUEST_TIMEOUT_MS = 30000;        // 30s (augmenté de 15s pour fiabilité)
    
    // Délai minimum entre requêtes HTTP successives (Fix v11.29: évite connection lost)
    constexpr uint32_t MIN_DELAY_BETWEEN_REQUESTS_MS = 500;  // 500ms entre requêtes pour éviter saturation TCP

    inline void getHeartbeatUrl(char* buffer, size_t bufferSize) { 
        snprintf(buffer, bufferSize, "%s%s", BASE_URL, HEARTBEAT_ENDPOINT); 
    }
    
    inline void getServerUrl(char* buffer, size_t bufferSize) {
        strncpy(buffer, BASE_URL, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
    }
    
    inline void getWebSocketEndpoint(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s/ws", BASE_URL);
    }
}

// =============================================================================
// CONFIGURATION API
// =============================================================================
namespace ApiConfig {
    constexpr const char* API_KEY = "fdGTMoptd5CD2ert3";
}

// =============================================================================
// CONFIGURATION EMAIL
// =============================================================================
namespace EmailConfig {
    constexpr const char* SMTP_HOST = "smtp.gmail.com";
    constexpr uint16_t SMTP_PORT = 465;
    // Note: SENDER_EMAIL et SENDER_PASSWORD sont définis dans secrets.h
    // Ils sont accessibles via constants.h qui fait le lien
    constexpr const char* DEFAULT_RECIPIENT = "oliv.arn.lau@gmail.com";
    constexpr size_t MAX_EMAIL_LENGTH = 96;
}

// =============================================================================
// CONFIGURATION SYSTÈME DE LOGS
// =============================================================================
namespace LogConfig {
    // Niveaux de log
    enum LogLevel {
        LOG_NONE = 0,
        LOG_ERROR = 1,
        LOG_WARN = 2,
        LOG_INFO = 3,
        LOG_DEBUG = 4,
        LOG_VERBOSE = 5
    };
    
    // =============================================================================
    // CONFIGURATION MONITEUR SÉRIE
    // =============================================================================
    // Le flag SERIAL_MONITOR_ENABLED contrôle l'activation/désactivation du moniteur série.
    // Le flag SENSOR_LOGS_ENABLED contrôle spécifiquement les messages des capteurs.
    // 
    // UTILISATION:
    // - Définir ENABLE_SERIAL_MONITOR=0 dans platformio.ini pour désactiver le moniteur série
    // - Définir ENABLE_SERIAL_MONITOR=1 dans platformio.ini pour activer le moniteur série
    // - Définir ENABLE_SENSOR_LOGS=0 pour désactiver uniquement les logs de capteurs
    // - Définir ENABLE_SENSOR_LOGS=1 pour activer les logs de capteurs
    // - Si non défini, utilise la configuration par défaut selon le profil
    // 
    // EXEMPLES dans platformio.ini:
    // build_flags = -DENABLE_SERIAL_MONITOR=0    # Désactiver le moniteur série
    // build_flags = -DENABLE_SERIAL_MONITOR=1    # Activer le moniteur série
    // build_flags = -DENABLE_SENSOR_LOGS=0       # Désactiver les logs de capteurs uniquement
    // build_flags = -DENABLE_SENSOR_LOGS=1       # Activer les logs de capteurs
    //
    // Flag global pour activer/désactiver le moniteur série
    // Peut être surchargé par les profils ou défini explicitement
    #ifndef ENABLE_SERIAL_MONITOR
    #if defined(PROFILE_PROD)
        constexpr LogLevel DEFAULT_LEVEL = LOG_ERROR;
        constexpr bool SERIAL_ENABLED = false;
        constexpr bool SERIAL_MONITOR_ENABLED = false;  // Nouveau flag explicite
    #elif defined(PROFILE_TEST)
        constexpr LogLevel DEFAULT_LEVEL = LOG_INFO;
        constexpr bool SERIAL_ENABLED = true;
        constexpr bool SERIAL_MONITOR_ENABLED = true;   // Nouveau flag explicite
    #else // PROFILE_DEV ou default
        constexpr LogLevel DEFAULT_LEVEL = LOG_DEBUG;
        constexpr bool SERIAL_ENABLED = true;
        constexpr bool SERIAL_MONITOR_ENABLED = true;   // Nouveau flag explicite
    #endif
    #else
        // Utiliser la définition explicite ENABLE_SERIAL_MONITOR si fournie
        constexpr bool SERIAL_MONITOR_ENABLED = (ENABLE_SERIAL_MONITOR == 1);
        #if defined(PROFILE_PROD)
            constexpr LogLevel DEFAULT_LEVEL = LOG_ERROR;
            constexpr bool SERIAL_ENABLED = SERIAL_MONITOR_ENABLED;
        #elif defined(PROFILE_TEST)
            constexpr LogLevel DEFAULT_LEVEL = LOG_INFO;
            constexpr bool SERIAL_ENABLED = SERIAL_MONITOR_ENABLED;
        #else // PROFILE_DEV ou default
            constexpr LogLevel DEFAULT_LEVEL = LOG_DEBUG;
            constexpr bool SERIAL_ENABLED = SERIAL_MONITOR_ENABLED;
        #endif
    #endif
    
    // Flag pour désactiver les messages des capteurs dans le moniteur série
    // Permet de masquer les logs des capteurs pour se concentrer sur d'autres messages
    #ifndef ENABLE_SENSOR_LOGS
        // Par défaut, les logs de capteurs sont activés si le moniteur série est activé
        constexpr bool SENSOR_LOGS_ENABLED = SERIAL_MONITOR_ENABLED;
    #else
        // Utiliser la définition explicite ENABLE_SENSOR_LOGS si fournie
        constexpr bool SENSOR_LOGS_ENABLED = (ENABLE_SENSOR_LOGS == 1) && SERIAL_MONITOR_ENABLED;
    #endif
    
    // Macros de log unifiées
    #define LOG_ERROR(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_ERROR && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_WARN && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_INFO && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_DEBUG && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
    #define LOG_VERBOSE(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_VERBOSE && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[VERB] " fmt "\n", ##__VA_ARGS__)

    // Macros conditionnelles pour rediriger les appels Serial directs
    // Ces macros permettent de désactiver complètement les appels Serial.print quand SERIAL_MONITOR_ENABLED = false
    #define SERIAL_PRINT_IF_ENABLED(x) do { if (LogConfig::SERIAL_MONITOR_ENABLED) Serial.print(x); } while(0)
    #define SERIAL_PRINTLN_IF_ENABLED(x) do { if (LogConfig::SERIAL_MONITOR_ENABLED) Serial.println(x); } while(0)
    #define SERIAL_PRINTF_IF_ENABLED(fmt, ...) do { if (LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf(fmt, ##__VA_ARGS__); } while(0)

    // Macros spécialisées pour les logs de capteurs - peuvent être désactivées indépendamment
    // Utilisez ces macros pour tous les messages concernant les capteurs ([Sensor], [AirSensor], [SystemSensors], etc.)
    #define SENSOR_LOG_PRINT(x) do { if (LogConfig::SENSOR_LOGS_ENABLED) Serial.print(x); } while(0)
    #define SENSOR_LOG_PRINTLN(x) do { if (LogConfig::SENSOR_LOGS_ENABLED) Serial.println(x); } while(0)
    #define SENSOR_LOG_PRINTF(fmt, ...) do { if (LogConfig::SENSOR_LOGS_ENABLED) Serial.printf(fmt, ##__VA_ARGS__); } while(0)
}

// Désactivation complète de Serial en production quand le moniteur est off
// v11.116: Implémentation sûre qui évite les crashs et optimise le code compilé
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 0)) || (!defined(ENABLE_SERIAL_MONITOR) && defined(PROFILE_PROD))
    // Structure NullSerial optimisée pour être complètement éliminée à la compilation
    namespace LogConfig {
        struct NullSerialType {
            // Toutes les méthodes sont inline constexpr pour élimination à la compilation
            template<typename... Args>
            inline constexpr size_t printf(const char*, Args...) const { return 0U; }
            inline constexpr size_t println() const { return 0U; }
            template<typename T>
            inline constexpr size_t println(const T&) const { return 0U; }
            template<typename T>
            inline constexpr size_t print(const T&) const { return 0U; }
            // Méthodes supplémentaires pour compatibilité complète
            inline constexpr void begin(unsigned long) const {}
            inline constexpr void end() const {}
            inline constexpr void flush() const {}
            inline constexpr int available() const { return 0; }
            inline constexpr int read() const { return -1; }
            inline constexpr size_t write(uint8_t) const { return 0U; }
            inline constexpr size_t write(const uint8_t*, size_t) const { return 0U; }
            inline constexpr operator bool() const { return false; }
        };
        static constexpr NullSerialType NullSerial{};
    }
    // Redéfinition de Serial uniquement si PROFILE_PROD et SERIAL désactivé
    #define Serial LogConfig::NullSerial
    // Désactiver aussi Serial1 et Serial2 s'ils existent
    #define Serial1 LogConfig::NullSerial
    #define Serial2 LogConfig::NullSerial
#endif

// =============================================================================
// CONFIGURATION CAPTEURS ET VALIDATION
// =============================================================================
namespace SensorConfig {
    // Délais et stabilisation génériques
    constexpr uint32_t SENSOR_READ_DELAY_MS = 100;
    constexpr uint32_t I2C_STABILIZATION_DELAY_MS = 100;

    namespace DefaultValues {
        constexpr float TEMP_AIR_DEFAULT = 20.0f;
        constexpr float HUMIDITY_DEFAULT = 50.0f;
        constexpr float TEMP_WATER_DEFAULT = 20.0f;
    }

    namespace ValidationRanges {
        constexpr float TEMP_MIN = -50.0f;
        constexpr float TEMP_MAX = 100.0f;
        constexpr float HUMIDITY_MIN = 0.0f;
        constexpr float HUMIDITY_MAX = 100.0f;
    }

    // DS18B20 (température eau)
    namespace DS18B20 {
        constexpr uint8_t RESOLUTION_BITS = 10;
        constexpr uint16_t CONVERSION_DELAY_MS = 250;
        constexpr uint16_t READING_INTERVAL_MS = 400;
        constexpr uint8_t STABILIZATION_READINGS = 1;
        constexpr uint16_t STABILIZATION_DELAY_MS = 50;
        constexpr uint16_t ONEWIRE_RESET_DELAY_MS = 100;
    }

    // Validation température eau (après filtrage)
    namespace WaterTemp {
        constexpr float MIN_VALID = 5.0f;
        constexpr float MAX_VALID = 60.0f;
        constexpr float MAX_DELTA = 3.0f;
        constexpr uint8_t MIN_READINGS = 4;
        constexpr uint8_t TOTAL_READINGS = 7;
        constexpr uint16_t RETRY_DELAY_MS = 200;
        constexpr uint8_t MAX_RETRIES = 5;
    }

    // DHT11/DHT22 (air)
    namespace AirSensor {
        constexpr float TEMP_MIN = 3.0f;
        constexpr float TEMP_MAX = 50.0f;
        constexpr float HUMIDITY_MIN = 10.0f;
        constexpr float HUMIDITY_MAX = 100.0f;
    }

    namespace DHT {
        constexpr uint32_t MIN_READ_INTERVAL_MS = 3000;
        constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 2000;
        constexpr float TEMP_MAX_DELTA_C = 3.0f;
        constexpr float HUMIDITY_MAX_DELTA_PERCENT = 10.0f;
    }

    // Ultrasoniques (HC-SR04)
    namespace Ultrasonic {
        constexpr uint16_t MIN_DISTANCE_CM = 2;
        constexpr uint16_t MAX_DISTANCE_CM = 400;
        constexpr uint16_t MAX_DELTA_CM = 30;
        constexpr uint32_t TIMEOUT_US = 30000;
        constexpr uint8_t DEFAULT_SAMPLES = 5;
        constexpr uint32_t MIN_DELAY_MS = 60;
        constexpr uint16_t US_TO_CM_FACTOR = 58;
        constexpr uint8_t FILTERED_READINGS_COUNT = 3;
    }

    // Autres capteurs analogiques
    namespace Analog {
        constexpr uint16_t ADC_MAX_VALUE = 4095;
    }

    namespace History {
        constexpr uint8_t AQUA_HISTORY_SIZE = 16;
        constexpr uint8_t SENSOR_READINGS_COUNT = 3;
    }
}

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
// CONFIGURATION SOMMEIL ET ÉCONOMIE D'ÉNERGIE
// =============================================================================
namespace SleepConfig {
    // ========================================
    // DÉLAIS D'INACTIVITÉ AVANT SOMMEIL
    // ========================================
    
    // SUPPRIMÉ: INACTIVITY_DELAY_MS obsolète - remplacé par le système adaptatif
    
    // Délai minimum absolu depuis le dernier réveil
    // Protection : le système reste éveillé au moins ce temps après chaque réveil
    constexpr uint32_t MIN_AWAKE_TIME_MS = 480000;          // 8 minutes minimum éveillé (augmenté de 2.5 à 8 min)
    
    // Délai d'inactivité web avant autorisation de sommeil
    // Si activité web récente, attendre ce délai avant de permettre le sommeil
    constexpr uint32_t WEB_INACTIVITY_TIMEOUT_MS = 600000;   // 10 minutes d'inactivité web
    
    // ========================================
    // DÉLAIS D'INACTIVITÉ ADAPTATIFS
    // ========================================
    // IMPORTANT: Ces constantes contrôlent les DÉLAIS D'INACTIVITÉ avant autorisation de sommeil,
    // PAS la durée de sommeil elle-même (qui est contrôlée par le serveur distant via freqWakeSec)
    
    // Délai d'inactivité minimum (en secondes)
    // Utilisé quand des erreurs récentes sont détectées (surveillance accrue)
    constexpr uint32_t MIN_INACTIVITY_DELAY_SEC = 300;             // 5 minutes minimum d'inactivité (augmenté de 3 à 5 min)
    
    // Délai d'inactivité maximum (en secondes)
    // Limite supérieure pour éviter des délais trop longs avant sommeil
    constexpr uint32_t MAX_INACTIVITY_DELAY_SEC = 3600;            // 1 heure maximum d'inactivité
    
    // Délai d'inactivité normal (en secondes)
    // Utilisé en fonctionnement standard - augmenté à 8 min pour permettre plus de temps aux marées
    constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 480;          // 8 minutes normal d'inactivité (augmenté de 5 à 8 min)
    
    // Délai d'inactivité en cas d'erreurs (en secondes)
    // Réduit pour surveillance accrue quand des problèmes sont détectés
    constexpr uint32_t ERROR_INACTIVITY_DELAY_SEC = 180;            // 3 minutes si erreurs
    
    // Délai d'inactivité nocturne (en secondes)
    // Réduit la nuit pour dormir plus rapidement (22h-6h)
    // Calcul: NORMAL_INACTIVITY_DELAY_SEC ÷ 2 = 480 ÷ 2 = 240s (4 min)
    constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 240;          // 4 minutes la nuit (augmenté de 2.5 à 4 min)
    
    // ========================================
    // CONTRÔLE MULTIPLICATIF DU SOMMEIL
    // ========================================
    // IMPORTANT: Logique multiplicative respectant les ajustements du serveur
    // La durée nocturne est toujours freqWakeSec × NIGHT_SLEEP_MULTIPLIER
    
    // Multiplicateur nocturne pour la durée de sommeil
    // La nuit, le système dort freqWakeSec × 3 pour économie d'énergie maximale
    constexpr uint8_t NIGHT_SLEEP_MULTIPLIER = 3;                // ×3 la nuit
    
    // Activation du contrôle multiplicatif de la durée de sommeil
    // Si true: nuit = freqWakeSec × NIGHT_SLEEP_MULTIPLIER, jour = freqWakeSec
    // Si false: utilise toujours freqWakeSec du serveur (jour et nuit)
    constexpr bool LOCAL_SLEEP_DURATION_CONTROL = true;           // Contrôle multiplicatif activé
    
    // ========================================
    // PROTECTION ANTI-SPAM DÉBORDEMENT
    // ========================================
    
    // Paramètres de protection contre le spam d'emails de débordement
    constexpr uint32_t FLOOD_DEBOUNCE_MIN = 3;              // Délai avant validation du trop-plein (minutes)
    constexpr uint32_t FLOOD_COOLDOWN_MIN = 180;            // Délai entre emails d'alerte (minutes) 
    constexpr uint16_t FLOOD_HYST_CM = 5;                   // Hystérésis de sortie (cm au-dessus de limFlood)
    constexpr uint32_t FLOOD_RESET_STABLE_MIN = 15;         // Stabilité avant réarmement (minutes)
    
    // ========================================
    // OPTIMISATION WIFI ET CONNEXION
    // ========================================
    
    // Seuils RSSI pour qualité de connexion optimale
    constexpr int8_t WIFI_RSSI_EXCELLENT = -30;             // Excellent signal (> -30 dBm)
    constexpr int8_t WIFI_RSSI_GOOD = -50;                  // Bon signal (-30 à -50 dBm)
    constexpr int8_t WIFI_RSSI_FAIR = -70;                  // Signal acceptable (-50 à -70 dBm)
    constexpr int8_t WIFI_RSSI_POOR = -80;                  // Signal faible (-70 à -80 dBm)
    constexpr int8_t WIFI_RSSI_MINIMUM = -85;               // Signal minimum acceptable (-80 à -85 dBm)
    constexpr int8_t WIFI_RSSI_CRITICAL = -90;              // Signal critique, reconnexion recommandée
    
    // Paramètres de reconnexion intelligente
    constexpr uint32_t WIFI_RECONNECT_DELAY_MS = 2000;      // Délai entre tentatives de reconnexion (ms)
    constexpr uint32_t WIFI_MAX_RECONNECT_ATTEMPTS = 5;     // Nombre max de tentatives de reconnexion
    constexpr uint32_t WIFI_STABILITY_CHECK_INTERVAL_MS = 30000; // Vérification stabilité toutes les 30s
    constexpr uint32_t WIFI_WEAK_SIGNAL_THRESHOLD_MS = 60000;    // Seuil faible signal avant reconnexion (60s)
    
    // ========================================
    // GESTION TEMPORELLE ROBUSTE
    // ========================================
    
    // Validation des epochs temporels
    constexpr time_t EPOCH_MIN_VALID = 1600000000;          // Epoch minimum valide (2020-09-13)
    constexpr time_t EPOCH_MAX_VALID = 2000000000;          // Epoch maximum valide (2033-05-18)
    constexpr time_t EPOCH_DEFAULT_FALLBACK = 1704067200;   // Epoch par défaut (2024-01-01 00:00:00)
    constexpr time_t EPOCH_COMPILE_TIME = 1735689600;       // Epoch de compilation (2025-01-01 00:00:00)
    
    // Paramètres de correction de dérive
    constexpr float DRIFT_CORRECTION_THRESHOLD_PPM = 50.0f; // Seuil pour appliquer correction (50 PPM)
    constexpr uint32_t DRIFT_CORRECTION_INTERVAL_MS = 300000; // Intervalle correction (5 minutes)
    constexpr float DRIFT_CORRECTION_FACTOR = 0.8f;         // Facteur de correction (80% de la dérive)
    
    // Correction de dérive par défaut (quand pas de sync NTP)
    // Valeur négative pour compenser la dérive positive naturelle de l'ESP32
    // Ajuster selon les observations : -25 PPM = correction de 25 PPM vers l'arrière
    constexpr float DEFAULT_DRIFT_CORRECTION_PPM = -25.0f;  // Correction par défaut (-25 PPM)
    constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = false;  // Désactivé pour observation

    // Interrupteur global pour désactiver toute correction de dérive
    constexpr bool ENABLE_DRIFT_CORRECTION = false;          // OFF: pas de correction appliquée
    
    // Paramètres de sauvegarde intelligente
    constexpr time_t MIN_TIME_DIFF_FOR_SAVE_SEC = 60;       // Différence minimum pour sauvegarde (1 minute)
    constexpr uint32_t MAX_SAVE_INTERVAL_MS = 1800000;      // Sauvegarde forcée toutes les 30 minutes
    constexpr uint32_t MIN_SAVE_INTERVAL_MS = 300000;       // Sauvegarde minimum toutes les 5 minutes
    
    // ========================================
    // CONFIGURATION ADAPTATIVE
    // ========================================
    
    // Activation du sommeil adaptatif
    // Si true : délais ajustés selon conditions (erreurs, nuit, échecs)
    // Si false : délai fixe à NORMAL_SLEEP_TIME_SEC
    constexpr bool ADAPTIVE_SLEEP_ENABLED = true;            // Sleep adaptatif activé
    
    // Heures de nuit pour sommeil prolongé (format 24h)
    constexpr uint8_t NIGHT_START_HOUR = 22;                 // Début nuit : 22h
    constexpr uint8_t NIGHT_END_HOUR = 6;                   // Fin nuit : 6h
    
    // ========================================
    // DÉTECTION D'ACTIVITÉ ET MARÉE
    // ========================================
    
    // Seuil de déclenchement marée montante (en cm)
    // Si différence de niveau > ce seuil, sommeil anticipé autorisé
    constexpr int16_t TIDE_TRIGGER_THRESHOLD_CM = 1;         // 1 cm de différence
    
    // ========================================
    // VALIDATION SYSTÈME AVANT SOMMEIL
    // ========================================
    
    // Vérifier que les capteurs répondent avant sommeil
    constexpr bool VALIDATE_SENSORS_BEFORE_SLEEP = true;     // Validation capteurs
    
    // Vérifier la connexion WiFi avant sommeil
    constexpr bool VALIDATE_WIFI_BEFORE_SLEEP = true;        // Validation WiFi
    
    // Vérifier les niveaux d'eau avant sommeil
    constexpr bool VALIDATE_WATER_LEVELS_BEFORE_SLEEP = true; // Validation niveaux
    
    // ========================================
    // GESTION DES ÉCHECS DE RÉVEIL
    // ========================================
    
    // Seuil d'alerte pour échecs consécutifs
    constexpr uint8_t WAKEUP_FAILURE_ALERT_THRESHOLD = 3;    // Alerte après 3 échecs
    
    // Facteur de multiplication du délai en cas d'échecs
    constexpr uint8_t WAKEUP_FAILURE_DELAY_MULTIPLIER = 2;   // Double le délai
    
    // ========================================
    // OPTIMISATIONS ÉNERGÉTIQUES
    // ========================================
    
    // Sauvegarder l'heure avant sommeil
    constexpr bool SAVE_TIME_BEFORE_SLEEP = true;            // Sauvegarde heure
    
    // Sauvegarder les identifiants WiFi avant sommeil
    constexpr bool SAVE_WIFI_CREDENTIALS_BEFORE_SLEEP = true; // Sauvegarde WiFi
    
    // Déconnecter WiFi avant sommeil pour économie
    constexpr bool DISCONNECT_WIFI_BEFORE_SLEEP = true;      // Déconnexion WiFi
    
    // Reconnexion automatique WiFi au réveil
    constexpr bool AUTO_RECONNECT_WIFI_AFTER_SLEEP = true;  // Reconnexion auto
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
// CONFIGURATION TÂCHES FREERTOS - STRATÉGIE WEB DÉDIÉ
// =============================================================================
namespace TaskConfig {
    // Tailles de stack (optimisées - réduites de 25% avec marge de sécurité)
    constexpr uint32_t SENSOR_TASK_STACK_SIZE = 6144;        // Réduit de 8192 à 6144 (économie: 2KB)
    constexpr uint32_t WEB_TASK_STACK_SIZE = 6144;           // Réduit de 8192 à 6144 (économie: 2KB)
    constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 9216;    // Réduit de 12288 à 9216 (économie: 3KB)
    constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 6144;       // Réduit de 8192 à 6144 (économie: 2KB)
    constexpr uint32_t OTA_TASK_STACK_SIZE = 9216;           // Réduit de 12288 à 9216 (économie: 3KB)
    
    // Priorités des tâches - Stratégie Web Dédié Optimisée
    constexpr uint8_t SENSOR_TASK_PRIORITY = 5;             // CRITIQUE - Capteurs prioritaires absolus
    constexpr uint8_t WEB_TASK_PRIORITY = 4;                 // TRÈS HAUTE - Interface web ultra-réactive
    constexpr uint8_t AUTOMATION_TASK_PRIORITY = 2;          // MOYENNE - Logique métier pure
    constexpr uint8_t DISPLAY_TASK_PRIORITY = 1;             // BASSE - Affichage en arrière-plan

    // Core d'exécution
    constexpr uint8_t SENSOR_TASK_CORE_ID = 1;               // Acquisition capteurs sur core 1 (loop Arduino)
    constexpr uint8_t AUTOMATION_TASK_CORE_ID = 1;           // Logique métier alignée avec capteurs
    constexpr uint8_t WEB_TASK_CORE_ID = 0;                  // Serveur web/WebSocket sur core 0 (stack WiFi)
    constexpr uint8_t DISPLAY_TASK_CORE_ID = 0;              // Affichage OLED sur core 0

    // Configuration Web Server optimisée
    constexpr uint8_t WEB_SERVER_CORE_ID = 0;              // Serveur web sur Core 0 (CPU principal)
    constexpr uint8_t WEB_SERVER_PRIORITY = 4;             // Priorité élevée pour le serveur web
    constexpr uint32_t WEB_SERVER_STACK_SIZE = 9216;       // Stack réduit pour le serveur web (économie: 3KB)
}

// =============================================================================
// CONFIGURATION BUFFERS ET MÉMOIRE
// =============================================================================
namespace BufferConfig {
    // Tailles de buffers HTTP
    constexpr uint32_t HTTP_BUFFER_SIZE = 4096;
    constexpr uint32_t HTTP_TX_BUFFER_SIZE = 4096;
    
    // Tailles de documents JSON
    constexpr uint32_t JSON_DOCUMENT_SIZE = 4096;
    
    // Limites de taille pour emails
    constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 6000;
    constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 5000;
    constexpr uint32_t JSON_PREVIEW_MAX_SIZE = 200;
    
    // Seuils de mémoire
    constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 5000;
    constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 10000;
}

// =============================================================================
// CONFIGURATION RÉSEAU
// =============================================================================
namespace NetworkConfig {
    // Timeouts HTTP
    constexpr uint32_t HTTP_TIMEOUT_MS = 30000;
    constexpr uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 300000;  // 5 minutes
    
    // Codes HTTP attendus
    constexpr int HTTP_OK_CODE = 200;
    
    // Configuration backoff exponentiel
    constexpr uint32_t BACKOFF_BASE_MS = 200;
    
    // Configuration WiFi
    constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
    
    // User-Agent HTTP
    constexpr const char* HTTP_USER_AGENT = "ESP32-OTA-Client/1.0";
    
    // Configuration serveur web optimisée
    constexpr uint32_t WEB_SERVER_MAX_CONNECTIONS = 12; // Connexions simultanées max (augmenté)
    constexpr uint32_t WEB_SERVER_TIMEOUT_MS = 8000;    // Timeout serveur web 8s (augmenté)
    constexpr uint32_t WEB_SERVER_KEEPALIVE_MS = 30000; // Keep-alive 30s
}

// =============================================================================
// CONFIGURATION CAPTEURS ÉTENDUE
// =============================================================================
// Fusionné dans SensorConfig
// =============================================================================
// CONFIGURATION DISPLAY ÉTENDUE
// =============================================================================
namespace DisplayConfig {
    constexpr uint8_t OLED_WIDTH = 128;
    constexpr uint8_t OLED_HEIGHT = 64;
    constexpr uint8_t OLED_I2C_ADDR = 0x3C;
    
    // Configuration overlay OTA
    constexpr int OTA_OVERLAY_X_POS = 100;
    constexpr int OTA_OVERLAY_Y_POS = 0;
    constexpr int OTA_OVERLAY_WIDTH = 28;
    constexpr int OTA_OVERLAY_HEIGHT = 8;
    
    // Durée splash screen
    constexpr uint32_t SPLASH_DURATION_MS = 2000;
    
    // Limites pourcentage
    constexpr int PERCENTAGE_MAX = 100;

  // ========================================
  // BARRES DE PROGRESSION
  // ========================================
  constexpr int PROGRESS_BAR_X = 4;
  constexpr int PROGRESS_BAR_Y = 48;
  constexpr int PROGRESS_BAR_WIDTH = 120;
  constexpr int PROGRESS_BAR_HEIGHT = 10;
  constexpr int PROGRESS_BAR_ALT_Y = 40;

  // ========================================
  // ROTATION D'ÉCRAN
  // ========================================
  constexpr uint32_t SCREEN_SWITCH_INTERVAL_MS = 4000;    // Intervalle changement écran (4s)

  // ========================================
  // PARAMÈTRES OLED BAS NIVEAU
  // ========================================
  constexpr uint8_t DISPLAY_WHITE = 1;
  constexpr uint8_t DISPLAY_BLACK = 0;
  constexpr uint8_t SSD1306_SWITCHCAPVCC = 0;
}

// =============================================================================
// HELPERS ET UTILITAIRES
// =============================================================================
namespace Utils {
    // Obtenir le nom du profil actuel
    inline const char* getProfileName() {
        #if defined(PROFILE_PROD)
            return "PRODUCTION";
        #elif defined(PROFILE_TEST)
            return "TEST";
        #elif defined(PROFILE_DEV)
            return "DEVELOPMENT";
        #else
            return "DEFAULT";
        #endif
    }
    
    // Obtenir une description complète du système
    inline void getSystemInfo(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "FFP5CS v%s [%s/%s]", 
                 ProjectConfig::VERSION, ProjectConfig::BOARD_TYPE, getProfileName());
    }
}

// =============================================================================
// COMPATIBILITÉ ET ALIAS (pour migration depuis config.h)
// =============================================================================

namespace CompatibilityAliases {
    // Configuration display (alias)
    constexpr uint8_t OLED_WIDTH = DisplayConfig::OLED_WIDTH;
    constexpr uint8_t OLED_HEIGHT = DisplayConfig::OLED_HEIGHT;
    
    // Configuration servo (alias)
    constexpr int SERVO_MIN_ANGLE = ActuatorConfig::SERVO_MIN_ANGLE;
    constexpr int SERVO_MAX_ANGLE = ActuatorConfig::SERVO_MAX_ANGLE;
    
    // Debug flags
    constexpr bool DEBUG_MODE = (LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_DEBUG);
    constexpr bool VERBOSE_LOGGING = (LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_VERBOSE);
}

// =============================================================================
// FONCTIONS UTILITAIRES DE COMPATIBILITÉ
// =============================================================================
namespace CompatibilityUtils {
    // Obtenir le nom de l'environnement (ancien style)
    inline const char* getEnvironmentName() {
        return Utils::getProfileName();
    }
    
    // Vérifier si on est en mode debug
    inline bool isDebugMode() {
        return LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_DEBUG;
    }
}

// =============================================================================
// MACROS DE DEBUG COMPATIBILITÉ
// =============================================================================
// Macros de debug - redirigées vers le nouveau système
// Note: Ces macros sont définies après l'inclusion des bibliothèques externes
// pour éviter les conflits avec DHT et autres bibliothèques

// Les macros DEBUG_PRINT* sont maintenant définies dans config.h pour éviter les conflits

// =============================================================================
// RÉTRO-COMPATIBILITÉ (minimal pour compilation)
// =============================================================================
namespace Config {
    constexpr const char* VERSION = ProjectConfig::VERSION;
    constexpr const char* SENSOR = ProjectConfig::BOARD_TYPE;
    constexpr const char* API_KEY = ApiConfig::API_KEY;
    constexpr const char* DEFAULT_MAIL_TO = EmailConfig::DEFAULT_RECIPIENT;
    constexpr const char* SERVER_POST_DATA = ServerConfig::POST_DATA_ENDPOINT;
    constexpr const char* SERVER_OUTPUT = ServerConfig::OUTPUT_ENDPOINT;
    constexpr const char* SMTP_HOST = EmailConfig::SMTP_HOST;
    constexpr uint16_t SMTP_PORT_SSL = EmailConfig::SMTP_PORT;
    
    namespace Default {
        constexpr int AQ_LIMIT_CM = ActuatorConfig::Default::AQUA_LEVEL_CM;
        constexpr int TANK_LIMIT_CM = ActuatorConfig::Default::TANK_LEVEL_CM;
        constexpr float HEATER_THRESHOLD = ActuatorConfig::Default::HEATER_THRESHOLD_C;
    }
}


// =============================================================================
// CONFIGURATION DÉLAIS ET TIMING ÉTENDUE
// =============================================================================
// Fusionné dans TimingConfig

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

// =============================================================================
// CONFIGURATION CAPTEURS SPÉCIFIQUES
// =============================================================================
// Fusionnée dans SensorConfig

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


// =============================================================================
// CONFIGURATION MODEM SLEEP + LIGHT SLEEP
// =============================================================================
namespace ModemSleepConfig {
    // Activation du modem sleep avec light sleep automatique
    constexpr bool ENABLE_MODEM_SLEEP = true;              // Activer le modem sleep
    
    // Test automatique de compatibilité DTIM
    constexpr bool AUTO_TEST_DTIM = true;                  // Test automatique DTIM
    
    // Intervalle de test DTIM (en millisecondes)
    constexpr unsigned long DTIM_TEST_INTERVAL_MS = 300000; // 5 minutes
    
    // Configuration DTIM optimale
    constexpr int DTIM_POWER_SAVE_MODE = 1; // WIFI_PS_MIN_MODEM equivalent
    
    // Logs détaillés pour debug
    constexpr bool ENABLE_DETAILED_LOGS = true;            // Logs détaillés
    
    // Fallback automatique vers light sleep classique si modem sleep échoue
    constexpr bool ENABLE_AUTO_FALLBACK = true;            // Fallback automatique
}
