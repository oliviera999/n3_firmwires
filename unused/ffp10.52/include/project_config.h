#pragma once

// =============================================================================
// CONFIGURATION CENTRALE DU PROJET FFP3CS4
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
    constexpr const char* VERSION = "10.52";
    
    // Identification matérielle
    #if defined(BOARD_S3)
        constexpr const char* BOARD_TYPE = "esp32-s3";
    #else
        constexpr const char* BOARD_TYPE = "esp32-wroom";
    #endif
}

// =============================================================================
// CONFIGURATION SERVEUR
// =============================================================================
namespace ServerConfig {
    constexpr const char* BASE_URL = "http://iot.olution.info";
    // Option: serveur secondaire (auto-hébergé/local). Laisser vide pour désactiver.
    // Exemple: "http://192.168.1.50" ou "http://nas.local:8080"
    #ifndef SECONDARY_BASE_URL
    #define SECONDARY_BASE_URL ""
    #endif
    
    // Endpoints selon le profil
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
        // Environnement de test (endpoints avec "2")
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/ffp3datas/post-ffp3-data2.php";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/ffp3control/ffp3-outputs-action2.php?action=outputs_state&board=1";
    #else
        // Production (endpoints sans "2")
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/ffp3datas/post-ffp3-data.php";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/ffp3control/ffp3-outputs-action.php?action=outputs_state&board=1";
    #endif
    
    // URLs complètes
    inline String getPostDataUrl() {
        return String(BASE_URL) + POST_DATA_ENDPOINT;
    }
    
    inline String getOutputUrl() {
        return String(BASE_URL) + OUTPUT_ENDPOINT;
    }

    // URLs complètes (serveur secondaire) si configuré
    inline String getSecondaryPostDataUrl() {
        const char* b = SECONDARY_BASE_URL;
        if (b && b[0]) return String(b) + POST_DATA_ENDPOINT;
        return String();
    }
    
    inline String getSecondaryOutputUrl() {
        const char* b = SECONDARY_BASE_URL;
        if (b && b[0]) return String(b) + OUTPUT_ENDPOINT;
        return String();
    }
    
    // Autres endpoints
    constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/ffp3datas/heartbeat.php";
    constexpr const char* OTA_BASE_PATH = "/ffp3/ota/";
    
    // Timeouts optimisés pour réactivité
    constexpr uint32_t CONNECTION_TIMEOUT_MS = 5000;      // Réduit de 10s à 5s
    constexpr uint32_t REQUEST_TIMEOUT_MS = 15000;        // Réduit de 30s à 15s

    inline String getHeartbeatUrl() { return String(BASE_URL) + HEARTBEAT_ENDPOINT; }
    inline String getSecondaryHeartbeatUrl() {
        const char* b = SECONDARY_BASE_URL;
        if (b && b[0]) return String(b) + HEARTBEAT_ENDPOINT;
        return String();
    }
    
    inline String getServerUrl() {
        return BASE_URL;
    }
    
    inline String getWebSocketEndpoint() {
        return String(BASE_URL) + "/ws";
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
    
    // Configuration par profil
    #if defined(PROFILE_PROD)
        constexpr LogLevel DEFAULT_LEVEL = LOG_ERROR;
        constexpr bool SERIAL_ENABLED = false;
    #elif defined(PROFILE_TEST)
        constexpr LogLevel DEFAULT_LEVEL = LOG_INFO;
        constexpr bool SERIAL_ENABLED = true;
    #else // PROFILE_DEV ou default
        constexpr LogLevel DEFAULT_LEVEL = LOG_DEBUG;
        constexpr bool SERIAL_ENABLED = true;
    #endif
    
    // Macros de log unifiées
    #define LOG_ERROR(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_ERROR && LogConfig::SERIAL_ENABLED) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_WARN && LogConfig::SERIAL_ENABLED) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_INFO && LogConfig::SERIAL_ENABLED) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_DEBUG && LogConfig::SERIAL_ENABLED) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
    #define LOG_VERBOSE(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_VERBOSE && LogConfig::SERIAL_ENABLED) Serial.printf("[VERB] " fmt "\n", ##__VA_ARGS__)
}

// =============================================================================
// CONFIGURATION CAPTEURS ET VALIDATION
// =============================================================================
namespace SensorConfig {
    // Validation température eau (DS18B20)
    namespace WaterTemp {
        constexpr float MIN_VALID = 5.0f;   // Minimum réaliste pour aquarium
        constexpr float MAX_VALID = 60.0f;  // Maximum réaliste pour aquarium
        constexpr float MAX_DELTA = 3.0f;   // Écart max entre mesures
        constexpr uint8_t MIN_READINGS = 4; // Lectures minimales pour médiane
        constexpr uint8_t TOTAL_READINGS = 7; // Total de lectures
        constexpr uint16_t RETRY_DELAY_MS = 200;
        constexpr uint8_t MAX_RETRIES = 5;
    }
    
