#pragma once

#include <Arduino.h>

// =============================================================================
// CONFIGURATION UNIFIÉE DU PROJET FFP5CS
// =============================================================================
// Remplace l'ancienne configuration éclatée en multiples fichiers
// =============================================================================

// -----------------------------------------------------------------------------
// 1. VERSION ET IDENTIFICATION
// -----------------------------------------------------------------------------
namespace ProjectConfig {
    // Simplification séquentielle réseau (plus de tâche mail dédiée)
    constexpr const char* VERSION = "11.156";
    
    // Type d'environnement
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

namespace Utils {
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
    
    inline void getSystemInfo(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "FFP5CS v%s [%s/%s]", 
                 ProjectConfig::VERSION, ProjectConfig::BOARD_TYPE, getProfileName());
    }
}

// -----------------------------------------------------------------------------
// 2. SYSTÈME ET TIMING
// -----------------------------------------------------------------------------
namespace SystemConfig {
    constexpr uint32_t SERIAL_BAUD_RATE = 115200;
    
    // NTP (UTC+1 Maroc)
    constexpr int32_t NTP_GMT_OFFSET_SEC = 3600;
    constexpr int32_t NTP_DAYLIGHT_OFFSET_SEC = 0;
    constexpr const char* NTP_SERVER = "pool.ntp.org";

    // Time validation
    constexpr time_t EPOCH_MIN_VALID = 1704067200; // 2024-01-01
    constexpr time_t EPOCH_MAX_VALID = 2524608000; // 2050-01-01
    
    // OTA
    constexpr uint16_t ARDUINO_OTA_PORT = 3232;
    
    // Délais
    constexpr uint32_t INITIAL_DELAY_MS = 200;
    constexpr uint32_t FINAL_INIT_DELAY_MS = 1000;
    
    // Hostname
    constexpr const char* HOSTNAME_PREFIX = "ffp5";
    constexpr size_t HOSTNAME_BUFFER_SIZE = 20;
}

namespace GlobalTimeouts {
    constexpr uint32_t GLOBAL_MAX_MS = 5000;
    // Note: HTTP timeout unifié dans NetworkConfig::HTTP_TIMEOUT_MS (5000ms)
    // Utiliser NetworkConfig::HTTP_TIMEOUT_MS pour les timeouts HTTP
    constexpr uint32_t DS18B20_MAX_MS = 1000;
}

namespace TimingConfig {
    // WiFi
    constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
    constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
    constexpr uint32_t WIFI_WATCHDOG_TIMEOUT_MS = 30000;
    
    // Serveur
    constexpr uint32_t SERVER_SYNC_INTERVAL_MS = 60000;
    constexpr uint32_t SERVER_RETRY_INTERVAL_MS = 5000;
    
    // Tâches périodiques
    constexpr uint32_t OTA_CHECK_INTERVAL_MS = 7200000; // 2h
    constexpr uint32_t OTA_PROGRESS_UPDATE_INTERVAL_MS = 1000; // 1s
    constexpr uint32_t DIGEST_INTERVAL_MS = 3600000;    // 1h
    constexpr uint32_t STATS_REPORT_INTERVAL_MS = 300000; // 5 min
    
    // Protection et Timeouts
    constexpr uint32_t WAKEUP_PROTECTION_DURATION_MS = 30000;
    constexpr uint32_t WEB_ACTIVITY_TIMEOUT_MS = 60000;
    
    // Intervalles d'affichage
    constexpr uint32_t MIN_DISPLAY_INTERVAL_MS = 100;
    constexpr uint32_t BOUFFE_DISPLAY_INTERVAL_MS = 1000;
    constexpr uint32_t PUMP_STATS_DISPLAY_INTERVAL_MS = 1000;
    constexpr uint32_t DRIFT_DISPLAY_INTERVAL_MS = 1000;
}

namespace MonitoringConfig {
    constexpr bool ENABLE_DRIFT_VISUAL_INDICATOR = true;
    constexpr uint32_t DRIFT_CHECK_INTERVAL_MS = 60000;
}

