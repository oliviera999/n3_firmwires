#pragma once

#include <Arduino.h>
#include "web_client.h"
#include "config_manager.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "data_queue.h"
#include <ArduinoJson.h>
#include <Preferences.h>

/**
 * Module Network pour Automatism
 * 
 * Responsabilité: Communication avec serveur distant
 * - Récupération état serveur (fetchRemoteState)
 * - Envoi mises à jour (sendFullUpdate)
 * - Application configuration serveur (applyConfigFromJson)
 * - Gestion état distant (handleRemoteState) 
 * - Détection changements critiques (checkCriticalChanges)
 */
class AutomatismNetwork {
public:
    /**
     * Constructeur
     * @param web Référence WebClient
     * @param cfg Référence ConfigManager
     */
    AutomatismNetwork(WebClient& web, ConfigManager& cfg);
    
    /**
     * Initialisation du module (après montage LittleFS)
     * DOIT être appelé dans setup() après LittleFS.begin()
     * @return true si succès, false sinon
     */
    bool begin();
    
    // === COMMUNICATION SERVEUR ===
    
    /**
     * Récupère l'état depuis le serveur distant
     * @param doc Document JSON à remplir
     * @return true si succès
     */
    bool fetchRemoteState(ArduinoJson::JsonDocument& doc);
    
    /**
     * Envoie une mise à jour complète au serveur
     * @param readings Lectures capteurs
     * @param acts Actionneurs (états)
     * @param diffMaree Différence marée
     * @param bouffeMatin Heure bouffe matin
     * @param bouffeMidi Heure bouffe midi
     * @param bouffeSoir Heure bouffe soir
     * @param tempsGros Durée gros poissons
     * @param tempsPetits Durée petits poissons
     * @param bouffePetits Flag petits
     * @param bouffeGros Flag gros
     * @param forceWakeUp Flag wake
     * @param freqWakeSec Fréquence wake
     * @param refillDurationSec Durée refill
     * @param extraPairs Paramètres supplémentaires
     * @return true si succès
     */
    bool sendFullUpdate(
        const SensorReadings& readings,
        SystemActuators& acts,
        int diffMaree,
        uint8_t bouffeMatin, uint8_t bouffeMidi, uint8_t bouffeSoir,
        uint16_t tempsGros, uint16_t tempsPetits,
        const String& bouffePetits, const String& bouffeGros,
        bool forceWakeUp, uint16_t freqWakeSec,
        uint32_t refillDurationSec,
        const char* extraPairs = nullptr
    );
    
    /**
     * Applique la configuration depuis un document JSON
     * @param doc Document JSON de configuration
     */
    void applyConfigFromJson(const ArduinoJson::JsonDocument& doc);
    
    // === REMOTE STATE MANAGEMENT (divisé en sous-méthodes) ===
    