    // Validation température/humidité air (DHT)
    namespace AirSensor {
        #if defined(ENVIRONMENT_PROD_TEST) || (defined(BOARD_WROOM) && !defined(DHT22_SENSOR))
            // ESP32-WROOM test (ENVIRONMENT_PROD_TEST) ou WROOM par défaut utilise DHT11 (plages limitées)
            constexpr float TEMP_MIN = 5.0f;
            constexpr float TEMP_MAX = 50.0f;
            constexpr float HUMIDITY_MIN = 20.0f;
            constexpr float HUMIDITY_MAX = 95.0f;
        #else
            // ESP32-S3 ou DHT22 explicite (plages étendues)
            constexpr float TEMP_MIN = -40.0f;
            constexpr float TEMP_MAX = 80.0f;
            constexpr float HUMIDITY_MIN = 0.0f;
            constexpr float HUMIDITY_MAX = 100.0f;
        #endif
    }
    
    // Validation capteurs ultrasoniques
    namespace Ultrasonic {
        constexpr uint16_t MIN_DISTANCE_CM = 2;
        constexpr uint16_t MAX_DISTANCE_CM = 400;
        constexpr uint16_t MAX_DELTA_CM = 30;
        constexpr uint32_t TIMEOUT_US = 30000; // 30ms
        constexpr uint8_t DEFAULT_SAMPLES = 5;
    }
    
    // Validation autres capteurs
    namespace Analog {
        constexpr uint16_t ADC_MAX_VALUE = 4095; // 12-bit ADC
        // Voltage supprimé - pas de mesure de tension
        // constexpr uint16_t VOLTAGE_MAX_MV = 3300;
    }
}

// =============================================================================
// CONFIGURATION TEMPORELLE
// =============================================================================
namespace TimingConfig {
    // Intervalles de base optimisés
    constexpr uint32_t SENSOR_READ_INTERVAL_MS = 1500;  // Réduit de 2s à 1.5s
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
    constexpr uint32_t MIN_AWAKE_TIME_MS = 150000;          // 2.5 minutes minimum éveillé (augmenté de 1 à 2.5 min)
    
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
    constexpr uint32_t MIN_INACTIVITY_DELAY_SEC = 180;             // 3 minutes minimum d'inactivité (réduit de 5 à 3 min)
    
    // Délai d'inactivité maximum (en secondes)
    // Limite supérieure pour éviter des délais trop longs avant sommeil
    constexpr uint32_t MAX_INACTIVITY_DELAY_SEC = 3600;            // 1 heure maximum d'inactivité
    
    // Délai d'inactivité normal (en secondes)
    // Utilisé en fonctionnement standard - réaugmenté à 5 min pour stabilité
    constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 300;          // 5 minutes normal d'inactivité (réaugmenté de 2.5 à 5 min)
    
    // Délai d'inactivité en cas d'erreurs (en secondes)
    // Réduit pour surveillance accrue quand des problèmes sont détectés
    constexpr uint32_t ERROR_INACTIVITY_DELAY_SEC = 90;             // 1.5 minutes si erreurs (réduit de 3 à 1.5 min)
    