// -----------------------------------------------------------------------------
// 2.1 DIAGNOSTICS (FEATURE FLAGS)
// -----------------------------------------------------------------------------
// v11.145: Désactivation des diagnostics non essentiels pour simplification
// Ces features ajoutent de la complexité sans bénéfice réel en production
//
// NOTE: FEATURE_DIAG_DIGEST et FEATURE_DIAG_TIME_DRIFT sont définis à 0.
// Le code conditionnel associé a été supprimé (voir RAPPORT_ANALYSE_CODE_MORT_2026-01-25.md).
// Ces flags sont conservés ici uniquement pour référence historique.
#ifndef FEATURE_DIAG_DIGEST
  #define FEATURE_DIAG_DIGEST 0  // Désactivé: emails digest non essentiels
  // CODE SUPPRIMÉ: Le bloc conditionnel a été retiré de app.cpp
#endif

#ifndef FEATURE_DIAG_STATS
  #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    #define FEATURE_DIAG_STATS 1  // Activé en test/dev pour diagnostics
  #else
    #define FEATURE_DIAG_STATS 0  // Désactivé en production
  #endif
  // LÉGITIME: Code conditionnel utilisé en mode test/dev
#endif

#ifndef FEATURE_DIAG_TIME_DRIFT
  #define FEATURE_DIAG_TIME_DRIFT 0  // Désactivé: monitoring drift non essentiel
  // CODE SUPPRIMÉ: Les blocs conditionnels ont été retirés de app.cpp et app_tasks.cpp
#endif

#ifndef FEATURE_DIAG_STACK_LOGS
  #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    #define FEATURE_DIAG_STACK_LOGS 1  // Activé en test/dev pour diagnostics stacks
  #else
    #define FEATURE_DIAG_STACK_LOGS 0  // Désactivé en production
  #endif
  // LÉGITIME: Code conditionnel utilisé en mode test/dev
#endif

// -----------------------------------------------------------------------------
// 3. RÉSEAU ET SERVEUR
// -----------------------------------------------------------------------------
namespace NetworkConfig {
    constexpr uint32_t WEB_SERVER_TIMEOUT_MS = 2000;
    constexpr uint8_t WEB_SERVER_MAX_CONNECTIONS = 4;
    constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;
    // Timeout HTTP unifié (conforme à .cursorrules: max 5s pour opérations réseau)
    constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
    // Timeout OTA séparé : téléchargement firmware nécessite plus de temps
    // que requêtes HTTP standard
    // Justification : connexions lentes peuvent nécessiter jusqu'à 30s
    // pour télécharger un firmware complet
    // 30s pour téléchargements OTA (justifié par taille firmware)
    constexpr uint32_t OTA_TIMEOUT_MS = 30000;
    constexpr uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 300000; // 5 min
    constexpr uint32_t MIN_DELAY_BETWEEN_REQUESTS_MS = 1000;
    constexpr uint32_t BACKOFF_BASE_MS = 1000;
    
    constexpr const char* HTTP_USER_AGENT = "FFP5CS-ESP32";
    constexpr int HTTP_OK_CODE = 200;
}

namespace ServerConfig {
    constexpr const char* BASE_URL = "https://iot.olution.info";
    
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV) || defined(USE_TEST_ENDPOINTS)
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data-test";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs-test/state";
        constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat-test";
    #else
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs/state";
        constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat";
    #endif
    
    constexpr const char* OTA_BASE_PATH = "/ffp3/ota/";
    
    // Helpers pour buffers statiques
    inline void getPostDataUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s%s", BASE_URL, POST_DATA_ENDPOINT);
    }
    
    inline void getOutputUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s%s", BASE_URL, OUTPUT_ENDPOINT);
    }
    
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

namespace ApiConfig {
    constexpr const char* API_KEY = "fdGTMoptd5CD2ert3";
}

namespace EmailConfig {
    constexpr const char* SMTP_HOST = "smtp.gmail.com";
    constexpr uint16_t SMTP_PORT = 465;  // SSL direct
    constexpr const char* DEFAULT_RECIPIENT = "oliv.arn.lau@gmail.com";
    constexpr size_t MAX_EMAIL_LENGTH = 96;
}

