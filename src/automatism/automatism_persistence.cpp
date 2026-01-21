#include "automatism_persistence.h"
#include <Arduino.h>
#include "nvs_manager.h" // v11.105: Utilisation du gestionnaire NVS centralisé

// ============================================================================
// Module: AutomatismPersistence
// Responsabilité: Sauvegarde/chargement état actionneurs en NVS
// v11.105: Ce module a été migré pour utiliser g_nvsManager
// ============================================================================

// Namespace "actSnap"  -> NVS_NAMESPACES::STATE (prefixe "snap_")
// Namespace "actState" -> NVS_NAMESPACES::STATE (prefixe "state_")

void AutomatismPersistence::saveActuatorSnapshot(bool pumpAquaWasOn, bool heaterWasOn, bool lightWasOn) {
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_pending", true);
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_aqua", pumpAquaWasOn);
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_heater", heaterWasOn);
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_light", lightWasOn);
  
  Serial.printf("[Persistence] Snapshot actionneurs NVS: aqua=%s heater=%s light=%s\n",
                pumpAquaWasOn?"ON":"OFF", heaterWasOn?"ON":"OFF", lightWasOn?"ON":"OFF");
}

bool AutomatismPersistence::loadActuatorSnapshot(bool& pumpAquaWasOn, bool& heaterWasOn, bool& lightWasOn) {
  bool pending;
  g_nvsManager.loadBool(NVS_NAMESPACES::STATE, "snap_pending", pending, false);
  
  if (!pending) { 
    return false; 
  }
  
  g_nvsManager.loadBool(NVS_NAMESPACES::STATE, "snap_aqua", pumpAquaWasOn, false);
  g_nvsManager.loadBool(NVS_NAMESPACES::STATE, "snap_heater", heaterWasOn, false);
  g_nvsManager.loadBool(NVS_NAMESPACES::STATE, "snap_light", lightWasOn, false);
  
  Serial.printf("[Persistence] Snapshot chargé depuis NVS: aqua=%s heater=%s light=%s\n",
                pumpAquaWasOn?"ON":"OFF", heaterWasOn?"ON":"OFF", lightWasOn?"ON":"OFF");
  
  return true;
}

void AutomatismPersistence::clearActuatorSnapshot() {
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_pending", false);
  
  Serial.println("[Persistence] Snapshot actionneurs effacé");
}

// ============================================================================
// ÉTATS ACTUELS PERSISTANTS (Priorité locale)
// ============================================================================

void AutomatismPersistence::saveCurrentActuatorState(const char* actuator, bool state) {
  // Concaténation pour créer la clé préfixée
  char key[32];
  snprintf(key, sizeof(key), "state_%s", actuator);

  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, key, state);
  
  // Timestamp de la dernière modification locale (millisecondes)
  g_nvsManager.saveULong(NVS_NAMESPACES::STATE, "state_lastLocal", millis());

  Serial.printf("[Persistence] État %s=%s sauvegardé en NVS (priorité locale)\n",
                actuator, state ? "ON" : "OFF");
}

bool AutomatismPersistence::loadCurrentActuatorState(const char* actuator, bool defaultValue) {
  char key[32];
  snprintf(key, sizeof(key), "state_%s", actuator);
  
  bool state;
  g_nvsManager.loadBool(NVS_NAMESPACES::STATE, key, state, defaultValue);
  
  return state;
}

uint32_t AutomatismPersistence::getLastLocalActionTime() {
  unsigned long timestamp;
  g_nvsManager.loadULong(NVS_NAMESPACES::STATE, "state_lastLocal", timestamp, 0);
  
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
  char key_state[32];
  snprintf(key_state, sizeof(key_state), "sync_%s", actuator);
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, key_state, state);

  int count;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
  
  bool alreadyPending = false;
  for (int i = 0; i < count; i++) {
    char key_item[16];
    snprintf(key_item, sizeof(key_item), "sync_item_%d", i);
    char item[64];
    g_nvsManager.loadString(NVS_NAMESPACES::STATE, key_item, item, sizeof(item), "");
    if (strcmp(item, actuator) == 0) {
      alreadyPending = true;
      break;
    }
  }
  
  if (!alreadyPending) {
    char key_item[16];
    snprintf(key_item, sizeof(key_item), "sync_item_%d", count);
    g_nvsManager.saveString(NVS_NAMESPACES::STATE, key_item, actuator);
    count++;
    g_nvsManager.saveInt(NVS_NAMESPACES::STATE, "sync_count", count);
  }
  
  g_nvsManager.saveULong(NVS_NAMESPACES::STATE, "sync_lastSync", millis());
  
  Serial.printf("[Persistence] ⏳ Pending sync marqué: %s=%s (total: %u)\n",
                actuator, state ? "ON" : "OFF", count);
}

