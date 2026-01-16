#include "automatism_actuators.h"
#include "automatism.h"  // Pour accéder à autoCtrl complet (sendFullUpdate, _sensors)
#include "automatism_persistence.h"
#include "realtime_websocket.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// Déclarations externes
extern Automatism g_autoCtrl;

// ============================================================================
// Module: AutomatismActuators
// Responsabilité: Contrôle manuel actionneurs avec sync serveur
// ============================================================================

AutomatismActuators::AutomatismActuators(SystemActuators& acts, ConfigManager& cfg)
    : _acts(acts), _config(cfg) {
}

// ============================================================================
// SYSTÈME DE SYNCHRONISATION SERVEUR DIFFÉRÉE
// Remplacé les tâches FreeRTOS par un marqueur simple
// ============================================================================

void AutomatismActuators::markForSync(const char* payload) {
    _needsServerSync = true;
    _pendingSyncPayload = payload;
    Serial.printf("[Actuators] 📌 Sync serveur marquée: %s\n", payload);
}

String AutomatismActuators::consumePendingSyncPayload() {
    String payload = _pendingSyncPayload;
    _needsServerSync = false;
    _pendingSyncPayload = "";
    return payload;
}

// ============================================================================
// POMPE AQUARIUM
// ============================================================================

void AutomatismActuators::startAquaPumpManual() {
    Serial.println(F("[Actuators] 🐠 Démarrage manuel pompe aquarium (local)..."));
    
    // 1. ACTIVATION IMMÉDIATE
    _acts.startAquaPump();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("aqua", true);
    
    Serial.printf("[Actuators] ✅ Pompe aquarium démarrée - Heap: %u bytes\n", ESP.getFreeHeap());
    
    // 3. WebSocket immédiat (feedback utilisateur instantané)
    g_realtimeWebSocket.broadcastNow();
    
    // 4. Marquer pour synchronisation serveur au prochain cycle
    markForSync("etatPompeAqua=1");
}

void AutomatismActuators::stopAquaPumpManual() {
    Serial.println(F("[Actuators] 🛑 Arrêt manuel pompe aquarium (local)..."));
    
    // 1. ARRÊT IMMÉDIAT
    _acts.stopAquaPump();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("aqua", false);
    
    Serial.printf("[Actuators] ✅ Pompe aquarium arrêtée - Heap: %u bytes\n", ESP.getFreeHeap());
    
    // 3. WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();
    
    // 4. Marquer pour synchronisation serveur au prochain cycle
    markForSync("etatPompeAqua=0");
}

// ============================================================================
// POMPE RÉSERVE
// ============================================================================

void AutomatismActuators::startTankPumpManual() {
    Serial.println(F("[Actuators] 🚰 Démarrage manuel pompe réserve (local)..."));
    
    // Note: Watchdog géré par tâche appelante
    // Ne pas appeler esp_task_wdt_reset() ici (erreur "task not found")
    
    // 1. ACTIVATION IMMÉDIATE
    _acts.startTankPump();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("tank", true);
    
    Serial.printf("[Actuators] ✅ Pompe réserve démarrée - Heap: %u bytes\n", ESP.getFreeHeap());
    
    // 3. WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();
    
    // 4. Marquer pour synchronisation serveur au prochain cycle
    markForSync("etatPompeReserve=1");
}

void AutomatismActuators::stopTankPumpManual() {
    Serial.println(F("[Actuators] 🛑 Arrêt manuel pompe réserve (local)..."));
    
    // 1. ARRÊT IMMÉDIAT
    _acts.stopTankPump();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("tank", false);
    
    Serial.printf("[Actuators] ✅ Pompe réserve arrêtée - Heap: %u bytes\n", ESP.getFreeHeap());
    
    // 3. WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();
    
    // 4. Marquer pour synchronisation serveur au prochain cycle
    markForSync("etatPompeReserve=0");
}

// ============================================================================
// CHAUFFAGE
// ============================================================================

void AutomatismActuators::startHeaterManual() {
    Serial.println(F("[Actuators] 🔥 Activation manuelle chauffage (local)..."));
    
    // 1. ACTIVATION IMMÉDIATE
    _acts.startHeater();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("heater", true);
    
    // 3. WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();
    
    // 4. Marquer pour synchronisation serveur au prochain cycle
    markForSync("etatHeat=1");
}

void AutomatismActuators::stopHeaterManual() {
    Serial.println(F("[Actuators] 🧊 Arrêt manuel chauffage (local)..."));
    
    // 1. ARRÊT IMMÉDIAT
    _acts.stopHeater();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("heater", false);
    
    // 3. WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();
    
    // 4. Marquer pour synchronisation serveur au prochain cycle
    markForSync("etatHeat=0");
}

// ============================================================================
// LUMIÈRE
// ============================================================================

void AutomatismActuators::startLightManual() {
    Serial.println(F("[Actuators] 💡 Activation manuelle lumière (local)..."));
    
    // 1. ACTIVATION IMMÉDIATE
    _acts.startLight();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("light", true);
    
    // 3. WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();
    
    // 4. Marquer pour synchronisation serveur au prochain cycle
    markForSync("etatUV=1");
}

void AutomatismActuators::stopLightManual() {
    Serial.println(F("[Actuators] 🌙 Arrêt manuel lumière (local)..."));
    
    // 1. ARRÊT IMMÉDIAT
    _acts.stopLight();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("light", false);
    
    // 3. WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();
    
    // 4. Marquer pour synchronisation serveur au prochain cycle
    markForSync("etatUV=0");
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void AutomatismActuators::toggleEmailNotifications() {
    // Récupérer état actuel depuis g_autoCtrl (car stocké dans network module)
    extern Automatism g_autoCtrl;
    bool currentState = g_autoCtrl.isEmailEnabled();
    bool newState = !currentState;
    
    Serial.printf("[Actuators] 📧 Toggle notifications email: %s → %s\n",
                  currentState ? "ON" : "OFF", newState ? "ON" : "OFF");
    
    // Note: L'état emailEnabled est géré par le module Network
    // On délègue via autoCtrl qui a accès au module Network
    // Cette méthode trigger juste le toggle et la sync serveur
    
    // WebSocket immédiat pour feedback
    g_realtimeWebSocket.broadcastNow();
    
    // Marquer pour synchronisation serveur au prochain cycle
    const char* payload = newState ? "mailNotif=1" : "mailNotif=0";
    markForSync(payload);
}

void AutomatismActuators::toggleForceWakeup() {
    // Récupérer état actuel
    bool currentState = _config.getForceWakeUp();
    bool newState = !currentState;
    
    Serial.printf("[Actuators] 🔓 Toggle Force Wakeup: %s → %s\n",
                  currentState ? "ON" : "OFF", newState ? "ON" : "OFF");
    
    // Mise à jour config
    _config.setForceWakeUp(newState);
    _config.forceSaveBouffeFlags();  // Sauvegarde immédiate
    
    // WebSocket immédiat
    g_realtimeWebSocket.broadcastNow();
    
    // Marquer pour synchronisation serveur au prochain cycle
    const char* payload = newState ? "forceWakeUp=1" : "forceWakeUp=0";
    markForSync(payload);
}