// -----------------------------------------------------------------------------
// 4. MÉMOIRE ET BUFFERS
// -----------------------------------------------------------------------------
namespace BufferConfig {
    #if defined(BOARD_S3)
        constexpr uint32_t HTTP_BUFFER_SIZE = 4096;
        constexpr uint32_t HTTP_TX_BUFFER_SIZE = 4096;
        constexpr uint32_t JSON_DOCUMENT_SIZE = 4096;
        constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 6000;
        constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 5000;
        constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 10000;
        constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 20000;
    #else
        // v11.158: Optimisation buffers - réduits pour libérer mémoire et réduire fragmentation
        constexpr uint32_t HTTP_BUFFER_SIZE = 1024;  // Réduit de 2048 (requêtes typiquement < 1024 bytes)
        constexpr uint32_t HTTP_TX_BUFFER_SIZE = 1024;  // Réduit de 2048
        constexpr uint32_t JSON_DOCUMENT_SIZE = 1024;  // Réduit de 2048 (réponses serveur < 500 bytes)
        constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 2000;  // Réduit de 3000 (emails typiquement < 2000 bytes)
        constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 1500;  // Réduit de 2500
        constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 8000;
        constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 15000;
    #endif
    
    constexpr uint32_t JSON_PREVIEW_MAX_SIZE = 200;
}

// -----------------------------------------------------------------------------
// 5. CAPTEURS ET ACTIONNEURS
// -----------------------------------------------------------------------------
namespace SensorConfig {
    constexpr uint32_t SENSOR_READ_DELAY_MS = 100;
    constexpr uint32_t I2C_STABILIZATION_DELAY_MS = 100;

    namespace DefaultValues {
        constexpr float TEMP_AIR_DEFAULT = 20.0f;
        constexpr float HUMIDITY_DEFAULT = 50.0f;
        constexpr float TEMP_WATER_DEFAULT = 20.0f;
    }

    namespace WaterTemp {
        constexpr float MIN_VALID = 5.0f;
        constexpr float MAX_VALID = 60.0f;
        constexpr float MAX_DELTA = 3.0f;
        constexpr uint8_t MIN_READINGS = 4;
        constexpr uint8_t TOTAL_READINGS = 7;
        constexpr uint16_t RETRY_DELAY_MS = 200;
        constexpr uint8_t MAX_RETRIES = 5;
    }

    namespace AirSensor {
        constexpr float TEMP_MIN = 3.0f;
        constexpr float TEMP_MAX = 50.0f;
        constexpr float HUMIDITY_MIN = 10.0f;
        constexpr float HUMIDITY_MAX = 100.0f;
    }

    namespace DHT {
        // Délai minimum entre lectures: 2500ms (compromis entre 2000ms datasheet et stabilité)
        // Le datasheet DHT22 recommande minimum 2s, on utilise 2.5s pour plus de marge
        constexpr uint32_t MIN_READ_INTERVAL_MS = 2500;
        constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 2000;
    }

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

    namespace History {
        constexpr uint8_t AQUA_HISTORY_SIZE = 16;
        constexpr uint8_t SENSOR_READINGS_COUNT = 3;
    }
    
    namespace DS18B20 {
        constexpr uint8_t RESOLUTION_BITS = 10;
        // 220ms = 187.5ms (datasheet) + 17% marge (recommandation: +10-20%)
        constexpr uint16_t CONVERSION_DELAY_MS = 220;
        constexpr uint16_t READING_INTERVAL_MS = 400;
        constexpr uint8_t STABILIZATION_READINGS = 1;
        constexpr uint16_t STABILIZATION_DELAY_MS = 50;
        constexpr uint16_t ONEWIRE_RESET_DELAY_MS = 100;
    }
}

namespace ActuatorConfig {
    namespace Default {
        constexpr int AQUA_LEVEL_CM = 18;
        constexpr int TANK_LEVEL_CM = 80;
        constexpr float HEATER_THRESHOLD_C = 18.0f;
        constexpr uint16_t FEED_BIG_DURATION_SEC = 3;
        constexpr uint16_t FEED_SMALL_DURATION_SEC = 2;
    }
    
    constexpr int SERVO_MIN_ANGLE = 0;
    constexpr int SERVO_MAX_ANGLE = 180;
}

// -----------------------------------------------------------------------------
// 6. LOGGING ET DEBUG
// -----------------------------------------------------------------------------
namespace LogConfig {
    enum LogLevel {
        LOG_NONE = 0,
        LOG_ERROR = 1,
        LOG_WARN = 2,
        LOG_INFO = 3,
        LOG_DEBUG = 4,
        LOG_VERBOSE = 5
    };
    
