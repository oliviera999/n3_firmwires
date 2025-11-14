#pragma once

#include <Arduino.h>
#ifndef DISABLE_ASYNC_WEBSERVER
#include <ESPAsyncWebServer.h>
#endif

/**
 * Optimiseur réseau simplifié pour améliorer les performances HTTP
 * Gestion du cache HTTP (ETag/304) et headers optimisés
 */
class NetworkOptimizer {
private:
    // Calcule un CRC32 simple pour l'ETag
    static uint32_t computeCRC32(const uint8_t* data, size_t length) {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < length; ++i) {
            crc ^= static_cast<uint32_t>(data[i]);
            for (int j = 0; j < 8; ++j) {
                if (crc & 1u) {
                    crc = (crc >> 1) ^ 0xEDB88320u;
                } else {
                    crc >>= 1;
                }
            }
        }
        return ~crc;
    }

    static String computeEtagHex(const String& content) {
        uint32_t crc = computeCRC32(reinterpret_cast<const uint8_t*>(content.c_str()), content.length());
        char buf[11];
        snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(crc));
        return String(buf);
    }

public:
    /**
     * Envoie une réponse JSON optimisée
     */
    #ifndef DISABLE_ASYNC_WEBSERVER
    static void sendOptimizedJson(AsyncWebServerRequest* req, const String& json, int statusCode = 200) {
        AsyncWebServerResponse* response = req->beginResponse(statusCode, "application/json", json);
        response->addHeader("Cache-Control", "no-cache, must-revalidate");
        response->addHeader("Connection", "keep-alive");
        response->addHeader("Keep-Alive", "timeout=5, max=100");
        req->send(response);
    }
    
    /**
     * Envoie une réponse avec cache intelligent (ETag/304)
     */
    static void sendWithCache(AsyncWebServerRequest* req, const String& content, 
                             const char* contentType, unsigned long maxAge = 3600) {
        String etag = String("\"") + computeEtagHex(content) + String("\"");
        const AsyncWebHeader* inm = req->getHeader("If-None-Match");
        String clientETag = inm ? inm->value() : "";
        clientETag.trim();
        
        if (clientETag.length() && clientETag == etag) {
            req->send(304); // Not Modified
            return;
        }
        
        AsyncWebServerResponse* response = req->beginResponse(200, contentType, content);
        response->addHeader("ETag", etag);
        response->addHeader("Cache-Control", String("public, max-age=") + String(maxAge));
        response->addHeader("Connection", "keep-alive");
        response->addHeader("Keep-Alive", "timeout=5, max=100");
        req->send(response);
    }
    #else
    static void sendOptimizedJson(void* req, const String& json, int statusCode = 200) {}
    static void sendWithCache(void* req, const String& content, const char* contentType, unsigned long maxAge = 3600) {}
    #endif
};
