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
    }
    
    // Clés système
    namespace System {
        constexpr const char* OTA_UPDATE_FLAG = "ota_update_flag";
        constexpr const char* OTA_PREV_VER = "ota_prevVer";
        constexpr const char* OTA_IN_PROGRESS = "ota_in_progress";
        constexpr const char* NET_SEND_EN = "net_send_en";
        constexpr const char* NET_RECV_EN = "net_recv_en";
    }
}
