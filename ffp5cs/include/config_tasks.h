#pragma once
// config_tasks.h — sommeil/économie d'énergie (SleepConfig + WIFI_APPLY_MODEM_SLEEP)
// et stacks/priorités/cores FreeRTOS (TaskConfig). Extrait de config.h (découpe).
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>     // UBaseType_t / BaseType_t (TaskConfig)
#include "board_traits.h"
#include "config_system.h"     // SystemConfig::EPOCH_* (alias dans SleepConfig)

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
    // Fallback si NVS vide ET sync NTP échoue. EPOCH_COMPILE_TIME = date de build (pio_build_epoch.py).
#if defined(FIRMWARE_BUILD_EPOCH)
    inline constexpr time_t EPOCH_COMPILE_TIME = static_cast<time_t>(FIRMWARE_BUILD_EPOCH);
#else
    inline constexpr time_t EPOCH_COMPILE_TIME = 1780444800; // 2026-06-03 00:00:00 UTC (secours hors PIO)
#endif
    inline constexpr time_t EPOCH_DEFAULT_FALLBACK = 1769904000; // 2026-02-01 00:00:00 UTC (dernier recours)
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
//
// Inventaire application v13.70 (audit 2026-05) - WROOM prod, ordres de grandeur :
//   - Stacks statiques FreeRTOS :
//       sensor    (3072 octets)
//       auto      (10240)
//       net       (12800 prod / 14224 beta / 9216 test)
//       ota       (12288 prod / 11264 beta / 9216 test)
//       Total stacks statiques ≈ 38,4 KB (prod) — était ~26 KB dans l'inventaire pré-v13.36.
//   - Stacks heap (créées via xTaskCreatePinnedToCore avec malloc) :
//       webTask         (10240)
//       postSenderTask  (8192)
//       Total heap stacks ≈ 18,4 KB.
//   - TCB FreeRTOS (×~6) : ~0,4 KB statique.
//   - Pools / caches statiques :
//       NetRequest pool (8 × ~928 o, payload 896) : ~7,4 KB
//       s_remoteJsonCache, s_lastFetchedJson, s_deferredRemoteJson : ~1,5 + 2 + 1,5 ≈ 5 KB
//       s_dbvarsCachedSrc : ~1 KB ; documents outputs/state 2048 o alloués sur heap au poll.
//   - Buffers applicatifs (mailer s_mailMessageBuffer ~4,3 KB, web_server, app) : ~10-12 KB
//   - Globaux (PowerManager, WebClient, Mailer, Diagnostics, NVSManager) : ~3 KB
//
// Total BSS applicatif statique WROOM prod : **~60-65 KB** (était annoncé ~50 KB).
// Marge restante sous 160 KB IDF : faible une fois BSS système (WiFi/LwIP/Arduino) ajouté.
// Raison des réductions stacks beta/test : récupérer de la DRAM au link (`dram0_0_seg`).
//
// Bonnes pratiques :
//   - Éviter d'ajouter de gros buffers statiques sans vérifier le link.
//   - Préférer le heap pour tout ce qui peut être alloué après le boot.
//   - Quand on ajoute une nouvelle tâche, choisir entre stack statique (BSS) vs malloc dans
//     `xTaskCreatePinnedToCore` selon la taille (>= 4 KB en heap typiquement).
// -----------------------------------------------------------------------------

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
#if defined(BOARD_WROOM) && defined(PROFILE_BETA)
    // v13.29: réduction beta uniquement pour récupérer de la DRAM au link
    inline constexpr uint32_t SENSOR_TASK_STACK_SIZE = 2560;
#else
    inline constexpr uint32_t SENSOR_TASK_STACK_SIZE = 3072;  // Réduit de 4096 (HWM: 1864 libres, marge 1208)
#endif
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
    // S3: 12 KB (run 5 min 2026-03: HWM 78%, alerte >70% répétée; 10 KB insuffisant)
    // WROOM + PROFILE_TEST : stacks réduites (dram0.bss AsyncWebServer/WebSockets ~+10 Ko vs prod)
#if defined(BOARD_S3)
    inline constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 12288;  // 12 KB (S3)
#elif defined(BOARD_WROOM) && defined(PROFILE_TEST)
    inline constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 8192;   // wroom-test : marge link
#else
    inline constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 10240;  // 10KB (WROOM prod/beta)
#endif
    inline constexpr UBaseType_t AUTOMATION_TASK_PRIORITY = 3;
    inline constexpr BaseType_t AUTOMATION_TASK_CORE_ID = 1;
    
    inline constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 3072;  // Réduit de 4096 (HWM: 2328 libres, marge 744)
    inline constexpr UBaseType_t DISPLAY_TASK_PRIORITY = 1;
    inline constexpr BaseType_t DISPLAY_TASK_CORE_ID = 1;

    // Tâche OTA dédiée (prioritaire sur netTask) — stack dédiée pour éviter overflow TLS/Update