    // Délai d'inactivité nocturne (en secondes)
    // Réduit la nuit pour dormir plus rapidement (22h-6h)
    // Calcul: NORMAL_INACTIVITY_DELAY_SEC ÷ 2 = 300 ÷ 2 = 150s (2.5 min)
    constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 150;          // 2.5 minutes la nuit (réduit de 5 à 2.5 min)
    
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
    constexpr const char* HOSTNAME_PREFIX = "ffp3";
    constexpr size_t HOSTNAME_BUFFER_SIZE = 20;
}

// =============================================================================
// CONFIGURATION TÂCHES FREERTOS - STRATÉGIE WEB DÉDIÉ
// =============================================================================
namespace TaskConfig {
    // Tailles de stack
    constexpr uint32_t SENSOR_TASK_STACK_SIZE = 8192;
    constexpr uint32_t WEB_TASK_STACK_SIZE = 8192;           // Nouvelle tâche web dédiée
    constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 12288;
    constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 8192;
    constexpr uint32_t OTA_TASK_STACK_SIZE = 12288;
    
    // Priorités des tâches - Stratégie Web Dédié Optimisée
    constexpr uint8_t SENSOR_TASK_PRIORITY = 5;             // CRITIQUE - Capteurs prioritaires absolus
    constexpr uint8_t WEB_TASK_PRIORITY = 4;                 // TRÈS HAUTE - Interface web ultra-réactive
    constexpr uint8_t AUTOMATION_TASK_PRIORITY = 2;          // MOYENNE - Logique métier pure
    constexpr uint8_t DISPLAY_TASK_PRIORITY = 1;             // BASSE - Affichage en arrière-plan
    
    // Core d'exécution
    constexpr uint8_t TASK_CORE_ID = 1;
    
    // Configuration Web Server optimisée
    constexpr uint8_t WEB_SERVER_CORE_ID = 0;              // Serveur web sur Core 0 (CPU principal)
    constexpr uint8_t WEB_SERVER_PRIORITY = 4;             // Priorité élevée pour le serveur web
    constexpr uint32_t WEB_SERVER_STACK_SIZE = 12288;      // Stack plus important pour le serveur web
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
namespace ExtendedSensorConfig {
    // Facteurs de conversion
    constexpr uint16_t ULTRASONIC_US_TO_CM_FACTOR = 58;  // µs vers cm
    
    // Délais entre lectures
    constexpr uint32_t DHT_MIN_READ_INTERVAL_MS = 2000;
    constexpr uint32_t DHT_INIT_STABILIZATION_DELAY_MS = 1000; // Délai de stabilisation DHT
    constexpr uint32_t SENSOR_READ_DELAY_MS = 100;
    constexpr uint32_t I2C_STABILIZATION_DELAY_MS = 100;
    
    // Valeurs par défaut en cas d'erreur
    namespace DefaultValues {
        constexpr float TEMP_AIR_DEFAULT = 20.0f;
        constexpr float HUMIDITY_DEFAULT = 50.0f;
        constexpr float TEMP_WATER_DEFAULT = 20.0f;
    }
    
