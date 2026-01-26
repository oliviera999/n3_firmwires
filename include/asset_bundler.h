#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>
#include <vector>
#ifndef DISABLE_ASYNC_WEBSERVER
#include <ESPAsyncWebServer.h>
#endif

/**
 * Bundler d'assets pour optimiser les requêtes HTTP
 * Combine plusieurs fichiers en un seul pour réduire les requêtes
 */
class AssetBundler {
private:
    struct AssetFile {
        char path[64];
        char contentType[32];
        bool compressible;
    };
    
    // Configuration des bundles selon le type de board
    static constexpr size_t MAX_BUNDLE_SIZE = 
        #ifdef BOARD_S3
        32768; // ESP32-S3 : bundles plus gros (32KB)
        #else
        16384; // ESP32-WROOM : bundles limités (16KB)
        #endif
    
public:
    /**
     * Crée un bundle JavaScript
     * @param files Liste des fichiers à inclure
     * @param buffer Buffer de sortie
     * @param bufferSize Taille du buffer
     * @return Taille du bundle créé (0 si erreur)
     */
    static size_t createJsBundle(const char* files[], size_t fileCount,
                                 char* buffer, size_t bufferSize) {
        if (!buffer || bufferSize == 0) return 0;
        
        size_t offset = 0;
        buffer[0] = '\0';
        
        for (size_t i = 0; i < fileCount; i++) {
            const char* file = files[i];
            if (!file) continue;
            
            if (LittleFS.exists(file)) {
                File f = LittleFS.open(file, "r");
                if (f) {
                    // Ajouter un commentaire pour identifier le fichier
                    int written = snprintf(buffer + offset, bufferSize - offset,
                                           "\n/* %s */\n", file);
                    if (written > 0 && (size_t)written < bufferSize - offset) {
                        offset += written;
                    }
                    
                    // Lire le contenu
                    while (f.available() && offset < bufferSize - 100) {
                        char c = f.read();
                        if (offset < bufferSize - 1) {
                            buffer[offset++] = c;
                        } else {
                            break;
                        }
                    }
                    
                    if (offset < bufferSize - 1) {
                        buffer[offset++] = '\n';
                    }
                    f.close();
                }
            }
        }
        
        buffer[offset] = '\0';
        return offset;
    }
    
    /**
     * Crée un bundle CSS
     * @param files Liste des fichiers CSS à inclure
     * @param buffer Buffer de sortie
     * @param bufferSize Taille du buffer
     * @return Taille du bundle créé (0 si erreur)
     */
    static size_t createCssBundle(const char* files[], size_t fileCount,
                                  char* buffer, size_t bufferSize) {
        if (!buffer || bufferSize == 0) return 0;
        
        size_t offset = 0;
        buffer[0] = '\0';
        
        for (size_t i = 0; i < fileCount; i++) {
            const char* file = files[i];
            if (!file) continue;
            
            if (LittleFS.exists(file)) {
                File f = LittleFS.open(file, "r");
                if (f) {
                    // Ajouter un commentaire pour identifier le fichier
                    int written = snprintf(buffer + offset, bufferSize - offset,
                                           "\n/* %s */\n", file);
                    if (written > 0 && (size_t)written < bufferSize - offset) {
                        offset += written;
                    }
                    
                    // Lire le contenu
                    while (f.available() && offset < bufferSize - 100) {
                        char c = f.read();
                        if (offset < bufferSize - 1) {
                            buffer[offset++] = c;
                        } else {
                            break;
                        }
                    }
                    
                    if (offset < bufferSize - 1) {
                        buffer[offset++] = '\n';
                    }
                    f.close();
                }
            }
        }
        
        buffer[offset] = '\0';
        return offset;
    }
    