#if defined(BOARD_WROOM) && defined(PROFILE_TEST)
    inline constexpr uint32_t OTA_TASK_STACK_SIZE = 9216;   // wroom-test : BSS
#elif defined(BOARD_WROOM) && defined(PROFILE_BETA)
    // v13.29: réduction beta uniquement pour récupérer de la DRAM au link
    inline constexpr uint32_t OTA_TASK_STACK_SIZE = 11264;
#else
    inline constexpr uint32_t OTA_TASK_STACK_SIZE = 12288;  // 12 KB WROOM prod/beta (TLS OTA)
#endif
    inline constexpr UBaseType_t OTA_TASK_PRIORITY = 3;     // Supérieure à NET_TASK_PRIORITY (2)
    // Priorité absolue pendant verrou OTA exclusif (otaTask + OTA_Update pendant check/perform/download)
    inline constexpr UBaseType_t OTA_TASK_PRIORITY_WHILE_RUNNING = 10;
    inline constexpr BaseType_t OTA_TASK_CORE_ID = 0;
    
    // Tâche réseau (TLS/HTTP) - propriétaire unique de WebClient/TLS
    // v11.159: Réduit de 10KB à 8KB (Phase 3 - HWM: 5584 libres, marge 4656)
    // v11.197: Augmenté 8KB → 12KB - stack overflow dans netTask lors de checkForUpdate OTA
    // S3: 16 KB (stack canary netTask observé sur S3 - mbedTLS/HTTP plus gourmands, monitoring 2026-03)
    // WROOM prod/beta : 14384 (marge dram0 link) ; wroom-test : 10240 (AsyncWeb + WS)
#if defined(BOARD_S3)
    inline constexpr uint32_t NET_TASK_STACK_SIZE = 16384;  // S3
#elif defined(BOARD_WROOM) && defined(PROFILE_TEST)
    inline constexpr uint32_t NET_TASK_STACK_SIZE = 9216;   // wroom-test (dram0 vs AsyncWeb)
#elif defined(BOARD_WROOM) && defined(PROFILE_BETA)
    inline constexpr uint32_t NET_TASK_STACK_SIZE = 13968;  // v14.06 : −32 octets link dram0 (merge master)
#elif defined(BOARD_WROOM) && defined(PROFILE_PROD)
    // v13.36/v13.96: netTaskStack[] en BSS — réduction vs 14376 pour link dram0_0_seg (GCC 14 / IDF 5.5)
    inline constexpr uint32_t NET_TASK_STACK_SIZE = 12560;  // v14.11 : −64 octets link dram0_0_seg (clean rebuild GCC 14)
#else
    inline constexpr uint32_t NET_TASK_STACK_SIZE = 14376;   // WROOM (fallback)
#endif
    inline constexpr UBaseType_t NET_TASK_PRIORITY = 2;      // Priorité moyenne pour traitement réseau
    inline constexpr BaseType_t NET_TASK_CORE_ID = 0;        // Core 0 pour ne pas impacter capteurs

    // Tâche dédiée envoi POST (fire-and-forget : post-data + heartbeat)
    // 8 KB S3 et WROOM — stack canary postSender (HTTPS/postToUrl) observé S3 puis WROOM (monitoring 2026-03)
#if defined(BOARD_S3)
    inline constexpr uint32_t POST_SENDER_TASK_STACK_SIZE = 8192;
#else
    inline constexpr uint32_t POST_SENDER_TASK_STACK_SIZE = 8192;  // WROOM: aligné S3 (TLS heartbeat)
#endif
    inline constexpr UBaseType_t POST_SENDER_TASK_PRIORITY = 1;    // Sous netTask
    inline constexpr BaseType_t POST_SENDER_TASK_CORE_ID = 0;
    
    // Tâche mail asynchrone (v11.143) - évite de bloquer automationTask pendant SMTP
    // v11.161: Augmenté de 12KB à 16KB - stack overflow persistant pendant TLS/SMTP handshake
#if defined(BOARD_WROOM) && defined(PROFILE_TEST)
    inline constexpr uint32_t MAIL_TASK_STACK_SIZE = 12288;  // wroom-test : BSS
#else
    inline constexpr uint32_t MAIL_TASK_STACK_SIZE = 15168;  // WROOM prod/beta / S3 (réf. HWM mail/TLS ; pas de tâche dédiée)
#endif
    inline constexpr UBaseType_t MAIL_TASK_PRIORITY = 1;     // Basse priorité (non critique)
    inline constexpr BaseType_t MAIL_TASK_CORE_ID = 0;       // Core 0 pour ne pas impacter capteurs
    inline constexpr uint8_t MAIL_QUEUE_SIZE = 6;            // Réduit 8→6 (piste 2 rapport mémoire), robustesse envoi conservée
}

// Note: namespace DefaultValues supprimé (v11.174 simplification)
// - WATER_TEMP était un doublon de SensorConfig::DefaultValues::TEMP_WATER_DEFAULT
