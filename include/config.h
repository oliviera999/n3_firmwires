#pragma once

#include <Arduino.h>
#include "gpio_mapping.h"  // Pour GPIODefaults (source de vérité des valeurs par défaut)

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
    inline constexpr const char* VERSION = "11.170";  // UI controles, sync, web_server, web_client
    
    // Type d'environnement
    #if defined(PROFILE_DEV)
        inline constexpr const char* PROFILE_TYPE = "dev";
    #elif defined(PROFILE_TEST)
        inline constexpr const char* PROFILE_TYPE = "test";
    #elif defined(PROFILE_PROD)
        inline constexpr const char* PROFILE_TYPE = "prod";
    #else
        inline constexpr const char* PROFILE_TYPE = "unknown";
    #endif
    
    // Identification matérielle
    #if defined(BOARD_S3)
        inline constexpr const char* BOARD_TYPE = "esp32-s3";
    #else
        inline constexpr const char* BOARD_TYPE = "esp32-wroom";
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
    inline constexpr uint32_t SERIAL_BAUD_RATE = 115200;
    
    // NTP (UTC+1 Maroc)
    inline constexpr int32_t NTP_GMT_OFFSET_SEC = 3600;
    inline constexpr int32_t NTP_DAYLIGHT_OFFSET_SEC = 0;
    inline constexpr const char* NTP_SERVER = "pool.ntp.org";

    // Time validation
    inline constexpr time_t EPOCH_MIN_VALID = 1704067200; // 2024-01-01
    inline constexpr time_t EPOCH_MAX_VALID = 2524608000; // 2050-01-01
    
    // OTA
    inline constexpr uint16_t ARDUINO_OTA_PORT = 3232;
    
    // Délais
    inline constexpr uint32_t INITIAL_DELAY_MS = 200;
    inline constexpr uint32_t FINAL_INIT_DELAY_MS = 1000;
    
    // Hostname
    inline constexpr const char* HOSTNAME_PREFIX = "ffp5";
    inline constexpr size_t HOSTNAME_BUFFER_SIZE = 20;
}

// Note: GlobalTimeouts supprimé (v11.174 simplification)
// - GLOBAL_MAX_MS remplacé par NetworkConfig::HTTP_TIMEOUT_MS
// - DS18B20_MAX_MS déplacé dans SensorConfig::DS18B20::TIMEOUT_MS

namespace TimingConfig {
    // WiFi - v11.165: Timeout réduit à 3s (règle offline-first: max 3s blocage)
    inline constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 3000;
    // v11.168: Timeout boot plus long pour laisser le temps de récupérer config serveur
    // Au boot uniquement, on peut attendre un peu plus car c'est le seul moment
    // où on peut récupérer la config distante de manière fiable
    inline constexpr uint32_t WIFI_BOOT_TIMEOUT_MS = 8000;
    inline constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
    inline constexpr uint32_t WIFI_WATCHDOG_TIMEOUT_MS = 30000;
    
    // Serveur
    inline constexpr uint32_t SERVER_SYNC_INTERVAL_MS = 60000;
    inline constexpr uint32_t SERVER_RETRY_INTERVAL_MS = 5000;
    
    // Tâches périodiques
    inline constexpr uint32_t OTA_CHECK_INTERVAL_MS = 7200000; // 2h
    inline constexpr uint32_t OTA_PROGRESS_UPDATE_INTERVAL_MS = 1000; // 1s
    inline constexpr uint32_t DIGEST_INTERVAL_MS = 3600000;    // 1h
    inline constexpr uint32_t STATS_REPORT_INTERVAL_MS = 300000; // 5 min
    
    // Protection et Timeouts
    inline constexpr uint32_t WAKEUP_PROTECTION_DURATION_MS = 30000;
    inline constexpr uint32_t WEB_ACTIVITY_TIMEOUT_MS = 60000;
    
