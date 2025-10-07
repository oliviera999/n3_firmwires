#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "json_pool.h"
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
    static constexpr unsigned long NO_CLIENT_HEARTBEAT_INTERVAL_MS = 30000; // 30s quand aucun client
    static constexpr unsigned long CLIENT_INACTIVITY_TIMEOUT_MS = 60000;   // 60s avant considérer inactif
    
    unsigned long lastBroadcast = 0;
    unsigned long lastClientActivity = 0;
    unsigned long lastNoClientHeartbeat = 0;
    bool isActive = false;
    bool hasActiveClients = false;
    
    // Références vers les systèmes
    SystemSensors* sensors = nullptr;
    SystemActuators* actuators = nullptr;
    bool forceWakeUpState = false; // État du Force Wakeup
    
public:
    RealtimeWebSocket() : webSocket(WS_PORT, "/ws"), mutex(xSemaphoreCreateMutex()) {
        // Configuration du serveur WebSocket avec heartbeat adaptatif
        webSocket.enableHeartbeat(15000, 3000, 2); // Heartbeat toutes les 15s quand clients connectés
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
        
        isActive = true;
        Serial.printf("[WebSocket] Serveur WebSocket démarré sur le port %d\n", WS_PORT);
        Serial.println("[WebSocket] Temps réel activé - Connexions WebSocket acceptées");
    }
    
    /**
     * Arrête le serveur WebSocket
     */
    void end() {
        if (mutex) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            isActive = false;
            xSemaphoreGive(mutex);
        }
        webSocket.close();
    }
    
    /**
     * Met à jour l'état du Force Wakeup
     */
    void updateForceWakeUpState(bool state) {
        forceWakeUpState = state;
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
                    hasActiveClients = false;
                    Serial.println("[WebSocket] Aucun client connecté - système peut entrer en veille");
                }
                break;
                
            case WStype_CONNECTED: {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[WebSocket] Client %u connecté depuis %s\n", num, ip.toString().c_str());
                
                // Notifier l'activité client et réveiller le système si nécessaire
                notifyClientActivity();
                
                // Envoyer les données actuelles au nouveau client
                sendCurrentData(num);
                break;
            }
            
            case WStype_TEXT: {
                // Traiter les messages du client
                String message = String((char*)payload);
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
    void handleClientMessage(uint8_t clientNum, const String& message) {
        // Analyser le message JSON
        ArduinoJson::DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, message);
        
        if (error) {
            Serial.printf("[WebSocket] Erreur parsing JSON du client %u: %s\n", clientNum, error.c_str());
            return;
        }
        
        // Traiter les commandes
        if (doc.containsKey("type")) {
            String type = doc["type"];
            
            if (type == "ping") {
                // Répondre au ping
                sendPong(clientNum);
            } else if (type == "subscribe") {
                // Client s'abonne aux mises à jour
                sendCurrentData(clientNum);
            } else if (type == "control" && doc.containsKey("action")) {
                // Commande de contrôle (si autorisé)
                handleControlCommand(clientNum, doc["action"]);
            }
        }
    }
    
    /**
     * Envoie les données actuelles à un client
     */
    void sendCurrentData(uint8_t clientNum) {
        if (!sensors || !actuators) return;
        
        // Utiliser le pool JSON
        ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(768);
        if (!doc) return;
        
        // Récupérer les données via le cache
        SensorReadings readings = sensorCache.getReadings(*sensors);
        
        // Construire la réponse
        (*doc)["type"] = "sensor_data";
        (*doc)["tempWater"] = readings.tempWater;
        (*doc)["tempAir"] = readings.tempAir;
        (*doc)["humidity"] = readings.humidity;
        (*doc)["wlAqua"] = readings.wlAqua;
        (*doc)["wlTank"] = readings.wlTank;
        (*doc)["wlPota"] = readings.wlPota;
        (*doc)["luminosite"] = readings.luminosite;
        (*doc)["pumpAqua"] = actuators->isAquaPumpRunning();
        (*doc)["pumpTank"] = actuators->isTankPumpRunning();
        (*doc)["heater"] = actuators->isHeaterOn();
        (*doc)["light"] = actuators->isLightOn();
        (*doc)["forceWakeup"] = forceWakeUpState;
        (*doc)["resetMode"] = 0; // resetMode est toujours 0 en temps normal
        (*doc)["timestamp"] = millis();
        
        // Informations WiFi STA
        bool staConnected = WiFi.status() == WL_CONNECTED;
        (*doc)["wifiStaConnected"] = staConnected;
        if (staConnected) {
            (*doc)["wifiStaSSID"] = WiFi.SSID();
            (*doc)["wifiStaIP"] = WiFi.localIP().toString();
            (*doc)["wifiStaRSSI"] = WiFi.RSSI();
        } else {
            (*doc)["wifiStaSSID"] = "";
            (*doc)["wifiStaIP"] = "";
            (*doc)["wifiStaRSSI"] = 0;
        }
        
        // Informations WiFi AP
        wifi_mode_t mode = WiFi.getMode();
        bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
        (*doc)["wifiApActive"] = apActive;
        if (apActive) {
            (*doc)["wifiApSSID"] = WiFi.softAPSSID();
            (*doc)["wifiApIP"] = WiFi.softAPIP().toString();
            (*doc)["wifiApClients"] = WiFi.softAPgetStationNum();
        } else {
            (*doc)["wifiApSSID"] = "";
            (*doc)["wifiApIP"] = "";
            (*doc)["wifiApClients"] = 0;
        }
        
        // Sérialiser et envoyer
        String json;
        serializeJson(*doc, json);
        webSocket.sendTXT(clientNum, json);
        
        // Libérer le document
        jsonPool.release(doc);
    }
    
    /**
     * Envoie un pong en réponse à un ping
     */
    void sendPong(uint8_t clientNum) {
        webSocket.sendTXT(clientNum, "{\"type\":\"pong\"}");
    }
    
    /**
     * Gère les commandes de contrôle
     */
    void handleControlCommand(uint8_t clientNum, const String& action) {
        // Pour l'instant, on ne permet que les commandes de lecture
        // Les commandes de contrôle peuvent être ajoutées plus tard si nécessaire
        
        if (action == "get_status") {
            sendCurrentData(clientNum);
        } else {
            // Commande non reconnue
            webSocket.sendTXT(clientNum, "{\"type\":\"error\",\"message\":\"Unknown command\"}");
        }
    }
    
    /**
     * Diffuse les données à tous les clients connectés
     * Optimisé pour la veille : réduit l'activité quand aucun client actif
     */
    void broadcastSensorData() {
        if (!isActive || !sensors || !actuators) return;
        
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
        if (hasActiveClients && (now - lastClientActivity > CLIENT_INACTIVITY_TIMEOUT_MS)) {
            // Réduire la fréquence de diffusion pour clients inactifs
            shouldBroadcast = (now - lastBroadcast > (BROADCAST_INTERVAL_MS * 4)); // 4x moins fréquent
        }
        
        if (shouldBroadcast) {
            // Utiliser le pool JSON
            ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(768);
            if (doc) {
                // Récupérer les données via le cache
                SensorReadings readings = sensorCache.getReadings(*sensors);
                
                // Construire la réponse
                (*doc)["type"] = "sensor_update";
                (*doc)["tempWater"] = readings.tempWater;
                (*doc)["tempAir"] = readings.tempAir;
                (*doc)["humidity"] = readings.humidity;
                (*doc)["wlAqua"] = readings.wlAqua;
                (*doc)["wlTank"] = readings.wlTank;
                (*doc)["wlPota"] = readings.wlPota;
                (*doc)["luminosite"] = readings.luminosite;
                (*doc)["pumpAqua"] = actuators->isAquaPumpRunning();
                (*doc)["pumpTank"] = actuators->isTankPumpRunning();
                (*doc)["heater"] = actuators->isHeaterOn();
                (*doc)["light"] = actuators->isLightOn();
                (*doc)["forceWakeup"] = forceWakeUpState;
                (*doc)["resetMode"] = 0; // resetMode est toujours 0 en temps normal
                (*doc)["timestamp"] = now;
                
                // Informations WiFi STA
                bool staConnected = WiFi.status() == WL_CONNECTED;
                (*doc)["wifiStaConnected"] = staConnected;
                if (staConnected) {
                    (*doc)["wifiStaSSID"] = WiFi.SSID();
                    (*doc)["wifiStaIP"] = WiFi.localIP().toString();
                    (*doc)["wifiStaRSSI"] = WiFi.RSSI();
                } else {
                    (*doc)["wifiStaSSID"] = "";
                    (*doc)["wifiStaIP"] = "";
                    (*doc)["wifiStaRSSI"] = 0;
                }
                
                // Informations WiFi AP
                wifi_mode_t mode = WiFi.getMode();
                bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
                (*doc)["wifiApActive"] = apActive;
                if (apActive) {
                    (*doc)["wifiApSSID"] = WiFi.softAPSSID();
                    (*doc)["wifiApIP"] = WiFi.softAPIP().toString();
                    (*doc)["wifiApClients"] = WiFi.softAPgetStationNum();
                } else {
                    (*doc)["wifiApSSID"] = "";
                    (*doc)["wifiApIP"] = "";
                    (*doc)["wifiApClients"] = 0;
                }
                
                // Sérialiser et diffuser
                String json;
                serializeJson(*doc, json);
                webSocket.broadcastTXT(json);
                
                // Libérer le document
                jsonPool.release(doc);
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
    void sendActionConfirm(const String& action, const String& result) {
        if (!isActive) return;
        
        // Message léger et ciblé pour confirmation immédiate
        String json = "{\"type\":\"action_confirm\",\"action\":\"" + action + 
                      "\",\"result\":\"" + result + "\",\"timestamp\":" + 
                      String(millis()) + "}";
        
        // Envoi direct sans mutex pour éviter les deadlocks
        webSocket.broadcastTXT(json);
        Serial.printf("[WebSocket] ✅ Confirmation action: %s = %s\n", action.c_str(), result.c_str());
    }
    
    /**
     * Diffuse immédiatement un état courant, sans attendre l'intervalle
     */
    void broadcastNow() {
        if (!isActive || !sensors || !actuators) return;
        
        // CORRECTION : Timeout au lieu de portMAX_DELAY pour éviter les deadlocks
        if (mutex) {
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                Serial.println("[WebSocket] ⚠️ Mutex timeout dans broadcastNow(), skip");
                return;
            }
        }
        // Utiliser le pool JSON
        ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(768);
        if (doc) {
            SensorReadings readings = sensorCache.getReadings(*sensors);
            (*doc)["type"] = "sensor_update";
            (*doc)["tempWater"] = readings.tempWater;
            (*doc)["tempAir"] = readings.tempAir;
            (*doc)["humidity"] = readings.humidity;
            (*doc)["wlAqua"] = readings.wlAqua;
            (*doc)["wlTank"] = readings.wlTank;
            (*doc)["wlPota"] = readings.wlPota;
            (*doc)["luminosite"] = readings.luminosite;
            (*doc)["pumpAqua"] = actuators->isAquaPumpRunning();
            (*doc)["pumpTank"] = actuators->isTankPumpRunning();
            (*doc)["heater"] = actuators->isHeaterOn();
            (*doc)["light"] = actuators->isLightOn();
            (*doc)["forceWakeup"] = forceWakeUpState;
            (*doc)["timestamp"] = millis();
            
            // Informations WiFi STA
            bool staConnected = WiFi.status() == WL_CONNECTED;
            (*doc)["wifiStaConnected"] = staConnected;
            if (staConnected) {
                (*doc)["wifiStaSSID"] = WiFi.SSID();
                (*doc)["wifiStaIP"] = WiFi.localIP().toString();
                (*doc)["wifiStaRSSI"] = WiFi.RSSI();
            } else {
                (*doc)["wifiStaSSID"] = "";
                (*doc)["wifiStaIP"] = "";
                (*doc)["wifiStaRSSI"] = 0;
            }
            
            // Informations WiFi AP
            wifi_mode_t mode = WiFi.getMode();
            bool apActive = (mode == WIFI_AP || mode == WIFI_AP_STA);
            (*doc)["wifiApActive"] = apActive;
            if (apActive) {
                (*doc)["wifiApSSID"] = WiFi.softAPSSID();
                (*doc)["wifiApIP"] = WiFi.softAPIP().toString();
                (*doc)["wifiApClients"] = WiFi.softAPgetStationNum();
            } else {
                (*doc)["wifiApSSID"] = "";
                (*doc)["wifiApIP"] = "";
                (*doc)["wifiApClients"] = 0;
            }
            
            String json; serializeJson(*doc, json);
            webSocket.broadcastTXT(json);
            jsonPool.release(doc);
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
        if (isActive) {
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
        return isActive;
    }
    
    /**
     * Notifie tous les clients d'un changement de réseau WiFi imminent
     */
    void notifyWifiChange(const String& newSSID) {
        if (!isActive) return;
        
        String message = "{\"type\":\"wifi_change\",\"ssid\":\"" + newSSID + "\",\"message\":\"Changement de réseau WiFi en cours...\"}";
        webSocket.broadcastTXT(message);
        
        // Donner le temps au message d'être envoyé
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /**
     * Ferme proprement toutes les connexions WebSocket
     */
    void closeAllConnections() {
        if (!isActive) return;
        
        Serial.println("[WebSocket] Fermeture de toutes les connexions...");
        
        // Envoyer un message de fermeture à tous les clients
        String closeMessage = "{\"type\":\"server_closing\",\"message\":\"Serveur en cours de reconfiguration\"}";
        webSocket.broadcastTXT(closeMessage);
        
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
        if (!isActive) return true;
        
        unsigned long now = millis();
        uint8_t clients = const_cast<WebSocketsServer&>(webSocket).connectedClients();
        
        // Pas de clients connectés
        if (clients == 0) return true;
        
        // Clients connectés mais inactifs depuis longtemps
        if (hasActiveClients && (now - lastClientActivity > CLIENT_INACTIVITY_TIMEOUT_MS)) {
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
        stats.isActive = isActive;
        stats.hasActiveClients = hasActiveClients;
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
extern RealtimeWebSocket realtimeWebSocket;