    /**
     * Polling serveur distant + cache + UI
     * @param doc Document JSON à remplir
     * @param currentMillis Temps actuel (millis)
     * @param autoCtrl Référence vers Automatism pour accès état
     * @return true si données reçues, false sinon
     */
    bool pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis, class Automatism& autoCtrl);
    
    /**
     * Gère les commandes reset distantes
     * @param doc Document JSON reçu
     * @param autoCtrl Référence vers Automatism
     * @return true si reset exécuté (ESP va redémarrer)
     */
    bool handleResetCommand(const ArduinoJson::JsonDocument& doc, class Automatism& autoCtrl);
    
    /**
     * Parse et applique la configuration distante
     * @param doc Document JSON reçu
     * @param autoCtrl Référence vers Automatism
     */
    void parseRemoteConfig(const ArduinoJson::JsonDocument& doc, class Automatism& autoCtrl);
    
    /**
     * Gère les commandes de nourrissage manuel distant
     * @param doc Document JSON reçu
     * @param autoCtrl Référence vers Automatism
     */
    void handleRemoteFeedingCommands(const ArduinoJson::JsonDocument& doc, class Automatism& autoCtrl);
    
    /**
     * Gère les actionneurs et GPIO dynamiques distants
     * @param doc Document JSON reçu
     * @param autoCtrl Référence vers Automatism
     */
    void handleRemoteActuators(const ArduinoJson::JsonDocument& doc, class Automatism& autoCtrl);
    
    /**
     * Détecte les changements critiques depuis le serveur
     * Méthode complexe (~285 lignes)
     * @param readings Lectures capteurs actuelles
     */
    void checkCriticalChanges(const SensorReadings& readings);
    
    // === PERSISTANCE ET ACK (v11.31) ===
    
    /**
     * Rejoue les données en attente depuis la queue
     * Appel automatique après envoi réussi
     * @return Nombre de payloads rejoués
     */
    uint16_t replayQueuedData();
    
    /**
     * Envoie un ACK immédiat au serveur pour une commande exécutée
     * @param command Nom de la commande ("bouffePetits", "pump_tank", etc.)
     * @param status État de la commande ("executed", "on", "off", etc.)
     * @return true si ACK envoyé avec succès
     */
    bool sendCommandAck(const char* command, const char* status);
    
    /**
     * Log une exécution de commande distante avec statistiques
     * @param command Nom de la commande
     * @param success true si succès
     */
    void logRemoteCommandExecution(const char* command, bool success);
    
    // === CONFIGURATION ===
    
    /**
     * Définit l'adresse email
     */
    void setEmailAddress(const String& address) { _emailAddress = address; }
    
    /**
     * Active/désactive les emails
     */
    void setEmailEnabled(bool enabled) { _emailEnabled = enabled; }
    
    /**
     * Définit la fréquence de réveil (secondes)
     */
    void setFreqWakeSec(uint16_t freq) { _freqWakeSec = freq; }
    
    // === GETTERS ===
    
    const String& getEmailAddress() const { return _emailAddress; }
    bool isEmailEnabled() const { return _emailEnabled; }
    uint16_t getFreqWakeSec() const { return _freqWakeSec; }
    uint16_t getLimFlood() const { return _limFlood; }
    uint16_t getAqThresholdCm() const { return _aqThresholdCm; }
    uint16_t getTankThresholdCm() const { return _tankThresholdCm; }
    float getHeaterThresholdC() const { return _heaterThresholdC; }
    
    /**
     * Vérifie si le serveur est OK
     */
    bool isServerOk() const { return _serverOk; }
    
    /**
     * États d'envoi/réception
     * -1: erreur, 0: en cours/idle, 1: OK
     */
    int8_t getSendState() const { return _sendState; }
    int8_t getRecvState() const { return _recvState; }
    
    /**
     * v11.69: Alias pour getSendState() pour plus de clarté
     * @return État de la dernière communication avec le serveur
     */
    int8_t getLastSendState() const { return _sendState; }
    
private:
    WebClient& _web;
    ConfigManager& _config;
    DataQueue _dataQueue;  // v11.31: File d'attente persistante
    
    // Configuration serveur
    String _emailAddress;
    bool _emailEnabled;
    uint16_t _freqWakeSec;
    
    // Seuils
    uint16_t _limFlood;
    uint16_t _aqThresholdCm;
    uint16_t _tankThresholdCm;
    float _heaterThresholdC;
    
    // Timing
    unsigned long _lastSend;
    unsigned long _lastRemoteFetch;
    static constexpr unsigned long SEND_INTERVAL_MS = 120000;        // 2 minutes
    static constexpr unsigned long REMOTE_FETCH_INTERVAL_MS = 4000;  // 4 secondes (v11.82+: réactivité accrue commandes)
    
    // État
    bool _serverOk;
    int8_t _sendState;  // -1 erreur, 0 en cours/idle, 1 OK
    int8_t _recvState;  // idem
    
    // État précédent pour détection changements (checkCriticalChanges)
    bool _prevPumpTank;
    bool _prevPumpAqua;
    bool _prevBouffeMatin;
    bool _prevBouffeMidi;
    bool _prevBouffeSoir;
    
    // === HELPERS PRIVÉS ===
    
    /**
     * Normalise les clés JSON du serveur distant vers format interface
     * v11.40: Garantit cohérence entre serveur distant et interface web
     * @param src Document source (format serveur)
     * @param dst Document destination (format interface normalisé)
     */
    static void normalizeServerKeys(
        const ArduinoJson::JsonDocument& src,
        ArduinoJson::JsonDocument& dst
    );
    
    /**
     * Helper: Évalue si une valeur JSON est "truthy"
     * Accepte: bool true, int 1, string "1"/"true"/"on"/"checked"
     */
    static bool isTrue(ArduinoJson::JsonVariantConst v);
    
    /**
     * Helper: Évalue si une valeur JSON est "falsey"
     * Accepte: bool false, int 0, string "0"/"false"/"off"/"unchecked"
     */
    static bool isFalse(ArduinoJson::JsonVariantConst v);
    
    /**
     * Helper: Assigne une variable si la clé est présente dans le JSON
     * @param doc Document JSON
     * @param key Clé à rechercher
     * @param var Variable à assigner (référence)
     */
    template<typename T>
    static void assignIfPresent(const ArduinoJson::JsonDocument& doc, const char* key, T& var) {
        if (!doc.containsKey(key)) return;
        T v = doc[key].as<T>();
        if constexpr (std::is_arithmetic_v<T>) {
            if (v == 0) return; // Ignorer zéros (non définis)
        }
        var = v;
    }
};