    // Intervalle tâche capteurs: >= DHT MIN_READ_INTERVAL_MS (datasheet 2s, config 2.5s)
    inline constexpr uint32_t SENSOR_TASK_INTERVAL_MS = 2500;

    // Intervalles d'affichage
    inline constexpr uint32_t MIN_DISPLAY_INTERVAL_MS = 100;
    inline constexpr uint32_t BOUFFE_DISPLAY_INTERVAL_MS = 1000;
    inline constexpr uint32_t PUMP_STATS_DISPLAY_INTERVAL_MS = 1000;
    inline constexpr uint32_t DRIFT_DISPLAY_INTERVAL_MS = 1000;
}

namespace MonitoringConfig {
    inline constexpr bool ENABLE_DRIFT_VISUAL_INDICATOR = true;
    inline constexpr uint32_t DRIFT_CHECK_INTERVAL_MS = 60000;
}

// -----------------------------------------------------------------------------
// 2.1 DIAGNOSTICS (FEATURE FLAGS)
// -----------------------------------------------------------------------------
// v11.145: Désactivation des diagnostics non essentiels pour simplification
// Diagnostics non essentiels (digest, time drift) désactivés ; code et flags retirés.

#ifndef FEATURE_DIAG_STATS
  #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    #define FEATURE_DIAG_STATS 1  // Activé en test/dev pour diagnostics
  #else
    #define FEATURE_DIAG_STATS 0  // Désactivé en production
  #endif
  // LÉGITIME: Code conditionnel utilisé en mode test/dev
#endif

#ifndef FEATURE_DIAG_STACK_LOGS
  #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    #define FEATURE_DIAG_STACK_LOGS 1  // Activé en test/dev pour diagnostics stacks
  #else
    #define FEATURE_DIAG_STACK_LOGS 0  // Désactivé en production
  #endif
  // LÉGITIME: Code conditionnel utilisé en mode test/dev
#endif

// Garde-fou HTTPS: refuser la requête si le plus grand bloc libre < 45 KB (TLS ~42–46 KB contigus).
// Définir à 0 pour désactiver (tenter TLS quand même; échec si allocation impossible).
#ifndef FEATURE_HTTP_HEAP_GUARD
  #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    #define FEATURE_HTTP_HEAP_GUARD 1  // Activé en test/dev par défaut
  #else
    #define FEATURE_HTTP_HEAP_GUARD 0  // Désactivé en production
  #endif
#endif

// -----------------------------------------------------------------------------
// 3. RÉSEAU ET SERVEUR
// -----------------------------------------------------------------------------
namespace NetworkConfig {
    inline constexpr uint32_t WEB_SERVER_TIMEOUT_MS = 2000;
    inline constexpr uint8_t WEB_SERVER_MAX_CONNECTIONS = 4;
    // Timeout HTTP unifié (conforme à .cursorrules: max 5s pour opérations réseau)
    inline constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
    // Timeout OTA séparé : téléchargement firmware nécessite plus de temps
    // que requêtes HTTP standard
    // Justification : connexions lentes peuvent nécessiter jusqu'à 30s
    // pour télécharger un firmware complet
    // 30s pour téléchargements OTA (justifié par taille firmware)
    inline constexpr uint32_t OTA_TIMEOUT_MS = 30000;
    inline constexpr uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 300000; // 5 min
    inline constexpr uint32_t MIN_DELAY_BETWEEN_REQUESTS_MS = 1000;
    inline constexpr uint32_t BACKOFF_BASE_MS = 1000;
    
    inline constexpr const char* HTTP_USER_AGENT = "FFP5CS-ESP32";
    inline constexpr int HTTP_OK_CODE = 200;
}

