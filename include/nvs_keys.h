#pragma once

/**
 * NVS Keys - Clés de stockage Non-Volatile centralisées
 * 
 * Ce fichier centralise toutes les clés NVS utilisées dans le firmware FFP5CS.
 * Cela évite les duplications de chaînes littérales et facilite la maintenance.
 * 
 * Organisation:
 * - Config: Variables de configuration utilisateur (nourrissage, etc.)
 * - System: Variables système (OTA, réseau, etc.)
 */

namespace NVSKeys {
    // Clés de configuration utilisateur
    namespace Config {
        constexpr const char* BOUFFE_MATIN = "bouffe_matin";
        constexpr const char* BOUFFE_MIDI = "bouffe_midi";
        constexpr const char* BOUFFE_SOIR = "bouffe_soir";
        constexpr const char* BOUFFE_JOUR = "bouffe_jour";
        constexpr const char* BF_PMP_LOCK = "bf_pmp_lock";
        constexpr const char* REMOTE_JSON = "remote_json";
        constexpr const char* EMAIL = "email";
    }
    
    // Clés système (noms ≤15 car. pour limite NVS ESP-IDF)
    namespace System {
        constexpr const char* OTA_UPDATE_FLAG = "ota_upd_flag";
        constexpr const char* OTA_PREV_VER = "ota_prevVer";
        constexpr const char* OTA_IN_PROGRESS = "ota_in_prog";
        constexpr const char* NET_SEND_EN = "net_send_en";
        constexpr const char* NET_RECV_EN = "net_recv_en";
        constexpr const char* RTC_EPOCH = "rtc_epoch";
        constexpr const char* FORCE_WAKE_UP = "force_wake_up";
    }
    
    // Clés diagnostics (LOGS)
    // Note: diag_crashFlag/panicCause = panic exception; crash_has/crash_reason = reset reason.
    // Les deux jeux coexistent (post-mortem vs boot); unification optionnelle à long terme.
    namespace Diag {
        constexpr const char* CRASH_FLAG = "diag_crashFlag";
        constexpr const char* PANIC_CAUSE = "diag_panicCause";
        constexpr const char* CRASH_HAS = "crash_has";
        constexpr const char* CRASH_REASON = "crash_reason";
        constexpr const char* REBOOT_CNT = "diag_rebootCnt";
        constexpr const char* MIN_HEAP = "diag_minHeap";
        constexpr const char* HTTP_OK = "diag_httpOk";
        constexpr const char* HTTP_KO = "diag_httpKo";
        constexpr const char* OTA_OK = "diag_otaOk";
        constexpr const char* OTA_KO = "diag_otaKo";
        constexpr const char* LAST_UPTIME = "diag_lastUptime";
        constexpr const char* LAST_HEAP = "diag_lastHeap";
    }
    
    // Clés capteurs (noms ≤15 car.)
    namespace Sensors {
        constexpr const char* TEMP_LAST_VALID = "temp_last_ok";
    }
    
    // Clés automatisme - états et snapshots
    namespace Automatism {
        constexpr const char* SNAP_PENDING = "snap_pending";
        constexpr const char* SNAP_AQUA = "snap_aqua";
        constexpr const char* SNAP_HEATER = "snap_heater";
        constexpr const char* SNAP_LIGHT = "snap_light";
        constexpr const char* STATE_PUMP_AQUA = "state_pumpAqua";
        constexpr const char* STATE_HEATER = "state_heater";
        constexpr const char* STATE_LAST_LOCAL = "state_lastLocal";
        constexpr const char* ALERT_FLOOD_LAST = "alert_flood_ts";
    }
    
    // Clés synchronisation serveur
    namespace Sync {
        constexpr const char* COUNT = "sync_count";
        constexpr const char* LAST_SYNC = "sync_lastSync";
        constexpr const char* CONFIG = "sync_config";
        // Préfixe pour les items en attente (sync_item_0, sync_item_1, etc.)
        constexpr const char* ITEM_PREFIX = "sync_item_";
    }
    
    // Clés WebClient - queue de POSTs
    namespace WebClient {
        constexpr const char* POST_Q_COUNT = "post_q_count";
        // Préfixe pour les payloads en queue (post_q_0, post_q_1, etc.)
        constexpr const char* POST_Q_PREFIX = "post_q_";
    }
}
