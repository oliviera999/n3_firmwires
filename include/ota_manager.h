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

class DisplayView;

class OTAManager {
private:
    bool m_isUpdating;
    unsigned long m_lastCheck;
    unsigned long m_checkInterval;
    char m_currentVersion[32];
    char m_remoteVersion[32];
    char m_firmwareUrl[256];
    int m_firmwareSize;
    char m_firmwareMD5[33];
    
    // Variables pour les fichiers filesystem
    char m_filesystemUrl[256];
    int m_filesystemSize;
    char m_filesystemMD5[33];
    
    // Nouvelles variables pour la stabilité
    TaskHandle_t m_updateTaskHandle;
    esp_http_client_handle_t m_httpClient;
    
    // Affichage OLED (optionnel, défini par setDisplay depuis le contexte applicatif)
    DisplayView* m_display = nullptr;

    // Rappels (callbacks)
    std::function<void(int)> m_progressCallback;
    std::function<void(const char*)> m_statusCallback;
    std::function<void(const char*)> m_errorCallback;
    
    // Journalisation détaillée
    void log(const char* message);
    void logError(const char* error);
    void logProgress(int progress, size_t downloaded, size_t total, float speed);
    
    // Validation
    bool validateMetadata(const JsonDocument& doc);
    bool validateFirmwareSize(size_t expected, size_t actual);
    bool validateFilesystemSize(size_t expected, size_t actual);
    bool validateSpace(size_t required);
    bool selectArtifactFromMetadata(const JsonDocument& doc, char* outVersion, size_t versionSize, char* outUrl, size_t urlSize, int& outSize, char* outMD5, size_t md5Size);
    bool selectFilesystemFromMetadata(const JsonDocument& doc, char* outUrl, size_t urlSize, int& outSize, char* outMD5, size_t md5Size);
    
    // Téléchargement (méthodes multiples pour la robustesse)
    bool downloadMetadata(char* payload, size_t payloadSize);
    bool downloadFirmware(const char* url, size_t expectedSize);
    bool downloadFirmwareModern(const char* url, size_t expectedSize);
    // Ancienne variante supprimée dans l'implémentation. Conserver uniquement les modes modern et ultra.
    bool downloadFirmwareUltraRevolutionary(const char* url, size_t expectedSize);
    
    // Téléchargement des fichiers filesystem
    bool downloadFilesystem(const char* url, size_t expectedSize, const char* expectedMD5);
    
    // Tâche dédiée pour l'OTA
    static void updateTask(void* parameter);
    
public:
    OTAManager();
    ~OTAManager();
    
    // Configuration
    void setDisplay(DisplayView* display) { m_display = display; }
    void setProgressCallback(std::function<void(int)> callback);
    void setStatusCallback(std::function<void(const char*)> callback);
    void setErrorCallback(std::function<void(const char*)> callback);
    void setCheckInterval(unsigned long interval);
    
    // Gestion des versions
    void setCurrentVersion(const char* version);
    const char* getCurrentVersion() const;
    const char* getRemoteVersion() const;
    
    // Vérification et mise à jour
    bool checkForUpdate();
    bool performUpdate();
    bool isUpdating() const;
    
    // Statistiques
    unsigned long getLastCheck() const;
    int getFirmwareSize() const;
    const char* getFirmwareUrl() const;
    const char* getFirmwareMD5() const { return m_firmwareMD5; }
    
    // Statistiques filesystem
    int getFilesystemSize() const { return m_filesystemSize; }
    const char* getFilesystemUrl() const { return m_filesystemUrl; }
    const char* getFilesystemMD5() const { return m_filesystemMD5; }
    
    // Utilitaires
    static int compareVersions(const char* version1, const char* version2);
    static void formatBytes(size_t bytes, char* buffer, size_t bufferSize);
    static void formatSpeed(float speed, char* buffer, size_t bufferSize);
}; 