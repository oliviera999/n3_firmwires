#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "web_client.h"
#include "config_manager.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "data_queue.h"
#include "config.h"

// Forward declaration
class Automatism;

class AutomatismSync {
public:
    AutomatismSync(WebClient& web, ConfigManager& cfg);

    bool begin();

    // Méthode principale appelée par Automatism::update()
    // Gère la synchronisation périodique
    void update(const SensorReadings& readings, SystemActuators& acts, Automatism& core);

    // Envoi immédiat (déclenché par événement)
    bool sendFullUpdate(const SensorReadings& readings,
                        SystemActuators& acts,
                        Automatism& core,
                        const char* extraPairs = nullptr);

    // Récupération état distant
    bool fetchRemoteState(ArduinoJson::JsonDocument& doc);
    
    // Polling régulier des commandes distantes
    bool pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis);

    // Getters d'état
    bool isServerOk() const { return _serverOk; }
    int8_t getSendState() const { return _sendState; }
    int8_t getRecvState() const { return _recvState; }

    // Configuration des seuils (synchronisés avec AutomatismNetwork)
    void setLimFlood(uint16_t val) { _limFlood = val; }
    void setAqThresholdCm(uint16_t val) { _aqThresholdCm = val; }
    void setTankThresholdCm(uint16_t val) { _tankThresholdCm = val; }
    void setHeaterThresholdC(float val) { _heaterThresholdC = val; }
    void setEmailAddress(const char* addr);
    void setEmailEnabled(bool v) { _emailEnabled = v; }
    void setFreqWakeSec(uint16_t v) { _freqWakeSec = v; }

    // Nouvelles méthodes pour gérer les commandes distantes
    void handleRemoteFeedingCommands(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl);
    void applyConfigFromJson(const ArduinoJson::JsonDocument& doc);

    // Accesseurs configuration
    uint16_t getLimFlood() const { return _limFlood; }
    uint16_t getAqThresholdCm() const { return _aqThresholdCm; }
    uint16_t getTankThresholdCm() const { return _tankThresholdCm; }
    float getHeaterThresholdC() const { return _heaterThresholdC; }
    const char* getEmailAddress() const { return _emailAddress; }
    bool isEmailEnabled() const { return _emailEnabled; }
    uint16_t getFreqWakeSec() const { return _freqWakeSec; }

    // Rejeu des données en queue
    uint16_t replayQueuedData();

    // ACK commandes
    bool sendCommandAck(const char* command, const char* status);
    void logRemoteCommandExecution(const char* command, bool success);

private:
    WebClient& _web;
    ConfigManager& _config;
    DataQueue _dataQueue;

    // État
    bool _serverOk;
    int8_t _sendState;
    int8_t _recvState;
    unsigned long _lastSend;
    unsigned long _lastRemoteFetch;
    unsigned long _lastRemoteFeedResetMs;

    // Configuration locale (miroir de Automatism)
    uint16_t _limFlood;
    uint16_t _aqThresholdCm;
    uint16_t _tankThresholdCm;
    float _heaterThresholdC;
    char _emailAddress[96]; // MAX_EMAIL_LENGTH
    bool _emailEnabled;
    uint16_t _freqWakeSec;

    // Gestion backoff
    uint8_t _consecutiveSendFailures;
    uint32_t _currentBackoffMs;
    uint32_t _lastSendAttemptMs;
    
    // Constantes
    static constexpr uint16_t QUEUE_MAX_ENTRIES = 40;
    static constexpr size_t MAX_PAYLOAD_BYTES = 960;
    static constexpr unsigned long SEND_INTERVAL_MS = 120000;
    static constexpr unsigned long REMOTE_FETCH_INTERVAL_MS = 12000; // 12 secondes (optimisation polling)
    static constexpr unsigned long REMOTE_FEED_RESET_COOLDOWN_MS = 2000;

    // Helpers
    bool canAttemptSend(uint32_t nowMs) const;
    void registerSendResult(bool success, size_t payloadBytes, uint32_t durationMs, uint32_t heapBefore, uint32_t heapAfter);
    void logQueueState(const char* reason, uint16_t size) const;
};

