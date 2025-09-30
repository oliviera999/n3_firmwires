#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
// #include <zlib.h>  // Temporairement désactivé - bibliothèque non disponible

/**
 * Optimiseur réseau pour améliorer les performances HTTP
 * Gestion de la compression, headers optimisés, et cache intelligent
 */
class NetworkOptimizer {
private:
    // Configuration selon le type de board
    static constexpr size_t COMPRESSION_THRESHOLD = 
        #ifdef BOARD_S3
        50;   // ESP32-S3 : compression plus agressive
        #else
        100;  // ESP32-WROOM : compression standard
        #endif
    
    static constexpr size_t MAX_COMPRESSION_SIZE = 
        #ifdef BOARD_S3
        8192; // ESP32-S3 : plus de mémoire pour compression
        #else
        4096; // ESP32-WROOM : taille limitée
        #endif
    
    // Calcule un CRC32 (polynôme 0xEDB88320) sur une chaîne pour l'ETag
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
     * Compresse une chaîne avec gzip (implémentation simplifiée)
     * @param input Données à compresser
     * @return Données compressées si taille > seuil, sinon données originales
     */
    static String gzipCompress(const String& input) {
        // Compression simple basée sur la taille
        if (input.length() < COMPRESSION_THRESHOLD) {
            return input; // Pas de compression pour les petits contenus
        }
        
        // Pour l'instant, retourner les données telles quelles
        // TODO: Implémenter une compression réelle si nécessaire
        return input;
    }
    
    /**
     * Envoie une réponse JSON optimisée
     * @param req Requête HTTP
     * @param json Données JSON
     * @param statusCode Code de statut HTTP (défaut: 200)
     */
    static void sendOptimizedJson(AsyncWebServerRequest* req, const String& json, int statusCode = 200) {
        // Toujours envoyer en clair sans Content-Encoding pour éviter les erreurs de décodage côté navigateur
        AsyncWebServerResponse* response = req->beginResponse(statusCode, "application/json", json);
        response->addHeader("Cache-Control", "no-cache, must-revalidate");
        response->addHeader("Connection", "keep-alive");
        response->addHeader("Keep-Alive", "timeout=5, max=100");
        req->send(response);
    }
    
    /**
     * Envoie une réponse avec cache intelligent
     * @param req Requête HTTP
     * @param content Contenu à envoyer
     * @param contentType Type de contenu MIME
     * @param maxAge Âge maximum du cache en secondes
     */
    static void sendWithCache(AsyncWebServerRequest* req, const String& content, 
                             const char* contentType, unsigned long maxAge = 3600) {
        // Générer un ETag robuste basé sur CRC32 du contenu
        String etag = String("\"") + computeEtagHex(content) + String("\"");
        const AsyncWebHeader* inm = req->getHeader("If-None-Match");
        String clientETag = inm ? inm->value() : "";
        clientETag.trim();
        
        // Vérifier si le client a déjà la version
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
    
    /**
     * Envoie une réponse avec compression et cache
     * @param req Requête HTTP
     * @param content Contenu à envoyer
     * @param contentType Type de contenu MIME
     * @param maxAge Âge maximum du cache en secondes
     */
    static void sendOptimized(AsyncWebServerRequest* req, const String& content, 
                             const char* contentType, unsigned long maxAge = 3600) {
        // Vérifier si le client accepte la compression
        if (acceptsGzip(req) && content.length() > COMPRESSION_THRESHOLD) {
            String compressed = gzipCompress(content);
            if (compressed.length() < content.length()) {
                // Compression réussie
                AsyncWebServerResponse* response = req->beginResponse(200, contentType, compressed);
                response->addHeader("Content-Encoding", "gzip");
                response->addHeader("Cache-Control", String("public, max-age=") + String(maxAge));
                addOptimizationHeaders(response);
                req->send(response);
                return;
            }
        }
        
        // Fallback vers cache sans compression
        sendWithCache(req, content, contentType, maxAge);
    }
    
    /**
     * Ajoute les headers d'optimisation à une réponse
     * @param response Réponse HTTP
     */
    static void addOptimizationHeaders(AsyncWebServerResponse* response) {
        response->addHeader("Connection", "keep-alive");
        response->addHeader("Keep-Alive", "timeout=5, max=100");
        response->addHeader("X-Content-Type-Options", "nosniff");
        response->addHeader("X-Frame-Options", "DENY");
        response->addHeader("X-XSS-Protection", "1; mode=block");
    }
    
    /**
     * Vérifie si le client accepte la compression gzip
     * @param req Requête HTTP
     * @return true si gzip accepté
     */
    static bool acceptsGzip(AsyncWebServerRequest* req) {
        const AsyncWebHeader* acceptEncoding = req->getHeader("Accept-Encoding");
        if (!acceptEncoding) return false;
        
        String encoding = acceptEncoding->value();
        encoding.toLowerCase();
        return encoding.indexOf("gzip") >= 0;
    }
    
    /**
     * Obtient les statistiques de compression
     */
    struct CompressionStats {
        size_t totalRequests;
        size_t compressedRequests;
        size_t bytesSaved;
        float compressionRatio;
    };
    
    static CompressionStats getCompressionStats() {
        // À implémenter si nécessaire pour le monitoring
        return {0, 0, 0, 0.0f};
    }
};
