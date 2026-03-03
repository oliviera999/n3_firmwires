#pragma once

#include <Arduino.h>
#include <cmath>  // Pour isnan() dans SensorValidation
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
    // v12.10: RTC DS3231 optionnel (option A), run propre
    inline constexpr const char* VERSION = "12.15";
    
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
            #if defined(BOARD_S3)
                return "S3-TEST";
            #else
                return "TEST";
            #endif
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
    
    // v11.178: Helpers utilitaires pour réduire duplication de code (audit helpers-utils)
    
    // Copie sûre de chaîne avec null-termination garantie
    // Remplace le pattern: strncpy(dest, src, sizeof(dest)-1); dest[sizeof(dest)-1] = '\0';
    inline void safeStrncpy(char* dest, const char* src, size_t destSize) {
        if (destSize == 0) return;
        strncpy(dest, src, destSize - 1);
        dest[destSize - 1] = '\0';
    }
    
    // Formatage d'adresse IP dans un buffer
    // Remplace le pattern répété: snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3])
    inline void formatIP(const uint8_t* ip, char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
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

    // Time validation - À réviser en release majeure (rejet vieux NVS / validation epoch).
    // EPOCH_MIN_VALID doit être proche de la date actuelle pour invalider les vieux epochs en NVS.
    inline constexpr time_t EPOCH_MIN_VALID = 1767225600; // 2026-01-01 00:00:00 UTC
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

// Optionnel : DNS personnalisé (réservé). Arduino-ESP32 n'expose pas setDNS() ; en pratique
// configurer le DNS sur le routeur (DHCP option 6) ou accepter la latence. Définir à 1 pour
// activer un éventuel futur code (ex. config IP statique + DNS).
#ifndef WIFI_USE_CUSTOM_DNS
#define WIFI_USE_CUSTOM_DNS 0
#endif

namespace TimingConfig {
    // WiFi - 5 s pour timeouts génériques (HTTP, etc.)
    inline constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 5000;
    // 8 s par tentative d'association WiFi (routeurs lents, DHCP, signaux faibles)
    // S3 PSRAM test: 4 s pour limiter blocage boot (splash) quand WiFi absent/faible
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    inline constexpr uint32_t WIFI_CONNECT_ATTEMPT_TIMEOUT_MS = 4000;
#else
    inline constexpr uint32_t WIFI_CONNECT_ATTEMPT_TIMEOUT_MS = 8000;
#endif
    // v11.168: Timeout boot plus long pour laisser le temps de récupérer config serveur
    // Au boot uniquement, on peut attendre un peu plus car c'est le seul moment
    // où on peut récupérer la config distante de manière fiable
    inline constexpr uint32_t WIFI_BOOT_TIMEOUT_MS = 8000;
    inline constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
    inline constexpr uint32_t WIFI_WATCHDOG_TIMEOUT_MS = 30000;
    // Délai après disconnect avant scan (stabilisation chip WiFi)
    inline constexpr uint32_t WIFI_POST_DISCONNECT_DELAY_MS = 500;
    // Délai avant premier scan au boot (stabilisation RF)
    inline constexpr uint32_t WIFI_PRE_SCAN_DELAY_MS = 300;
    // Délai entre tentatives sur réseaux différents (évite états intermédiaires)
    inline constexpr uint32_t WIFI_DELAY_BETWEEN_NETWORKS_MS = 250;
    // Délai avant 4e tentative par réseau visible (routeur instable)
    inline constexpr uint32_t WIFI_FOURTH_ATTEMPT_DELAY_MS = 1000;
    
    // Serveur
    inline constexpr uint32_t SERVER_SYNC_INTERVAL_MS = 60000;
    inline constexpr uint32_t SERVER_RETRY_INTERVAL_MS = 5000;
    
    // Tâches périodiques
    inline constexpr uint32_t OTA_CHECK_INTERVAL_MS = 7200000; // 2h
    inline constexpr uint32_t OTA_PROGRESS_UPDATE_INTERVAL_MS = 1000; // 1s
    inline constexpr uint32_t DIGEST_INTERVAL_MS = 3600000;    // 1h
    inline constexpr uint32_t NTP_SYNC_INTERVAL_MS = 3600000;  // 1h - sync NTP périodique (PowerManager)
    inline constexpr uint32_t STATS_REPORT_INTERVAL_MS = 300000; // 5 min
    
    // Protection et Timeouts
    inline constexpr uint32_t WAKEUP_PROTECTION_DURATION_MS = 30000;
    inline constexpr uint32_t WEB_ACTIVITY_TIMEOUT_MS = 60000;
    
    // Intervalle tâche capteurs: >= DHT MIN_READ_INTERVAL_MS (datasheet 2s, config 2.5s). 10s adapté aquaponie.
    inline constexpr uint32_t SENSOR_TASK_INTERVAL_MS = 10000;
    // Timeout attente queue capteurs dans automationTask — doit être >= SENSOR_TASK_INTERVAL_MS pour
    // recevoir au moins une lecture par cycle (sinon timeouts et fallback fréquents alors que capteurs OK).
    inline constexpr uint32_t AUTOMATION_QUEUE_RECEIVE_TIMEOUT_MS = 12000;
    // Timeout max pour lecture capteurs (protection watchdog)
    inline constexpr uint32_t MAX_SENSOR_TIME_MS = 30000;

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

#ifndef FEATURE_DIAG_OLED_LOGS
  #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    #define FEATURE_DIAG_OLED_LOGS 1  // Logs debug OLED (hypothesisId, throttle, etc.)
  #else
    #define FEATURE_DIAG_OLED_LOGS 0  // Désactivé en production
  #endif
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

// Reboot automatique quotidien : une fois par jour entre 0h et 4h (heure locale). Mettre à 0 pour désactiver.
#ifndef FEATURE_DAILY_REBOOT
#define FEATURE_DAILY_REBOOT 1
#endif
#if FEATURE_DAILY_REBOOT
namespace DailyRebootConfig {
    inline constexpr int HOUR_START = 0;   // Minuit
    inline constexpr int HOUR_END = 4;     // Exclus (fenêtre 0h–3h59)
    inline constexpr unsigned long CHECK_INTERVAL_MS = 60000;  // Vérification au plus une fois par minute
}
#endif

// -----------------------------------------------------------------------------
// 3. RÉSEAU ET SERVEUR
// -----------------------------------------------------------------------------
namespace NetworkConfig {
    inline constexpr uint16_t WEB_SERVER_PORT = 80;
    inline constexpr uint16_t WS_PORT = 81;  // v11.178: Port WebSocket centralisé (audit)
    inline constexpr uint32_t WEB_SERVER_TIMEOUT_MS = 2000;
    // Réduit de 4 à 2 pour limiter pics heap (AsyncResponseStream/cbuf par connexion → moins de panic).
    inline constexpr uint8_t WEB_SERVER_MAX_CONNECTIONS = 2;
    // Timeout HTTP unifié (v11.190: 5s, règle projet "timeouts réseau courts ≤ 5s")
    inline constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
    // GET outputs/state : timeout plus long (net task uniquement). 8 s pour rester < FETCH_REMOTE_STATE_RPC_TIMEOUT_MS.
    inline constexpr uint32_t OUTPUTS_STATE_HTTP_TIMEOUT_MS = 8000;  // 8 s (évite abandon caller avant fin GET)
    // RPC FetchRemoteState : timeout = HTTP + marge queue (POST ~8s peut précéder le GET)
    inline constexpr uint32_t FETCH_REMOTE_STATE_RPC_TIMEOUT_MS = 30000;  // 30 s (évite abandon avant fin GET si file chargée)
    // Intervalle min entre deux GET en branche timeout (fallback sans capteurs) — évite saturation netTask
    inline constexpr uint32_t REMOTE_FETCH_FALLBACK_INTERVAL_MS = 6000;   // 6 s (aligné poll data branch)
    // POST post-data / ack : dérogation (latence serveur). Observé jusqu'à ~15,5 s (4G, iot.olution.info) ; 18 s marge.
    inline constexpr uint32_t HTTP_POST_TIMEOUT_MS = 18000;  // 18 s (session 2026-02-14 : max 15568 ms)
    // Scan WiFi: nombre max d'APs retournés (wifi_manager, web_server)
    inline constexpr uint16_t WIFI_SCAN_MAX_RECORDS = 16;
    // Timeout RPC pour POST (attente netTask) — doit être > durée HTTP observée pour éviter abandon avant fin
    inline constexpr uint32_t HTTP_POST_RPC_TIMEOUT_MS = 26000;  // 26 s (marge vs POST 18 s + file)
    // Réponse chunked : nombre max de lectures vides avant arrêt (évite IncompleteInput entre paquets TCP)
    inline constexpr uint8_t OUTPUTS_STATE_MAX_EMPTY_READS = 40;
    // Timeout mutex TLS pour serialization SMTP/HTTPS (aligné 5s)
    inline constexpr uint32_t TLS_MUTEX_TIMEOUT_MS = 5000;
    // Fetch au réveil : timeout plus long (dérogation acceptable car critique pour commandes programmées)
    inline constexpr uint32_t WAKEUP_FETCH_TIMEOUT_MS = 15000;  // 15s par tentative
    inline constexpr int WAKEUP_FETCH_MAX_RETRIES = 3;
    inline constexpr uint32_t WAKEUP_FETCH_RETRY_DELAY_MS = 2000;  // 2s entre retries
    inline constexpr uint32_t WAKEUP_NETWORK_STABILIZATION_DELAY_MS = 1000;  // 1s après waitForNetworkReady
    // Timeout OTA séparé : téléchargement firmware nécessite plus de temps
    // que requêtes HTTP standard
    // Justification : connexions lentes peuvent nécessiter jusqu'à 30s
    // pour télécharger un firmware complet
    // 30s pour téléchargements OTA (justifié par taille firmware)
    inline constexpr uint32_t OTA_TIMEOUT_MS = 30000;
    // Timeout phase connexion esp_http_client (méthode moderne) : 10s pour rester sous TWDT, éviter reboot si TLS/réseau échoue
    inline constexpr uint32_t OTA_CONNECT_TIMEOUT_MS = 10000;
    inline constexpr uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 300000; // 5 min
    inline constexpr uint32_t MIN_DELAY_BETWEEN_REQUESTS_MS = 1000;
    inline constexpr uint32_t BACKOFF_BASE_MS = 1000;
    
    inline constexpr const char* HTTP_USER_AGENT = "FFP5CS-ESP32";
    
    // v11.178: Codes HTTP centralisés (audit http-codes)
    inline constexpr int HTTP_OK = 200;
    inline constexpr int HTTP_NO_CONTENT = 204;
    inline constexpr int HTTP_BAD_REQUEST = 400;
    inline constexpr int HTTP_UNAUTHORIZED = 401;
    inline constexpr int HTTP_NOT_FOUND = 404;
    inline constexpr int HTTP_INTERNAL_ERROR = 500;
    inline constexpr int HTTP_SERVICE_UNAVAILABLE = 503;
    // Alias pour compatibilité
    inline constexpr int HTTP_OK_CODE = HTTP_OK;

#if defined(BOARD_S3)
    // S3 only: AP de secours – IP fixe (captive portal)
    inline constexpr uint8_t AP_IP_B0 = 192;
    inline constexpr uint8_t AP_IP_B1 = 168;
    inline constexpr uint8_t AP_IP_B2 = 4;
    inline constexpr uint8_t AP_IP_B3 = 1;
    inline constexpr uint8_t AP_GW_B0 = 192;
    inline constexpr uint8_t AP_GW_B1 = 168;
    inline constexpr uint8_t AP_GW_B2 = 4;
    inline constexpr uint8_t AP_GW_B3 = 1;
    inline constexpr uint8_t AP_SUBNET_B0 = 255;
    inline constexpr uint8_t AP_SUBNET_B1 = 255;
    inline constexpr uint8_t AP_SUBNET_B2 = 255;
    inline constexpr uint8_t AP_SUBNET_B3 = 0;
    // Heap minimum pour démarrer le DNSServer (captive portal) en mode AP
    inline constexpr uint32_t MIN_HEAP_AP_DNS = 40000;  // 40 KB
#endif
}

// Authentification interface web locale (onglets protégés : admin / ffp3)
namespace WebAuthConfig {
    inline constexpr const char* WEB_AUTH_USER = "admin";
    inline constexpr const char* WEB_AUTH_PASS = "ffp3";
    inline constexpr size_t WEB_AUTH_COOKIE_NAME_LEN = 11;  // "ffp5cs_auth"
    inline constexpr size_t WEB_AUTH_TOKEN_HEX_LEN = 32;     // 16 bytes en hex
}

namespace ServerConfig {
    // v11.162: HTTP par défaut pour réduire fragmentation mémoire (TLS ~32KB contigu requis)
    // Le serveur iot.olution.info est contrôlé, les données capteurs ne sont pas sensibles
    inline constexpr const char* BASE_URL = "http://iot.olution.info";
    // Diagnostic latence : IP du serveur (iot.olution.info). Non utilisé par défaut : hébergement mutualisé
    // renvoie 403/404 sur l'IP ; sur un VPS on pourrait l'utiliser + Host pour éviter le DNS.
    inline constexpr const char* BASE_URL_TEST_IP = "http://109.234.162.74";
    // HTTPS réservé pour OTA (sécurité critique pour mises à jour firmware)
    inline constexpr const char* BASE_URL_SECURE = "https://iot.olution.info";
    
    #if defined(USE_TEST3_ENDPOINTS)
        // Profil wroom-s3-test : routes serveur TEST3 (aquaponie3-test, post-data3-test, outputs3-test, heartbeat3-test)
        inline constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data3-test";
        inline constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs3-test/state";
        inline constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat3-test";
    #elif defined(BOARD_S3) && defined(PROFILE_PROD)
        // Profil wroom-s3-prod : routes serveur S3 prod (post-data3, outputs3, heartbeat3)
        inline constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data3";
        inline constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs3/state";
        inline constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat3";
    #elif defined(PROFILE_TEST) || defined(PROFILE_DEV) || defined(USE_TEST_ENDPOINTS)
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
        inline constexpr uint32_t JSON_DOCUMENT_SIZE_DBVARS = 4096;
        // metadata.json ~1129 bytes, structure channels — parsing OTA
        inline constexpr uint32_t JSON_DOCUMENT_SIZE_OTA_METADATA = 1536;
        inline constexpr uint32_t OUTPUTS_STATE_READ_BUFFER_SIZE = 4096;  // GET outputs/state body
        inline constexpr uint32_t POST_PAYLOAD_MAX_SIZE = 4096;
        inline constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 6000;
        inline constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 5000;
        inline constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 10000;
        inline constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 20000;
    #else
        // v11.158: Optimisation buffers - réduits pour libérer mémoire et réduire fragmentation
        inline constexpr uint32_t HTTP_BUFFER_SIZE = 1024;  // Réduit de 2048 (requêtes typiquement < 1024 bytes)
        inline constexpr uint32_t HTTP_TX_BUFFER_SIZE = 1024;  // Réduit de 2048
        // PROFILE_TEST aligné wroom-prod (1024) pour éviter IncompleteInput / sorties précoces
        // GET /api/outputs/state: ~28 clés (numériques + symboliques), typ. < 900 bytes (logs: "28 clés")
        inline constexpr uint32_t JSON_DOCUMENT_SIZE = 1024;
        // GET /dbvars: réponse plus grande (mail, mailNotif, ~20 clés) pour éviter troncature
        inline constexpr uint32_t JSON_DOCUMENT_SIZE_DBVARS = 2048;
        // metadata.json ~1129 bytes, structure channels — parsing OTA (1024 insuffisant)
        inline constexpr uint32_t JSON_DOCUMENT_SIZE_OTA_METADATA = 1536;
        // GET outputs/state: buffer lecture body plus grand pour éviter IncompleteInput (réponse > 1024)
        inline constexpr uint32_t OUTPUTS_STATE_READ_BUFFER_SIZE = 2048;
        inline constexpr uint32_t POST_PAYLOAD_MAX_SIZE = 1024;  // Limite payload postData (malloc si besoin pour tenir en DRAM)
        inline constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 2000;  // Réduit de 3000 (emails typiquement < 2000 bytes)
        inline constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 1500;  // Réduit de 2500
        inline constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 8000;
        inline constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 15000;
    #endif
}

// Seuils mémoire pour handlers HTTP (heap minimum requis)
// v11.173: Seuils réduits pour ESP32-WROOM (320KB RAM) - éviter 503 trop agressifs
// S3 (N16R8): seuils plus élevés pour réduire les 503 sous charge (plus de RAM/PSRAM)
namespace HeapConfig {
#if defined(BOARD_S3)
    inline constexpr uint32_t MIN_HEAP_JSON_ROUTE = 28000;      // S3: moins restrictif
    inline constexpr uint32_t MIN_HEAP_DBVARS_ROUTE = 35000;    // S3
    inline constexpr uint32_t MIN_HEAP_WIFI_ROUTE = 24000;      // S3
    inline constexpr uint32_t MIN_HEAP_EMAIL_ASYNC = 45000;     // S3
    inline constexpr uint32_t MIN_HEAP_OTA = 50000;             // S3: 50 KB (réduit refus OTA)
    inline constexpr uint32_t MIN_HEAP_BLOCK_FOR_ASYNC_TASK = 12288;  // 12 KB (inchangé)
    inline constexpr uint32_t MIN_HEAP_BLOCK_FOR_MAIL_TLS = 32768;   // 32 KB (taille réserve)
    inline constexpr uint32_t MIN_HEAP_BLOCK_FOR_MAIL_TLS_CONNECT = 30000;  // 30 KB (décision connexion SMTP)
    inline constexpr uint32_t MIN_HEAP_MAIL_SEND = 45000;       // S3: 45 KB
    // Heap libre minimum avant de libérer la réserve mail pour envoi (32K + ceci >= 45K).
    inline constexpr uint32_t MIN_HEAP_FREE_WHEN_USING_MAIL_RESERVE = 14000;  // 14 KB (32K+14K >= 45K)
    inline constexpr uint32_t MIN_HEAP_RESPONSE_STREAM = 36864; // S3: 36 KB (réduit 503 streams)
    // Heap libre minimum pour accepter une connexion TLS (SMTP/HTTPS). Source de vérité unique.
    inline constexpr uint32_t MIN_HEAP_FOR_TLS = 35000;        // 35 KB (aligné v11.159)
#else
    inline constexpr uint32_t MIN_HEAP_JSON_ROUTE = 20000;      // /json endpoint (réduit de 50K)
    inline constexpr uint32_t MIN_HEAP_DBVARS_ROUTE = 25000;    // /dbvars endpoint (réduit de 55K)
    inline constexpr uint32_t MIN_HEAP_WIFI_ROUTE = 18000;      // /wifi/* endpoints (réduit de 40K)
    inline constexpr uint32_t MIN_HEAP_EMAIL_ASYNC = 35000;     // Email async task fallback
    inline constexpr uint32_t MIN_HEAP_OTA = 35000;             // OTA check
    // Plus grand bloc libre minimum pour créer une tâche one-shot (évite fragmentation long uptime)
    inline constexpr uint32_t MIN_HEAP_BLOCK_FOR_ASYNC_TASK = 12288;  // 12 KB (stack 4 KB + marge)
    // Bloc contigu minimum pour SMTP/TLS (ESP Mail Client + mbedTLS). Réserve libérable utilisée si heap fragmenté.
    // 31 KB: session 2026-02-15 bloc observé 31732 → seuil 32K jamais atteint ; 31K pour réserver au boot.
    inline constexpr uint32_t MIN_HEAP_BLOCK_FOR_MAIL_TLS = 31000;  // 31 KB (taille réserve)
    // Seuil dans le mailer après préparation message (petites allocs) : 30K pour accepter ~30708 observé.
    inline constexpr uint32_t MIN_HEAP_BLOCK_FOR_MAIL_TLS_CONNECT = 30000;  // 30 KB (décision connexion SMTP)
    // Heap libre minimum avant envoi mail (Core 1). Évite abort() si TLS/allocs internes échouent.
    inline constexpr uint32_t MIN_HEAP_MAIL_SEND = 40000;  // 40 KB
    // Stratégie anti-fragmentation : si on a la réserve 31K allouée au boot, on peut envoyer en la libérant d'abord.
    // Il faut au moins ce heap libre avant de libérer la réserve pour qu'après libération on ait 31K+ceci >= 40K.
    inline constexpr uint32_t MIN_HEAP_FREE_WHEN_USING_MAIL_RESERVE = 10000;  // 10 KB (31K+10K >= 40K)
    // Minimum free heap avant beginResponseStream. La lib AsyncWebServer (cbuf/WebResponses)
    // alloue et redimensionne pendant write(); en dessous → Failed to allocate → abort().
    // 28 KB marge pour fragmentation + réponses en vol (run 30 min encore abort sans 20K).
    inline constexpr uint32_t MIN_HEAP_RESPONSE_STREAM = 28672;  // 28 KB
    // Heap libre minimum pour accepter une connexion TLS (SMTP/HTTPS). Source de vérité unique.
    inline constexpr uint32_t MIN_HEAP_FOR_TLS = 35000;         // 35 KB (aligné v11.159)
#endif
}

// Intervalles minimum entre deux créations de tâches one-shot (réduit fragmentation / clics répétés)
namespace AsyncTaskConfig {
    inline constexpr unsigned long FEED_TASK_MIN_MS = 10000;   // 10 s entre deux tâches feed
    inline constexpr unsigned long WIFI_CONNECT_MIN_MS = 30000; // 30 s entre deux tentatives WiFi
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
    // Test de connectivité capteur (DHT, DS18B20)
    inline constexpr uint32_t CONNECTIVITY_TEST_TIMEOUT_MS = 2000;
    // Debounce pour sauvegarde température NVS
    inline constexpr uint32_t NVS_TEMP_DEBOUNCE_MS = 60000;

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
        // Type: DHT22 uniquement en wroom-prod (USE_DHT22), DHT11 pour tous les autres envs (sensors.cpp).
        // Délai minimum entre lectures: 2500ms (compromis entre 2000ms datasheet et stabilité)
        // DHT11: 1s min, DHT22: 2s min (datasheet). On utilise 2.5s pour les deux.
        inline constexpr uint32_t MIN_READ_INTERVAL_MS = 2500;
        inline constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 2000;
        // Timeout récupération temp/hum (robustTemperatureC, robustHumidityC) - évite INT_WDT
        inline constexpr uint32_t RECOVERY_TIMEOUT_MS = 2000;
    }

    // Alternative DHT pour envs S3 (USE_AIR_SENSOR_BME280). I2C, plus rapide que DHT.
    namespace BME280 {
        inline constexpr uint32_t MIN_READ_INTERVAL_MS = 500;
        inline constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 100;
        inline constexpr uint8_t I2C_ADDRESS = 0x76;  // SDO à GND (0x77 si SDO à VDD)
    }

    namespace Ultrasonic {
        inline constexpr uint16_t MIN_DISTANCE_CM = 2;
        inline constexpr uint16_t MAX_DISTANCE_CM = 400;
        // Plage validée dans system_sensors pour niveaux eau (potager, aquarium, réservoir)
        inline constexpr uint16_t MAX_VALID_LEVEL_CM = 500;
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

// -----------------------------------------------------------------------------
// 5.1 HELPERS VALIDATION CAPTEURS (v11.176 - audit élimination duplications)
// -----------------------------------------------------------------------------
// Ces fonctions inline remplacent le pattern répété isnan() + range check
// utilisé dans sensors.cpp, automatism.cpp, app_tasks.cpp, web_client.cpp
namespace SensorValidation {
    // Valide une température d'eau (DS18B20)
    // Retourne true si la valeur est valide, false si NaN ou hors plage
    // Note: -127.0f est le code erreur DallasTemperature
    inline bool isValidWaterTemp(float temp) {
        return !isnan(temp) && 
               temp != -127.0f &&  // Code erreur Dallas
               temp >= SensorConfig::WaterTemp::MIN_VALID && 
               temp <= SensorConfig::WaterTemp::MAX_VALID;
    }
    
    // Valide une température d'air (DHT22)
    inline bool isValidAirTemp(float temp) {
        return !isnan(temp) && 
               temp >= SensorConfig::AirSensor::TEMP_MIN && 
               temp <= SensorConfig::AirSensor::TEMP_MAX;
    }
    
    // Valide une humidité (DHT22)
    inline bool isValidHumidity(float humidity) {
        return !isnan(humidity) && 
               humidity >= SensorConfig::AirSensor::HUMIDITY_MIN && 
               humidity <= SensorConfig::AirSensor::HUMIDITY_MAX;
    }
    
    // Valide une distance ultrasonique
    inline bool isValidDistance(uint16_t distance) {
        return distance >= SensorConfig::Ultrasonic::MIN_DISTANCE_CM && 
               distance <= SensorConfig::Ultrasonic::MAX_DISTANCE_CM;
    }
    
    // Applique une valeur par défaut si la température d'eau est invalide
    inline float sanitizeWaterTemp(float temp) {
        return isValidWaterTemp(temp) ? temp : SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
    }
    
    // Applique une valeur par défaut si la température d'air est invalide
    inline float sanitizeAirTemp(float temp) {
        return isValidAirTemp(temp) ? temp : SensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
    }
    
    // Applique une valeur par défaut si l'humidité est invalide
    inline float sanitizeHumidity(float humidity) {
        return isValidHumidity(humidity) ? humidity : SensorConfig::DefaultValues::HUMIDITY_DEFAULT;
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

// -----------------------------------------------------------------------------
// 7. DISPLAY
// -----------------------------------------------------------------------------
namespace DisplayConfig {
    inline constexpr uint8_t OLED_WIDTH = 128;
    inline constexpr uint8_t OLED_HEIGHT = 64;
    // Adresse I2C OLED : source unique dans include/pins.h (Pins::OLED_ADDR)

    inline constexpr int PERCENTAGE_MAX = 100;
    
    // Intervalle de rafraîchissement OLED pour automatismes
    inline constexpr uint32_t OLED_INTERVAL_MS = 80;
    inline constexpr uint32_t OLED_COUNTDOWN_INTERVAL_MS = 250;

    // Status bar layout (barre d'état sur la dernière ligne de l'écran)
    inline constexpr int STATUS_BAR_HEIGHT = 8;
    inline constexpr int STATUS_BAR_Y = OLED_HEIGHT - STATUS_BAR_HEIGHT;  // 56 : dernière ligne
    inline constexpr int STATUS_BAR_WIFI_X = 0;       // Position indicateur WiFi
    inline constexpr int STATUS_BAR_SENDRECV_X = 60;  // Position indicateurs S/R
    inline constexpr int STATUS_BAR_TIDE_X = 80;      // Position indicateur marée
    inline constexpr int STATUS_BAR_MAIL_X = 90;      // Position indicateur mail

    // OTA overlay position
    inline constexpr int OTA_OVERLAY_X_POS = 100;
    inline constexpr int OTA_OVERLAY_Y_POS = 0;
    inline constexpr int OTA_OVERLAY_WIDTH = 28;
    inline constexpr int OTA_OVERLAY_HEIGHT = 8;
    
    inline constexpr uint32_t SPLASH_DURATION_MS = 3000;  // Durée du splash screen (3 secondes)
    inline constexpr uint32_t SCREEN_SWITCH_INTERVAL_MS = 6000;
    
    inline constexpr uint8_t DISPLAY_WHITE = 1;
    inline constexpr uint8_t DISPLAY_BLACK = 0;
    // VCC OLED : utiliser la macro SSD1306_SWITCHCAPVCC de la lib Adafruit (0x02) dans display_view.cpp
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
    inline constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 300;  // 5 min (comme le jour)
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
    // Seuil en dessous duquel modem-sleep est désactivé pour stabiliser la liaison
    inline constexpr int8_t WIFI_RSSI_MODEM_SLEEP_THRESHOLD = -80;
    inline constexpr int8_t WIFI_RSSI_MINIMUM = -90;
    inline constexpr int8_t WIFI_RSSI_CRITICAL = -95;
    
    inline constexpr uint32_t WIFI_STABILITY_CHECK_INTERVAL_MS = 60000;
    inline constexpr uint32_t WIFI_WEAK_SIGNAL_THRESHOLD_MS = 300000;
    inline constexpr uint8_t WIFI_MAX_RECONNECT_ATTEMPTS = 5;
    // Exception documentée: 5s pour stabiliser la reconnexion WiFi (réseau instable).
    // Dérogation à la règle "blocage thread principal ≤ 3s" : acceptée car reconnexion rare
    // (intervalle WIFI_STABILITY_CHECK_INTERVAL_MS 60s) et priorité stabilité liaison.
    inline constexpr uint32_t WIFI_RECONNECT_DELAY_MS = 5000;

    // Modem-sleep WiFi : activé par défaut (économie d'énergie). Désactiver via build -DWIFI_MODEM_SLEEP_ENABLED=0
    // Nom C++ distinct du macro pour éviter expansion préprocesseur (WIFI_MODEM_SLEEP_ENABLED → 0).
#if defined(WIFI_MODEM_SLEEP_ENABLED) && (WIFI_MODEM_SLEEP_ENABLED == 0)
    inline constexpr bool MODEM_SLEEP_ENABLED = false;
#else
    inline constexpr bool MODEM_SLEEP_ENABLED = true;
#endif

    // Constantes PowerManager manquantes
    inline constexpr bool AUTO_RECONNECT_WIFI_AFTER_SLEEP = true;
    inline constexpr bool SAVE_TIME_BEFORE_SLEEP = true;
    // À réviser en release majeure : fallback si NVS vide ET sync NTP échoue.
    inline constexpr time_t EPOCH_COMPILE_TIME = 1769904000; // 2026-02-01 00:00:00 UTC (vérifié)
    inline constexpr time_t EPOCH_DEFAULT_FALLBACK = 1769904000; // 2026-02-01 00:00:00 UTC (vérifié)
    inline constexpr bool ENABLE_DRIFT_CORRECTION = true;
    inline constexpr uint32_t DRIFT_CORRECTION_INTERVAL_MS = 3600000;
    inline constexpr float DRIFT_CORRECTION_THRESHOLD_PPM = 100.0f;
    inline constexpr float DRIFT_CORRECTION_FACTOR = 0.5f;
    inline constexpr float DRIFT_PPM_MAX = 500.0f;  // Plafond PPM (évite correction excessive si mesure aberrante)
    inline constexpr uint32_t MAX_RTC_REGRESSION_SEC = 3600;  // 1 h - régression max acceptée avant refus sauvegarde
    inline constexpr uint32_t MAX_SAVE_INTERVAL_MS = 86400000;
    inline constexpr uint32_t MIN_SAVE_INTERVAL_MS = 3600000;
    inline constexpr uint32_t MIN_TIME_DIFF_FOR_SAVE_SEC = 60;
}

// N'appelle WiFi.setSleep(enable) que si SleepConfig::MODEM_SLEEP_ENABLED est vrai. Fichiers utilisateurs : inclure config.h et WiFi (ex. WiFi.h).
#define WIFI_APPLY_MODEM_SLEEP(enable) do { if (::SleepConfig::MODEM_SLEEP_ENABLED) { ::WiFi.setSleep(enable); } } while(0)

// -----------------------------------------------------------------------------
// INVENTAIRE DRAM STATIQUE (ESP32-WROOM) – Ne pas dépasser la limite
// -----------------------------------------------------------------------------
// ESP32 : 520 KB SRAM = 320 KB DRAM + 200 KB IRAM.
// Contrainte IDF : au plus 160 KB en allocation statique (.data + .bss) ; le reste
// n'est disponible qu'en heap à l'exécution.
// Inventaire application (WROOM, ordre de grandeur) :
//   - Stacks + TCB (sensor, automation, net ; webTask en heap) : ~26 KB
//   - Pools / cache (NetRequest, JSON fallback, remoteJson, deferredJson, etc.) : ~10 KB
//   - Buffers applicatifs (app, mailer, web_server) : ~11 KB
//   - Globaux (PowerManager, WebClient, Mailer, Diagnostics, NVSManager) : ~3 KB
//   Total app ~50 KB. BSS système (IDF/WiFi/LwIP) consomme le reste ; la marge réelle
//   sous 160 KB est faible (passer webTask en statique ou augmenter les stacks dépasse).
// Éviter d'ajouter de gros buffers statiques sans vérifier le link (region dram0_0_seg).

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
    // v11.194: Augmenté 8KB → 10KB - HWM observé 1268 bytes libres (marge insuffisante, risque overflow)
    inline constexpr uint32_t WEB_TASK_STACK_SIZE = 10240;  // 10KB - WebSocket + AsyncWebServer + marge
    // Baissé de 2 à 1 - le web n'est pas critique (offline-first)
    inline constexpr UBaseType_t WEB_TASK_PRIORITY = 1;
    // Core 1 : évite starvation TWDT quand async_tcp monopolise core 0 pendant POST (P1)
    inline constexpr BaseType_t WEB_TASK_CORE_ID = 1;
    
    // v11.157: Augmenté de 6KB à 8KB pour éviter stack overflow (HWM: 100 bytes libres = critique)
    // v11.171: Augmenté de 8KB à 10KB (audit: HWM utilisé à 95%, marge insuffisante)
    // Le crash se produit dans automationTask lors de la sauvegarde NVS
    inline constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 10240;  // 10KB
    inline constexpr UBaseType_t AUTOMATION_TASK_PRIORITY = 3;
    inline constexpr BaseType_t AUTOMATION_TASK_CORE_ID = 1;
    
    inline constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 3072;  // Réduit de 4096 (HWM: 2328 libres, marge 744)
    inline constexpr UBaseType_t DISPLAY_TASK_PRIORITY = 1;
    inline constexpr BaseType_t DISPLAY_TASK_CORE_ID = 1;

    inline constexpr uint32_t OTA_TASK_STACK_SIZE = 8192;
    
    // Tâche réseau (TLS/HTTP) - propriétaire unique de WebClient/TLS
    // v11.159: Réduit de 10KB à 8KB (Phase 3 - HWM: 5584 libres, marge 4656)
    // v11.197: Augmenté 8KB → 12KB - stack overflow dans netTask lors de checkForUpdate OTA
    // S3: 12 KB (stack canary netTask sur S3 - mbedTLS/HTTP plus gourmand)
#if defined(BOARD_S3)
    inline constexpr uint32_t NET_TASK_STACK_SIZE = 12288;  // 12 KB (S3)
#else
    inline constexpr uint32_t NET_TASK_STACK_SIZE = 12288;  // 12 KB (WROOM - requis pour OTA checkForUpdate + TLS/HTTPS)
#endif
    inline constexpr UBaseType_t NET_TASK_PRIORITY = 2;      // Priorité moyenne pour traitement réseau
    inline constexpr BaseType_t NET_TASK_CORE_ID = 0;        // Core 0 pour ne pas impacter capteurs
    
    // Tâche mail asynchrone (v11.143) - évite de bloquer automationTask pendant SMTP
    // v11.161: Augmenté de 12KB à 16KB - stack overflow persistant pendant TLS/SMTP handshake
    // Réduit pour résoudre overflow DRAM dram0_0_seg (sans mise à jour plateforme). 16384→15168 (-1216 B).
    inline constexpr uint32_t MAIL_TASK_STACK_SIZE = 15168;  // ~14.8 KB (surveiller HWM mail/TLS)
    inline constexpr UBaseType_t MAIL_TASK_PRIORITY = 1;     // Basse priorité (non critique)
    inline constexpr BaseType_t MAIL_TASK_CORE_ID = 0;       // Core 0 pour ne pas impacter capteurs
    inline constexpr uint8_t MAIL_QUEUE_SIZE = 6;            // Réduit 8→6 (piste 2 rapport mémoire), robustesse envoi conservée
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
        inline void begin(unsigned long) const {}
        inline void end() const {}
        inline void flush() const {}
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