    // Plages de validation
    namespace ValidationRanges {
        constexpr float TEMP_MIN = -50.0f;
        constexpr float TEMP_MAX = 100.0f;
        constexpr float HUMIDITY_MIN = 0.0f;
        constexpr float HUMIDITY_MAX = 100.0f;
    }
}

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
}

// =============================================================================
// FEATURES FLAGS
// =============================================================================
namespace Features {
    // Fonctionnalités activées/désactivées selon le profil
    #if defined(PROFILE_PROD)
        constexpr bool OLED_ENABLED = true;
        constexpr bool OTA_ENABLED = true;
        constexpr bool MAIL_ENABLED = true;
        constexpr bool WEB_SERVER_ENABLED = true;
        constexpr bool ARDUINO_OTA_ENABLED = false; // Désactivé en prod pour sécurité
    #else
        constexpr bool OLED_ENABLED = true;
        constexpr bool OTA_ENABLED = true;
        constexpr bool MAIL_ENABLED = true;
        constexpr bool WEB_SERVER_ENABLED = true;
        constexpr bool ARDUINO_OTA_ENABLED = true;
    #endif
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
    inline String getSystemInfo() {
        String info = "FFP3CS4 v";
        info += ProjectConfig::VERSION;
        info += " [";
        info += ProjectConfig::BOARD_TYPE;
        info += "/";
        info += getProfileName();
        info += "]";
        return info;
    }
}

// =============================================================================
// COMPATIBILITÉ ET ALIAS (pour migration depuis config.h)
// =============================================================================

// Macros de préprocesseur pour les features (nécessaires pour #if)
#if defined(PROFILE_PROD)
    #define FEATURE_OLED 1
    #define FEATURE_OTA 1
    #ifndef FEATURE_MAIL
        #define FEATURE_MAIL 1
    #endif
    #define FEATURE_ARDUINO_OTA 0  // Désactivé en prod pour sécurité
    #define FEATURE_HTTP_OTA 1
    #define FEATURE_WIFI_APSTA 1
#else
    #define FEATURE_OLED 1
    #define FEATURE_OTA 1
    #ifndef FEATURE_MAIL
        #define FEATURE_MAIL 1
    #endif
    #ifndef FEATURE_ARDUINO_OTA
        #define FEATURE_ARDUINO_OTA 1
    #endif
    #define FEATURE_HTTP_OTA 1
    #define FEATURE_WIFI_APSTA 1
#endif

namespace CompatibilityAliases {
    // Features flags (ancien style) - maintenant synchronisées avec les macros
    constexpr bool FEATURE_OLED_BOOL = Features::OLED_ENABLED;
    constexpr bool FEATURE_OTA_BOOL = Features::OTA_ENABLED;
    constexpr bool FEATURE_MAIL_BOOL = Features::MAIL_ENABLED;
    constexpr bool FEATURE_ARDUINO_OTA_BOOL = Features::ARDUINO_OTA_ENABLED;
    constexpr bool FEATURE_HTTP_OTA_BOOL = Features::OTA_ENABLED;
    constexpr bool FEATURE_WIFI_APSTA_BOOL = true; // Toujours activé
    
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
// RÉTRO-COMPATIBILITÉ (temporaire, à supprimer progressivement)
// =============================================================================
// Ces alias permettent de ne pas casser le code existant
namespace Config {
    using namespace ProjectConfig;
    constexpr const char* VERSION = ProjectConfig::VERSION;
    constexpr const char* SENSOR = ProjectConfig::BOARD_TYPE;
    constexpr const char* SERVER_POST_DATA = ServerConfig::POST_DATA_ENDPOINT;
    constexpr const char* SERVER_OUTPUT = ServerConfig::OUTPUT_ENDPOINT;
    constexpr const char* API_KEY = ApiConfig::API_KEY;
    constexpr const char* SMTP_HOST = EmailConfig::SMTP_HOST;
    constexpr uint16_t SMTP_PORT_SSL = EmailConfig::SMTP_PORT;
    constexpr const char* DEFAULT_MAIL_TO = EmailConfig::DEFAULT_RECIPIENT;
    
    namespace Default {
        using namespace ActuatorConfig::Default;
        constexpr int AQ_LIMIT_CM = AQUA_LEVEL_CM;
        constexpr int TANK_LIMIT_CM = TANK_LEVEL_CM;
        constexpr int HEATER_THRESHOLD = static_cast<int>(HEATER_THRESHOLD_C);
    }
}

// Fin du namespace Config de rétro-compatibilité

// =============================================================================
// CONFIGURATION DÉLAIS ET TIMING ÉTENDUE
// =============================================================================
namespace TimingConfigExtended {
    // ========================================
    // DÉLAIS CAPTEURS ET MESURES
    // ========================================
    
