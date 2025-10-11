#pragma once

#include <Arduino.h>
#include <Preferences.h>

/**
 * Module Persistence pour Automatism
 * 
 * Responsabilité: Sauvegarde et chargement de l'état des actionneurs en NVS
 * Utilisé pour restaurer l'état après sommeil (light sleep)
 * 
 * Namespace NVS: "actSnap"
 * Keys: pending (bool), aqua (bool), heater (bool), light (bool)
 */
class AutomatismPersistence {
public:
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
};