    // Configuration par défaut selon l'environnement
    #if defined(PROFILE_PROD)
        // Production: ERROR uniquement (et INFO critique)
        constexpr LogLevel DEFAULT_LEVEL = LOG_ERROR;
        constexpr bool SERIAL_ENABLED = false;
        constexpr bool SENSOR_LOGS_ENABLED = false;
    #else
        // Test/Dev: INFO par défaut
        constexpr LogLevel DEFAULT_LEVEL = LOG_INFO;
        constexpr bool SERIAL_ENABLED = true;
        constexpr bool SENSOR_LOGS_ENABLED = true;
    #endif
}

// Macros de logging unifiées
#if defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)
    #define LOG_PRINT(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
    #define LOG_PRINTLN(msg) Serial.println(msg)
    
    #define LOG_ERROR(fmt, ...) Serial.printf("[ERR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...)  Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...)  Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
    
    // Debug seulement si niveau suffisant (à implémenter proprement si nécessaire)
    #define LOG_DEBUG(fmt, ...) Serial.printf("[DBG] " fmt "\n", ##__VA_ARGS__)
    
    // Macros conditionnelles capteurs
    #if defined(ENABLE_SENSOR_LOGS) && (ENABLE_SENSOR_LOGS == 1)
        #define SENSOR_LOG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
        #define SENSOR_LOG_PRINTLN(msg) Serial.println(msg)
    #else
        #define SENSOR_LOG_PRINTF(fmt, ...) ((void)0)
        #define SENSOR_LOG_PRINTLN(msg) ((void)0)
    #endif

#else
    // Logs désactivés
    #define LOG_PRINT(fmt, ...) ((void)0)
    #define LOG_PRINTLN(msg) ((void)0)
    #define LOG_ERROR(fmt, ...) ((void)0)
    #define LOG_WARN(fmt, ...) ((void)0)
    #define LOG_INFO(fmt, ...) ((void)0)
    #define LOG_DEBUG(fmt, ...) ((void)0)
    #define SENSOR_LOG_PRINTF(fmt, ...) ((void)0)
    #define SENSOR_LOG_PRINTLN(msg) ((void)0)
#endif

// Macros de compatibilité (désactivées car gérées par log.h)
// #define LOG(level, fmt, ...) LOG_INFO(fmt, ##__VA_ARGS__)
// #define LOG_TIME(level, fmt, ...) LOG_INFO(fmt, ##__VA_ARGS__)
// #define LOG_DRIFT(level, fmt, ...) LOG_INFO(fmt, ##__VA_ARGS__)

// -----------------------------------------------------------------------------
// 7. DISPLAY
// -----------------------------------------------------------------------------
namespace DisplayConfig {
    constexpr uint8_t OLED_WIDTH = 128;
    constexpr uint8_t OLED_HEIGHT = 64;
    constexpr uint8_t OLED_I2C_ADDR = 0x3C;
    
    constexpr int PERCENTAGE_MAX = 100;

    constexpr int OTA_OVERLAY_X_POS = 100;
    constexpr int OTA_OVERLAY_Y_POS = 0;
    constexpr int OTA_OVERLAY_WIDTH = 28;
    constexpr int OTA_OVERLAY_HEIGHT = 8;
    
    constexpr uint32_t SPLASH_DURATION_MS = 3000;  // Durée du splash screen (3 secondes)
    constexpr uint32_t SCREEN_SWITCH_INTERVAL_MS = 6000;
    
    constexpr uint8_t DISPLAY_WHITE = 1;
    constexpr uint8_t DISPLAY_BLACK = 0;
    constexpr uint8_t SSD1306_SWITCHCAPVCC = 0;
}

// -----------------------------------------------------------------------------
// 8. SOMMEIL ET ÉCONOMIE D'ÉNERGIE
// -----------------------------------------------------------------------------
namespace SleepConfig {
    constexpr uint32_t LIGHT_SLEEP_DURATION_MS = 60000; // 1 minute
    constexpr uint32_t DEEP_SLEEP_DURATION_MS = 3600000; // 1 heure