    // Délais ultrasoniques (microsecondes)
    constexpr uint32_t ULTRASONIC_PRE_TRIGGER_DELAY_US = 2;     // Délai avant déclenchement pulse ultrasonique
    constexpr uint32_t ULTRASONIC_PULSE_DURATION_US = 10;       // Durée du pulse ultrasonique (µs)
    
    // Délais entre lectures capteurs (millisecondes)
    constexpr uint32_t SENSOR_READ_DELAY_MS = 50;              // Délai entre lectures capteurs individuels
    constexpr uint32_t SENSOR_RESET_DELAY_MS = 500;            // Délai après reset d'un capteur
    constexpr uint32_t SENSOR_STABILIZATION_DELAY_MS = 1;      // Délai entre échantillons pour stabilisation
    
    // ========================================
    // DÉLAIS SYSTÈME ET INITIALISATION
    // ========================================
    
    // Délais d'initialisation
    constexpr uint32_t SYSTEM_INIT_SAFETY_DELAY_MS = 3000;     // Délai de sécurité après initialisation système
    constexpr uint32_t TASK_START_LATENCY_MS = 50;             // Latence pour laisser démarrer les tâches
    constexpr uint32_t CPU_LOAD_REDUCTION_DELAY_MS = 500;     // Délai pour réduire la charge CPU
    
    // Délais WiFi et réseau
    constexpr uint32_t WIFI_INIT_DELAY_MS = 50;                // Délai initialisation WiFi
    constexpr uint32_t WIFI_RECONNECT_DELAY_MS = 100;         // Délai optimisé pour reconnexion WiFi
    constexpr uint32_t WIFI_CONNECTION_TIMEOUT_MS = 10000;    // Timeout connexion WiFi (10 secondes)
    
    // ========================================
    // DÉLAIS ACTIONNEURS ET SERVOS
    // ========================================
    
    // Délais servo et actionneurs
    constexpr uint32_t SERVO_ACTION_DELAY_MS = 200;           // Délai entre actions servo
    constexpr uint32_t ACTUATOR_SHORT_DELAY_MS = 50;          // Délai court entre actions actionneurs
    constexpr uint32_t ACTUATOR_MEDIUM_DELAY_MS = 100;        // Délai moyen entre actions actionneurs
    constexpr uint32_t ACTUATOR_LONG_DELAY_MS = 150;         // Délai long entre actions actionneurs
    
    // ========================================
    // DÉLAIS OTA ET MISE À JOUR
    // ========================================
    
    // Délais OTA spécifiques
    constexpr uint32_t OTA_MICRO_DELAY_MS = 1;                // Délai micro entre opérations OTA
    constexpr uint32_t OTA_FAILURE_DELAY_MS = 3000;          // Délai après échec OTA
    constexpr uint32_t OTA_RETRY_PAUSE_MS = 500;             // Pause entre tentatives OTA
    constexpr uint32_t OTA_SHORT_PAUSE_MS = 5;                // Pause courte entre opérations OTA
    constexpr uint32_t OTA_CHUNK_PAUSE_MS = 10;              // Pause entre chunks OTA
    constexpr uint32_t OTA_SPECIFIC_DELAY_MS = 100;          // Délai spécifique OTA
    
    // ========================================
    // DÉLAIS MONITORING ET DRIFT
    // ========================================
    
    // Délais monitoring temps
    constexpr uint32_t TIME_DRIFT_RETRY_DELAY_MS = 1000;      // Délai entre tentatives correction drift
    constexpr uint32_t DEBUG_LOG_INTERVAL_MS = 10000;         // Intervalle log debug (10 secondes)
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

// =============================================================================
// CONFIGURATION AFFICHAGE ÉTENDUE
// =============================================================================
namespace DisplayConfigExtended {
    // ========================================
    // POSITIONS ET DIMENSIONS BARRES DE PROGRESSION
    // ========================================
    
