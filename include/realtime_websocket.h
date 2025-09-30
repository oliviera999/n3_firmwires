#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "json_pool.h"
#include "sensor_cache.h"
#include "system_sensors.h"
#include "system_actuators.h"

/**
 * Serveur WebSocket pour les mises à jour temps réel
 * Compatible ESP32 et ESP32-S3
 */
class RealtimeWebSocket {
private:
    WebSocketsServer webSocket;
    SemaphoreHandle_t mutex;
    
    // Configuration selon le type de board
    static constexpr uint16_t WS_PORT = 81;
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
    
    unsigned long lastBroadcast = 0;
    bool isActive = false;
    
    // Références vers les systèmes
    SystemSensors* sensors = nullptr;
    SystemActuators* actuators = nullptr;
    
public:
    RealtimeWebSocket() : webSocket(WS_PORT, "/ws"), mutex(xSemaphoreCreateMutex()) {
        // Configuration du serveur WebSocket
        webSocket.enableHeartbeat(15000, 3000, 2); // Heartbeat toutes les 15s
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
     * Traite les événements WebSocket
     */
    void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                Serial.printf("[WebSocket] Client %u déconnecté\n", num);
                break;
                
            case WStype_CONNECTED: {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[WebSocket] Client %u connecté depuis %s\n", num, ip.toString().c_str());
                
                // Envoyer les données actuelles au nouveau client
                sendCurrentData(num);
                break;
            }
            
            case WStype_TEXT: {
                // Traiter les messages du client
                String message = String((char*)payload);
                handleClientMessage(num, message);
                break;
            }
            
            case WStype_PING:
                // Le client a envoyé un ping
                break;
                
            case WStype_PONG:
                // Le client a répondu au ping
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
        ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
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
        (*doc)["voltage"] = readings.voltageMv;
        (*doc)["timestamp"] = millis();
        
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
     */
    void broadcastSensorData() {
        if (!isActive || !sensors || !actuators) return;
        
        if (mutex) {
            xSemaphoreTake(mutex, portMAX_DELAY);
        }
        
        unsigned long now = millis();
        if (now - lastBroadcast > BROADCAST_INTERVAL_MS) {
            if (webSocket.connectedClients() > 0) {
                // Utiliser le pool JSON
                ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
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
                    (*doc)["voltage"] = readings.voltageMv;
                    (*doc)["timestamp"] = now;
                    
                    // Sérialiser et diffuser
                    String json;
                    serializeJson(*doc, json);
                    webSocket.broadcastTXT(json);
                    
                    // Libérer le document
                    jsonPool.release(doc);
                }
            }
            lastBroadcast = now;
        }
        
        if (mutex) {
            xSemaphoreGive(mutex);
        }
    }

    /**
     * Diffuse immédiatement un état courant, sans attendre l'intervalle
     */
    void broadcastNow() {
        if (!isActive || !sensors || !actuators) return;
        if (mutex) {
            xSemaphoreTake(mutex, portMAX_DELAY);
        }
        // Utiliser le pool JSON
        ArduinoJson::DynamicJsonDocument* doc = jsonPool.acquire(512);
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
            (*doc)["voltage"] = readings.voltageMv;
            (*doc)["voltageV"] = (float)readings.voltageMv / 1000.0f;
            (*doc)["timestamp"] = millis();
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
     * Obtient les statistiques du serveur WebSocket
     */
    struct WebSocketStats {
        uint8_t connectedClients;
        bool isActive;
        unsigned long lastBroadcast;
        unsigned long broadcastInterval;
    };
    
    WebSocketStats getStats() const {
        WebSocketStats stats;
        stats.connectedClients = const_cast<WebSocketsServer&>(webSocket).connectedClients();
        stats.isActive = isActive;
        
        if (mutex) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            stats.lastBroadcast = lastBroadcast;
            xSemaphoreGive(mutex);
        }
        
        stats.broadcastInterval = BROADCAST_INTERVAL_MS;
        return stats;
    }
};

// Instance globale du serveur WebSocket temps réel
extern RealtimeWebSocket realtimeWebSocket;