    // Time validation - Alias vers SystemConfig pour éviter duplication
    constexpr time_t EPOCH_MIN_VALID = SystemConfig::EPOCH_MIN_VALID;
    constexpr time_t EPOCH_MAX_VALID = SystemConfig::EPOCH_MAX_VALID;
    
    // Valeurs manquantes ajoutées
    constexpr int16_t TIDE_TRIGGER_THRESHOLD_CM = 10;
    constexpr uint32_t MIN_INACTIVITY_DELAY_SEC = 300;
    constexpr uint32_t MAX_INACTIVITY_DELAY_SEC = 3600;
    constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 300;
    constexpr uint32_t ERROR_INACTIVITY_DELAY_SEC = 90;
    constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 1800;
    constexpr bool ADAPTIVE_SLEEP_ENABLED = true;
    constexpr uint32_t FLOOD_COOLDOWN_MIN = 60;
    constexpr uint32_t FLOOD_DEBOUNCE_MIN = 5;
    constexpr uint16_t FLOOD_HYST_CM = 2;
    constexpr uint32_t FLOOD_RESET_STABLE_MIN = 15;
    constexpr uint8_t WAKEUP_FAILURE_ALERT_THRESHOLD = 3;
    constexpr uint32_t MIN_AWAKE_TIME_MS = 15000;
    constexpr bool LOCAL_SLEEP_DURATION_CONTROL = true;
    constexpr float NIGHT_SLEEP_MULTIPLIER = 3.0f;
    
    constexpr int8_t WIFI_RSSI_EXCELLENT = -55;
    constexpr int8_t WIFI_RSSI_GOOD = -65;
    constexpr int8_t WIFI_RSSI_FAIR = -75;
    constexpr int8_t WIFI_RSSI_POOR = -85;
    constexpr int8_t WIFI_RSSI_MINIMUM = -90;
    constexpr int8_t WIFI_RSSI_CRITICAL = -95;
    
    constexpr uint32_t WIFI_STABILITY_CHECK_INTERVAL_MS = 60000;
    constexpr uint32_t WIFI_WEAK_SIGNAL_THRESHOLD_MS = 300000;
    constexpr uint8_t WIFI_MAX_RECONNECT_ATTEMPTS = 5;
    constexpr uint32_t WIFI_RECONNECT_DELAY_MS = 5000;

    // Constantes PowerManager manquantes
    constexpr bool AUTO_RECONNECT_WIFI_AFTER_SLEEP = true;
    constexpr bool SAVE_TIME_BEFORE_SLEEP = true;
    constexpr time_t EPOCH_COMPILE_TIME = 1704067200; // Fallback
    constexpr time_t EPOCH_DEFAULT_FALLBACK = 1704067200;
    constexpr bool ENABLE_DRIFT_CORRECTION = true;
    constexpr uint32_t DRIFT_CORRECTION_INTERVAL_MS = 3600000;
    constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = true;
    constexpr float DEFAULT_DRIFT_CORRECTION_PPM = 0.0f;
    constexpr float DRIFT_CORRECTION_THRESHOLD_PPM = 100.0f;
    constexpr float DRIFT_CORRECTION_FACTOR = 0.5f;
    constexpr uint32_t MAX_SAVE_INTERVAL_MS = 86400000;
    constexpr uint32_t MIN_SAVE_INTERVAL_MS = 3600000;
    constexpr uint32_t MIN_TIME_DIFF_FOR_SAVE_SEC = 60;
}

namespace TaskConfig {
    // PISTE 5: Vérification des stacks FreeRTOS pour TLS
    // Les stacks sont suffisantes pour TLS:
    // - AUTOMATION_TASK: 8KB (suffisant pour TLS appelé depuis automationTask)
    // - WEB_TASK: 6KB (suffisant pour opérations web)
    // - MAIL_TASK: 10KB (spécifiquement dimensionnée pour TLS/SMTP)
    // - Loop() utilise la stack par défaut (configurée par ESP-IDF, typiquement 8KB)
    // Note: TLS peut être appelé depuis loop() via fetchRemoteState()
    // La stack par défaut devrait être suffisante
    
    constexpr uint32_t DEFAULT_STACK_SIZE = 4096;
    constexpr UBaseType_t DEFAULT_PRIORITY = 1;
    
