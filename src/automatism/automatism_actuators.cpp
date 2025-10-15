#include "automatism_actuators.h"
#include "automatism.h"  // Pour accéder à autoCtrl complet (sendFullUpdate, _sensors)
#include "automatism_persistence.h"
#include "realtime_websocket.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// Déclarations externes
extern Automatism autoCtrl;
extern RealtimeWebSocket realtimeWebSocket;

// ============================================================================
// Module: AutomatismActuators
// Responsabilité: Contrôle manuel actionneurs avec sync serveur
// ============================================================================

AutomatismActuators::AutomatismActuators(SystemActuators& acts, ConfigManager& cfg)
    : _acts(acts), _config(cfg) {
}

// ============================================================================
// HELPER: Synchronisation Asynchrone (Factorisation)
// Pattern répété 8x dans automatism.cpp original
// ============================================================================

void AutomatismActuators::syncActuatorStateAsync(
    const char* taskName,
    const char* extraParams,
    TaskHandle_t& taskHandle,
    const char* actionDesc
) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[Actuators] ⚠️ Pas de WiFi - %s localement uniquement\n", actionDesc);
        return;
    }
    
    Serial.println(F("[Actuators] 🌐 Synchronisation serveur en arrière-plan..."));
    
    // Vérifier si une tâche de sync est déjà en cours
    if (taskHandle != nullptr && eTaskGetState(taskHandle) != eDeleted) {
        Serial.printf("[Actuators] ⚠️ Tâche %s déjà en cours - skip\n", taskName);
        return;
    }
    
    // Paramètres pour la tâche
    struct SyncParams {
        const char* extraParams;
        const char* actionDesc;
        TaskHandle_t* handle;
    };
    
    SyncParams* params = new SyncParams{extraParams, actionDesc, &taskHandle};
    
    BaseType_t result = xTaskCreate([](void* param) {
        SyncParams* p = (SyncParams*)param;
        
        vTaskDelay(pdMS_TO_TICKS(50));
        
        extern Automatism autoCtrl;
        SensorReadings freshReadings = autoCtrl.readSensors();
        bool success = autoCtrl.sendFullUpdate(freshReadings, p->extraParams);
        
        if (success) {
            Serial.printf("[Actuators] ✅ Sync serveur OK - %s\n", p->actionDesc);
        } else {
            Serial.printf("[Actuators] ⚠️ Sync serveur échec - %s\n", p->actionDesc);
        }
        
        *(p->handle) = nullptr;  // Reset handle avant suppression
        delete p;
        vTaskDelete(NULL);
    }, taskName, 4096, params, 1, &taskHandle);
    
    if (result != pdPASS) {
        Serial.printf("[Actuators] ❌ Échec création tâche %s\n", taskName);
        delete params;
        taskHandle = nullptr;
    }
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
    realtimeWebSocket.broadcastNow();
    
    // 4. Synchronisation serveur en arrière-plan
    syncActuatorStateAsync("sync_aqua_start", "etatPompeAqua=1", 
                          _syncAquaStartHandle, "pompe aqua activée");
}

void AutomatismActuators::stopAquaPumpManual() {
    Serial.println(F("[Actuators] 🛑 Arrêt manuel pompe aquarium (local)..."));
    
    // 1. ARRÊT IMMÉDIAT
    _acts.stopAquaPump();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("aqua", false);
    
    Serial.printf("[Actuators] ✅ Pompe aquarium arrêtée - Heap: %u bytes\n", ESP.getFreeHeap());
    
    // 3. WebSocket immédiat
    realtimeWebSocket.broadcastNow();
    
    // 4. Synchronisation serveur
    syncActuatorStateAsync("sync_aqua_stop", "etatPompeAqua=0",
                          _syncAquaStopHandle, "pompe aqua arrêtée");
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
    realtimeWebSocket.broadcastNow();
    
    // 4. Synchronisation serveur
    syncActuatorStateAsync("sync_tank_start", "etatPompeReserve=1",
                          _syncTankStartHandle, "pompe réserve activée");
}