namespace ServerConfig {
    // v11.162: HTTP par défaut pour réduire fragmentation mémoire (TLS ~32KB contigu requis)
    // Le serveur iot.olution.info est contrôlé, les données capteurs ne sont pas sensibles
    inline constexpr const char* BASE_URL = "http://iot.olution.info";
    // HTTPS réservé pour OTA (sécurité critique pour mises à jour firmware)
    inline constexpr const char* BASE_URL_SECURE = "https://iot.olution.info";
    
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV) || defined(USE_TEST_ENDPOINTS)
        inline constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data-test";
        inline constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs-test/state";
        inline constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat-test";
    #else
        inline constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data";
        inline constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs/state";
        inline constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat";
    #endif
    
    inline constexpr const char* OTA_BASE_PATH = "/ffp3/ota/";
    
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
    
    // v11.162: Helper OTA avec HTTPS explicite (sécurité requise)
    inline void getOtaBaseUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s%s", BASE_URL_SECURE, OTA_BASE_PATH);
    }
}

namespace ApiConfig {
    inline constexpr const char* API_KEY = "fdGTMoptd5CD2ert3";
}

namespace EmailConfig {
    inline constexpr const char* SMTP_HOST = "smtp.gmail.com";
    inline constexpr uint16_t SMTP_PORT = 465;  // SSL direct
    inline constexpr const char* DEFAULT_RECIPIENT = "oliv.arn.lau@gmail.com";
    inline constexpr size_t MAX_EMAIL_LENGTH = 96;
}

// -----------------------------------------------------------------------------
// 4. MÉMOIRE ET BUFFERS
// -----------------------------------------------------------------------------
namespace BufferConfig {
    #if defined(BOARD_S3)
        inline constexpr uint32_t HTTP_BUFFER_SIZE = 4096;
        inline constexpr uint32_t HTTP_TX_BUFFER_SIZE = 4096;
        inline constexpr uint32_t JSON_DOCUMENT_SIZE = 4096;
        inline constexpr uint32_t POST_PAYLOAD_MAX_SIZE = 4096;
        inline constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 6000;
        inline constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 5000;
        inline constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 10000;
        inline constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 20000;
    #else
        // v11.158: Optimisation buffers - réduits pour libérer mémoire et réduire fragmentation
        inline constexpr uint32_t HTTP_BUFFER_SIZE = 1024;  // Réduit de 2048 (requêtes typiquement < 1024 bytes)
        inline constexpr uint32_t HTTP_TX_BUFFER_SIZE = 1024;  // Réduit de 2048
        // PROFILE_TEST (wroom-test): 384 pour tenir en DRAM; wroom-prod: 1024
        inline constexpr uint32_t JSON_DOCUMENT_SIZE =
            #if defined(PROFILE_TEST)
            384
            #else
            1024
            #endif
            ;  // Réponses serveur typiquement < 500 bytes
        inline constexpr uint32_t POST_PAYLOAD_MAX_SIZE = 1024;  // Limite payload postData (malloc si besoin pour tenir en DRAM)
        inline constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 2000;  // Réduit de 3000 (emails typiquement < 2000 bytes)
        inline constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 1500;  // Réduit de 2500
        inline constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 8000;
        inline constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 15000;
    #endif
}

// Seuils mémoire pour handlers HTTP (heap minimum requis)
namespace HeapConfig {
    inline constexpr uint32_t MIN_HEAP_JSON_ROUTE = 50000;      // /json endpoint
    inline constexpr uint32_t MIN_HEAP_DBVARS_ROUTE = 55000;    // /dbvars endpoint
    inline constexpr uint32_t MIN_HEAP_WIFI_ROUTE = 40000;      // /wifi/* endpoints
    inline constexpr uint32_t MIN_HEAP_EMAIL_ASYNC = 50000;     // Email async task fallback
    inline constexpr uint32_t MIN_HEAP_OTA = 50000;             // OTA check
}

