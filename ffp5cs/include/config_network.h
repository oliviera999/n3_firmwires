#pragma once
// config_network.h — réseau/HTTP/OTA timeouts, AP de secours, auth web, URLs
// serveur, API key, mailer SMTP. Extrait de config.h (découpe).
#include <Arduino.h>
#include "board_traits.h"
#include "server_url_config.h"
#include "config_secrets.h"   // Secrets::*, SecretsValidation (static_assert prod)

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
    // GET outputs/state : timeout HTTP côté netTask (mutex partagé avec postSender).
    inline constexpr uint32_t OUTPUTS_STATE_HTTP_TIMEOUT_MS = 8000;  // 8 s
    // RPC FetchRemoteState : POST (~19 s observé 4G) + GET + marge file netTask.
    inline constexpr uint32_t FETCH_REMOTE_STATE_RPC_TIMEOUT_MS = 28000;  // 28 s (v13.95)
    // Intervalle min entre deux GET en branche timeout (fallback sans capteurs) — évite saturation netTask
    inline constexpr uint32_t REMOTE_FETCH_FALLBACK_INTERVAL_MS = 6000;   // 6 s (aligné poll data branch)
    // POST post-data / ack : dérogation (latence serveur). Observé jusqu'à ~20,2 s (4G, monitoring 2026-06) ; 22 s marge WROOM.
#if defined(BOARD_S3)
    inline constexpr uint32_t HTTP_POST_TIMEOUT_MS = 15000;  // 15 s (S3: rester sous TWDT 30s, monitoring 2026-03)
    // RPC >= HTTP + 2 s marge (évite abandon caller avant fin POST — v12.35)
    inline constexpr uint32_t HTTP_POST_RPC_TIMEOUT_MS = 17000;  // 17 s
#else
    inline constexpr uint32_t HTTP_POST_TIMEOUT_MS = 28000;  // 28 s (monitoring 2026-06-07 : ~23 s + retry 401 HMAC)
    inline constexpr uint32_t HTTP_POST_RPC_TIMEOUT_MS = 30000;  // 30 s (aligné POST 28 s + marge file)
#endif
    // Scan WiFi: nombre max d'APs retournés (wifi_manager, web_server)
    inline constexpr uint16_t WIFI_SCAN_MAX_RECORDS = 16;
    // Réponse chunked : nombre max de lectures vides avant arrêt (évite IncompleteInput entre paquets TCP)
    inline constexpr uint8_t OUTPUTS_STATE_MAX_EMPTY_READS = 40;
    // Timeout mutex TLS pour serialization SMTP/HTTPS (aligné 5s)
    inline constexpr uint32_t TLS_MUTEX_TIMEOUT_MS = 5000;
    // Handshake TLS WebClient métier (wroom-prod-https) — rester sous TWDT WROOM 30s
    inline constexpr uint32_t HTTP_TLS_HANDSHAKE_TIMEOUT_MS = 5000;
    // Fetch au réveil : timeout plus long (dérogation acceptable car critique pour commandes programmées)
    inline constexpr uint32_t WAKEUP_FETCH_TIMEOUT_MS = 28000;  // 28 s (aligné FETCH_REMOTE_STATE_RPC_TIMEOUT_MS)
    inline constexpr int WAKEUP_FETCH_MAX_RETRIES = 3;
    inline constexpr uint32_t WAKEUP_FETCH_RETRY_DELAY_MS = 2000;  // 2s entre retries
    inline constexpr uint32_t WAKEUP_NETWORK_STABILIZATION_DELAY_MS = 1000;  // 1s après waitForNetworkReady
    // Attente vidage files net/postSender après POST réveil, avant OTA ou mail TLS
    inline constexpr uint32_t WAKEUP_POST_DRAIN_TIMEOUT_MS = 25000;
    // Attente files net/postSender vides + mutex transport avant WiFi.disconnect (veille légère)
#if defined(BOARD_S3)
    inline constexpr uint32_t LIGHT_SLEEP_HTTP_QUIESCE_TIMEOUT_MS = 32000;  // POST 15s + GET 8s + marge
#else
    inline constexpr uint32_t LIGHT_SLEEP_HTTP_QUIESCE_TIMEOUT_MS = 40000;  // POST 28s + GET 8s + marge
#endif
    // Timeout OTA séparé : téléchargement firmware nécessite plus de temps
    // que requêtes HTTP standard
    // Justification : connexions lentes peuvent nécessiter jusqu'à 30s
    // pour télécharger un firmware complet
    // 30s pour téléchargements OTA (justifié par taille firmware)
    inline constexpr uint32_t OTA_TIMEOUT_MS = 30000;
    // Timeout phase connexion esp_http_client (méthode moderne) : 10s pour rester sous TWDT, éviter reboot si TLS/réseau échoue
    inline constexpr uint32_t OTA_CONNECT_TIMEOUT_MS = 10000;
    inline constexpr uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 300000; // 5 min
    // C2 (téléchargement OTA résumable) : nb max de (re)connexions sur coupure réseau avant abandon,
    // et base du backoff exponentiel entre tentatives de reprise (1s, 2s, 4s, … plafonné).
    inline constexpr int      OTA_RESUME_MAX_ATTEMPTS = 6;
    inline constexpr uint32_t OTA_RESUME_BACKOFF_BASE_MS = 1000;
    inline constexpr uint32_t OTA_RESUME_BACKOFF_MAX_MS = 16000;
    inline constexpr uint32_t MIN_DELAY_BETWEEN_REQUESTS_MS = 1000;
    inline constexpr uint32_t BACKOFF_BASE_MS = 1000;
    
    inline constexpr const char* HTTP_USER_AGENT = "FFP5CS-ESP32";
    
    // v11.178: Codes HTTP centralisés (audit http-codes)
    inline constexpr int HTTP_OK = 200;
    inline constexpr int HTTP_NO_CONTENT = 204;
    inline constexpr int HTTP_PARTIAL_CONTENT = 206;        // C2: reprise OTA via Range (serveur OtaFileController)
    inline constexpr int HTTP_RANGE_NOT_SATISFIABLE = 416;  // C2: offset Range invalide → fatal
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
    // v12.37: Heap minimum AVANT WiFi.mode()/softAP() — en dessous : skip AP secours (évite LoadProhibited)
    inline constexpr uint32_t MIN_HEAP_AP_MODE = 10240;  // 10 KB
    // v12.33: Heap minimum pour scan WiFi avant softAP (évite LoadProhibited si allocations échouent, tous boards)
    inline constexpr uint32_t MIN_HEAP_AP_SCAN = 12288;  // 12 KB — en dessous : canal 6 direct, pas de scan
}

