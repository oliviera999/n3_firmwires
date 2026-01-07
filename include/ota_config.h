#pragma once

#include "config.h"

// Configuration des URLs OTA par modèle d'ESP32
// Modifiez ces URLs selon votre infrastructure

namespace OTAConfig {
    // Dériver depuis la configuration serveur centrale
    // BASE_URL + OTA_BASE_PATH
    
    // Dossiers spécifiques par modèle
    // Ne pas commencer par '/' pour éviter les doubles slash dans les URLs
    constexpr const char* ESP32_S3_FOLDER = "esp32-s3/";
    constexpr const char* ESP32_WROOM_FOLDER = "esp32-wroom/";
    constexpr const char* ESP32_C3_FOLDER = "esp32-c3/";
    constexpr const char* ESP32_DEFAULT_FOLDER = "esp32-wroom/";
    
    // Noms de fichiers pour les mises à jour
    constexpr const char* FIRMWARE_FILE = "firmware.bin";
    constexpr const char* FILESYSTEM_FILE = "filesystem.bin";
    // Fichiers optionnels (non utilisés actuellement)
    // constexpr const char* VERSION_FILE = "version.json";
    // constexpr const char* MANIFEST_FILE = "manifest.json";
    constexpr const char* METADATA_FILE = "metadata.json";
    
    // Timeout pour les requêtes HTTP (en millisecondes)
    constexpr int HTTP_TIMEOUT = 30000;
    
    // Taille maximale du firmware (en bytes)
    constexpr size_t MAX_FIRMWARE_SIZE = 16777216; // 16MB pour ESP32-S3
    
    // Taille maximale du filesystem (en bytes) - basée sur la partition spiffs
    constexpr size_t MAX_FILESYSTEM_SIZE = 1114112; // ~1MB (0x110000 bytes)
    
    // Mode DANGEREUX: forcer l'OTA sans vérifications (taille/MD5) pour tous les profils
    // - Utilise UPDATE_SIZE_UNKNOWN au lieu d'une taille fixe
    // - Ignore les écarts de taille entre attendu/réel
    // - Force Update.end(true)
    constexpr bool OTA_UNSAFE_FORCE = true;
    
    // Configuration pour les tests locaux
    #ifdef OTA_LOCAL_TEST
    constexpr const char* LOCAL_SERVER_URL = "http://192.168.1.100:8080/ota/";
    #endif
    
    // Fonction pour obtenir le dossier OTA selon le modèle (déclaration/definition placée avant l'utilisation)
    inline const char* getOTAFolder() {
        #if defined(BOARD_S3)
            return ESP32_S3_FOLDER;
        #else
            return ESP32_WROOM_FOLDER;
        #endif
    }
    
    // Fonction pour obtenir l'URL OTA selon le modèle
    inline String getOTABaseUrl() {
        // Normaliser OTA_BASE_PATH pour garantir un seul '/'
        String base = String(ServerConfig::BASE_URL);
        String path = String(ServerConfig::OTA_BASE_PATH);
        if (!path.startsWith("/")) path = String("/") + path;
        if (!path.endsWith("/")) path += "/";
        return base + path + getOTAFolder();
    }
    
    // Fonction pour obtenir l'URL de métadonnées (dérivée de ServerConfig et du chemin OTA)
    inline String getMetadataUrl() {
        String base = String(ServerConfig::BASE_URL);
        String path = String(ServerConfig::OTA_BASE_PATH);
        if (!path.startsWith("/")) path = String("/") + path;
        if (!path.endsWith("/")) path += "/";
        return base + path + String(METADATA_FILE);
    }
    
    // Fonction pour obtenir l'URL de version
    // URL version/manifest non utilisées (désactivées pour clarté)
    // inline String getVersionUrl() { return getOTABaseUrl() + VERSION_FILE; }
    
    // Fonction pour obtenir l'URL du firmware (non utilisée, la sélection se fait via metadata.json)
    // inline String getFirmwareUrl() { return getOTABaseUrl() + FIRMWARE_FILE; }
} 