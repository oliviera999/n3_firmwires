#pragma once

#include <Arduino.h>
#include <LittleFS.h>
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
        String path;
        String contentType;
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
     * @return Bundle combiné
     */
    static String createJsBundle(const std::vector<String>& files) {
        String bundle = "";
        bundle.reserve(MAX_BUNDLE_SIZE);
        
        for (const String& file : files) {
            if (LittleFS.exists(file)) {
                File f = LittleFS.open(file, "r");
                if (f) {
                    // Ajouter un commentaire pour identifier le fichier
                    bundle += "\n/* " + file + " */\n";
                    
                    // Lire le contenu
                    while (f.available() && bundle.length() < MAX_BUNDLE_SIZE - 100) {
                        bundle += (char)f.read();
                    }
                    
                    bundle += "\n";
                    f.close();
                }
            }
        }
        
        return bundle;
    }
    
    /**
     * Crée un bundle CSS
     * @param files Liste des fichiers CSS à inclure
     * @return Bundle CSS combiné
     */
    static String createCssBundle(const std::vector<String>& files) {
        String bundle = "";
        bundle.reserve(MAX_BUNDLE_SIZE);
        
        for (const String& file : files) {
            if (LittleFS.exists(file)) {
                File f = LittleFS.open(file, "r");
                if (f) {
                    // Ajouter un commentaire pour identifier le fichier
                    bundle += "\n/* " + file + " */\n";
                    
                    // Lire le contenu
                    while (f.available() && bundle.length() < MAX_BUNDLE_SIZE - 100) {
                        bundle += (char)f.read();
                    }
                    
                    bundle += "\n";
                    f.close();
                }
            }
        }
        
        return bundle;
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
            std::vector<String> jsFiles = {
                "/chart.js",
                "/utils.js"
            };
            
            String bundle = createJsBundle(jsFiles);
            
            if (bundle.length() > 0) {
                AsyncWebServerResponse* response = req->beginResponse(200, "application/javascript", bundle);
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
            std::vector<String> cssFiles = {
                "/bootstrap.min.css",
                "/dashboard.css",
                "/uplot.min.css"
            };
            
            String bundle = createCssBundle(cssFiles);
            
            if (bundle.length() > 0) {
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
            std::vector<String> jsFiles = {
                "/utils.js"
            };
            
            String bundle = createJsBundle(jsFiles);
            
            if (bundle.length() > 0) {
                AsyncWebServerResponse* response = req->beginResponse(200, "application/javascript", bundle);
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
    static size_t getFileSize(const String& path) {
        if (!LittleFS.exists(path)) return 0;
        
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
    static bool isBundleAvailable(const String& bundleType) {
        if (bundleType == "js") {
            return getFileSize("/chart.js") > 0 || getFileSize("/utils.js") > 0;
        } else if (bundleType == "css") {
            return getFileSize("/bootstrap.min.css") > 0 || getFileSize("/uplot.min.css") > 0 || getFileSize("/dashboard.css") > 0;
        } else if (bundleType == "min") {
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
        String files[] = {"/chart.js", "/utils.js", "/bootstrap.min.css", "/dashboard.css", "/uplot.min.css"};
        
        for (const String& file : files) {
            size_t size = getFileSize(file);
            if (size > 0) {
                stats.totalFiles++;
                stats.totalSize += size;
            }
        }
        
        // Calculer les tailles des bundles
        if (isBundleAvailable("js")) {
            std::vector<String> jsFiles = {"/chart.js", "/utils.js"};
            String jsBundle = createJsBundle(jsFiles);
            stats.jsBundleSize = jsBundle.length();
            stats.jsAvailable = true;
        }
        
        if (isBundleAvailable("css")) {
            std::vector<String> cssFiles = {"/bootstrap.min.css", "/dashboard.css", "/uplot.min.css"};
            String cssBundle = createCssBundle(cssFiles);
            stats.cssBundleSize = cssBundle.length();
            stats.cssAvailable = true;
        }
        
        if (isBundleAvailable("min")) {
            std::vector<String> minFiles = {"/utils.js"};
            String minBundle = createJsBundle(minFiles);
            stats.minBundleSize = minBundle.length();
            stats.minAvailable = true;
        }
        
        return stats;
    }
};