    /**
     * Configure les routes de bundles pour le serveur web
     * @param server Serveur web AsyncWebServer
     */
    #ifndef DISABLE_ASYNC_WEBSERVER
    static void setupBundleRoutes(AsyncWebServer* server) {
        if (!server) return;
        
        // Bundle JavaScript principal
        server->on("/bundle.js", HTTP_GET, [](AsyncWebServerRequest* req) {
            const char* jsFiles[] = {"/chart.js", "/utils.js"};
            char bundle[MAX_BUNDLE_SIZE];
            size_t bundleSize = createJsBundle(jsFiles, 2, bundle, sizeof(bundle));
            
            if (bundleSize > 0) {
                AsyncWebServerResponse* response = 
                  req->beginResponse(200, "application/javascript", bundle);
                response->addHeader("Cache-Control", "public, max-age=604800"); // 7 jours
                response->addHeader("Connection", "keep-alive");
                response->addHeader("Keep-Alive", "timeout=5, max=100");
                req->send(response);
            } else {
                req->send(404, "text/plain", "Bundle not available");
            }
        });
        
        // Bundle CSS principal
        server->on("/bundle.css", HTTP_GET, [](AsyncWebServerRequest* req) {
            const char* cssFiles[] = {"/bootstrap.min.css", "/dashboard.css", "/uplot.min.css"};
            char bundle[MAX_BUNDLE_SIZE];
            size_t bundleSize = createCssBundle(cssFiles, 3, bundle, sizeof(bundle));
            
            if (bundleSize > 0) {
                AsyncWebServerResponse* response = req->beginResponse(200, "text/css", bundle);
                response->addHeader("Cache-Control", "public, max-age=604800"); // 7 jours
                response->addHeader("Connection", "keep-alive");
                response->addHeader("Keep-Alive", "timeout=5, max=100");
                req->send(response);
            } else {
                req->send(404, "text/plain", "Bundle not available");
            }
        });
        
        // Bundle minimal pour les connexions lentes
        server->on("/bundle.min.js", HTTP_GET, [](AsyncWebServerRequest* req) {
            const char* jsFiles[] = {"/utils.js"};
            char bundle[MAX_BUNDLE_SIZE];
            size_t bundleSize = createJsBundle(jsFiles, 1, bundle, sizeof(bundle));
            
            if (bundleSize > 0) {
                AsyncWebServerResponse* response = 
                  req->beginResponse(200, "application/javascript", bundle);
                response->addHeader("Cache-Control", "public, max-age=604800");
                response->addHeader("Connection", "keep-alive");
                response->addHeader("Keep-Alive", "timeout=5, max=100");
                req->send(response);
            } else {
                req->send(404, "text/plain", "Bundle not available");
            }
        });
        
        Serial.println("[AssetBundler] Routes de bundles configurées");
    }
    #else
    static void setupBundleRoutes(void* server) {
        // Fonction vide si DISABLE_ASYNC_WEBSERVER est défini
    }
    #endif
    
    /**
     * Obtient la taille d'un fichier
     * @param path Chemin du fichier
     * @return Taille en bytes (0 si fichier inexistant)
     */
    static size_t getFileSize(const char* path) {
        if (!path || !LittleFS.exists(path)) return 0;
        
        File f = LittleFS.open(path, "r");
        if (!f) return 0;
        
        size_t size = f.size();
        f.close();
        return size;
    }
    
    /**
     * Vérifie si un bundle est disponible
     * @param bundleType Type de bundle ("js", "css", "min")
     * @return true si disponible
     */
    static bool isBundleAvailable(const char* bundleType) {
        if (!bundleType) return false;
        
        if (strcmp(bundleType, "js") == 0) {
            return getFileSize("/chart.js") > 0 || getFileSize("/utils.js") > 0;
        } else if (strcmp(bundleType, "css") == 0) {
            return getFileSize("/bootstrap.min.css") > 0 || 
                   getFileSize("/uplot.min.css") > 0 || 
                   getFileSize("/dashboard.css") > 0;
        } else if (strcmp(bundleType, "min") == 0) {
            return getFileSize("/utils.js") > 0;
        }
        return false;
    }
    
    /**
     * Obtient les statistiques des bundles
     */
    struct BundleStats {
        size_t jsBundleSize;
        size_t cssBundleSize;
        size_t minBundleSize;
        bool jsAvailable;
        bool cssAvailable;
        bool minAvailable;
        size_t totalFiles;
        size_t totalSize;
    };
    
    static BundleStats getBundleStats() {
        BundleStats stats = {0, 0, 0, false, false, false, 0, 0};
        
        // Vérifier les fichiers individuels
        const char* files[] = {
          "/chart.js", "/utils.js", "/bootstrap.min.css",
          "/dashboard.css", "/uplot.min.css"
        };
        
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
            size_t size = getFileSize(files[i]);
            if (size > 0) {
                stats.totalFiles++;
                stats.totalSize += size;
            }
        }
        
        // Calculer les tailles des bundles
        if (isBundleAvailable("js")) {
            const char* jsFiles[] = {"/chart.js", "/utils.js"};
            char jsBundle[MAX_BUNDLE_SIZE];
            stats.jsBundleSize = createJsBundle(jsFiles, 2, jsBundle, sizeof(jsBundle));
            stats.jsAvailable = (stats.jsBundleSize > 0);
        }
        
        if (isBundleAvailable("css")) {
            const char* cssFiles[] = {"/bootstrap.min.css", "/dashboard.css", "/uplot.min.css"};
            char cssBundle[MAX_BUNDLE_SIZE];
            stats.cssBundleSize = createCssBundle(cssFiles, 3, cssBundle, sizeof(cssBundle));
            stats.cssAvailable = (stats.cssBundleSize > 0);
        }
        
        if (isBundleAvailable("min")) {
            const char* minFiles[] = {"/utils.js"};
            char minBundle[MAX_BUNDLE_SIZE];
            stats.minBundleSize = createJsBundle(minFiles, 1, minBundle, sizeof(minBundle));
            stats.minAvailable = (stats.minBundleSize > 0);
        }
        
        return stats;
    }
};
