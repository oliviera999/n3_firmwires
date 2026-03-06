#pragma once

#include "config.h"
#include <cstring>

// Configuration des URLs OTA par modèle d'ESP32
// Modifiez ces URLs selon votre infrastructure

namespace OTAConfig {
    // Dériver depuis la configuration serveur centrale
    // BASE_URL + OTA_BASE_PATH
    
    // Dossiers spécifiques par modèle
    // Ne pas commencer par '/' pour éviter les doubles slash dans les URLs
    constexpr const char* ESP32_S3_FOLDER = "esp32-s3/";
    constexpr const char* ESP32_WROOM_FOLDER = "esp32-wroom/";
    
    // Noms de fichiers pour les mises à jour
    constexpr const char* METADATA_FILE = "metadata.json";
    // Champs metadata filesystem (alignés publish_ota.ps1 et selectFilesystemFromMetadata)
    // filesystem_url, filesystem_size, filesystem_md5 dans channels.<canal>.<model>
    
    // Timeout pour les requêtes HTTP métadonnées (ms) — dérogation règle "≤5s" : OTA critique, netTask
    // avec feed watchdog. Doit rester < TWDT 30s pour éviter reset netTask.
#if defined(BOARD_S3)
    constexpr int HTTP_TIMEOUT = 15000;  // 15 s (S3 sans PSRAM: TWDT 30s, monitoring 2026-03)
#else
    constexpr int HTTP_TIMEOUT = 20000;
#endif
    
    // Taille maximale du filesystem (bytes) — alignée partition spiffs WROOM (0x0B0000)
    constexpr size_t MAX_FILESYSTEM_SIZE = 720896;  // 0x0B0000 — partitions_esp32_wroom_ota_fs_medium.csv
    // Taille partition app OTA (fallback si metadata.size absent) — 0x1A0000
    constexpr size_t OTA_APP_PARTITION_SIZE = 1744896;

    // Mode DANGEREUX: forcer l'OTA avec taille flexible (taille/MD5 partiellement assouplis).
    // À false : le serveur doit envoyer Content-Length correct pour firmware.bin et littlefs.bin.
    // - true : UPDATE_SIZE_UNKNOWN, ignore écarts de taille, Update.end(true).
    // - false : taille connue, vérifications strictes, Update.end(false). Recommandé en production.
    constexpr bool OTA_UNSAFE_FORCE = false;
    
    // Fonction pour obtenir le dossier OTA selon le modèle
    // (déclaration/definition placée avant l'utilisation)
    inline const char* getOTAFolder() {
        #if defined(BOARD_S3)
            return ESP32_S3_FOLDER;
        #else
            return ESP32_WROOM_FOLDER;
        #endif
    }
    
    // Fonction pour obtenir l'URL OTA selon le modèle
    // v11.162: Utilise BASE_URL_SECURE (HTTPS) pour la sécurité des mises à jour firmware
    inline void getOTABaseUrl(char* buffer, size_t bufferSize) {
        if (!buffer || bufferSize == 0) return;
        
        // Normaliser OTA_BASE_PATH pour garantir un seul '/'
        const char* base = ServerConfig::BASE_URL_SECURE;  // HTTPS pour OTA (sécurité critique)
        const char* path = ServerConfig::OTA_BASE_PATH;
        const char* folder = getOTAFolder();
        
        // Construire le chemin normalisé
        char normalizedPath[128];
        if (path[0] != '/') {
            snprintf(normalizedPath, sizeof(normalizedPath), "/%s", path);
        } else {
            strncpy(normalizedPath, path, sizeof(normalizedPath) - 1);
            normalizedPath[sizeof(normalizedPath) - 1] = '\0';
        }
        
        // Ajouter '/' à la fin si nécessaire
        size_t pathLen = strlen(normalizedPath);
        if (normalizedPath[pathLen - 1] != '/') {
            if (pathLen < sizeof(normalizedPath) - 1) {
                normalizedPath[pathLen] = '/';
                normalizedPath[pathLen + 1] = '\0';
            }
        }
        
        // Construire l'URL complète
        snprintf(buffer, bufferSize, "%s%s%s", base, normalizedPath, folder);
    }
    
    // Fonction pour obtenir l'URL de métadonnées (dérivée de ServerConfig et du chemin OTA)
    // v11.162: Utilise BASE_URL_SECURE (HTTPS) pour la sécurité des mises à jour firmware
    inline void getMetadataUrl(char* buffer, size_t bufferSize) {
        if (!buffer || bufferSize == 0) return;
        
        const char* base = ServerConfig::BASE_URL_SECURE;  // HTTPS pour OTA (sécurité critique)
        const char* path = ServerConfig::OTA_BASE_PATH;
        
        // Construire le chemin normalisé
        char normalizedPath[128];
        if (path[0] != '/') {
            snprintf(normalizedPath, sizeof(normalizedPath), "/%s", path);
        } else {
            strncpy(normalizedPath, path, sizeof(normalizedPath) - 1);
            normalizedPath[sizeof(normalizedPath) - 1] = '\0';
        }
        
        // Ajouter '/' à la fin si nécessaire
        size_t pathLen = strlen(normalizedPath);
        if (normalizedPath[pathLen - 1] != '/') {
            if (pathLen < sizeof(normalizedPath) - 1) {
                normalizedPath[pathLen] = '/';
                normalizedPath[pathLen + 1] = '\0';
            }
        }
        
        // Construire l'URL complète
        snprintf(buffer, bufferSize, "%s%s%s", base, normalizedPath, METADATA_FILE);
    }
    
    // Fonction pour obtenir l'URL de version
    // URL version/manifest non utilisées (désactivées pour clarté)
    // inline String getVersionUrl() { return getOTABaseUrl() + VERSION_FILE; }
    
    // Fonction pour obtenir l'URL du firmware
    // (non utilisée, la sélection se fait via metadata.json)
    // inline String getFirmwareUrl() { return getOTABaseUrl() + FIRMWARE_FILE; }
} 