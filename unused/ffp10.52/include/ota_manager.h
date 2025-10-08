#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#ifndef ESP_HTTP_CLIENT_FWD_DECL
#define ESP_HTTP_CLIENT_FWD_DECL
// Forward declaration to avoid pulling http_parser symbols into public headers
typedef struct esp_http_client * esp_http_client_handle_t;
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ota_config.h"

class OTAManager {
private:
    bool m_isUpdating;
    unsigned long m_lastCheck;
    unsigned long m_checkInterval;
    String m_currentVersion;
    String m_remoteVersion;
    String m_firmwareUrl;
    int m_firmwareSize;
    String m_firmwareMD5;
    
    // Variables pour les fichiers filesystem
    String m_filesystemUrl;
    int m_filesystemSize;
    String m_filesystemMD5;
    
    // Nouvelles variables pour la stabilité
    TaskHandle_t m_updateTaskHandle;
    esp_http_client_handle_t m_httpClient;
    
    // Rappels (callbacks)
    std::function<void(int)> m_progressCallback;
    std::function<void(const String&)> m_statusCallback;
    std::function<void(const String&)> m_errorCallback;
    
    // Journalisation détaillée
    void log(const String& message);
    void logError(const String& error);
    void logProgress(int progress, size_t downloaded, size_t total, float speed);
    
    // Validation
    bool validateMetadata(const JsonDocument& doc);
    bool validateFirmwareSize(size_t expected, size_t actual);
    bool validateFilesystemSize(size_t expected, size_t actual);
    bool validateSpace(size_t required);
    bool selectArtifactFromMetadata(const JsonDocument& doc, String& outVersion, String& outUrl, int& outSize, String& outMD5);
    bool selectFilesystemFromMetadata(const JsonDocument& doc, String& outUrl, int& outSize, String& outMD5);
    
    // Téléchargement (méthodes multiples pour la robustesse)
    bool downloadMetadata(String& payload);
    bool downloadFirmware(const String& url, size_t expectedSize);
    bool downloadFirmwareModern(const String& url, size_t expectedSize);
    // Ancienne variante supprimée dans l'implémentation. Conserver uniquement les modes modern et ultra.
    bool downloadFirmwareUltraRevolutionary(const String& url, size_t expectedSize);
    
    // Téléchargement des fichiers filesystem
    bool downloadFilesystem(const String& url, size_t expectedSize, const String& expectedMD5);
    
    // Tâche dédiée pour l'OTA
    static void updateTask(void* parameter);
    
public:
    OTAManager();
    ~OTAManager();
    
    // Configuration
    void setProgressCallback(std::function<void(int)> callback);
    void setStatusCallback(std::function<void(const String&)> callback);
    void setErrorCallback(std::function<void(const String&)> callback);
    void setCheckInterval(unsigned long interval);
    
    // Gestion des versions
    void setCurrentVersion(const String& version);
    String getCurrentVersion() const;
    String getRemoteVersion() const;
    
    // Vérification et mise à jour
    bool checkForUpdate();
    bool performUpdate();
    bool isUpdating() const;
    
    // Statistiques
    unsigned long getLastCheck() const;
    int getFirmwareSize() const;
    String getFirmwareUrl() const;
    String getFirmwareMD5() const { return m_firmwareMD5; }
    
    // Statistiques filesystem
    int getFilesystemSize() const { return m_filesystemSize; }
    String getFilesystemUrl() const { return m_filesystemUrl; }
    String getFilesystemMD5() const { return m_filesystemMD5; }
    
    // Utilitaires
    static int compareVersions(const String& version1, const String& version2);
    static String formatBytes(size_t bytes);
    static String formatSpeed(float speed);
}; 