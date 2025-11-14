#pragma once

// =============================================================================
// CONFIGURATION SERVEUR, API ET EMAIL
// =============================================================================
// Module: Configuration des endpoints serveur, API et email
// Taille: ~55 lignes  
// =============================================================================

#include <Arduino.h>

// =============================================================================
// CONFIGURATION SERVEUR
// =============================================================================
namespace ServerConfig {
    constexpr const char* BASE_URL = "https://iot.olution.info";
    
    // Endpoints selon le profil
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
        // Environnement de test: legacy POST route (DB write path)
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data-test";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs-test/state";
    #else
        // Production: legacy POST route (DB write path)
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs/state";
    #endif
    
    // URLs complètes - Utilisation de buffers statiques pour éviter la fragmentation mémoire
    inline void getPostDataUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s%s", BASE_URL, POST_DATA_ENDPOINT);
    }
    
    inline void getOutputUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s%s", BASE_URL, OUTPUT_ENDPOINT);
    }
    
    // Autres endpoints
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat-test";  // Slim controller (TEST)
    #else
    constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat";      // Slim controller (PROD)
    #endif
    constexpr const char* OTA_BASE_PATH = "/ffp3/ota/";
    
    inline void getHeartbeatUrl(char* buffer, size_t bufferSize) { 
        snprintf(buffer, bufferSize, "%s%s", BASE_URL, HEARTBEAT_ENDPOINT); 
    }
    
    inline void getServerUrl(char* buffer, size_t bufferSize) {
        strncpy(buffer, BASE_URL, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
    }
    
    inline void getWebSocketEndpoint(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s/ws", BASE_URL);
    }
}

// =============================================================================
// CONFIGURATION API
// =============================================================================
namespace ApiConfig {
    // SÉCURITÉ: API key à déplacer dans secrets.h
    // TEMPORAIRE: Garder la clé ici jusqu'à migration complète vers secrets.h
    constexpr const char* API_KEY = "fdGTMoptd5CD2ert3";
    
    // Pour utiliser après migration: ApiConfig::getApiKey()
    // extern const char* getApiKey(); // À définir dans secrets.h
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
    constexpr size_t MAX_EMAIL_LENGTH = 96;
}