void AutomatismPersistence::markConfigPendingSync() {
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "sync_config", true);
  
  int count;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
  
  bool alreadyPending = false;
  for (int i = 0; i < count; i++) {
    char key_item[16];
    snprintf(key_item, sizeof(key_item), "sync_item_%d", i);
    char item[64];
    g_nvsManager.loadString(NVS_NAMESPACES::STATE, key_item, item, sizeof(item), "");
    if (strcmp(item, "config") == 0) {
      alreadyPending = true;
      break;
    }
  }
  
  if (!alreadyPending) {
    char key_item[16];
    snprintf(key_item, sizeof(key_item), "sync_item_%d", count);
    g_nvsManager.saveString(NVS_NAMESPACES::STATE, key_item, "config");
    count++;
    g_nvsManager.saveInt(NVS_NAMESPACES::STATE, "sync_count", count);
  }
  
  g_nvsManager.saveULong(NVS_NAMESPACES::STATE, "sync_lastSync", millis());

  Serial.printf("[Persistence] ⏳ Config pending sync marquée (total: %u)\n", count);
}

void AutomatismPersistence::clearPendingSync(const char* actuator) {
  char key_state[32];
  snprintf(key_state, sizeof(key_state), "sync_%s", actuator);
  g_nvsManager.removeKey(NVS_NAMESPACES::STATE, key_state);
  
  int count;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
  
  int newCount = 0;
  for (int i = 0; i < count; i++) {
    char oldKey[16];
    snprintf(oldKey, sizeof(oldKey), "sync_item_%d", i);
    char item[64];
    g_nvsManager.loadString(NVS_NAMESPACES::STATE, oldKey, item, sizeof(item), "");
    
    if (strcmp(item, actuator) != 0 && strlen(item) > 0) {
      if (newCount != i) {
        char newKey[16];
        snprintf(newKey, sizeof(newKey), "sync_item_%d", newCount);
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, newKey, item);
      }
      newCount++;
    }
  }

  // Supprimer les anciennes clés non re-numérotées
  for (int i = newCount; i < count; i++) {
    char oldKey[16];
    snprintf(oldKey, sizeof(oldKey), "sync_item_%d", i);
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, oldKey);
  }
  
  g_nvsManager.saveInt(NVS_NAMESPACES::STATE, "sync_count", newCount);
  
  Serial.printf("[Persistence] ✅ Pending sync effacé: %s (reste: %u)\n", actuator, newCount);
}

void AutomatismPersistence::clearConfigPendingSync() {
  g_nvsManager.removeKey(NVS_NAMESPACES::STATE, "sync_config");
  
  int count;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);

  int newCount = 0;
  for (int i = 0; i < count; i++) {
    char oldKey[16];
    snprintf(oldKey, sizeof(oldKey), "sync_item_%d", i);
    char item[64];
    g_nvsManager.loadString(NVS_NAMESPACES::STATE, oldKey, item, sizeof(item), "");
    
    if (strcmp(item, "config") != 0 && strlen(item) > 0) {
      if (newCount != i) {
        char newKey[16];
        snprintf(newKey, sizeof(newKey), "sync_item_%d", newCount);
        g_nvsManager.saveString(NVS_NAMESPACES::STATE, newKey, item);
      }
      newCount++;
    }
  }

  // Supprimer les anciennes clés non re-numérotées
  for (int i = newCount; i < count; i++) {
    char oldKey[16];
    snprintf(oldKey, sizeof(oldKey), "sync_item_%d", i);
    g_nvsManager.removeKey(NVS_NAMESPACES::STATE, oldKey);
  }

  g_nvsManager.saveInt(NVS_NAMESPACES::STATE, "sync_count", newCount);

  Serial.printf("[Persistence] ✅ Config pending sync effacée (reste: %u)\n", newCount);
}

bool AutomatismPersistence::hasPendingSync() {
  int count;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
  return count > 0;
}

uint8_t AutomatismPersistence::getPendingSyncCount() {
  int count;
  g_nvsManager.loadInt(NVS_NAMESPACES::STATE, "sync_count", count, 0);
  return (uint8_t)count;
}

uint32_t AutomatismPersistence::getLastPendingSyncTime() {
  unsigned long timestamp;
  g_nvsManager.loadULong(NVS_NAMESPACES::STATE, "sync_lastSync", timestamp, 0);
  return timestamp;
}