void AutomatismActuators::stopTankPumpManual() {
    Serial.println(F("[Actuators] 🛑 Arrêt manuel pompe réserve (local)..."));
    
    // 1. ARRÊT IMMÉDIAT
    _acts.stopTankPump();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("tank", false);
    
    Serial.printf("[Actuators] ✅ Pompe réserve arrêtée - Heap: %u bytes\n", ESP.getFreeHeap());
    
    // 3. WebSocket immédiat
    realtimeWebSocket.broadcastNow();
    
    // 4. Synchronisation serveur
    syncActuatorStateAsync("sync_tank_stop", "etatPompeReserve=0",
                          _syncTankStopHandle, "pompe réserve arrêtée");
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
    realtimeWebSocket.broadcastNow();
    
    // 4. Synchronisation serveur
    syncActuatorStateAsync("sync_heater_start", "etatHeat=1",
                          _syncHeaterStartHandle, "chauffage activé");
}

void AutomatismActuators::stopHeaterManual() {
    Serial.println(F("[Actuators] 🧊 Arrêt manuel chauffage (local)..."));
    
    // 1. ARRÊT IMMÉDIAT
    _acts.stopHeater();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("heater", false);
    
    // 3. WebSocket immédiat
    realtimeWebSocket.broadcastNow();
    
    // 4. Synchronisation serveur
    syncActuatorStateAsync("sync_heater_stop", "etatHeat=0",
                          _syncHeaterStopHandle, "chauffage arrêté");
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
    realtimeWebSocket.broadcastNow();
    
    // 4. Synchronisation serveur
    syncActuatorStateAsync("sync_light_start", "etatUV=1",
                          _syncLightStartHandle, "lumière activée");
}

void AutomatismActuators::stopLightManual() {
    Serial.println(F("[Actuators] 🌙 Arrêt manuel lumière (local)..."));
    
    // 1. ARRÊT IMMÉDIAT
    _acts.stopLight();
    
    // 2. SAUVEGARDE NVS IMMÉDIATE (PRIORITÉ LOCALE)
    AutomatismPersistence::saveCurrentActuatorState("light", false);
    
    // 3. WebSocket immédiat
    realtimeWebSocket.broadcastNow();
    
    // 4. Synchronisation serveur
    syncActuatorStateAsync("sync_light_stop", "etatUV=0",
                          _syncLightStopHandle, "lumière arrêtée");
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void AutomatismActuators::toggleEmailNotifications() {
    // Récupérer état actuel depuis autoCtrl (car stocké dans network module)
    extern Automatism autoCtrl;
    bool currentState = autoCtrl.isEmailEnabled();
    bool newState = !currentState;
    
    Serial.printf("[Actuators] 📧 Toggle notifications email: %s → %s\n",
                  currentState ? "ON" : "OFF", newState ? "ON" : "OFF");
    
    // Note: L'état emailEnabled est géré par le module Network
    // On délègue via autoCtrl qui a accès au module Network
    // Cette méthode trigger juste le toggle et la sync serveur
    
    // WebSocket immédiat pour feedback
    realtimeWebSocket.broadcastNow();
    
    // Synchronisation serveur
    const char* params = newState ? "emailEnabled=1" : "emailEnabled=0";
    TaskHandle_t tempHandle = nullptr;
    syncActuatorStateAsync("sync_email_toggle", params, tempHandle, 
                          newState ? "emails activés" : "emails désactivés");
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
    realtimeWebSocket.broadcastNow();
    
    // Synchronisation serveur
    const char* params = newState ? "forceWakeUp=1" : "forceWakeUp=0";
    TaskHandle_t tempHandle = nullptr;
    syncActuatorStateAsync("sync_forcewake_toggle", params, tempHandle,
                          newState ? "force wakeup ON" : "force wakeup OFF");
}

