#pragma once
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"  // Pour getSSID sans String Arduino
#include <functional>
#include <ArduinoJson.h>
#include <cstring>
#if defined(BOARD_S3)
#include <DNSServer.h>
#endif
class DisplayView;

class WifiManager {
 public:
  struct Credential {
    const char* ssid;
    const char* password;
  };

  // v11.191: Timeout 5 s par tentative (renforce connexion routeurs lents) ; retries toutes les 5 s
  WifiManager(const Credential* list, size_t count, uint32_t timeoutMs = 5000, uint32_t retryIntervalMs = 5000);

  // Tente de se connecter ; retourne true si connecté. hostname optionnel (appliqué avant begin, utile au boot).
  bool connect(class DisplayView* disp = nullptr, const char* hostname = nullptr);

  // Connexion manuelle à un SSID spécifique
  bool connectTo(const char* ssid, const char* password, class DisplayView* disp = nullptr);

  // Renvoie le SSID auquel on est connecté ou "".
  void currentSSID(char* buffer, size_t bufferSize) const;

  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

  // Démarre un point d'accès lorsqu'aucun réseau n'est disponible
  bool startFallbackAP();

  // À appeler régulièrement (ex. dans loop) pour tenter périodiquement une
  // reconnexion lorsqu'on est en mode AP ou déconnecté. L'intervalle est
  // fixé dans le constructeur (60 s par défaut).
  void loop(class DisplayView* disp = nullptr);

  // S3 PSRAM: initialise WiFi.mode() une seule fois après 10 s depuis loop() (tryDelayedModeInit).
  // À appeler depuis loop() ; sans effet sur les autres envs.
  void tryDelayedModeInit();

  // S3 PSRAM: désactivé (init faite par tryDelayedModeInit depuis loop après 10 s).
  void startDelayedModeInitTask();

  // Nouvelles méthodes pour gestion intelligente du signal
  int8_t getCurrentRSSI() const;
  void getSignalQuality(char* buffer, size_t bufferSize) const;
  bool shouldReconnect();
  void checkConnectionStability();
  
  // Méthodes pour contrôle manuel WiFi
  bool disconnect();
  bool reconnect(class DisplayView* disp = nullptr);

 private:
  const Credential* _list;
  size_t _count;
  uint32_t _timeoutMs;

  // Intervalle minimum entre deux tentatives automatiques (ms)
  uint32_t _retryIntervalMs = 60000;
  uint32_t _lastAttemptMs = 0;

  // Gestion backoff et état
  uint32_t _baseRetryMs = 60000;
  bool _connecting = false;
  
  // Variables pour gestion intelligente du signal
  int8_t _lastRSSI = 0;
  uint32_t _lastRSSICheck = 0;
  uint32_t _weakSignalStartTime = 0;
  uint32_t _reconnectAttempts = 0;
  uint32_t _lastReconnectAttempt = 0;

#if defined(BOARD_S3)
  DNSServer _apDnsServer;
  bool _apDnsStarted = false;
#endif
};

/**
 * Helpers WiFi pour éviter les String temporaires et la duplication de code
 * v11.166: Réécrit pour utiliser buffers statiques (audit fragmentation heap)
 */
namespace WiFiHelpers {
    /**
     * Copie le SSID STA dans un buffer char[]
     * @param buffer Buffer de destination (min 33 bytes recommandé)
     * @param size Taille du buffer
     * v11.182: Réactivation - uniquement si STA connecté et mode STA/AP_STA
     */
    inline void getSSID(char* buffer, size_t size) {
        if (!buffer || size == 0) return;
        buffer[0] = '\0';
        if (WiFi.status() != WL_CONNECTED) return;
        wifi_mode_t mode = WiFi.getMode();
        if (mode != WIFI_STA && mode != WIFI_AP_STA) return;
        wifi_config_t conf;
        if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return;
        size_t len = strnlen(reinterpret_cast<const char*>(conf.sta.ssid), 32);
        if (len >= size) len = size - 1;
        memcpy(buffer, conf.sta.ssid, len);
        buffer[len] = '\0';
    }
    
