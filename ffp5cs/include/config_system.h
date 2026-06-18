#pragma once
// config_system.h — version/profil, utilitaires, système, timing, monitoring,
// flags FEATURE_DIAG_*/HTTP_HEAP_GUARD/DAILY_REBOOT. Extrait de config.h (découpe).
#include <Arduino.h>
#include "board_traits.h"

// -----------------------------------------------------------------------------
// 1. VERSION ET IDENTIFICATION
// -----------------------------------------------------------------------------
namespace ProjectConfig {
    // Historique complet : voir VERSION.md (la liste exhaustive des versions est maintenue
    // uniquement dans VERSION.md depuis la v13.52, audit général 2026-05).
    inline constexpr const char* VERSION = "14.14";

    // Type d'environnement
    #if defined(PROFILE_DEV)
        inline constexpr const char* PROFILE_TYPE = "dev";
    #elif defined(PROFILE_TEST)
        inline constexpr const char* PROFILE_TYPE = "test";
    #elif defined(PROFILE_BETA)
        inline constexpr const char* PROFILE_TYPE = "beta";
    #elif defined(PROFILE_PROD)
        inline constexpr const char* PROFILE_TYPE = "prod";
    #else
        inline constexpr const char* PROFILE_TYPE = "unknown";
    #endif
    
    // Identification matérielle (via BoardTraits, ex-#ifdef BOARD_S3)
    inline constexpr const char* BOARD_TYPE = BoardTraits::isS3() ? "esp32-s3" : "esp32-wroom";
}

namespace Utils {
    inline const char* getProfileName() {
        #if defined(PROFILE_PROD)
            return "PRODUCTION";
        #elif defined(PROFILE_BETA)
            return "BETA";
        #elif defined(PROFILE_TEST)
            return BoardTraits::isS3() ? "S3-TEST" : "TEST";
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
    // Reconnexion après réveil : dérogation pour réseaux lents/faibles (DHCP, association jusqu'à 8s)
    inline constexpr uint32_t WIFI_RECONNECT_AFTER_WAKE_MS = 12000;
    // 15 s par tentative d'association WiFi (box 4G / routeurs lents, DHCP) — permet liens faibles au boot et manuel
    // S3 PSRAM test: 4 s pour limiter blocage boot (splash) quand WiFi absent/faible
    // S3 PSRAM : 4 s (limite le blocage boot/splash) ; autres cibles : 15 s.
    // Ex-#ifdef converti via BoardTraits (sélection de valeur, comportement identique).
    inline constexpr uint32_t WIFI_CONNECT_ATTEMPT_TIMEOUT_MS =
        (BoardTraits::isS3() && BoardTraits::hasPsram()) ? 4000U : 15000U;
    // v11.168: Timeout boot plus long pour laisser le temps de récupérer config serveur
    // Au boot uniquement, on peut attendre un peu plus car c'est le seul moment
    // où on peut récupérer la config distante de manière fiable
    inline constexpr uint32_t WIFI_BOOT_TIMEOUT_MS = 8000;
    // Attente fin fetch config netTask avant POST boot (GET 8s + délai TLS + marge)
    inline constexpr uint32_t BOOT_CONFIG_FETCH_WAIT_MS = 25000;
    inline constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
    inline constexpr uint32_t WIFI_WATCHDOG_TIMEOUT_MS = 30000;
    // Délai après disconnect avant scan (stabilisation chip WiFi)
    inline constexpr uint32_t WIFI_POST_DISCONNECT_DELAY_MS = 500;
    // Délai avant premier scan au boot (stabilisation RF, 500 ms améliore détection)
    inline constexpr uint32_t WIFI_PRE_SCAN_DELAY_MS = 500;
    // Délai entre tentatives sur réseaux différents (évite états intermédiaires)
    inline constexpr uint32_t WIFI_DELAY_BETWEEN_NETWORKS_MS = 250;
    // Délai avant 2e tentative (sans BSSID) pour laisser le routeur/box 4G respirer
    inline constexpr uint32_t WIFI_SECOND_ATTEMPT_DELAY_MS = 500;
    // Délai avant 4e tentative par réseau visible (routeur instable)
    inline constexpr uint32_t WIFI_FOURTH_ATTEMPT_DELAY_MS = 1000;
    // Intervalle entre tentatives en mode AP de secours (backoff long pour box 4G / AUTH_EXPIRE)
    inline constexpr uint32_t WIFI_AP_FALLBACK_RETRY_INTERVAL_MS = 45000;

    // Serveur
    inline constexpr uint32_t SERVER_SYNC_INTERVAL_MS = 60000;
    inline constexpr uint32_t SERVER_RETRY_INTERVAL_MS = 5000;
    
    // Tâches périodiques
    inline constexpr uint32_t OTA_CHECK_INTERVAL_MS = 7200000; // 2h
    // Pas d'attente otaTask : ne jamais bloquer plus longtemps sans reset WDT (TWDT 30s/60s)
    inline constexpr uint32_t OTA_WDT_FEED_INTERVAL_MS = 10000; // 10s
    // Délai après mail réveil avant demande OTA (stabilisation TCP/IP / heap post-TLS)
    inline constexpr uint32_t OTA_CHECK_DELAY_AFTER_WAKE_MS = 3000;
    // Retries post-boot WROOM prod : si le 1er essai OTA est reporté (heap fragmenté au démarrage).
    inline constexpr uint32_t OTA_BOOT_RETRY_1_MS = 60000;    // 1 min
    inline constexpr uint32_t OTA_BOOT_RETRY_2_MS = 180000;   // 3 min
    inline constexpr uint32_t OTA_BOOT_RETRY_3_MS = 600000;   // 10 min
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