    // v11.157: Réductions basées sur HWM analysés (sensor:1864, web:5332, display:2328 libres)
    // autoTask: 7356 libres au boot mais 94.9% utilisé plus tard - NE PAS RÉDUIRE
    constexpr uint32_t SENSOR_TASK_STACK_SIZE = 3072;  // Réduit de 4096 (HWM: 1864 libres, marge 1208)
    constexpr UBaseType_t SENSOR_TASK_PRIORITY = 2;
    constexpr BaseType_t SENSOR_TASK_CORE_ID = 1;
    
    // v11.159: Réduit de 5KB à 4KB (Phase 3 - HWM: 5332 libres, marge 212)
    constexpr uint32_t WEB_TASK_STACK_SIZE = 4096;  // 4KB
    // Baissé de 2 à 1 - le web n'est pas critique (offline-first)
    constexpr UBaseType_t WEB_TASK_PRIORITY = 1;
    constexpr BaseType_t WEB_TASK_CORE_ID = 0;
    
    // v11.157: Augmenté de 6KB à 8KB pour éviter stack overflow (HWM: 100 bytes libres = critique)
    // Le crash se produit dans automationTask lors de la sauvegarde NVS
    constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 8192;  // 8KB
    constexpr UBaseType_t AUTOMATION_TASK_PRIORITY = 3;
    constexpr BaseType_t AUTOMATION_TASK_CORE_ID = 1;
    
    constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 3072;  // Réduit de 4096 (HWM: 2328 libres, marge 744)
    constexpr UBaseType_t DISPLAY_TASK_PRIORITY = 1;
    constexpr BaseType_t DISPLAY_TASK_CORE_ID = 1;

    constexpr uint32_t OTA_TASK_STACK_SIZE = 8192;
    
    // Tâche réseau (TLS/HTTP) - propriétaire unique de WebClient/TLS
    // v11.159: Réduit de 10KB à 8KB (Phase 3 - HWM: 5584 libres, marge 4656)
    constexpr uint32_t NET_TASK_STACK_SIZE = 8192;   // 8 KB (requis pour TLS/HTTPS avec mbedTLS)
    constexpr UBaseType_t NET_TASK_PRIORITY = 2;      // Priorité moyenne pour traitement réseau
    constexpr BaseType_t NET_TASK_CORE_ID = 0;        // Core 0 pour ne pas impacter capteurs
    
    // Tâche mail asynchrone (v11.143) - évite de bloquer automationTask pendant SMTP
    constexpr uint32_t MAIL_TASK_STACK_SIZE = 10240;  // 10 KB (requis pour TLS/SMTP avec mbedTLS)
    constexpr UBaseType_t MAIL_TASK_PRIORITY = 1;     // Basse priorité (non critique)
    constexpr BaseType_t MAIL_TASK_CORE_ID = 0;       // Core 0 pour ne pas impacter capteurs
    constexpr uint8_t MAIL_QUEUE_SIZE = 2;            // 2 mails en attente (réduit de 5 - v11.144)
}

namespace DefaultValues {
    constexpr float WATER_TEMP = 20.0f;
}

// -----------------------------------------------------------------------------
// 9. DÉSACTIVATION SÛRE DE SERIAL EN PROD
// -----------------------------------------------------------------------------
// Quand ENABLE_SERIAL_MONITOR=0 (ou profil PROD sans override), on redirige Serial
// vers un stub constexpr pour éliminer à la compilation les appels Serial.* et
// réduire la taille flash (wroom-prod est proche de la limite de partition).
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 0)) || \
    (!defined(ENABLE_SERIAL_MONITOR) && defined(PROFILE_PROD))
namespace LogConfig {
    struct NullSerialType {
        template<typename... Args>
        inline constexpr size_t printf(const char*, Args...) const { return 0U; }
        inline constexpr size_t println() const { return 0U; }
        template<typename T>
        inline constexpr size_t println(const T&) const { return 0U; }
        template<typename T>
        inline constexpr size_t print(const T&) const { return 0U; }
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
}  // namespace LogConfig

#define Serial LogConfig::NullSerial
#define Serial1 LogConfig::NullSerial
#define Serial2 LogConfig::NullSerial
#endif