// -----------------------------------------------------------------------------
// 4.1 NVS – LIMITES SÉCURITÉ TAILLES
// -----------------------------------------------------------------------------
// Ces bornes protègent contre des valeurs corrompues en NVS qui pourraient
// conduire à des malloc() ou blobs trop gros côté firmware. Elles doivent
// rester prudentes mais suffisantes pour les usages prévus.
namespace NVSConfig {
    // Taille maximale acceptée pour un réseau WiFi sauvegardé "ssid|password"
    // (côté écriture on limite déjà à ~130 octets).
    inline constexpr size_t MAX_WIFI_SAVED_ENTRY_BYTES = 256;

    // Nombre maximal de réseaux WiFi sauvegardés côté NVS pour éviter que
    // la clé "count" ne fasse grossir indéfiniment la zone.
    inline constexpr size_t MAX_WIFI_SAVED_NETWORKS = 10;

    // Taille maximale de chaînes NVS lues dans les outils d’inspection
    // (alignée sur la taille des documents JSON).
    inline constexpr size_t MAX_INSPECTED_STRING_BYTES = BufferConfig::JSON_DOCUMENT_SIZE;
}

// -----------------------------------------------------------------------------
// 5. CAPTEURS ET ACTIONNEURS
// -----------------------------------------------------------------------------
namespace SensorConfig {
    inline constexpr uint32_t SENSOR_READ_DELAY_MS = 100;
    inline constexpr uint32_t I2C_STABILIZATION_DELAY_MS = 100;

    namespace DefaultValues {
        inline constexpr float TEMP_AIR_DEFAULT = 20.0f;
        inline constexpr float HUMIDITY_DEFAULT = 50.0f;
        inline constexpr float TEMP_WATER_DEFAULT = 20.0f;
    }
    
    // Valeurs fallback pour l'API JSON (affichage quand capteur invalide)
    // Ces valeurs sont utilisées dans /json, WebSocket, etc.
    namespace Fallback {
        inline constexpr float TEMP_WATER = 25.5f;
        inline constexpr float TEMP_AIR = 22.3f;
        inline constexpr float HUMIDITY = 65.0f;
        inline constexpr float WATER_LEVEL_AQUA = 15.2f;
        inline constexpr float WATER_LEVEL_TANK = 8.7f;
        inline constexpr float WATER_LEVEL_POTA = 12.1f;
        inline constexpr uint16_t LUMINOSITY = 450;
    }

    namespace WaterTemp {
        inline constexpr float MIN_VALID = 5.0f;
        inline constexpr float MAX_VALID = 60.0f;
        inline constexpr float MAX_DELTA = 3.0f;
        inline constexpr uint8_t MIN_READINGS = 4;
        inline constexpr uint8_t TOTAL_READINGS = 7;
        inline constexpr uint16_t RETRY_DELAY_MS = 200;
        inline constexpr uint8_t MAX_RETRIES = 5;
    }

    namespace AirSensor {
        inline constexpr float TEMP_MIN = 3.0f;
        inline constexpr float TEMP_MAX = 50.0f;
        inline constexpr float HUMIDITY_MIN = 10.0f;
        inline constexpr float HUMIDITY_MAX = 100.0f;
    }

    namespace DHT {
        // Délai minimum entre lectures: 2500ms (compromis entre 2000ms datasheet et stabilité)
        // DHT11: 1s min, DHT22: 2s min (datasheet). On utilise 2.5s pour les deux.
        inline constexpr uint32_t MIN_READ_INTERVAL_MS = 2500;
        inline constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 2000;
    }

    namespace Ultrasonic {
        inline constexpr uint16_t MIN_DISTANCE_CM = 2;
        inline constexpr uint16_t MAX_DISTANCE_CM = 400;
        inline constexpr uint16_t MAX_DELTA_CM = 30;
        inline constexpr uint32_t TIMEOUT_US = 30000;
        inline constexpr uint8_t DEFAULT_SAMPLES = 5;
        inline constexpr uint32_t MIN_DELAY_MS = 60;
        inline constexpr uint16_t US_TO_CM_FACTOR = 58;
        inline constexpr uint8_t FILTERED_READINGS_COUNT = 3;
    }

