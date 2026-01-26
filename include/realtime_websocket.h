#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <cstring>
// #include "json_pool.h" - Supprimé
#include "config.h"  // Pour BufferConfig::JSON_DOCUMENT_SIZE
#include "sensor_cache.h"
#include "system_sensors.h"
#include "system_actuators.h"

// Forward declaration
class Automatism;

/**
 * Serveur WebSocket pour les mises à jour temps réel
 * Compatible ESP32 et ESP32-S3
 */
class RealtimeWebSocket {
private:
    WebSocketsServer webSocket;
    SemaphoreHandle_t mutex;
    
    // Configuration selon le type de board
    static constexpr uint16_t WS_PORT = 81;  // Port WebSocket dédié
    static constexpr size_t MAX_CLIENTS = 
        #ifdef BOARD_S3
        8;  // ESP32-S3 : plus de connexions simultanées
        #else
        4;  // ESP32-WROOM : connexions limitées
        #endif
    
    static constexpr unsigned long BROADCAST_INTERVAL_MS = 
        #ifdef BOARD_S3
        250;  // ESP32-S3 : mises à jour très fréquentes
        #else
        500; // ESP32-WROOM : mises à jour optimisées
        #endif
    
    // Configuration pour l'optimisation veille
    // 30s quand aucun client
    static constexpr unsigned long NO_CLIENT_HEARTBEAT_INTERVAL_MS = 30000;
    // 60s avant considérer inactif
    static constexpr unsigned long CLIENT_INACTIVITY_TIMEOUT_MS = 60000;
    
    unsigned long lastBroadcast = 0;
    unsigned long lastClientActivity = 0;
    unsigned long lastNoClientHeartbeat = 0;
    bool _isActive = false;
    bool _hasActiveClients = false;
    
    // Références vers les systèmes
    SystemSensors* sensors = nullptr;
    SystemActuators* actuators = nullptr;
    bool _forceWakeUpState = false; // État du Force Wakeup
    
public:
    RealtimeWebSocket() : webSocket(WS_PORT, "/ws"), mutex(xSemaphoreCreateMutex()) {
        // Configuration du serveur WebSocket avec heartbeat adaptatif
        // Heartbeat toutes les 15s quand clients connectés
        webSocket.enableHeartbeat(15000, 3000, 2);
    }
    
