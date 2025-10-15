#pragma once

#include <Arduino.h>
#include <Preferences.h>

/**
 * Module Persistence pour Automatism
 * 
 * Responsabilité: Sauvegarde et chargement de l'état des actionneurs en NVS
 * Utilisé pour restaurer l'état après sommeil (light sleep)
 * 
 * Namespaces NVS: 
 * - "actSnap": Snapshots temporaires pour sleep/wake
 * - "actState": États actuels persistants (priorité locale)
 */
class AutomatismPersistence {
public:
    // ========================================
    // SNAPSHOTS SLEEP/WAKE (Temporaire)
    // ========================================
    
    /**
     * Sauvegarde l'état des actionneurs dans NVS avant sleep
     * @param pumpAquaWasOn État de la pompe aquarium
     * @param heaterWasOn État du chauffage
     * @param lightWasOn État de la lumière
     */
    static void saveActuatorSnapshot(bool pumpAquaWasOn, bool heaterWasOn, bool lightWasOn);
    
    /**
     * Charge l'état des actionneurs depuis NVS après wake
     * @param pumpAquaWasOn [out] État de la pompe aquarium
     * @param heaterWasOn [out] État du chauffage
     * @param lightWasOn [out] État de la lumière
     * @return true si snapshot valide trouvé, false sinon
     */
    static bool loadActuatorSnapshot(bool& pumpAquaWasOn, bool& heaterWasOn, bool& lightWasOn);
    
    /**
     * Efface le snapshot des actionneurs dans NVS
     * Appelé après restauration réussie
     */
    static void clearActuatorSnapshot();
    
    // ========================================
    // ÉTATS ACTUELS PERSISTANTS (Priorité locale)
    // ========================================
    
    /**
     * Sauvegarde l'état actuel d'un actionneur en NVS immédiatement
     * Utilisé après chaque action manuelle depuis le serveur local
     * @param actuator Nom de l'actionneur ("aqua", "heater", "light", "tank")
     * @param state État à sauvegarder (true=ON, false=OFF)
     */
    static void saveCurrentActuatorState(const char* actuator, bool state);
    
    /**
     * Charge l'état actuel d'un actionneur depuis NVS
     * @param actuator Nom de l'actionneur
     * @param defaultValue Valeur par défaut si non trouvée
     * @return État de l'actionneur
     */
    static bool loadCurrentActuatorState(const char* actuator, bool defaultValue = false);
    
    /**
     * Récupère le timestamp de la dernière action locale
     * @return Timestamp en millisecondes (millis()), 0 si aucune
     */
    static uint32_t getLastLocalActionTime();
    
    /**
     * Vérifie si une action locale récente a eu lieu
     * @param timeoutMs Durée de priorité locale en millisecondes
     * @return true si une action locale récente existe
     */
    static bool hasRecentLocalAction(uint32_t timeoutMs = 5000);
    
    // ========================================
    // PENDING SYNC (Synchronisation serveur distant en attente)
    // ========================================
    
    /**
     * Marque un actionneur comme nécessitant une synchronisation avec le serveur distant
     * @param actuator Nom de l'actionneur ("aqua", "heater", "light", "tank")
     * @param state État à synchroniser (true=ON, false=OFF)
     */
    static void markPendingSync(const char* actuator, bool state);
    
    /**
     * Marque les variables de configuration comme nécessitant une synchronisation
     * Utilisé après modification via /dbvars/update
     */
    static void markConfigPendingSync();
    
    /**
     * Efface le flag pending sync pour un actionneur après synchronisation réussie
     * @param actuator Nom de l'actionneur
     */
    static void clearPendingSync(const char* actuator);
    
    /**
     * Efface le flag pending sync pour la configuration
     */
    static void clearConfigPendingSync();
    
    /**
     * Vérifie s'il y a des synchronisations en attente
     * @return true si au moins une synchronisation est en attente
     */
    static bool hasPendingSync();
    
    /**
     * Récupère le nombre de synchronisations en attente
     * @return Nombre d'items en attente de sync
     */
    static uint8_t getPendingSyncCount();
    
    /**
     * Récupère le timestamp du dernier pending sync marqué
     * @return Timestamp en millisecondes, 0 si aucun
     */
    static uint32_t getLastPendingSyncTime();
};