    namespace History {
        inline constexpr uint8_t AQUA_HISTORY_SIZE = 16;
        inline constexpr uint8_t SENSOR_READINGS_COUNT = 3;
    }
    
    namespace DS18B20 {
        inline constexpr uint8_t RESOLUTION_BITS = 10;
        // 220ms = 187.5ms (datasheet) + 17% marge (recommandation: +10-20%)
        inline constexpr uint16_t CONVERSION_DELAY_MS = 220;
        inline constexpr uint16_t READING_INTERVAL_MS = 400;
        inline constexpr uint8_t STABILIZATION_READINGS = 1;
        inline constexpr uint16_t STABILIZATION_DELAY_MS = 50;
        inline constexpr uint16_t ONEWIRE_RESET_DELAY_MS = 100;
        // Timeout global lecture DS18B20 (ex-GlobalTimeouts::DS18B20_MAX_MS)
        inline constexpr uint32_t TIMEOUT_MS = 1000;
    }
    
    // Timeouts de test de réactivation des capteurs désactivés
    namespace Reactivation {
        // Ultrasonic: timeout court car lecture rapide
        inline constexpr uint32_t ULTRASONIC_TIMEOUT_MS = 500;
        // Temperature sensors (WaterTemp, AirSensor): timeout plus long
        inline constexpr uint32_t TEMPERATURE_TIMEOUT_MS = 1000;
    }
}

namespace ActuatorConfig {
    // Valeurs par défaut - référencent GPIODefaults (gpio_mapping.h) comme source de vérité
    namespace Default {
        inline constexpr int AQUA_LEVEL_CM = GPIODefaults::AQ_THRESHOLD_CM;
        inline constexpr int TANK_LEVEL_CM = GPIODefaults::TANK_THRESHOLD_CM;
        inline constexpr float HEATER_THRESHOLD_C = GPIODefaults::HEAT_THRESHOLD_C;
        inline constexpr uint16_t FEED_BIG_DURATION_SEC = GPIODefaults::FEED_BIG_DURATION_SEC;
        inline constexpr uint16_t FEED_SMALL_DURATION_SEC = GPIODefaults::FEED_SMALL_DURATION_SEC;
    }
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
        inline constexpr LogLevel DEFAULT_LEVEL = LOG_ERROR;
        inline constexpr bool SERIAL_ENABLED = false;
        inline constexpr bool SENSOR_LOGS_ENABLED = false;
    #else
        // Test/Dev: INFO par défaut
        inline constexpr LogLevel DEFAULT_LEVEL = LOG_INFO;
        inline constexpr bool SERIAL_ENABLED = true;
        inline constexpr bool SENSOR_LOGS_ENABLED = true;
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
    inline constexpr uint8_t OLED_WIDTH = 128;
    inline constexpr uint8_t OLED_HEIGHT = 64;
    inline constexpr uint8_t OLED_I2C_ADDR = 0x3C;
    
    inline constexpr int PERCENTAGE_MAX = 100;

    inline constexpr int OTA_OVERLAY_X_POS = 100;
    inline constexpr int OTA_OVERLAY_Y_POS = 0;
    inline constexpr int OTA_OVERLAY_WIDTH = 28;
    inline constexpr int OTA_OVERLAY_HEIGHT = 8;
    
    inline constexpr uint32_t SPLASH_DURATION_MS = 3000;  // Durée du splash screen (3 secondes)
    inline constexpr uint32_t SCREEN_SWITCH_INTERVAL_MS = 6000;
    
    inline constexpr uint8_t DISPLAY_WHITE = 1;
    inline constexpr uint8_t DISPLAY_BLACK = 0;
    inline constexpr uint8_t SSD1306_SWITCHCAPVCC = 0;
}