    ~RealtimeWebSocket() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initialise le serveur WebSocket
     * @param sensors Référence vers le système de capteurs
     * @param actuators Référence vers le système d'actionneurs
     */
    void begin(SystemSensors& sensors, SystemActuators& actuators) {
        this->sensors = &sensors;
        this->actuators = &actuators;
        
        webSocket.begin();
        webSocket.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
            this->handleWebSocketEvent(num, type, payload, length);
        });
        
        _isActive = true;
        Serial.printf("[WebSocket] Serveur WebSocket démarré sur le port %d\n", WS_PORT);
        Serial.println("[WebSocket] Temps réel activé - Connexions WebSocket acceptées");
    }
    
    /**
     * Arrête le serveur WebSocket
     */
    void end() {
        if (mutex) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            _isActive = false;
            xSemaphoreGive(mutex);
        }
        webSocket.close();
    }
    
    /**
     * Met à jour l'état du Force Wakeup
     */
    void updateForceWakeUpState(bool state) {
        _forceWakeUpState = state;
    }
    
    /**
     * Traite les événements WebSocket
     */
    void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                Serial.printf("[WebSocket] Client %u déconnecté\n", num);
                // Vérifier s'il reste des clients actifs
                if (webSocket.connectedClients() == 0) {
                    _hasActiveClients = false;
                    Serial.println(
                      "[WebSocket] Aucun client connecté - système peut entrer en veille");
                }
                break;
                
            case WStype_CONNECTED: {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[WebSocket] Client %u connecté depuis %d.%d.%d.%d\n",
                              num, ip[0], ip[1], ip[2], ip[3]);
                
                // Notifier l'activité client et réveiller le système si nécessaire
                notifyClientActivity();
                
                // Envoyer les données actuelles au nouveau client
                sendCurrentData(num);
                break;
            }
            
            case WStype_TEXT: {
                // Traiter les messages du client
                char message[512];
                size_t msgLen = length < sizeof(message) - 1 ? length : sizeof(message) - 1;
                memcpy(message, payload, msgLen);
                message[msgLen] = '\0';
                notifyClientActivity(); // Notifier l'activité client
                handleClientMessage(num, message);
                break;
            }
            
            case WStype_PING:
                // Le client a envoyé un ping
                notifyClientActivity(); // Notifier l'activité client
                break;
                
            case WStype_PONG:
                // Le client a répondu au ping
                notifyClientActivity(); // Notifier l'activité client
                break;
                
            default:
                break;
        }
    }
    
    /**
     * Gère les messages du client
     */
    void handleClientMessage(uint8_t clientNum, const char* message) {
        // Analyser le message JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, message);
        
        if (error) {
            Serial.printf("[WebSocket] Erreur parsing JSON du client %u: %s\n",
                          clientNum, error.c_str());
            return;
        }
        
        // Traiter les commandes
        if (doc["type"].is<const char*>()) {
            const char* type = doc["type"].as<const char*>();
            
            if (type && strcmp(type, "ping") == 0) {
                // Répondre au ping
                sendPong(clientNum);
            } else if (type && strcmp(type, "subscribe") == 0) {
                // Client s'abonne aux mises à jour
                sendCurrentData(clientNum);
            } else if (type && strcmp(type, "control") == 0 && doc["action"].is<const char*>()) {
                // Commande de contrôle (si autorisé)
                const char* action = doc["action"].as<const char*>();
                if (action) {
                    handleControlCommand(clientNum, action);
                }
            }
        }
    }
    
    /**
     * Envoie les données actuelles à un client
     */
    void sendCurrentData(uint8_t clientNum) {
        if (!sensors || !actuators) return;
        
        // Utiliser allocation statique pour éviter fragmentation mémoire
        StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
        
        // Récupérer les données via le cache
        SensorReadings readings = sensorCache.getReadings(*sensors);
        
        // Construire la réponse
        doc["type"] = "sensor_data";
        doc["tempWater"] = readings.tempWater;
        doc["tempAir"] = readings.tempAir;
        doc["humidity"] = readings.humidity;
        doc["wlAqua"] = readings.wlAqua;
        doc["wlTank"] = readings.wlTank;
        doc["wlPota"] = readings.wlPota;
        doc["luminosite"] = readings.luminosite;
        doc["pumpAqua"] = actuators->isAquaPumpRunning();
        doc["pumpTank"] = actuators->isTankPumpRunning();
        doc["heater"] = actuators->isHeaterOn();
        doc["light"] = actuators->isLightOn();
        doc["forceWakeup"] = _forceWakeUpState;
        doc["resetMode"] = 0; // resetMode est toujours 0 en temps normal
        doc["timestamp"] = millis();
        
        // Informations WiFi STA
        bool staConnected = WiFi.status() == WL_CONNECTED;
        doc["wifiStaConnected"] = staConnected;
        if (staConnected) {
            char staSSIDBuf[33];
            strncpy(staSSIDBuf, WiFi.SSID().c_str(), sizeof(staSSIDBuf) - 1);
            staSSIDBuf[sizeof(staSSIDBuf) - 1] = '\0';
            doc["wifiStaSSID"] = staSSIDBuf;
            IPAddress staIP = WiFi.localIP();
            char staIPBuf[16];
            snprintf(staIPBuf, sizeof(staIPBuf), "%d.%d.%d.%d",
                     staIP[0], staIP[1], staIP[2], staIP[3]);
            doc["wifiStaIP"] = staIPBuf;
            doc["wifiStaRSSI"] = WiFi.RSSI();
        } else {
            doc["wifiStaSSID"] = "";
            doc["wifiStaIP"] = "";
            doc["wifiStaRSSI"] = 0;
        }
        
        // Informations WiFi AP
        wifi_mode_t mode = WiFi.getMode();
        bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
        doc["wifiApActive"] = apActive;
        if (apActive) {
            char apSSIDBuf[33];
            strncpy(apSSIDBuf, WiFi.softAPSSID().c_str(), sizeof(apSSIDBuf) - 1);
            apSSIDBuf[sizeof(apSSIDBuf) - 1] = '\0';
            doc["wifiApSSID"] = apSSIDBuf;
            IPAddress apIP = WiFi.softAPIP();
            char apIPBuf[16];
            snprintf(apIPBuf, sizeof(apIPBuf), "%d.%d.%d.%d",
                     apIP[0], apIP[1], apIP[2], apIP[3]);
            doc["wifiApIP"] = apIPBuf;
            doc["wifiApClients"] = WiFi.softAPgetStationNum();
        } else {
            doc["wifiApSSID"] = "";
            doc["wifiApIP"] = "";
            doc["wifiApClients"] = 0;
        }
        
        // Sérialiser et envoyer
        char json[1024];
        serializeJson(doc, json, sizeof(json));
        // Vérifier qu'il y a des clients connectés avant envoi
        if (webSocket.connectedClients() > 0) {
            webSocket.sendTXT(clientNum, json);
        }
    }
    
    /**
     * Envoie un pong en réponse à un ping
     */
    void sendPong(uint8_t clientNum) {
        // Vérifier qu'il y a des clients connectés avant envoi
        if (webSocket.connectedClients() > 0) {
            webSocket.sendTXT(clientNum, "{\"type\":\"pong\"}");
        }
    }
    
    /**
     * Gère les commandes de contrôle
     */
    void handleControlCommand(uint8_t clientNum, const char* action) {
        // Pour l'instant, on ne permet que les commandes de lecture
        // Les commandes de contrôle peuvent être ajoutées plus tard si nécessaire
        
        if (action && strcmp(action, "get_status") == 0) {
            sendCurrentData(clientNum);
        } else {
            // Commande non reconnue
            // Vérifier qu'il y a des clients connectés avant envoi
            if (webSocket.connectedClients() > 0) {
                webSocket.sendTXT(clientNum,
                                  "{\"type\":\"error\",\"message\":\"Unknown command\"}");
            }
        }
    }
    
    /**
     * Diffuse les données à tous les clients connectés
     * Optimisé pour la veille : réduit l'activité quand aucun client actif
     */
    void broadcastSensorData() {
        if (!_isActive || !sensors || !actuators) return;
        
        if (mutex) {
            xSemaphoreTake(mutex, portMAX_DELAY);
        }
        
        unsigned long now = millis();
        uint8_t connectedClients = webSocket.connectedClients();
        
        // Optimisation : pas de diffusion si aucun client connecté
        if (connectedClients == 0) {
            // Heartbeat minimal pour maintenir le serveur actif
            if (now - lastNoClientHeartbeat > NO_CLIENT_HEARTBEAT_INTERVAL_MS) {
                lastNoClientHeartbeat = now;
                // Pas de diffusion, juste un heartbeat interne
            }
            if (mutex) {
                xSemaphoreGive(mutex);
            }
            return;
        }
        
        // Vérifier si on doit diffuser selon l'intervalle normal
        bool shouldBroadcast = (now - lastBroadcast > BROADCAST_INTERVAL_MS);
        
        // Si clients connectés mais inactifs, réduire la fréquence
        if (_hasActiveClients && (now - lastClientActivity > CLIENT_INACTIVITY_TIMEOUT_MS)) {
            // Réduire la fréquence de diffusion pour clients inactifs
            // 4x moins fréquent
            shouldBroadcast = (now - lastBroadcast > (BROADCAST_INTERVAL_MS * 4));
        }
        
        if (shouldBroadcast) {
            // Utiliser allocation standard
            JsonDocument doc;
            
            // Récupérer les données via le cache
            SensorReadings readings = sensorCache.getReadings(*sensors);
            
            // Construire la réponse
            doc["type"] = "sensor_update";
            doc["tempWater"] = readings.tempWater;
            doc["tempAir"] = readings.tempAir;
            doc["humidity"] = readings.humidity;
            doc["wlAqua"] = readings.wlAqua;
            doc["wlTank"] = readings.wlTank;
            doc["wlPota"] = readings.wlPota;
            doc["luminosite"] = readings.luminosite;
            doc["pumpAqua"] = actuators->isAquaPumpRunning();
            doc["pumpTank"] = actuators->isTankPumpRunning();
            doc["heater"] = actuators->isHeaterOn();
            doc["light"] = actuators->isLightOn();
            doc["forceWakeup"] = _forceWakeUpState;
            doc["resetMode"] = 0; // resetMode est toujours 0 en temps normal
            doc["timestamp"] = now;
            
            // Informations WiFi STA
            bool staConnected = WiFi.status() == WL_CONNECTED;
            doc["wifiStaConnected"] = staConnected;
            if (staConnected) {
                char staSSIDBuf[33];
                strncpy(staSSIDBuf, WiFi.SSID().c_str(), sizeof(staSSIDBuf) - 1);
                staSSIDBuf[sizeof(staSSIDBuf) - 1] = '\0';
                doc["wifiStaSSID"] = staSSIDBuf;
                IPAddress staIP = WiFi.localIP();
                char staIPBuf[16];
                snprintf(staIPBuf, sizeof(staIPBuf), "%d.%d.%d.%d",
                     staIP[0], staIP[1], staIP[2], staIP[3]);
                doc["wifiStaIP"] = staIPBuf;
                doc["wifiStaRSSI"] = WiFi.RSSI();
            } else {
                doc["wifiStaSSID"] = "";
                doc["wifiStaIP"] = "";
                doc["wifiStaRSSI"] = 0;
            }
            
            // Informations WiFi AP
            wifi_mode_t mode = WiFi.getMode();
            bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
            doc["wifiApActive"] = apActive;
            if (apActive) {
                char apSSIDBuf[33];
                strncpy(apSSIDBuf, WiFi.softAPSSID().c_str(), sizeof(apSSIDBuf) - 1);
                apSSIDBuf[sizeof(apSSIDBuf) - 1] = '\0';
                doc["wifiApSSID"] = apSSIDBuf;
                IPAddress apIP = WiFi.softAPIP();
                char apIPBuf[16];
                snprintf(apIPBuf, sizeof(apIPBuf), "%d.%d.%d.%d",
                     apIP[0], apIP[1], apIP[2], apIP[3]);
                doc["wifiApIP"] = apIPBuf;
                doc["wifiApClients"] = WiFi.softAPgetStationNum();
            } else {
                doc["wifiApSSID"] = "";
                doc["wifiApIP"] = "";
                doc["wifiApClients"] = 0;
            }
            
            // Sérialiser et diffuser
            char json[1024];
            serializeJson(doc, json, sizeof(json));
            // Vérifier qu'il y a des clients connectés avant envoi
            if (webSocket.connectedClients() > 0) {
                webSocket.broadcastTXT(json);
            }
            
            lastBroadcast = now;
        }
        
        if (mutex) {
            xSemaphoreGive(mutex);
        }
    }

    /**
     * Envoie une confirmation d'action immédiate (pour éviter les timeouts)
     */
    void sendActionConfirm(const char* action, const char* result) {
        if (!_isActive) return;
        
        // Message léger et ciblé pour confirmation immédiate
        char json[256];
        snprintf(json, sizeof(json), 
                 "{\"type\":\"action_confirm\",\"action\":\"%s\","
                 "\"result\":\"%s\",\"timestamp\":%lu}",
                 action ? action : "", result ? result : "", millis());
        
        // Envoi direct sans mutex pour éviter les deadlocks
        // Vérifier qu'il y a des clients connectés avant envoi
        if (webSocket.connectedClients() > 0) {
            webSocket.broadcastTXT(json);
        }
        Serial.printf("[WebSocket] ✅ Confirmation action: %s = %s\n",
                      action ? action : "(null)",
                      result ? result : "(null)");
    }
    
    /**
     * Diffuse immédiatement un état courant, sans attendre l'intervalle
     */
    void broadcastNow() {
        if (!_isActive || !sensors || !actuators) return;
        
        // CORRECTION : Timeout au lieu de portMAX_DELAY pour éviter les deadlocks
        if (mutex) {
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                Serial.println("[WebSocket] ⚠️ Mutex timeout dans broadcastNow(), skip");
                return;
            }
        }
        // Utiliser allocation standard
        JsonDocument doc;
        
        SensorReadings readings = sensorCache.getReadings(*sensors);
        doc["type"] = "sensor_update";
        doc["tempWater"] = readings.tempWater;
        doc["tempAir"] = readings.tempAir;
        doc["humidity"] = readings.humidity;
        doc["wlAqua"] = readings.wlAqua;
        doc["wlTank"] = readings.wlTank;
        doc["wlPota"] = readings.wlPota;
        doc["luminosite"] = readings.luminosite;
        doc["pumpAqua"] = actuators->isAquaPumpRunning();
        doc["pumpTank"] = actuators->isTankPumpRunning();
        doc["heater"] = actuators->isHeaterOn();
        doc["light"] = actuators->isLightOn();
        doc["forceWakeup"] = _forceWakeUpState;
        doc["timestamp"] = millis();
        
        // Informations WiFi STA
        bool staConnected = WiFi.status() == WL_CONNECTED;
        doc["wifiStaConnected"] = staConnected;
        if (staConnected) {
            char staSSIDBuf[33];
            strncpy(staSSIDBuf, WiFi.SSID().c_str(), sizeof(staSSIDBuf) - 1);
            staSSIDBuf[sizeof(staSSIDBuf) - 1] = '\0';
            doc["wifiStaSSID"] = staSSIDBuf;
            IPAddress staIP = WiFi.localIP();
            char staIPBuf[16];
            snprintf(staIPBuf, sizeof(staIPBuf), "%d.%d.%d.%d",
                     staIP[0], staIP[1], staIP[2], staIP[3]);
            doc["wifiStaIP"] = staIPBuf;
            doc["wifiStaRSSI"] = WiFi.RSSI();
        } else {
            doc["wifiStaSSID"] = "";
            doc["wifiStaIP"] = "";
            doc["wifiStaRSSI"] = 0;
        }
        
        // Informations WiFi AP
        wifi_mode_t mode = WiFi.getMode();
        bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
        doc["wifiApActive"] = apActive;
        if (apActive) {
            char apSSIDBuf[33];
            strncpy(apSSIDBuf, WiFi.softAPSSID().c_str(), sizeof(apSSIDBuf) - 1);
            apSSIDBuf[sizeof(apSSIDBuf) - 1] = '\0';
            doc["wifiApSSID"] = apSSIDBuf;
            IPAddress apIP = WiFi.softAPIP();
            char apIPBuf[16];
            snprintf(apIPBuf, sizeof(apIPBuf), "%d.%d.%d.%d",
                     apIP[0], apIP[1], apIP[2], apIP[3]);
            doc["wifiApIP"] = apIPBuf;
            doc["wifiApClients"] = WiFi.softAPgetStationNum();
        } else {
            doc["wifiApSSID"] = "";
            doc["wifiApIP"] = "";
            doc["wifiApClients"] = 0;
        }
        
        char json[1024];
        serializeJson(doc, json, sizeof(json));
        // Vérifier qu'il y a des clients connectés avant envoi
        if (webSocket.connectedClients() > 0) {
            webSocket.broadcastTXT(json);
        }
        
        lastBroadcast = millis();
        if (mutex) {
            xSemaphoreGive(mutex);
        }
    }
    
    /**
     * Traite les boucles du serveur WebSocket
     */
    void loop() {
        if (_isActive) {
            webSocket.loop();
        }
    }
    
    /**
     * Obtient le nombre de clients connectés
     */
    uint8_t getConnectedClients() const {
        return const_cast<WebSocketsServer&>(webSocket).connectedClients();
    }
    
    /**
     * Vérifie si le serveur est actif
     */
    bool isRunning() const {
        return _isActive;
    }
    
    /**
     * Notifie tous les clients d'un changement de réseau WiFi imminent
     */
    void notifyWifiChange(const char* newSSID) {
        if (!_isActive) return;
        
        // Déterminer le type de message selon le contenu
        const char* msgType = "wifi_change";
        char message[512];
        
        if (newSSID && strstr(newSSID, "sleep")) {
            // Message de sleep
            msgType = "server_closing";
            snprintf(message, sizeof(message),
                     "{\"type\":\"server_closing\",\"message\":\"%s\"}",
                     newSSID ? newSSID : "");
        } else {
            // Message de changement WiFi normal
            snprintf(message, sizeof(message), 
                     "{\"type\":\"wifi_change\",\"ssid\":\"%s\","
                     "\"message\":\"Changement de réseau WiFi en cours...\"}",
                     newSSID ? newSSID : "");
        }
        
        // Vérifier qu'il y a des clients connectés avant envoi
        if (webSocket.connectedClients() > 0) {
            webSocket.broadcastTXT(message);
        }
        Serial.printf("[WebSocket] 📤 Notification envoyée aux clients: %s\n", msgType);
        
        // Donner le temps au message d'être envoyé
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    
    /**
     * Ferme proprement toutes les connexions WebSocket
     */
    void closeAllConnections() {
        if (!_isActive) return;
        
        Serial.println("[WebSocket] Fermeture de toutes les connexions...");
        
        // Envoyer un message de fermeture à tous les clients
        const char* closeMessage = 
          "{\"type\":\"server_closing\","
          "\"message\":\"Serveur en cours de reconfiguration\"}";
        // Vérifier qu'il y a des clients connectés avant envoi
        if (webSocket.connectedClients() > 0) {
            webSocket.broadcastTXT(closeMessage);
        }
        
        // Donner le temps au message d'être envoyé
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // Fermer toutes les connexions
        webSocket.disconnect();
        
        // Attendre que les connexions soient fermées
        vTaskDelay(pdMS_TO_TICKS(100));
        
        Serial.println("[WebSocket] Toutes les connexions fermées");
    }
    
    /**
     * Vérifie si le système peut entrer en veille
     * @return true si aucun client actif et pas d'activité récente
     */
    bool canEnterSleep() const {
        if (!_isActive) return true;
        
        unsigned long now = millis();
        uint8_t clients = const_cast<WebSocketsServer&>(webSocket).connectedClients();
        
        // Pas de clients connectés
        if (clients == 0) return true;
        
        // Clients connectés mais inactifs depuis longtemps
        if (_hasActiveClients && (now - lastClientActivity > CLIENT_INACTIVITY_TIMEOUT_MS)) {
            return true;
        }
        
        return false;
    }
    
    /**
     * Notifie une activité client (pour maintenir l'éveil)
     */
    void notifyClientActivity();
    
    /**
     * Obtient les statistiques du serveur WebSocket
     */
    struct WebSocketStats {
        uint8_t connectedClients;
        bool isActive;
        bool hasActiveClients;
        unsigned long lastBroadcast;
        unsigned long lastClientActivity;
        unsigned long broadcastInterval;
        bool canSleep;
    };
    
    WebSocketStats getStats() const {
        WebSocketStats stats;
        stats.connectedClients = const_cast<WebSocketsServer&>(webSocket).connectedClients();
        stats.isActive = _isActive;
        stats.hasActiveClients = _hasActiveClients;
        stats.canSleep = canEnterSleep();
        
        if (mutex) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            stats.lastBroadcast = lastBroadcast;
            stats.lastClientActivity = lastClientActivity;
            xSemaphoreGive(mutex);
        }
        
        stats.broadcastInterval = BROADCAST_INTERVAL_MS;
        return stats;
    }
};

// Instance globale du serveur WebSocket temps réel
extern RealtimeWebSocket g_realtimeWebSocket;