// v13.60 (audit sécurité): mot de passe WPA2 pour l'AP de secours S3 (captive portal).
// Vide / nullptr → AP ouvert (comportement legacy, déconseillé sur LAN partagé).
// Pour activer WPA2 : définir `Secrets::AP_FALLBACK_PASSWORD` dans `secrets_config.h`
// (8 caractères minimum exigés par la norme WPA2).
namespace ApFallbackConfig {
#if defined(SECRETS_INCLUDE_AP_FALLBACK)
    inline constexpr const char* PASSWORD = Secrets::AP_FALLBACK_PASSWORD;
#else
    inline constexpr const char* PASSWORD = nullptr;  // AP ouvert (legacy)
#endif
}

// Authentification interface web locale (onglets protégés). Source : secrets_config.h si SECRETS_INCLUDE_WEB_AUTH, sinon défaut.
namespace WebAuthConfig {
#if defined(SECRETS_INCLUDE_WEB_AUTH)
    inline constexpr const char* WEB_AUTH_USER = Secrets::WEB_AUTH_USER;
    inline constexpr const char* WEB_AUTH_PASS = Secrets::WEB_AUTH_PASS;
#else
    inline constexpr const char* WEB_AUTH_USER = "admin";
    inline constexpr const char* WEB_AUTH_PASS = "CHANGEZ_MOI";  // Configurer secrets_config.h
#endif
    inline constexpr size_t WEB_AUTH_COOKIE_NAME_LEN = 11;  // "ffp5cs_auth"
    inline constexpr size_t WEB_AUTH_TOKEN_HEX_LEN = 32;     // 16 bytes en hex
}

// v13.52: refus en PROFILE_PROD si le mot de passe d'admin web est resté au placeholder.
#if defined(PROFILE_PROD)
static_assert(!SecretsValidation::strEq(WebAuthConfig::WEB_AUTH_PASS, "CHANGEZ_MOI"),
    "PROFILE_PROD: WebAuthConfig::WEB_AUTH_PASS vaut encore le placeholder \"CHANGEZ_MOI\". "
    "Configurer secrets_config.h avec WEB_AUTH_PASS et definir SECRETS_INCLUDE_WEB_AUTH.");
#endif

namespace ServerConfig {
    // URL serveurs (prod/test/local) resolues dans un header testable en natif.
    inline constexpr const char* BASE_URL = ServerUrlConfig::BASE_URL;
    // Diagnostic latence : IP du serveur (iot.olution.info). Non utilisé par défaut : hébergement mutualisé
    // renvoie 403/404 sur l'IP ; sur un VPS on pourrait l'utiliser + Host pour éviter le DNS.
    inline constexpr const char* BASE_URL_TEST_IP = "http://109.234.162.74";
    inline constexpr const char* BASE_URL_SECURE = ServerUrlConfig::BASE_URL_SECURE;
    inline constexpr const char* POST_DATA_ENDPOINT = ServerUrlConfig::POST_DATA_ENDPOINT;
    inline constexpr const char* OUTPUT_ENDPOINT = ServerUrlConfig::OUTPUT_ENDPOINT;
    inline constexpr const char* HEARTBEAT_ENDPOINT = ServerUrlConfig::HEARTBEAT_ENDPOINT;
    inline constexpr const char* OTA_BASE_PATH = ServerUrlConfig::OTA_BASE_PATH;
    
    // Helpers pour buffers statiques
    inline void getPostDataUrl(char* buffer, size_t bufferSize) {
        ServerUrlConfig::getPostDataUrl(buffer, bufferSize);
    }
    
    inline void getOutputUrl(char* buffer, size_t bufferSize) {
        ServerUrlConfig::getOutputUrl(buffer, bufferSize);
    }
    
    inline void getHeartbeatUrl(char* buffer, size_t bufferSize) {
        ServerUrlConfig::getHeartbeatUrl(buffer, bufferSize);
    }
    
    // Helper OTA historique (HTTPS explicite) conservé pour compatibilité.
    inline void getOtaBaseUrl(char* buffer, size_t bufferSize) {
        ServerUrlConfig::getOtaBaseUrl(buffer, bufferSize);
    }
}

namespace ApiConfig {
    inline constexpr const char* API_KEY = Secrets::API_KEY;
}

namespace EmailConfig {
    inline constexpr const char* SMTP_HOST = "smtp.gmail.com";
    inline constexpr uint16_t SMTP_PORT = 465;  // SSL direct
    // Placeholder : configurer une vraie adresse dans secrets_config.h (DEFAULT_RECIPIENT)
    inline constexpr const char* PLACEHOLDER_RECIPIENT = "changez@moi.example";
    inline constexpr const char* DEFAULT_RECIPIENT = Secrets::DEFAULT_RECIPIENT;
    inline constexpr size_t MAX_EMAIL_LENGTH = 96;
}
