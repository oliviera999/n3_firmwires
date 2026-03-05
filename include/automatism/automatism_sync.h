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

    // Traite un doc déjà récupéré (normalise, enqueue sauvegarde NVS, met à jour doc).
    // Utilisé par fetchRemoteState et par netTask au boot.
    bool processFetchedRemoteConfig(ArduinoJson::JsonDocument& doc);

    /// Draine la file de sauvegarde NVS différée (à appeler depuis automation task uniquement).
    void processDeferredRemoteVarsSave();

    // Polling régulier des commandes distantes
    bool pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis);

    // Getters d'état
    bool isServerOk() const { return _serverOk; }
    int8_t getSendState() const { return _sendState; }
    int8_t getRecvState() const { return _recvState; }
    /// Timestamp (millis) du dernier POST réussi (0 si jamais)
    unsigned long getLastSendMs() const { return _lastSend; }
    /// Raison du dernier non-envoi (diagnostic à distance)
    enum DataSkipReason { SKIP_NONE = 0, SKIP_LOW_MEMORY = 1, SKIP_NET_FAIL = 2 };
    int getLastDataSkipReason() const { return static_cast<int>(_lastDataSkipReason); }

    // Observabilité POST (pour /api/status)
    uint32_t getPostOkCount() const { return _postOkCount; }
    uint32_t getPostFailCount() const { return _postFailCount; }
    uint32_t getLastPostDurationMs() const { return _lastPostDurationMs; }

    // Configuration des seuils (synchronisés avec AutomatismNetwork)
    void setLimFlood(uint16_t val) { _limFlood = val; }
    void setAqThresholdCm(uint16_t val) { _aqThresholdCm = val; }
    void setTankThresholdCm(uint16_t val) { _tankThresholdCm = val; }
    void setHeaterThresholdC(float val) { _heaterThresholdC = val; }
    void setEmailAddress(const char* addr);
    void setEmailEnabled(bool v) { _emailEnabled = v; }
    void setFreqWakeSec(uint16_t v) { _freqWakeSec = v; }

    /// Appelé par GPIOParser après exécution nourrissage distant (ack, reset flags, email)
    void onRemoteFeedExecuted(bool isSmall, Automatism& core);
    /// Initialise l'état edge detection depuis le doc (1er poll) sans déclencher
    void seedInitialStateIfFirstPoll(const ArduinoJson::JsonDocument& doc);
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
    
    // v11.168: Flag pour éviter l'écrasement des variables de config sur le serveur distant
    // Si false, le payload contient configSynced=0 et le serveur ignore les variables de config
    bool _configSyncedOnce;

    // Configuration locale (miroir de Automatism)
    uint16_t _limFlood;
    uint16_t _aqThresholdCm;
    uint16_t _tankThresholdCm;
    float _heaterThresholdC;
    char _emailAddress[96]; // MAX_EMAIL_LENGTH
    bool _emailEnabled;
    uint16_t _freqWakeSec;

    // Gestion backoff (simplifié - backoff supprimé, retries gérés par web_client)
    uint8_t _consecutiveSendFailures;
    uint32_t _lastSendAttemptMs;

    // Observabilité POST (exposé dans /api/status)
    uint32_t _postOkCount{0};
    uint32_t _postFailCount{0};
    uint32_t _lastPostDurationMs{0};

    /// Dernière raison de non-envoi (footer mail, diagnostic à distance)
    uint8_t _lastDataSkipReason{0};  // DataSkipReason

    bool _firstPollAfterBootDone{false};

    // Détection d'inflexion marée (pic/creux wlAqua) pour POST événementiel
    int8_t _trendDir{0};              // -1 = descente, 0 = inconnu, 1 = montée
    uint16_t _extremeWlAqua{0};       // Valeur extrême candidate (pic ou creux en cours)
    uint32_t _lastInflectionPostMs{0}; // Timestamp du dernier POST d'inflexion
    
    // Constantes
    // v11.158: Réduit de 40 à 20 entrées pour simplifier et libérer espace LittleFS
    static constexpr uint16_t QUEUE_MAX_ENTRIES = 5;  // Réduit de 20 à 5 (queue RAM simple)
    static constexpr size_t MAX_PAYLOAD_BYTES = 960;
    // Fenêtre priorité serveur (10 s / 20 s nourrissage) : ne pas écraser les changements web par le POST.
    // Garder SEND_INTERVAL_MS >= 2 * REMOTE_FETCH_INTERVAL_MS pour qu'au moins un GET voie la valeur avant écrasement.
    static constexpr unsigned long SEND_INTERVAL_MS = 30000;   // 30 s
    static constexpr unsigned long REMOTE_FETCH_INTERVAL_MS = 6000;   // 6 s (poll serveur distant)
    static constexpr unsigned long REMOTE_FEED_RESET_COOLDOWN_MS = 2000;
    static constexpr uint8_t INFLECTION_NOISE_CM = 3;             // Hystérésis renversement (> bruit +/-1cm)
    static constexpr uint32_t MIN_INFLECTION_INTERVAL_MS = 15000; // Min 15s entre POSTs d'inflexion

    // Helpers
    bool canAttemptSend(uint32_t nowMs) const;
    void registerSendResult(bool success, size_t payloadBytes, uint32_t durationMs, uint32_t heapBefore, uint32_t heapAfter);
    bool checkInflectionPoint(uint16_t wlAqua, uint32_t nowMs);
};

