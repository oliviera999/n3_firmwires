#include "automatism_persistence.h"
#include <Arduino.h>

// ============================================================================
// Module: AutomatismPersistence
// Responsabilité: Sauvegarde/chargement état actionneurs en NVS
// ============================================================================

// Namespace "actSnap" : Snapshots pour sleep/wake (temporaire)
// Namespace "actState": États actuels persistants (priorité locale)

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

// ============================================================================
// ÉTATS ACTUELS PERSISTANTS (Priorité locale)
// ============================================================================

void AutomatismPersistence::saveCurrentActuatorState(const char* actuator, bool state) {
  Preferences prefs;
  prefs.begin("actState", false);
  
  prefs.putBool(actuator, state);
  // Timestamp de la dernière modification locale (millisecondes)
  prefs.putUInt("lastLocal", millis());
  
  prefs.end();
  
  Serial.printf("[Persistence] État %s=%s sauvegardé en NVS (priorité locale)\n",
                actuator, state ? "ON" : "OFF");
}

bool AutomatismPersistence::loadCurrentActuatorState(const char* actuator, bool defaultValue) {
  Preferences prefs;
  prefs.begin("actState", true);
  bool state = prefs.getBool(actuator, defaultValue);
  prefs.end();
  
  return state;
}

uint32_t AutomatismPersistence::getLastLocalActionTime() {
  Preferences prefs;
  prefs.begin("actState", true);
  uint32_t timestamp = prefs.getUInt("lastLocal", 0);
  prefs.end();
  
  return timestamp;
}

bool AutomatismPersistence::hasRecentLocalAction(uint32_t timeoutMs) {
  uint32_t lastAction = getLastLocalActionTime();
  if (lastAction == 0) return false;
  
  uint32_t elapsed = millis() - lastAction;
  return elapsed < timeoutMs;
}

// ============================================================================
// PENDING SYNC (Synchronisation serveur distant en attente)
// ============================================================================

void AutomatismPersistence::markPendingSync(const char* actuator, bool state) {
  Preferences prefs;
  prefs.begin("pendingSync", false);
  
  // Sauvegarder l'état de l'actionneur
  prefs.putBool(actuator, state);
  
  // Incrémenter le compteur
  uint8_t count = prefs.getUChar("count", 0);
  
  // Vérifier si cet actionneur était déjà en attente
  bool alreadyPending = false;
  for (uint8_t i = 0; i < count; i++) {
    char key[16];
    snprintf(key, sizeof(key), "item_%u", i);
    String item = prefs.getString(key, "");
    if (item == String(actuator)) {
      alreadyPending = true;
      break;
    }
  }
  
  // Ajouter à la liste si pas déjà présent
  if (!alreadyPending) {
    char key[16];
    snprintf(key, sizeof(key), "item_%u", count);
    prefs.putString(key, actuator);
    count++;
    prefs.putUChar("count", count);
  }
  
  // Timestamp du dernier pending sync
  prefs.putUInt("lastSync", millis());
  
  prefs.end();
  
  Serial.printf("[Persistence] ⏳ Pending sync marqué: %s=%s (total: %u)\n",
                actuator, state ? "ON" : "OFF", count);
}

void AutomatismPersistence::markConfigPendingSync() {
  Preferences prefs;
  prefs.begin("pendingSync", false);
  
  // Marquer la config comme pending
  prefs.putBool("config", true);
  
  // Incrémenter le compteur si pas déjà présent
  uint8_t count = prefs.getUChar("count", 0);
  bool alreadyPending = false;
  
  for (uint8_t i = 0; i < count; i++) {
    char key[16];
    snprintf(key, sizeof(key), "item_%u", i);
    String item = prefs.getString(key, "");
    if (item == "config") {
      alreadyPending = true;
      break;
    }
  }
  
  if (!alreadyPending) {
    char key[16];
    snprintf(key, sizeof(key), "item_%u", count);
    prefs.putString(key, "config");
    count++;
    prefs.putUChar("count", count);
  }
  
  prefs.putUInt("lastSync", millis());
  prefs.end();
  
  Serial.printf("[Persistence] ⏳ Config pending sync marquée (total: %u)\n", count);
}

void AutomatismPersistence::clearPendingSync(const char* actuator) {
  Preferences prefs;
  prefs.begin("pendingSync", false);
  
  // Supprimer l'état de l'actionneur
  prefs.remove(actuator);
  
  // Retirer de la liste
  uint8_t count = prefs.getUChar("count", 0);
  uint8_t newCount = 0;
  
  for (uint8_t i = 0; i < count; i++) {
    char oldKey[16];
    snprintf(oldKey, sizeof(oldKey), "item_%u", i);
    String item = prefs.getString(oldKey, "");
    
    if (item != String(actuator) && item.length() > 0) {
      // Garder cet item, le renuméroter
      if (newCount != i) {
        char newKey[16];
        snprintf(newKey, sizeof(newKey), "item_%u", newCount);
        prefs.putString(newKey, item);
      }
      newCount++;
    } else {
      // Supprimer cet item
      prefs.remove(oldKey);
    }
  }
  
  prefs.putUChar("count", newCount);
  prefs.end();
  
  Serial.printf("[Persistence] ✅ Pending sync effacé: %s (reste: %u)\n", actuator, newCount);
}

void AutomatismPersistence::clearConfigPendingSync() {
  Preferences prefs;
  prefs.begin("pendingSync", false);
  
  prefs.remove("config");
  
  // Retirer de la liste
  uint8_t count = prefs.getUChar("count", 0);
  uint8_t newCount = 0;
  
  for (uint8_t i = 0; i < count; i++) {
    char oldKey[16];
    snprintf(oldKey, sizeof(oldKey), "item_%u", i);
    String item = prefs.getString(oldKey, "");
    
    if (item != "config" && item.length() > 0) {
      if (newCount != i) {
        char newKey[16];
        snprintf(newKey, sizeof(newKey), "item_%u", newCount);
        prefs.putString(newKey, item);
      }
      newCount++;
    } else {
      prefs.remove(oldKey);
    }
  }
  
  prefs.putUChar("count", newCount);
  prefs.end();
  
  Serial.printf("[Persistence] ✅ Config pending sync effacée (reste: %u)\n", newCount);
}

bool AutomatismPersistence::hasPendingSync() {
  Preferences prefs;
  prefs.begin("pendingSync", true);
  uint8_t count = prefs.getUChar("count", 0);
  prefs.end();
  
  return count > 0;
}

uint8_t AutomatismPersistence::getPendingSyncCount() {
  Preferences prefs;
  prefs.begin("pendingSync", true);
  uint8_t count = prefs.getUChar("count", 0);
  prefs.end();
  
  return count;
}

uint32_t AutomatismPersistence::getLastPendingSyncTime() {
  Preferences prefs;
  prefs.begin("pendingSync", true);
  uint32_t timestamp = prefs.getUInt("lastSync", 0);
  prefs.end();
  
  return timestamp;
}



