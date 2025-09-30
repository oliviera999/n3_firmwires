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
    
public:
    /**
     * Compresse une chaîne avec gzip (désactivé temporairement)
     * @param input Données à compresser
     * @return Données non compressées (compression désactivée)
     */
    static String gzipCompress(const String& input) {
        // Compression désactivée temporairement - retourner les données telles quelles
        return input;
    }
    
    /**
     * Envoie une réponse JSON optimisée
     * @param req Requête HTTP
     * @param json Données JSON
     * @param statusCode Code de statut HTTP (défaut: 200)
     */
    static void sendOptimizedJson(AsyncWebServerRequest* req, const String& json, int statusCode = 200) {
        if (json.length() > COMPRESSION_THRESHOLD) {
            // Compression pour les réponses importantes
            String compressed = gzipCompress(json);
            
            AsyncWebServerResponse* response = req->beginResponse(statusCode, "application/json", compressed);
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "no-cache, must-revalidate");
            response->addHeader("Connection", "keep-alive");
            response->addHeader("Keep-Alive", "timeout=5, max=100");
            response->addHeader("Vary", "Accept-Encoding");
            req->send(response);
        } else {
            // Réponse directe pour les petits contenus
            AsyncWebServerResponse* response = req->beginResponse(statusCode, "application/json", json);
            response->addHeader("Cache-Control", "no-cache, must-revalidate");
            response->addHeader("Connection", "keep-alive");
            response->addHeader("Keep-Alive", "timeout=5, max=100");
            req->send(response);
        }
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
        // Générer un ETag simple basé sur le hash du contenu
        String etag = String(content.length(), HEX);  // Utiliser la longueur au lieu de hashCode
        String clientETag = req->getHeader("If-None-Match") ? 
            req->getHeader("If-None-Match")->value() : "";
        
        // Vérifier si le client a déjà la version
        if (clientETag == etag) {
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
        if (content.length() > COMPRESSION_THRESHOLD) {
            // Compression + cache
            String compressed = gzipCompress(content);
            
            // Générer un ETag pour le contenu compressé
            String etag = String(compressed.length(), HEX);  // Utiliser la longueur au lieu de hashCode
            String clientETag = req->getHeader("If-None-Match") ? 
                req->getHeader("If-None-Match")->value() : "";
            
            if (clientETag == etag) {
                req->send(304); // Not Modified
                return;
            }
            
            AsyncWebServerResponse* response = req->beginResponse(200, contentType, compressed);
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("ETag", etag);
            response->addHeader("Cache-Control", String("public, max-age=") + String(maxAge));
            response->addHeader("Connection", "keep-alive");
            response->addHeader("Keep-Alive", "timeout=5, max=100");
            response->addHeader("Vary", "Accept-Encoding");
            req->send(response);
        } else {
            // Cache simple sans compression
            sendWithCache(req, content, contentType, maxAge);
        }
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
