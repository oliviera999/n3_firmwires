#pragma once

// =============================================================================
// CONFIGURATION RÉSEAU
// =============================================================================
// Module: Configuration réseau, HTTP, WiFi et serveur web
// Taille: ~24 lignes
// =============================================================================

#include <Arduino.h>

namespace NetworkConfig {
    // Timeouts HTTP
    constexpr uint32_t HTTP_TIMEOUT_MS = 30000;
    constexpr uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 300000;  // 5 minutes
    constexpr uint32_t CONNECTION_TIMEOUT_MS = 10000;      // 10 secondes
    constexpr uint32_t REQUEST_TIMEOUT_MS = 30000;         // 30 secondes
    constexpr uint32_t MIN_DELAY_BETWEEN_REQUESTS_MS = 500;  // 500ms entre requêtes
    
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