    // Barre de progression principale
    constexpr int PROGRESS_BAR_X = 4;                        // Position X barre de progression
    constexpr int PROGRESS_BAR_Y = 48;                       // Position Y barre de progression principale
    constexpr int PROGRESS_BAR_WIDTH = 120;                 // Largeur barre de progression (px)
    constexpr int PROGRESS_BAR_HEIGHT = 10;                  // Hauteur barre de progression (px)
    
    // Barre de progression alternative
    constexpr int PROGRESS_BAR_ALT_Y = 40;                   // Position Y barre de progression alternative
    
    // ========================================
    // INTERVALLES D'AFFICHAGE
    // ========================================
    
    // Intervalles changement écran
    constexpr uint32_t SCREEN_SWITCH_INTERVAL_MS = 4000;    // Intervalle changement écran (4s pour plus de dynamisme)
    
    // ========================================
    // CONFIGURATION OLED ÉTENDUE
    // ========================================
    
    // Couleurs et constantes d'affichage
    constexpr uint8_t DISPLAY_WHITE = 1;                     // Valeur couleur blanche OLED
    constexpr uint8_t DISPLAY_BLACK = 0;                     // Valeur couleur noire OLED
    constexpr uint8_t SSD1306_SWITCHCAPVCC = 0;              // Mode d'alimentation SSD1306
}

// =============================================================================
// CONFIGURATION VALIDATION ET SEUILS
// =============================================================================
namespace ValidationConfig {
    // ========================================
    // SEUILS TEMPÉRATURE ET HUMIDITÉ
    // ========================================
    
    // Validation température eau
    constexpr float WATER_TEMP_MAX_DELTA_C = 5.0f;          // Écart max accepté entre mesures température eau (°C)
    
    // Validation humidité
    constexpr float HUMIDITY_MIN_PERCENT = 0.0f;             // Humidité minimum valide (%)
    constexpr float HUMIDITY_MAX_PERCENT = 100.0f;           // Humidité maximum valide (%)
    
    // ========================================
    // VALEURS PAR DÉFAUT EN CAS D'ERREUR
    // ========================================
    
    // Valeurs par défaut capteurs
    constexpr float DEFAULT_WATER_LEVEL_AQUA = 0.0f;        // Valeur par défaut niveau aquarium
    constexpr float DEFAULT_WATER_LEVEL_TANK = 0.0f;         // Valeur par défaut niveau réservoir
    constexpr float DEFAULT_WATER_LEVEL_POTA = 0.0f;         // Valeur par défaut niveau potager
    constexpr float DEFAULT_LUMINOSITY = 0.0f;               // Valeur par défaut luminosité
    
    // ========================================
    // LIMITES ET SEUILS SYSTÈME
    // ========================================
    
    // Limites compteurs et timers
    constexpr uint32_t MAX_SECONDS_COUNTER = 65535u;         // Limite supérieure compteur secondes (16-bit)
    
    // Seuils de fonctionnement
    constexpr uint32_t IMMEDIATE_MODE_PERCENTAGE = 25;       // Pourcentage de temps en mode immédiat (25%)
    constexpr uint32_t MAIL_BLINK_FREQUENCY_MS = 200;       // Fréquence clignotement mail (200ms)
    
    // ========================================
    // SEUILS TEMPORELS
    // ========================================
    
    // Seuils de drift temps
    constexpr int32_t TIME_DRIFT_THRESHOLD_SEC = 3600;      // Seuil drift temps (1 heure en secondes)
    constexpr uint32_t MIN_TIME_SAVE_INTERVAL_MULTIPLIER = 3; // Multiplicateur intervalle sauvegarde temps
}

// =============================================================================
// CONFIGURATION CONVERSION ET CALCULS
// =============================================================================
namespace ConversionConfig {
    // ========================================
    // CONVERSIONS TEMPORELLES
    // ========================================
    
    // Conversion secondes vers heures
    constexpr uint32_t SECONDS_TO_HOURS_FACTOR = 3600;      // Facteur conversion secondes vers heures
    