    /**
     * Formate l'adresse IP locale dans un buffer char[]
     * @param buffer Buffer de destination (min 16 bytes)
     * @param size Taille du buffer
     */
    inline void getIPString(char* buffer, size_t size) {
        if (!buffer || size == 0) return;
        IPAddress ip = WiFi.localIP();
        snprintf(buffer, size, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    }
    
    /**
     * Copie l'adresse MAC dans un buffer char[] sans String temporaire
     * @param buffer Buffer de destination (min 18 bytes)
     * @param size Taille du buffer
     */
    inline void getMACString(char* buffer, size_t size) {
        if (!buffer || size < 18) {
            if (buffer && size > 0) buffer[0] = '\0';
            return;
        }
        // v11.166: Utilise la version byte array de macAddress (pas de String)
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(buffer, size, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    
    /**
     * Retourne le RSSI actuel (évite appels répétés)
     */
    inline int8_t getRSSI() {
        return WiFi.RSSI();
    }
    
    /**
     * Vérifie si connecté au WiFi STA
     */
    inline bool isConnected() {
        return WiFi.status() == WL_CONNECTED;
    }
    
    /**
     * Copie le SSID AP dans un buffer char[] sans String temporaire
     * @param buffer Buffer de destination (min 33 bytes recommandé)
     * @param size Taille du buffer
     */
    inline void getAPSSID(char* buffer, size_t size) {
        if (!buffer || size == 0) return;
        buffer[0] = '\0';
        wifi_mode_t mode = WiFi.getMode();
        if (mode != WIFI_AP && mode != WIFI_AP_STA) return;
        wifi_config_t conf;
        if (esp_wifi_get_config(WIFI_IF_AP, &conf) != ESP_OK) return;
        size_t len = conf.ap.ssid_len;
        if (len == 0) {
            len = strnlen(reinterpret_cast<const char*>(conf.ap.ssid), 32);
        }
        if (len >= size) len = size - 1;
        memcpy(buffer, conf.ap.ssid, len);
        buffer[len] = '\0';
    }
    
    /**
     * Formate l'adresse IP AP dans un buffer char[]
     * @param buffer Buffer de destination (min 16 bytes)
     * @param size Taille du buffer
     */
    inline void getAPIPString(char* buffer, size_t size) {
        if (!buffer || size == 0) return;
        IPAddress ip = WiFi.softAPIP();
        snprintf(buffer, size, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    }
    
    /**
     * Ajoute les informations WiFi STA et AP à un document JSON
     * Centralise le code de construction JSON WiFi dupliqué dans web_routes_status, realtime_websocket, etc.
     * 
     * Clés ajoutées:
     * - wifiStaConnected, wifiStaSSID, wifiStaIP, wifiStaRSSI, wifiStaMac
     * - wifiApActive, wifiApSSID, wifiApIP, wifiApClients
     * - wifiMode
     * 
     * @param doc Document JSON (ArduinoJson) où ajouter les infos
     * @param includeMac Inclure l'adresse MAC STA (optionnel, défaut true)
     */
    template<typename TDoc>
    inline void addWifiInfoToJson(TDoc& doc, bool includeMac = true) {
        // WiFi STA info
        bool staConnected = WiFi.status() == WL_CONNECTED;
        doc["wifiStaConnected"] = staConnected;
        
        if (staConnected) {
            char ssidBuf[33];
            getSSID(ssidBuf, sizeof(ssidBuf));
            // Utilise String() pour forcer ArduinoJson à copier la chaîne
            // (évite les pointeurs vers buffers locaux invalidés après retour)
            doc["wifiStaSSID"].set(ssidBuf);
            
            char ipBuf[16];
            getIPString(ipBuf, sizeof(ipBuf));
            doc["wifiStaIP"].set(ipBuf);
            
            doc["wifiStaRSSI"] = WiFi.RSSI();
        } else {
            doc["wifiStaSSID"] = "";
            doc["wifiStaIP"] = "";
            doc["wifiStaRSSI"] = 0;
        }
        
        if (includeMac) {
            char macBuf[18];
            getMACString(macBuf, sizeof(macBuf));
            doc["wifiStaMac"].set(macBuf);
        }
        
        // WiFi AP info
        wifi_mode_t mode = WiFi.getMode();
        bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
        doc["wifiApActive"] = apActive;
        
        if (apActive) {
            char apSSIDBuf[33];
            getAPSSID(apSSIDBuf, sizeof(apSSIDBuf));
            doc["wifiApSSID"].set(apSSIDBuf);
            
            char apIPBuf[16];
            getAPIPString(apIPBuf, sizeof(apIPBuf));
            doc["wifiApIP"].set(apIPBuf);
            
            doc["wifiApClients"] = WiFi.softAPgetStationNum();
        } else {
            doc["wifiApSSID"] = "";
            doc["wifiApIP"] = "";
            doc["wifiApClients"] = 0;
        }
        
        // WiFi mode string literals sont OK (constantes statiques)
        doc["wifiMode"] = (mode == WIFI_STA) ? "STA" : (mode == WIFI_AP) ? "AP" : "AP_STA";
    }
}

#endif // WIFI_MANAGER_H 