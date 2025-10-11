#include "automatism_persistence.h"
#include <Arduino.h>

// ============================================================================
// Module: AutomatismPersistence
// Responsabilité: Sauvegarde/chargement état actionneurs en NVS
// ============================================================================

void AutomatismPersistence::saveActuatorSnapshot(bool pumpAquaWasOn, bool heaterWasOn, bool lightWasOn) {
  Preferences prefs;
  prefs.begin("actSnap", false);
  prefs.putBool("pending", true);
  prefs.putBool("aqua", pumpAquaWasOn);
  prefs.putBool("heater", heaterWasOn);
  prefs.putBool("light", lightWasOn);
  prefs.end();
  
  Serial.printf("[Persistence] Snapshot actionneurs NVS: aqua=%s heater=%s light=%s\n",
                pumpAquaWasOn?"ON":"OFF", heaterWasOn?"ON":"OFF", lightWasOn?"ON":"OFF");
}

bool AutomatismPersistence::loadActuatorSnapshot(bool& pumpAquaWasOn, bool& heaterWasOn, bool& lightWasOn) {
  Preferences prefs;
  prefs.begin("actSnap", true);
  bool pending = prefs.getBool("pending", false);
  
  if (!pending) { 
    prefs.end(); 
    return false; 
  }
  
  pumpAquaWasOn = prefs.getBool("aqua", false);
  heaterWasOn   = prefs.getBool("heater", false);
  lightWasOn    = prefs.getBool("light", false);
  prefs.end();
  
  Serial.printf("[Persistence] Snapshot chargé depuis NVS: aqua=%s heater=%s light=%s\n",
                pumpAquaWasOn?"ON":"OFF", heaterWasOn?"ON":"OFF", lightWasOn?"ON":"OFF");
  
  return true;
}

void AutomatismPersistence::clearActuatorSnapshot() {
  Preferences prefs;
  prefs.begin("actSnap", false);
  prefs.putBool("pending", false);
  prefs.end();
  
  Serial.println("[Persistence] Snapshot actionneurs effacé");
}