// -----------------------------------------------------------------------------
// 8. SOMMEIL ET ÉCONOMIE D'ÉNERGIE
// -----------------------------------------------------------------------------
namespace SleepConfig {
    // Time validation - Alias vers SystemConfig pour éviter duplication
    inline constexpr time_t EPOCH_MIN_VALID = SystemConfig::EPOCH_MIN_VALID;
    inline constexpr time_t EPOCH_MAX_VALID = SystemConfig::EPOCH_MAX_VALID;
    
    // Valeurs manquantes ajoutées
    inline constexpr int16_t TIDE_TRIGGER_THRESHOLD_CM = 10;
    inline constexpr uint32_t MIN_INACTIVITY_DELAY_SEC = 300;
    inline constexpr uint32_t MAX_INACTIVITY_DELAY_SEC = 3600;
    inline constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 300;
    inline constexpr uint32_t ERROR_INACTIVITY_DELAY_SEC = 90;
    inline constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 1800;
    inline constexpr bool ADAPTIVE_SLEEP_ENABLED = true;
    inline constexpr uint32_t FLOOD_COOLDOWN_MIN = 60;
    inline constexpr uint32_t FLOOD_DEBOUNCE_MIN = 5;
    inline constexpr uint16_t FLOOD_HYST_CM = 2;
    inline constexpr uint32_t FLOOD_RESET_STABLE_MIN = 15;
    inline constexpr bool LOCAL_SLEEP_DURATION_CONTROL = true;
    inline constexpr float NIGHT_SLEEP_MULTIPLIER = 3.0f;
    
    inline constexpr int8_t WIFI_RSSI_EXCELLENT = -55;
    inline constexpr int8_t WIFI_RSSI_GOOD = -65;
    inline constexpr int8_t WIFI_RSSI_FAIR = -75;
    inline constexpr int8_t WIFI_RSSI_POOR = -85;
    inline constexpr int8_t WIFI_RSSI_MINIMUM = -90;
    inline constexpr int8_t WIFI_RSSI_CRITICAL = -95;
    
    inline constexpr uint32_t WIFI_STABILITY_CHECK_INTERVAL_MS = 60000;
    inline constexpr uint32_t WIFI_WEAK_SIGNAL_THRESHOLD_MS = 300000;
    inline constexpr uint8_t WIFI_MAX_RECONNECT_ATTEMPTS = 5;
    inline constexpr uint32_t WIFI_RECONNECT_DELAY_MS = 5000;

    // Constantes PowerManager manquantes
    inline constexpr bool AUTO_RECONNECT_WIFI_AFTER_SLEEP = true;
    inline constexpr bool SAVE_TIME_BEFORE_SLEEP = true;
    inline constexpr time_t EPOCH_COMPILE_TIME = SystemConfig::EPOCH_MIN_VALID; // Fallback - unifié avec SystemConfig
    inline constexpr time_t EPOCH_DEFAULT_FALLBACK = SystemConfig::EPOCH_MIN_VALID; // Unifié avec SystemConfig
    inline constexpr bool ENABLE_DRIFT_CORRECTION = true;
    inline constexpr uint32_t DRIFT_CORRECTION_INTERVAL_MS = 3600000;
    inline constexpr float DRIFT_CORRECTION_THRESHOLD_PPM = 100.0f;
    inline constexpr float DRIFT_CORRECTION_FACTOR = 0.5f;
    inline constexpr uint32_t MAX_SAVE_INTERVAL_MS = 86400000;
    inline constexpr uint32_t MIN_SAVE_INTERVAL_MS = 3600000;
    inline constexpr uint32_t MIN_TIME_DIFF_FOR_SAVE_SEC = 60;
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
    
