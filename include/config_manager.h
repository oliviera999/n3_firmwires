#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include "nvs_manager.h"

class ConfigManager {
private:
    // Variables d'état
    bool _bouffeMatinOk;
    bool _bouffeMidiOk;
    bool _bouffeSoirOk;
    int _lastJourBouf;
    bool _pompeAquaLocked;
    bool _forceWakeUp;
    bool _otaUpdateFlag;
    // Flags réseau
    bool _remoteSendEnabled;   // Envoi vers serveur distant autorisé
    bool _remoteRecvEnabled;   // Réception/polling depuis serveur autorisé
    
    // Cache pour optimiser les écritures NVS
    bool _cachedBouffeMatinOk;
    bool _cachedBouffeMidiOk;
    bool _cachedBouffeSoirOk;
    int _cachedLastJourBouf;
    bool _cachedPompeAquaLocked;
    bool _cachedForceWakeUp;
    bool _cachedOtaUpdateFlag;
    bool _flagsChanged;
    // Cache réseau (non lié à _flagsChanged)
    bool _cachedRemoteSendEnabled;
    bool _cachedRemoteRecvEnabled;
    
    // Utilisation du gestionnaire NVS centralisé
    // NVSManager g_nvsManager; // Instance globale
    
    // Méthodes privées
    bool hasChanges() const;
    void updateCache();

public:
    // Constructeur
    ConfigManager();
    
    // Méthodes de chargement/sauvegarde
    void loadBouffeFlags();
    void saveBouffeFlags();
    void forceSaveBouffeFlags();
    void resetBouffeFlags();
    // Réseau
    void loadNetworkFlags();
    
    /**
     * Charge TOUTES les variables de configuration depuis NVS au démarrage
     * Charge: email, seuils, durées, flags, etc.
     * Utilise valeurs par défaut si NVS vide (comportement 1b)
     * @return true si config chargée depuis NVS, false si valeurs par défaut utilisées
     */
    bool loadConfigFromNVS();
    
    // Méthodes pour les variables distantes
    void saveRemoteVars(const String& json);
    bool loadRemoteVars(String& json);
    
    // Méthodes pour le flag OTA
    void setOtaUpdateFlag(bool value);
    bool getOtaUpdateFlag() const;
    
    // Flags réseau
    void setRemoteSendEnabled(bool value);
    void setRemoteRecvEnabled(bool value);
    bool isRemoteSendEnabled() const { return _remoteSendEnabled; }
    bool isRemoteRecvEnabled() const { return _remoteRecvEnabled; }
    
    // Setters optimisés
    void setBouffeMatinOk(bool value);
    void setBouffeMidiOk(bool value);
    void setBouffeSoirOk(bool value);
    void setLastJourBouf(int value);
    void setPompeAquaLocked(bool value);
    void setForceWakeUp(bool value);
    
    // Getters
    bool getBouffeMatinOk() const { return _bouffeMatinOk; }
    bool getBouffeMidiOk() const { return _bouffeMidiOk; }
    bool getBouffeSoirOk() const { return _bouffeSoirOk; }
    int getLastJourBouf() const { return _lastJourBouf; }
    bool getPompeAquaLocked() const { return _pompeAquaLocked; }
    bool getForceWakeUp() const { return _forceWakeUp; }
};

#endif // CONFIG_MANAGER_H 