    // Conversion millisecondes vers secondes
    constexpr uint32_t MILLISECONDS_TO_SECONDS_FACTOR = 1000; // Facteur conversion ms vers secondes
    
    // ========================================
    // CONVERSIONS ULTRASONIQUES
    // ========================================
    
    // Calcul délai entre pings ultrasoniques
    constexpr uint32_t ULTRASONIC_PING_DELAY_MULTIPLIER = 20; // Multiplicateur pour délai entre pings ultrasoniques
    
    // ========================================
    // CONVERSIONS AFFICHAGE
    // ========================================
    
    // Conversion pour affichage pourcentage
    constexpr uint32_t PERCENTAGE_DISPLAY_DIVISOR = 100;     // Diviseur pour affichage pourcentage
}

// =============================================================================
// CONFIGURATION CAPTEURS SPÉCIFIQUES
// =============================================================================
namespace SensorSpecificConfig {
    // ========================================
    // CONFIGURATION DS18B20
    // ========================================
    
    // Paramètres DS18B20
    constexpr uint32_t DS18B20_STABILIZATION_DELAY_MS = 750; // Délai de stabilisation DS18B20
    constexpr uint8_t DS18B20_RESOLUTION_BITS = 10;          // Résolution DS18B20 (10 bits)
    constexpr uint16_t DS18B20_CONVERSION_DELAY_MS = 200;     // Délai conversion température DS18B20
    constexpr uint16_t DS18B20_READING_INTERVAL_MS = 300;    // Intervalle lectures DS18B20
    constexpr uint8_t DS18B20_STABILIZATION_READINGS = 1;    // Lectures stabilisation DS18B20
    constexpr uint16_t ONEWIRE_RESET_DELAY_MS = 100;         // Délai reset bus OneWire
    
    // ========================================
    // CONFIGURATION CAPTEURS AIR (DHT)
    // ========================================
    
    // Seuils validation DHT
    constexpr float DHT_TEMP_MAX_DELTA_C = 3.0f;             // Écart max accepté entre mesures température air (°C)
    constexpr float DHT_HUMIDITY_MAX_DELTA_PERCENT = 10.0f;  // Écart max accepté entre mesures humidité (%)
    
    // ========================================
    // CONFIGURATION ULTRASONIQUES
    // ========================================
    
    // Paramètres lectures ultrasoniques
    constexpr uint8_t ULTRASONIC_FILTERED_READINGS_COUNT = 3; // Nombre de lectures pour filtrage ultrasonique
    
    // ========================================
    // CONFIGURATION SYSTÈME CAPTEURS
    // ========================================
    
    // Historique et buffers
    constexpr uint8_t AQUA_HISTORY_SIZE = 16;                // Taille historique mesures aquarium
    constexpr uint8_t SENSOR_READINGS_COUNT = 3;             // Nombre de lectures pour validation capteurs
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

// =============================================================================
// CONFIGURATION RÉSEAU ÉTENDUE
// =============================================================================
namespace NetworkConfigExtended {
    // ========================================
    // CONFIGURATION SERVEURS LOCAUX
    // ========================================
    
    // Serveur OTA local
    constexpr const char* LOCAL_OTA_SERVER_URL = "http://192.168.1.100:8080/ota/"; // URL serveur OTA local
    
    // ========================================
    // CONFIGURATION FIRMWARE
    // ========================================
    
    // Tailles firmware
    constexpr size_t ESP32_S3_MAX_FIRMWARE_SIZE = 16777216;  // Taille max firmware ESP32-S3 (16MB)
    constexpr size_t ESP32_WROOM_MAX_FIRMWARE_SIZE = 16777216; // Taille max firmware ESP32-WROOM (16MB)
    
    // ========================================
    // CONFIGURATION HTTP ÉTENDUE
    // ========================================
    
    // Timeouts HTTP spécifiques
    constexpr uint32_t OTA_HTTP_TIMEOUT_MS = 30000;          // Timeout HTTP pour OTA (30 secondes)
}