    // v11.157: Réductions basées sur HWM analysés (sensor:1864, web:5332, display:2328 libres)
    // autoTask: 7356 libres au boot mais 94.9% utilisé plus tard - NE PAS RÉDUIRE
    inline constexpr uint32_t SENSOR_TASK_STACK_SIZE = 3072;  // Réduit de 4096 (HWM: 1864 libres, marge 1208)
    inline constexpr UBaseType_t SENSOR_TASK_PRIORITY = 2;
    inline constexpr BaseType_t SENSOR_TASK_CORE_ID = 1;
    
    // v11.169: Augmenté de 4KB à 8KB - stack overflow webTask avec WebSocket (Guru Meditation)
    inline constexpr uint32_t WEB_TASK_STACK_SIZE = 8192;  // 8KB - requis pour WebSocket + AsyncWebServer
    // Baissé de 2 à 1 - le web n'est pas critique (offline-first)
    inline constexpr UBaseType_t WEB_TASK_PRIORITY = 1;
    inline constexpr BaseType_t WEB_TASK_CORE_ID = 0;
    
    // v11.157: Augmenté de 6KB à 8KB pour éviter stack overflow (HWM: 100 bytes libres = critique)
    // Le crash se produit dans automationTask lors de la sauvegarde NVS
    inline constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 8192;  // 8KB
    inline constexpr UBaseType_t AUTOMATION_TASK_PRIORITY = 3;
    inline constexpr BaseType_t AUTOMATION_TASK_CORE_ID = 1;
    
    inline constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 3072;  // Réduit de 4096 (HWM: 2328 libres, marge 744)
    inline constexpr UBaseType_t DISPLAY_TASK_PRIORITY = 1;
    inline constexpr BaseType_t DISPLAY_TASK_CORE_ID = 1;

    inline constexpr uint32_t OTA_TASK_STACK_SIZE = 8192;
    
    // Tâche réseau (TLS/HTTP) - propriétaire unique de WebClient/TLS
    // v11.159: Réduit de 10KB à 8KB (Phase 3 - HWM: 5584 libres, marge 4656)
    inline constexpr uint32_t NET_TASK_STACK_SIZE = 8192;   // 8 KB (requis pour TLS/HTTPS avec mbedTLS)
    inline constexpr UBaseType_t NET_TASK_PRIORITY = 2;      // Priorité moyenne pour traitement réseau
    inline constexpr BaseType_t NET_TASK_CORE_ID = 0;        // Core 0 pour ne pas impacter capteurs
    
    // Tâche mail asynchrone (v11.143) - évite de bloquer automationTask pendant SMTP
    // v11.161: Augmenté de 12KB à 16KB - stack overflow persistant pendant TLS/SMTP handshake
    inline constexpr uint32_t MAIL_TASK_STACK_SIZE = 16384;  // 16 KB (ESP Mail Client + mbedTLS nécessitent beaucoup de stack)
    inline constexpr UBaseType_t MAIL_TASK_PRIORITY = 1;     // Basse priorité (non critique)
    inline constexpr BaseType_t MAIL_TASK_CORE_ID = 0;       // Core 0 pour ne pas impacter capteurs
    inline constexpr uint8_t MAIL_QUEUE_SIZE = 6;            // v11.163: 6 slots (~4KB) - marge supplémentaire avec délai démarrage
}

// Note: namespace DefaultValues supprimé (v11.174 simplification)
// - WATER_TEMP était un doublon de SensorConfig::DefaultValues::TEMP_WATER_DEFAULT

// -----------------------------------------------------------------------------
// 9. DÉSACTIVATION SÛRE DE SERIAL EN PROD
// -----------------------------------------------------------------------------
// Quand ENABLE_SERIAL_MONITOR=0 (ou profil PROD sans override), on redirige Serial
// vers un stub inline constexpr pour éliminer à la compilation les appels Serial.* et
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
    static inline constexpr NullSerialType NullSerial{};
}  // namespace LogConfig

#define Serial LogConfig::NullSerial
#define Serial1 LogConfig::NullSerial
#define Serial2 LogConfig::NullSerial
#endif
