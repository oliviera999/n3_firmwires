#pragma once
// config_buffers.h — tailles buffers/JSON, seuils heap, intervalles tâches
// one-shot, bornes NVS. Extrait de config.h (découpe).
#include <Arduino.h>
#include "board_traits.h"

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
        // Buffer réception body GET metadata. Voir docs/technical/OTA_PUBLISH.md (contrainte 2048 pour firmware < 12.25).
        inline constexpr uint32_t OTA_METADATA_PAYLOAD_BUFFER_SIZE = 3072;
        inline constexpr uint32_t OUTPUTS_STATE_READ_BUFFER_SIZE = 4096;  // GET outputs/state body
        // S3 sans PSRAM (wroom-s3-test): heap limitée comme WROOM — 4096*4 pour g_postSenderQueue échoue au boot.
        // Payload sync typ. < 800 bytes ; 896 suffit et permet création de la queue (POST/GET serveur opérationnels).
        // S3 avec PSRAM (wroom-s3-test-psram) : heap étendu 8 Mo → 4096 autorisé (réduit pression RAM interne).
#if defined(BOARD_HAS_PSRAM)
        inline constexpr uint32_t POST_PAYLOAD_MAX_SIZE = 4096;
#else
        inline constexpr uint32_t POST_PAYLOAD_MAX_SIZE = 896;
#endif
        inline constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 6000;
        inline constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 5000;
        inline constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 10000;
        inline constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 20000;
        inline constexpr uint32_t STATUS_HISTORY_BUFFER_SIZE = 4096;  // Route /api/sd-history
        inline constexpr uint32_t REMOTE_JSON_CACHE_SIZE = 2048;
    #else
        // v11.158: Optimisation buffers - réduits pour libérer mémoire et réduire fragmentation
        inline constexpr uint32_t HTTP_BUFFER_SIZE = 1024;  // Réduit de 2048 (requêtes typiquement < 1024 bytes)
        inline constexpr uint32_t HTTP_TX_BUFFER_SIZE = 1024;  // Réduit de 2048
        // PROFILE_TEST aligné wroom-prod (1024) pour éviter IncompleteInput / sorties précoces
        // GET /api/outputs/state: ~28 clés (numériques + symboliques), typ. < 900 bytes (logs: "28 clés")
        inline constexpr uint32_t JSON_DOCUMENT_SIZE = 1024;
        // metadata.json ~1129 bytes, structure channels — parsing OTA (1024 insuffisant)
        inline constexpr uint32_t JSON_DOCUMENT_SIZE_OTA_METADATA = 1536;
        // POST_PAYLOAD_MAX_SIZE réduit 896 sur WROOM pour éviter overflow DRAM (~3,7 KB). Payload sync typ. < 800 bytes.
        inline constexpr uint32_t POST_PAYLOAD_MAX_SIZE = 896;
#if !defined(PROFILE_TEST)
        inline constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 2000;
        inline constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 1500;
#endif
        inline constexpr uint32_t LOW_MEMORY_THRESHOLD_BYTES = 8000;
        inline constexpr uint32_t CRITICAL_MEMORY_THRESHOLD_BYTES = 15000;
        // Route /api/sd-history : réduit en wroom-test pour éviter overflow DRAM
#if defined(PROFILE_TEST)
        inline constexpr uint32_t STATUS_HISTORY_BUFFER_SIZE = 512;
        inline constexpr uint32_t JSON_DOCUMENT_SIZE_DBVARS = 1536;
        inline constexpr uint32_t REMOTE_JSON_CACHE_SIZE = 1024;
        inline constexpr uint32_t OTA_METADATA_PAYLOAD_BUFFER_SIZE = 2048;
        inline constexpr uint32_t OUTPUTS_STATE_READ_BUFFER_SIZE = 1536;
        inline constexpr uint32_t EMAIL_MAX_SIZE_BYTES = 1500;
        inline constexpr uint32_t EMAIL_DIGEST_MAX_SIZE_BYTES = 1000;
#else
        inline constexpr uint32_t STATUS_HISTORY_BUFFER_SIZE = 4096;
        inline constexpr uint32_t REMOTE_JSON_CACHE_SIZE = 1536;
#endif
        // GET /dbvars (wroom-prod) : réponse plus grande ; wroom-test : 1536 (DRAM)
#if !defined(PROFILE_TEST)
        inline constexpr uint32_t JSON_DOCUMENT_SIZE_DBVARS = 2048;
        inline constexpr uint32_t OTA_METADATA_PAYLOAD_BUFFER_SIZE = 3072;
        inline constexpr uint32_t OUTPUTS_STATE_READ_BUFFER_SIZE = 2048;
#endif
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
    // 32 KB réserve; seuil 32000 accepte bloc observé 32756 (run 5 min 2026-03) sans risque TLS
    inline constexpr uint32_t MIN_HEAP_BLOCK_FOR_MAIL_TLS = 32000;   // 32 KB - 768 (S3)
    inline constexpr uint32_t MIN_HEAP_BLOCK_FOR_MAIL_TLS_CONNECT = 30000;  // 30 KB (décision connexion SMTP)
    inline constexpr uint32_t MIN_HEAP_MAIL_SEND = 45000;       // S3: 45 KB
    // Heap libre minimum avant de libérer la réserve mail pour envoi (32K + ceci >= 45K).
    inline constexpr uint32_t MIN_HEAP_FREE_WHEN_USING_MAIL_RESERVE = 14000;  // 14 KB (32K+14K >= 45K)
    inline constexpr uint32_t MIN_HEAP_RESPONSE_STREAM = 36864; // S3: 36 KB (réduit 503 streams)
    // Heap libre minimum pour accepter une connexion TLS (SMTP/HTTPS). Source de vérité unique.
    inline constexpr uint32_t MIN_HEAP_FOR_TLS = 35000;        // 35 KB (aligné v11.159)
    // v13.70 (audit): aliases sémantiques pour différencier les usages futurs (v13.80 HTTPS).
    inline constexpr uint32_t MIN_HEAP_FOR_SMTP  = MIN_HEAP_FOR_TLS;  // SMTP via mbedTLS (mailer)
    inline constexpr uint32_t MIN_HEAP_FOR_HTTPS = MIN_HEAP_FOR_TLS;  // HTTPS (OTA metadata, v13.80+ data)
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
    // v13.70 (audit): aliases sémantiques pour différencier les usages futurs (v13.80 HTTPS).
    inline constexpr uint32_t MIN_HEAP_FOR_SMTP  = MIN_HEAP_FOR_TLS;  // SMTP via mbedTLS (mailer)
    inline constexpr uint32_t MIN_HEAP_FOR_HTTPS = MIN_HEAP_FOR_TLS;  // HTTPS (OTA metadata, v13.80+ data)
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
