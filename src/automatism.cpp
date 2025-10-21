// Place all includes before method definitions to ensure symbols are visible
#include "automatism.h"
#include "automatism/automatism_persistence.h"
#include "automatism/automatism_actuators.h"
#include "automatism/automatism_feeding.h"
#include "automatism/automatism_network.h"
#include "automatism/automatism_sleep.h"
#include <Arduino.h>
#include <cstring>
#include <string>
#include "project_config.h"
#include "mailer.h"
#include <ctime>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <algorithm>
#include "esp_task_wdt.h"
#include <Preferences.h>
#include "gpio_parser.h"

// Déclarations externes
extern Automatism autoCtrl;

namespace {
inline bool hasExpired(uint32_t targetMs, uint32_t nowMs) {
  return targetMs != 0 && static_cast<int32_t>(nowMs - targetMs) >= 0;
}

inline bool hasExpired(uint32_t targetMs) {
  return hasExpired(targetMs, millis());
}

// Fonctions utilitaires pour éviter la fragmentation mémoire avec String
// Utilise des buffers statiques pour éviter les allocations dynamiques
inline void formatDistanceAlert(char* buffer, size_t bufferSize, const char* prefix, float distance, const char* suffix, float threshold) {
  snprintf(buffer, bufferSize, "%s%.1f cm (%s%.1f)", prefix, distance, suffix, threshold);
}

inline void formatTemperatureAlert(char* buffer, size_t bufferSize, const char* prefix, float temp) {
  snprintf(buffer, bufferSize, "%s%.1f°C", prefix, temp);
}

inline bool isStillPending(uint32_t targetMs, uint32_t nowMs) {
  return targetMs != 0 && static_cast<int32_t>(targetMs - nowMs) > 0;
}

inline uint32_t remainingMs(uint32_t targetMs, uint32_t nowMs) {
  if (targetMs == 0) {
    return 0;
  }
  int32_t diff = static_cast<int32_t>(targetMs - nowMs);
  return diff > 0 ? static_cast<uint32_t>(diff) : 0U;
}

// Parse a JSON variant as a boolean in a robust way (accepts true/false, 1/0, and strings
// like "checked", "1", "true", "on", "yes").
static bool parseTruthy(ArduinoJson::JsonVariantConst v) {
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<int>()) return v.as<int>() == 1;
  if (v.is<const char*>()) {
    const char* p = v.as<const char*>();
    if (!p) return false;
    char buffer[32];
    strncpy(buffer, p, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    // Convertir en minuscules et supprimer les espaces
    for (char* c = buffer; *c; c++) {
      if (*c >= 'A' && *c <= 'Z') *c += 32; // toLowerCase
    }
    
    // Trim leading spaces
    char* start = buffer;
    while (*start == ' ' || *start == '\t') start++;
    
    // Trim trailing spaces
    char* end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t')) {
      *end = '\0';
      end--;
    }
    
    return strcmp(start, "1") == 0 || strcmp(start, "true") == 0 || 
           strcmp(start, "on") == 0 || strcmp(start, "checked") == 0 || 
           strcmp(start, "yes") == 0;
  }
  return false;
}
}

// ------------------------------------------------------------
// NVS helpers: snapshot des actionneurs autour du sleep
// Namespace: "actSnap"
// Keys: pending (bool), aqua (bool), heater (bool), light (bool)
// ------------------------------------------------------------
void Automatism::saveActuatorSnapshotToNVS(bool pumpAquaWasOn, bool heaterWasOn, bool lightWasOn) {
  // Délégation au module Persistence
  AutomatismPersistence::saveActuatorSnapshot(pumpAquaWasOn, heaterWasOn, lightWasOn);
}

bool Automatism::loadActuatorSnapshotFromNVS(bool& pumpAquaWasOn, bool& heaterWasOn, bool& lightWasOn) {
  // Délégation au module Persistence
  return AutomatismPersistence::loadActuatorSnapshot(pumpAquaWasOn, heaterWasOn, lightWasOn);
}

void Automatism::clearActuatorSnapshotInNVS() {
  // Délégation au module Persistence
  AutomatismPersistence::clearActuatorSnapshot();
}

void Automatism::startAquaPumpManualLocal() {
  _lastAquaManualOrigin = ManualOrigin::LOCAL_SERVER;
  // Délégation au module Actuators (implémentation factorisée)
  AutomatismActuators actuators(_acts, _config);
  actuators.startAquaPumpManual();
}

void Automatism::stopAquaPumpManualLocal() {
  _lastAquaStopReason = AquaPumpStopReason::MANUAL;
  _lastAquaManualOrigin = ManualOrigin::LOCAL_SERVER;
  // Délégation au module Actuators
  AutomatismActuators actuators(_acts, _config);
  actuators.stopAquaPumpManual();
}

void Automatism::startHeaterManualLocal() {
  // Délégation au module Actuators
  AutomatismActuators actuators(_acts, _config);
  actuators.startHeaterManual();
}

void Automatism::stopHeaterManualLocal() {
  // Délégation au module Actuators
  AutomatismActuators actuators(_acts, _config);
  actuators.stopHeaterManual();
}

void Automatism::startLightManualLocal() {
  // Délégation au module Actuators
  AutomatismActuators actuators(_acts, _config);
  actuators.startLightManual();
}

void Automatism::stopLightManualLocal() {
  // Délégation au module Actuators
  AutomatismActuators actuators(_acts, _config);
  actuators.stopLightManual();
}

void Automatism::toggleEmailNotifications() {
  // Toggle local de mailNotif
  mailNotif = !mailNotif;
  // Délégation au module Actuators pour sync
  AutomatismActuators actuators(_acts, _config);
  actuators.toggleEmailNotifications();
}

void Automatism::toggleForceWakeup() {
  // Toggle local + mise à jour WebSocket state
  forceWakeUp = !forceWakeUp;
  // Délégation au module Actuators pour save + sync
  AutomatismActuators actuators(_acts, _config);
  actuators.toggleForceWakeup();
}

void Automatism::triggerResetMode() {
  // Trigger Reset Mode - simule l'envoi de resetMode=1 au serveur
  Serial.println(F("[Auto] Reset Mode triggered from web interface"));
  
  // WebSocket immédiat pour feedback utilisateur
  // WebSocket broadcast removed for v11.68
  
  // Synchronisation serveur avec resetMode=1
  if (WiFi.status() == WL_CONNECTED) {
    static TaskHandle_t syncResetModeTaskHandle = nullptr;
    
    // Vérifier si une tâche de sync est déjà en cours
    if (syncResetModeTaskHandle != nullptr && eTaskGetState(syncResetModeTaskHandle) != eDeleted) {
      Serial.println(F("[Auto] ⚠️ Tâche de sync reset mode déjà en cours - skip"));
      return;
    }
    
    BaseType_t result = xTaskCreate([](void* param) {
      TaskHandle_t* taskHandle = (TaskHandle_t*)param;
      vTaskDelay(pdMS_TO_TICKS(50));
      SensorReadings freshReadings = autoCtrl._sensors.read();
      bool success = autoCtrl.sendFullUpdate(freshReadings, "resetMode=1");
      if (success) {
        Serial.println(F("[Auto] ✅ Reset Mode envoyé au serveur"));
      } else {
        Serial.println(F("[Auto] ⚠️ Échec envoi Reset Mode au serveur"));
      }
      *taskHandle = nullptr; // Reset handle avant suppression
      vTaskDelete(NULL);
    }, "sync_resetmode", 4096, &syncResetModeTaskHandle, 1, NULL);
    
    if (result != pdPASS) {
      Serial.println(F("[Auto] ❌ Échec création tâche sync reset mode"));
      syncResetModeTaskHandle = nullptr;
    }
  }
}

Automatism::Automatism(SystemSensors& sensors, SystemActuators& acts, WebClient& web, DisplayView& disp, PowerManager& power, Mailer& mailer, ConfigManager& config)
    : _sensors(sensors)
    , _acts(acts)
    , _web(web)
    , _disp(disp)
    , _power(power)
    , _mailer(mailer)
    , _config(config)
    , _feeding(acts, config, mailer, power)  // Module Feeding
    , _network(web, config)                // Module Network
    , _sleep(power, config)                // Module Sleep
{
  // Initialisation des valeurs par défaut
  mailNotif = false;  // Par défaut, emails désactivés
  mail = Config::DEFAULT_MAIL_TO;
  
  // Diagnostic initial email
  Serial.println(F("[Auto] ===== DIAGNOSTIC EMAIL INITIAL ====="));
  Serial.printf("[Auto] mailNotif par défaut: %s\n", mailNotif ? "TRUE" : "FALSE");
  Serial.printf("[Auto] mail par défaut: '%s'\n", mail.c_str());
  Serial.printf("[Auto] DEFAULT_MAIL_TO: '%s'\n", Config::DEFAULT_MAIL_TO);
  Serial.println(F("[Auto] ======================================"));
}

void Automatism::begin() {
  // ========================================
  // INITIALISATION MODULE NETWORK (v11.32)
  // ========================================
  // CRITIQUE: Doit être fait après LittleFS.begin() pour DataQueue
  Serial.println(F("[Auto] Initialisation module Network..."));
  if (!_network.begin()) {
    Serial.println(F("[Auto] ⚠️ Échec initialisation Network (DataQueue)"));
  }
  
  // Restauration de forceWakeUp depuis la configuration persistante
  forceWakeUp = _config.getForceWakeUp();
  Serial.printf("[Auto] forceWakeUp restauré depuis config: %s\n", forceWakeUp ? "true" : "false");
  
  // WebSocket state removed for v11.68
  
  // v11.66: Configuration callback fin de nourrissage pour reset serveur
  _feeding.setCompletionCallback([this](const char* feedingType) {
    SensorReadings cur = _sensors.read();
    char resetParams[64];
    snprintf(resetParams, sizeof(resetParams), "%s=0", feedingType);
    // Ajouter GPIO fallback si nécessaire
    if (strcmp(feedingType, "bouffePetits") == 0) {
      strncat(resetParams, "&108=0", sizeof(resetParams) - strlen(resetParams) - 1);
    } else if (strcmp(feedingType, "bouffeGros") == 0) {
      strncat(resetParams, "&109=0", sizeof(resetParams) - strlen(resetParams) - 1);
    }
    sendFullUpdate(cur, resetParams);
    Serial.printf("[Auto] Fin nourrissage notifiée au serveur - %s=0\n", feedingType);
    // Fin de cycle: réinitialiser le mode MANUEL pour l'affichage
    _manualFeedingActive = false;
  });
  
  // Protection temporaire contre l'écrasement par le serveur distant
  // pendant les premières secondes après le démarrage
  _wakeupProtectionEnd = millis() + TimingConfig::WAKEUP_PROTECTION_DURATION_MS; // 5 secondes de protection (optimisé)
  
  Serial.println(F("[Auto] Automatismes initialisés"));
  
  // === INITIALISATION SÉCURISÉE DU RESET MODE ===
  // S'assurer que resetMode est défini à 0 au boot pour éviter les resets non désirés
  Serial.println(F("[Auto] 🔒 Initialisation sécurisée resetMode=0 au boot"));
  
  // Log d'initialisation de la marée
  Serial.printf("[Auto] Initialisation marée: _lastDiffMaree=%d\n", _lastDiffMaree);
  
  // Chargement des flags de bouffe depuis la configuration persistante
  lastFeedDay = _config.getLastJourBouf();
  
  LOG_TIME(LOG_INFO, "[Auto] Dernier jour de bouffe: %d", lastFeedDay);

  // Point de départ pour le chronomètre de sommeil automatique
  _lastWakeMs = millis();

  // Charger anti-spam flood depuis NVS
  {
    Preferences prefs;
    prefs.begin("alerts", true);
    lastFloodEmailEpoch = prefs.getULong("floodLast", 0);
    prefs.end();
    Serial.printf("[Auto] lastFloodEmailEpoch restauré: %lu\n", (unsigned long)lastFloodEmailEpoch);
  }

  // Initialisation des timestamps d'activité au boot
  updateActivityTimestamp();
  _lastWebActivityMs = 0;
  _forceWakeFromWeb = false;

  // Initialisation des états précédents pour le suivi des changements
  _prevPumpTankState = _acts.isTankPumpRunning();
  _prevPumpAquaState = _acts.isAquaPumpRunning();
  _prevBouffeMatin   = _config.getBouffeMatinOk();
  _prevBouffeMidi    = _config.getBouffeMidiOk();
  _prevBouffeSoir    = _config.getBouffeSoirOk();

  // Restauration éventuelle d'un snapshot d'actionneurs en attente (après reboot)
  bool snapshotRestored = false;
  {
    bool snapAqua=false, snapHeater=false, snapLight=false;
    if (loadActuatorSnapshotFromNVS(snapAqua, snapHeater, snapLight)) {
      Serial.printf("[Auto] Snapshot actionneurs détecté au boot: aqua=%s heater=%s light=%s\n",
                    snapAqua?"ON":"OFF", snapHeater?"ON":"OFF", snapLight?"ON":"OFF");
      // Restauration sécurisée: la pompe réservoir reste OFF par design
      if (snapAqua)   _acts.startAquaPump();
      if (snapHeater) _acts.startHeater();
      if (snapLight)  _acts.startLight();
      _prevPumpAquaState = snapAqua;
      // Nettoyage du snapshot une fois restauré
      clearActuatorSnapshotInNVS();
      snapshotRestored = true;
      Serial.println(F("[Auto] Snapshot actionneurs restauré et nettoyé"));
    }
  }

  // Chargement des états GPIO actionneurs depuis NVS (chauffage, lumière)
  // Seulement si aucun snapshot n'a été restauré (pour éviter les conflits)
  if (!snapshotRestored) {
    // Aucun snapshot trouvé, charger les états persistants depuis NVS
    GPIOParser::loadGPIOStatesFromNVS(*this);
  } else {
    Serial.println(F("[Auto] Snapshot détecté, états GPIO persistants ignorés"));
  }

  // ------------------ Chargement variables distantes depuis NVS ------------------
  // Restauration des variables distantes depuis la flash (si disponible)
  {
    String cachedJson;
    if (_config.loadRemoteVars(cachedJson) && cachedJson.length()>0) {
      StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
      DeserializationError err = deserializeJson(doc, cachedJson);
      if (!err) {
        // Helper pour assigner si présent (copié de handleRemoteState)
        auto assignIfPresent = [&doc](const char* key, auto& var){
          if(!doc.containsKey(key)) return;
          using T = std::decay_t<decltype(var)>;
          T v = doc[key].as<T>();
          if constexpr (std::is_arithmetic_v<T>) {
            if (v == 0) return; // ignore 0 = non défini
          }
          var = v;
        };
        assignIfPresent("aqThreshold",      aqThresholdCm);
        assignIfPresent("tankThreshold",    tankThresholdCm);
        assignIfPresent("tempsGros", tempsGros);
        assignIfPresent("tempsPetits", tempsPetits);
        assignIfPresent("limFlood",  limFlood);
        if (doc.containsKey("tempsRemplissageSec")) refillDurationMs = doc["tempsRemplissageSec"].as<int>()*1000UL;
        if (doc.containsKey("chauffageThreshold")) {
          float newThreshold = doc["chauffageThreshold"].as<float>();
          if (newThreshold > 0.0f) { // Protection contre valeurs invalides
            heaterThresholdC = newThreshold;
            Serial.printf("[Auto] 🔥 Seuil chauffage restauré depuis NVS: %.1f°C\n", heaterThresholdC);
          }
        }
        if (doc.containsKey("mail")) mail      = doc["mail"].as<const char*>();
        if (doc.containsKey("mailNotif")) {
          mailNotif = parseTruthy(doc["mailNotif"]);
          Serial.printf("[Auto] 📧 mailNotif chargé: '%s' -> mailNotif=%s\n", 
                       doc["mailNotif"].as<const char*>(), mailNotif ? "TRUE" : "FALSE");
        }

        // Nouvelles variables à restaurer
        assignIfPresent("bouffeMatin", bouffeMatin);
        assignIfPresent("bouffeMidi",  bouffeMidi);
        assignIfPresent("bouffeSoir",  bouffeSoir);
        
        // v11.41: Durées nourrissage (format normalisé)
        assignIfPresent("tempsGros", tempsGros);
        assignIfPresent("tempsPetits", tempsPetits);

        // Parcours des clés numériques (>=100) pour restaurer les variables de configuration
        for (auto kv : doc.as<ArduinoJson::JsonObject>()) {
          const char* k = kv.key().c_str();
          bool allDigits = true;
          for (const char* p = k; *p; ++p) { if (!isdigit(*p)) { allDigits = false; break; } }
          if (!allDigits) continue;
          int id = atoi(k);
          if (id >= 100) {
            switch(id){
              case 100: mail = String(kv.value().as<const char*>()); break;
              case 101: mailNotif = parseTruthy(kv.value()); break;
              case 102: aqThresholdCm = kv.value().as<int>(); break;
              case 103: tankThresholdCm = kv.value().as<int>(); break;
              case 104: {
                float newThreshold = kv.value().as<float>();
                if (newThreshold > 0.0f) { // Protection contre valeurs invalides
                  heaterThresholdC = newThreshold;
                  Serial.printf("[Auto] 🔥 Seuil chauffage restauré (GPIO 104): %.1f°C\n", heaterThresholdC);
                }
              } break;
              case 105: { int v = kv.value().as<int>(); if (v) bouffeMatin = v; } break;
              case 106: { int v = kv.value().as<int>(); if (v) bouffeMidi = v; } break;
              case 107: { int v = kv.value().as<int>(); if (v) bouffeSoir = v; } break;
              case 111: {
                int v = kv.value().as<int>();
                if (v > 0) tempsGros = v; // ignore 0 pour conserver la durée existante
              } break;
              case 112: {
                int v = kv.value().as<int>();
                if (v > 0) tempsPetits = v; // idem
              } break;
              case 113: refillDurationMs = kv.value().as<int>() * 1000UL; break;
              case 114: limFlood = kv.value().as<int>(); break;
              case 115: {
                // CRITIQUE: On n'accepte que les valeurs explicitement "true" pour éviter l'écrasement
                auto v = kv.value();
                if (v.is<bool>() && v.as<bool>())      forceWakeUp = true;
                else if (v.is<int>() && v.as<int>() == 1)  forceWakeUp = true;
                else if (v.is<const char*>()) {
                  const char* p = v.as<const char*>();
                  if (!p) continue;
                  char buffer[32];
                  strncpy(buffer, p, sizeof(buffer) - 1);
                  buffer[sizeof(buffer) - 1] = '\0';
                  
                  // Convertir en minuscules et supprimer les espaces
                  for (char* c = buffer; *c; c++) {
                    if (*c >= 'A' && *c <= 'Z') *c += 32; // toLowerCase
                  }
                  
                  // Trim leading spaces
                  char* start = buffer;
                  while (*start == ' ' || *start == '\t') start++;
                  
                  // Trim trailing spaces
                  char* end = start + strlen(start) - 1;
                  while (end > start && (*end == ' ' || *end == '\t')) {
                    *end = '\0';
                    end--;
                  }
                  forceWakeUp = (strcmp(start, "1") == 0 || strcmp(start, "true") == 0 || 
                                strcmp(start, "on") == 0 || strcmp(start, "checked") == 0);
                }
                // Sauvegarde immédiate pour persistance
                _config.setForceWakeUp(forceWakeUp);
              } break;
              case 116: freqWakeSec = kv.value().as<int>(); break;
              default: break;
            }
          }
        }

        // Supporte également les clés textuelles "WakeUp" et "FreqWakeUp" pour
        // garantir la persistance de l'état de veille forcée même après un reboot.
        if (doc.containsKey("WakeUp")) {
          auto v = doc["WakeUp"];
          // CRITIQUE: On n'accepte que les valeurs explicitement "true" pour éviter l'écrasement
          if (v.is<bool>() && v.as<bool>())      forceWakeUp = true;
          else if (v.is<int>() && v.as<int>() == 1)  forceWakeUp = true;
          else if (v.is<const char*>()) {
            const char* p = v.as<const char*>();
            if (!p) return;
            char buffer[32];
            strncpy(buffer, p, sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            
            // Convertir en minuscules et supprimer les espaces
            for (char* c = buffer; *c; c++) {
              if (*c >= 'A' && *c <= 'Z') *c += 32; // toLowerCase
            }
            
            // Trim leading spaces
            char* start = buffer;
            while (*start == ' ' || *start == '\t') start++;
            
            // Trim trailing spaces
            char* end = start + strlen(start) - 1;
            while (end > start && (*end == ' ' || *end == '\t')) {
              *end = '\0';
              end--;
            }
            forceWakeUp = (strcmp(start, "1") == 0 || strcmp(start, "true") == 0 || 
                          strcmp(start, "on") == 0 || strcmp(start, "checked") == 0);
          }
          // Sauvegarde immédiate pour persistance
          _config.setForceWakeUp(forceWakeUp);
        }
        if (doc.containsKey("FreqWakeUp")) {
          int fv = doc["FreqWakeUp"].as<int>();
          if (fv > 0) freqWakeSec = fv;
        }
        
        // v11.40: Propager les durées et horaires de nourrissage au module Feeding
        _feeding.setDurations(tempsGros, tempsPetits);
        _feeding.setSchedule(bouffeMatin, bouffeMidi, bouffeSoir);
        Serial.printf("[Auto] Config Feeding: Durées %us/%us, Horaires %uh/%uh/%uh\n",
                     tempsGros, tempsPetits, bouffeMatin, bouffeMidi, bouffeSoir);

        Serial.println(F("[Auto] Variables distantes restaurées depuis NVS"));
        Serial.printf("[Auto] forceWakeUp restauré: %s\n", forceWakeUp ? "true" : "false");
      }
    }
  }
  
  // Synchronisation initiale de forceWakeUp avec le serveur distant
  // pour s'assurer que la valeur restaurée depuis la NVS est bien reflétée côté serveur
  // ATTENTION: Cette synchronisation est différée pour éviter les conflits de timing
  // avec les premières requêtes du serveur distant
  if (WiFi.status() == WL_CONNECTED) {
    // Délai pour laisser le temps au serveur de traiter les changements
    vTaskDelay(pdMS_TO_TICKS(3000)); // Augmenté à 3 secondes pour plus de sécurité
    
    SensorReadings initialReadings = _sensors.read();
    sendFullUpdate(initialReadings, nullptr); // Envoie forceWakeUp via le champ WakeUp
    Serial.println(F("[Auto] Synchronisation initiale de forceWakeUp envoyée au serveur (après délai)"));
  }
}

void Automatism::update() {
  SensorReadings r = _sensors.read();
  update(r);
}

void Automatism::updateDisplay() {
  // Mise à jour de l'affichage OLED uniquement (appelée par la tâche dédiée)
  if (!_disp.isPresent()) return;
  // Si des messages diagnostics sont affichés, on ne dessine pas le fond pour éviter toute superposition
  if (_disp.isLocked()) return;
  
  // Mécanisme de sécurité : forcer la fin du splash après 5 secondes maximum
  static unsigned long splashStartTime = 0;
  if (splashStartTime == 0) {
    splashStartTime = millis();
  } else if (millis() - splashStartTime > 5000) {
    // Si le splash dure plus de 5 secondes, le forcer à se terminer
    _disp.forceEndSplash();
    splashStartTime = 0; // Reset pour éviter les appels répétés
  }
  
  const uint32_t currentMillis = millis();
  
  // Gestion du changement d'écran toutes les 4 secondes (optimisé pour dynamisme)
  if (_lastScreenSwitch == 0) {
    // Initialisation du chronomètre au tout premier affichage
    _lastScreenSwitch = currentMillis;
  } else if (currentMillis - _lastScreenSwitch >= screenSwitchInterval) {
    _oledToggle = !_oledToggle;           // Bascule d'écran
    _lastScreenSwitch += screenSwitchInterval; // décale pour le prochain switch
    // Force un rafraîchissement immédiat lors du changement d'écran
    _disp.resetMainCache();
    _disp.resetStatusCache();
  }
  
  // Affichage avec cadence dynamique (250ms en décompte, 1000ms sinon)
  const uint32_t displayInterval = getRecommendedDisplayIntervalMs();
  if (currentMillis - _lastOled >= displayInterval) {
    _lastOled = currentMillis;
    
    // Log de debug pour diagnostiquer (réduit à toutes les 10 secondes)
    static unsigned long lastDebugLog = 0;
    if (currentMillis - lastDebugLog >= 10000) { // Log toutes les 10 secondes
      Serial.printf("[Display] updateDisplay appelée - OLED présent: %s\n", _disp.isPresent() ? "OUI" : "NON");
      lastDebugLog = currentMillis;
    }
    
    // Utilise les dernières mesures fournies par sensorTask pour éviter
    // les lectures capteurs répétées à chaque frame d'affichage
    SensorReadings readings = _lastReadings;
    
    // Vérification des valeurs pour détecter les erreurs
    if (readings.tempWater < -50 || readings.tempWater > 100 ||
        readings.tempAir < -50 || readings.tempAir > 100 ||
        readings.humidity < 0 || readings.humidity > 100) {
      Serial.println(F("[Display] Erreur lecture capteurs, utilisation valeurs par défaut"));
      // Valeurs par défaut en cas d'erreur (NaN pour les températures car 0°C est impossible)
      readings.tempWater = NAN;
      readings.tempAir = NAN;
      readings.humidity = NAN;
      readings.wlAqua = 0;
      readings.wlTank = 0;
      readings.wlPota = 0;
      readings.luminosite = 0;
    }
    
    // Déterminer si on est en mode décompte (nécessite une mise à jour fluide)
    bool isCountdownMode = isStillPending(_countdownEnd, currentMillis);
    
    // Protection contre les conflits d'affichage : si on est en mode nourrissage, on force le mode décompte
    if (_currentFeedingPhase != FeedingPhase::NONE) {
      isCountdownMode = true;
    }
    
    // Mode immédiat réduit à 25% du temps au lieu de 50%
    bool forceImmediateMode = isCountdownMode || 
                             (_currentFeedingPhase != FeedingPhase::NONE) ||
                             (currentMillis % 1000 < 250); // Mode immédiat 25% du temps
    
    _disp.setUpdateMode(forceImmediateMode);
    
    if (isCountdownMode) {
      // Affichage d'un compte à rebours lors des phases nourrissage / remplissage
      if (_currentFeedingPhase != FeedingPhase::NONE) {
        // Gestion des phases de nourrissage
        if (hasExpired(_feedingPhaseEnd, currentMillis) && _currentFeedingPhase == FeedingPhase::FEEDING_FORWARD) {
          // Transition vers la phase arrière
          _currentFeedingPhase = FeedingPhase::FEEDING_BACKWARD;
          _feedingPhaseEnd = _feedingTotalEnd; // La phase arrière dure jusqu'à la fin totale
        }
        
        // Affichage selon la phase
        if (_currentFeedingPhase == FeedingPhase::FEEDING_FORWARD) {
        uint32_t secLeft32 = remainingMs(_feedingPhaseEnd, currentMillis) / 1000UL;
          uint16_t secLeft = (secLeft32 > 65535u) ? 65535u : (uint16_t)secLeft32;
          _disp.showFeedingCountdown("Nourrissage", "avant", secLeft, (_manualFeedingActive || isFeedingInManualMode()));
        } else if (_currentFeedingPhase == FeedingPhase::FEEDING_BACKWARD) {
        uint32_t secLeft32 = remainingMs(_feedingTotalEnd, currentMillis) / 1000UL;
          uint16_t secLeft = (secLeft32 > 65535u) ? 65535u : (uint16_t)secLeft32;
          _disp.showFeedingCountdown("Nourrissage", "arriere", secLeft, (_manualFeedingActive || isFeedingInManualMode()));
        }
        
        // Réinitialisation quand le cycle est terminé
        if (hasExpired(_feedingTotalEnd, currentMillis)) {
          _currentFeedingPhase = FeedingPhase::NONE;
          _manualFeedingActive = false; // s'assurer de sortir du mode manuel
          // Notifier immédiatement le serveur distant que les flags nourrissage reviennent à 0
          if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
            // Envoi sélectif: on remet les deux à 0 pour éviter tout état bloqué
            SensorReadings cur = _sensors.read();
            sendFullUpdate(cur, "bouffePetits=0&bouffeGros=0&108=0&109=0");
            Serial.println(F("[Auto] ✅ Fin nourrissage notifiée au serveur - bouffePetits=0, bouffeGros=0"));
          }
        }
      } else {
        // Affichage standard pour les autres types de décompte (remplissage, etc.)
        uint32_t secLeft32 = remainingMs(_countdownEnd, currentMillis) / 1000UL;
        uint16_t secLeft = (secLeft32 > 65535u) ? 65535u : (uint16_t)secLeft32;
        _disp.showCountdown(_countdownLabel.c_str(), secLeft, isRefillingInManualMode());
      }
    } else {
      // Mode optimisé pour les affichages normaux
      _disp.beginUpdate();
      
      if (_oledToggle) {
        // Affichage principal (avec heure + secondes)
        _disp.showMain(readings.tempWater, readings.tempAir, readings.humidity, readings.wlAqua,
                       readings.wlTank, readings.wlPota, readings.luminosite, _power.getCurrentTimeString());
      } else {
        // Affichage des variables serveur / configuration distante
        _disp.showServerVars(_acts.isAquaPumpRunning(), _acts.isTankPumpRunning(), _acts.isHeaterOn(), _acts.isLightOn(),
                             bouffeMatin, bouffeMidi, bouffeSoir,
                             tempsPetits, tempsGros,
                             aqThresholdCm, tankThresholdCm, heaterThresholdC,
                             refillDurationMs / 1000, limFlood,
                             forceWakeUp, static_cast<uint16_t>(freqWakeSec));
      }

      // Barre d'état en bas de l'écran (icônes réseau, flèches transfert…)
      int diffNow = _sensors.diffMaree(readings.wlAqua);
      int8_t tideDir = 0;
      if (diffNow > tideTriggerCm) tideDir = 1;       // montante si + > seuil
      else if (diffNow < -tideTriggerCm) tideDir = -1; // descendante si - < -seuil
      _lastDiffMaree = diffNow;

      bool blinkNow = (mailBlinkUntil && isStillPending(mailBlinkUntil, currentMillis) && ((currentMillis / 200) % 2));
      _disp.drawStatus(sendState, recvState, WiFi.isConnected() ? WiFi.RSSI() : -127,
                       blinkNow, tideDir, diffNow);
      
      // Fin de la mise à jour d'affichage (flush automatique)
      _disp.endUpdate();
    }
    
    if (hasExpired(mailBlinkUntil, currentMillis)) mailBlinkUntil = 0;
  }
}

uint32_t Automatism::getRecommendedDisplayIntervalMs() {
  uint32_t now = millis();
  bool isCountdownMode = (isStillPending(_countdownEnd, now)
                          || (_currentFeedingPhase != FeedingPhase::NONE));
  return isCountdownMode ? 250u : 1000u;
}

void Automatism::update(const SensorReadings& readings) {
  // Met à jour le cache des dernières mesures
  _lastReadings = readings;
  // ========================================
  // PRIORITÉ ABSOLUE : NOURRISSAGE ET REMPLISSAGE
  // ========================================
  
  // 1. Vérification d'un nouveau jour pour reset des flags de bouffe (CRITIQUE)
  checkNewDay();

  // 2. Gestion des automatismes CRITIQUES en priorité absolue
  // Ces fonctions doivent s'exécuter en premier pour garantir le respect des temps
  handleFeeding();        // PRIORITÉ 1 : Nourrissage automatique (temps critiques)
  handleRefill(readings); // PRIORITÉ 2 : Remplissage automatique (temps critiques)
  
  // 3. Gestion des automatismes secondaires
  handleMaree(readings);
  handleAlerts(readings);

  // 4. Récupération de l'état distant - PROTECTION ANTI-RESET IMMÉDIAT
  // Chargement avec délai de sécurité pour éviter les resets immédiats après boot
  static bool firstUpdateDone = false;
  static bool bootResetModeSent = false;
  static uint32_t bootTime = millis();
  
  if (!firstUpdateDone) {
    // Attendre 10 secondes après le boot avant la première récupération
    // pour éviter les resets immédiats si le serveur a encore resetMode=1
    if (millis() - bootTime > 10000) {
      firstUpdateDone = true;
      Serial.println(F("[Auto] Première récupération d'état distant activée (délai sécurité 10s)"));
    } else {
      Serial.printf("[Auto] Attente sécurité avant première récupération: %lu ms restants\n", 
                    10000 - (millis() - bootTime));
      
      // === ENVOI IMMÉDIAT DES DONNÉES LOCALES AU BOOT ===
      // v11.69: Envoyer d'abord les données locales, puis récupérer les données distantes
      if (!bootResetModeSent && WiFi.status() == WL_CONNECTED) {
        Serial.println(F("[Auto] 🔄 Envoi immédiat des données locales au boot"));
        SensorReadings bootReadings = _sensors.read();
        bool success = sendFullUpdate(bootReadings, "resetMode=0");
        if (success) {
          Serial.println(F("[Auto] ✅ Données locales envoyées avec succès au boot"));
          // Attendre confirmation avant de récupérer les données distantes
          vTaskDelay(pdMS_TO_TICKS(1000)); // 1 seconde de délai
        } else {
          Serial.println(F("[Auto] ⚠️ Échec envoi données locales au boot"));
        }
        bootResetModeSent = true;
      }
      
      return; // Skip handleRemoteState() pendant les 10 premières secondes
    }
  }
  
  // v11.74: Toujours tenter la récupération distante (polling interne gère l'intervalle)
  // L'état d'envoi précédent n'empêche plus la récupération
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[Auto] ▶️ Récupération distante (forcer polling)"));
    handleRemoteState();
  } else {
    Serial.println(F("[Auto] ⚠️ WiFi déconnecté - skip récupération distante"));
  }

  // 5. Synchronisation serveur différée (actions manuelles locales)
  AutomatismActuators actuators(_acts, _config);
  if (actuators.needsSync() && WiFi.status() == WL_CONNECTED) {
    String payload = actuators.consumePendingSyncPayload();
    bool sent = sendFullUpdate(readings, payload.c_str());
    if (sent) {
      Serial.println("[Auto] ✅ Sync serveur réussie (action manuelle locale)");
    } else {
      Serial.println("[Auto] ⚠️ Sync serveur échec - sera retentée au prochain cycle");
      // Note: Si échec, le marqueur est déjà consommé, mais sendFullUpdate
      // utilise la DataQueue pour retry automatique
    }
  }

  // 6. Envoi périodique des mesures distantes (PRIORITÉ HAUTE - AVANT SLEEP)
  // Déplacé avant le check de sleep pour garantir l'envoi toutes les 2 minutes
  unsigned long currentMillis = millis();
  if (currentMillis - lastSend > sendInterval) {
    sendState = 0; // transfert en cours : flèche vide
    // Envoi complet des mesures + variables (resetMode=0 pour acquittement permanent)
    bool okSend = sendFullUpdate(readings, "resetMode=0");
    // Rafraîchit immédiatement l'icône pour indiquer l'envoi en cours
    if (_disp.isPresent()) {
    int diffNow = _sensors.diffMaree(readings.wlAqua);
    int8_t tideDir = 0;
    if (diffNow > tideTriggerCm) tideDir = 1;        // montante ~fenêtre
    else if (diffNow < -tideTriggerCm) tideDir = -1; // descendante ~fenêtre
    _lastDiffMaree = diffNow;
      
      // Log pour debug affichage marée (section envoi)
      if (diffNow > 10) { // Seulement si valeur suspecte
        Serial.printf("[OLED-Send] Affichage marée: diff=%d, dir=%d, niveau_actuel=%u\n", 
                      diffNow, tideDir, readings.wlAqua);
      }
      
      // Mise à jour optimisée de l'état d'envoi avec clignotement plus rapide
      _disp.beginUpdate();
      bool blinkNow2 = (mailBlinkUntil && isStillPending(mailBlinkUntil, currentMillis) && ((currentMillis/200)%2));
      _disp.drawStatus(sendState, recvState, WiFi.isConnected()?WiFi.RSSI():-127,
                       blinkNow2, tideDir, diffNow); // Clignotement plus rapide (200ms)
      _disp.endUpdate();
    }

    sendState = okSend ? 1 : -1;
    serverOk = okSend;
    lastSend = currentMillis;
  }

  // ========================================
  // PRIORITÉ HAUTE : ENTRÉE EN LIGHTSLEEP
  // ========================================
  // Vérification précoce pour l'entrée en sleep - APRÈS envoi POST
  // Ne pas interférer avec nourrissage/remplissage mais prioritaire sur le reste
  if (shouldEnterSleepEarly(readings)) {
    // Entrée en sleep immédiate si les conditions sont remplies
    handleAutoSleep(readings);
    return; // Sortie immédiate après sleep
  }

  // 6. Vérification finale pour l'entrée en sleep (fallback)
  // Appelé seulement si shouldEnterSleepEarly() n'a pas déjà déclenché le sleep
  handleAutoSleep(readings);

  // 7. Vérifie et remonte immédiatement les changements critiques
  checkCriticalChanges();
  
  // 8. Mise à jour des timestamps d'activité
  // updateActivityTimestamp(); // Supprimé: appelé seulement au boot et à la sortie de veille
  
  // 9. Sauvegarde périodique de l'état
  static unsigned long lastSave = 0;
  if (currentMillis - lastSave > 60000) {
    lastSave = currentMillis;
    saveFeedingState();
  }
}

void Automatism::checkNewDay() {
  // Délégation au module Feeding
  _feeding.checkNewDay();
}



void Automatism::saveFeedingState() {
  // Sauvegarde forceWakeUp (reste dans Automatism)
  _config.setForceWakeUp(forceWakeUp);
  // Délégation au module Feeding pour le reste
  _feeding.saveFeedingState();
}

void Automatism::handleRefill(const SensorReadings& r) {
  // ========================================
  // FONCTION CRITIQUE : REMPLISSAGE AUTOMATIQUE
  // PRIORITÉ ABSOLUE - TEMPS D'ACTIVATION CRITIQUES
  // ========================================
  
  unsigned long currentMillis = millis();
  
  // ========================================
  // 0. SÉCURITÉ AQUARIUM TROP PLEIN
  // Empêche le remplissage si l'aquarium est déjà trop plein (distance < limFlood)
  // ========================================
  if (r.wlAqua < limFlood) {
    if (!tankPumpLocked || _tankPumpLockReason != TankPumpLockReason::AQUARIUM_OVERFILL) {
      tankPumpLocked = true;
      _tankPumpLockReason = TankPumpLockReason::AQUARIUM_OVERFILL;
      if (_acts.isTankPumpRunning()) {
        _acts.stopTankPump(_pumpStartMs);
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY; // réutilisé pour motif sécurité
        _countdownEnd = 0;
        // Notifier immédiatement le serveur de l'arrêt
        if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
          SensorReadings cur = _sensors.read();
          sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
          Serial.println(F("[Auto] ✅ Arrêt sécurité notifié au serveur - pump_tank=0"));
        }
      }
      Serial.println(F("[CRITIQUE] Sécurité: aquarium trop plein – pompe réservoir verrouillée"));
    }
  } else {
    // Déverrouillage automatique si l'aquarium n'est plus en état de trop-plein
    if (tankPumpLocked && _tankPumpLockReason == TankPumpLockReason::AQUARIUM_OVERFILL && !inFlood) {
      tankPumpLocked = false;
      _tankPumpLockReason = TankPumpLockReason::NONE;
      emailTankSent = false;
      Serial.println(F("[Auto] Pompe réservoir déverrouillée (aquarium OK)"));
    }
  }
  
  // ========================================
  // 1. CONDITION DE DÉMARRAGE - PRIORITÉ ABSOLUE
  // ========================================
  // CORRECTION v11.10: Vérification supplémentaire pour éviter les cycles infinis
  // Si la pompe n'est pas en cours et qu'on est en mode manuel, réinitialiser
  if (!_acts.isTankPumpRunning() && _manualTankOverride) {
    Serial.println(F("[CRITIQUE] Mode manuel détecté sans pompe active - réinitialisation"));
    _manualTankOverride = false;
    _countdownEnd = 0;
    _pumpStartMs = 0;
  }
  
  if (r.wlAqua > aqThresholdCm && !tankPumpLocked && tankPumpRetries < MAX_PUMP_RETRIES && !_manualTankOverride) {
    // Correction: l'état de verrouillage de la pompe aquarium ne doit pas
    // bloquer le démarrage de la pompe réservoir.
    if (!_acts.isTankPumpRunning()) {
      // Sécurité pré-démarrage: ne pas démarrer si la réserve est trop basse
      if (r.wlTank > tankThresholdCm) {
        Serial.printf("[CRITIQUE] Démarrage bloqué: réserve trop basse (distance=%u cm > seuil=%u cm)\n", r.wlTank, tankThresholdCm);
        // Verrouille immédiatement pour éviter des tentatives répétées inutiles
        tankPumpLocked = true;
        _tankPumpLockReason = TankPumpLockReason::RESERVOIR_LOW;
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
        _countdownEnd = 0;
        if (mailNotif && !emailTankSent) {
          char msg[512];
          snprintf(msg, sizeof(msg), 
            "Démarrage REMPLISSAGE bloqué (réserve trop basse)\n"
            "- Réserve (distance): %d cm (seuil: %d cm)\n"
            "- Aqua: %d cm (seuil: %d cm)\n"
            "Déblocage: lorsque la distance réservoir < %d cm (confirmée).",
            r.wlTank, tankThresholdCm, r.wlAqua, aqThresholdCm, (int)tankThresholdCm - 5);
          _mailer.sendAlert("Pompe réservoir BLOQUÉE (réserve basse)", msg, mail.c_str());
          emailTankSent = true;
        }
        return; // ne pas démarrer
      }
      Serial.println(F("[CRITIQUE] === DÉBUT REMPLISSAGE AUTOMATIQUE ==="));
      Serial.printf("[CRITIQUE] Niveau aquarium: %d cm, Seuil: %d cm\n", r.wlAqua, aqThresholdCm);
      Serial.printf("[CRITIQUE] Durée configurée: %lu s\n", refillDurationMs / 1000);
      
      // DÉMARRAGE IMMÉDIAT - PRIORITÉ ABSOLUE
      uint32_t startTime = millis();
      
      _acts.startTankPump();
      _countdownLabel = "Refill";
      _countdownEnd = millis() + refillDurationMs;
      _pumpStartMs = millis(); // wrap-safe start time in ms
      _levelAtPumpStart = r.wlAqua;
      logActivity("Démarrage pompe réservoir automatique");
      
      // Envoi immédiat des données vers le serveur distant pour enregistrer l'activation de la pompe
  if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
        SensorReadings currentReadings = _sensors.read();
        sendFullUpdate(currentReadings, "etatPompeTank=1");
        Serial.println(F("[Auto] Données envoyées au serveur - pompe réservoir activée"));
      }
      
      // Notification e-mail au démarrage AUTO de la pompe réservoir
      // CORRECTION : Mail dupliqué supprimé - le mail sera envoyé par checkCriticalChanges()
      // pour éviter les doublons avec le système générique de détection des changements
      // Le mail contiendra les mêmes informations via le système unifié
      
      uint32_t executionTime = (uint32_t)(millis() - startTime);
      Serial.printf("[CRITIQUE] Pompe démarrée en %u ms\n", (unsigned)executionTime);
      Serial.printf("[CRITIQUE] Démarrage pompe réservoir (niveau: %d cm, seuil: %d cm, durée: %lu s)\n", 
                   r.wlAqua, aqThresholdCm, refillDurationMs / 1000);
      Serial.println(F("[CRITIQUE] === REMPLISSAGE EN COURS ==="));
    }
  }

  // ========================================
  // 1.5. FIN CYCLE MANUEL : Auto-stop + notification serveur (v11.66)
  // ========================================
  if (_manualTankOverride && _acts.isTankPumpRunning()) {
    if (_countdownEnd > 0 && millis() >= _countdownEnd) {
      Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE MANUEL (timeout) ==="));
      
      // Arrêt physique
      _acts.stopTankPump(_pumpStartMs);
      _lastTankStopReason = TankPumpStopReason::MANUAL;
      
      // Reset flags
      _manualTankOverride = false;
      _countdownEnd = 0;
      _pumpStartMs = 0;
      
      // Notification serveur : pump_tank=0 pour reset état distant
      if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
        SensorReadings cur = _sensors.read();
        bool success = sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
        if (success) {
          Serial.println(F("[Auto] ✅ Fin cycle notifiée au serveur - pump_tank=0"));
        } else {
          Serial.println(F("[Auto] ⚠️ Échec notification fin cycle au serveur - sera retentée"));
        }
      } else {
        Serial.println(F("[Auto] ⚠️ WiFi déconnecté - notification fin cycle reportée"));
      }
    }
  }

  // ========================================
  // 2. ARRÊT FORCÉ APRÈS DURÉE MAX - SÉCURITÉ CRITIQUE
  // ========================================
  if (_acts.isTankPumpRunning()) {
    const uint32_t nowMs = millis();
    const uint32_t elapsedMs = (uint32_t)(nowMs - _pumpStartMs); // wrap-safe
    const uint32_t maxMs = refillDurationMs;
    // Guard against absurd elapsed (e.g., uninitialized start or unit mismatch)
    if ((elapsedMs / 1000U) > 3000000U) {
      Serial.printf("[CRITIQUE] Anomaly: elapsed=%u ms (>3e6 s). Resetting pumpStartMs to now. nowMs=%u, startMs(old)=%u, maxMs=%u\n",
                    (unsigned)elapsedMs, (unsigned)nowMs, (unsigned)_pumpStartMs, (unsigned)maxMs);
      _pumpStartMs = nowMs;
    }
    if (elapsedMs >= maxMs) {
      Serial.printf("[CRITIQUE] Timing stop diag: nowMs=%u, startMs=%u, elapsedMs=%u, maxMs=%u\n",
                    (unsigned)nowMs, (unsigned)_pumpStartMs, (unsigned)elapsedMs, (unsigned)maxMs);
      Serial.println(F("[CRITIQUE] === ARRÊT FORCÉ REMPLISSAGE ==="));
      Serial.printf("[CRITIQUE] Durée écoulée: %u s, Durée max: %u s\n", (unsigned)(elapsedMs/1000U), (unsigned)(maxMs/1000U));
    Serial.println(F("[CRITIQUE] === ARRÊT FORCÉ REMPLISSAGE ==="));
    Serial.printf("[CRITIQUE] Durée écoulée: %lu s, Durée max: %lu s\n", 
                 (currentMillis - _pumpStartMs) / 1000, refillDurationMs / 1000);
    
    // ARRÊT IMMÉDIAT - SÉCURITÉ CRITIQUE
      const uint32_t stopStartMs = millis();

    // Raison: durée maximale atteinte
    _lastTankStopReason = TankPumpStopReason::MAX_DURATION;

      _acts.stopTankPump(_pumpStartMs);
      _pumpStartMs = 0; // reset cycle start
      _countdownEnd = 0;
      _manualTankOverride = false; // fin de la phase manuelle forcée
      
      // Notification serveur : pump_tank=0 pour reset état distant (arrêt forcé)
      if (WiFi.status() == WL_CONNECTED) {
        SensorReadings cur = _sensors.read();
        bool success = sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
        if (success) {
          Serial.println(F("[Auto] ✅ Fin cycle forcé notifiée au serveur - pump_tank=0"));
        } else {
          Serial.println(F("[Auto] ⚠️ Échec notification fin cycle forcé au serveur"));
        }
      }

    // Évalue l'efficacité : le niveau doit avoir augmenté d'au moins 1 cm depuis le début
    int levelImprovement = _levelAtPumpStart - r.wlAqua;
    Serial.printf("[CRITIQUE] Amélioration du niveau: %d cm\n", levelImprovement);
    
    if (levelImprovement < 1) {
      // Aucun progrès significatif : augmentation du compteur de tentatives
      ++tankPumpRetries;
      Serial.printf("[CRITIQUE] Pompe inefficace (%u/%u) – niveau %d cm (début: %d cm, amélioration: %d cm)\n", 
                   tankPumpRetries, MAX_PUMP_RETRIES, r.wlAqua, _levelAtPumpStart, levelImprovement);
      if (tankPumpRetries >= MAX_PUMP_RETRIES) {
        tankPumpLocked = true; // On abandonne jusqu'au prochain redémarrage manuel
        _tankPumpLockReason = TankPumpLockReason::INEFFICIENT;
        Serial.println(F("[CRITIQUE] Pompe réservoir BLOQUÉE – plus d'essais"));
        // Notification instantanée vers serveur / email éventuel
        sendFullUpdate(r, "etatPompeTank=0&pump_tankCmd=0&pump_tank=0");
        if (mailNotif && !emailTankSent) {
          char msg[1024];
          snprintf(msg, sizeof(msg), 
            "La pompe de réserve a été BLOQUÉE (inefficacité)\n"
            "- Tentatives consécutives: %d/%d\n"
            "- Amélioration mesurée: %.1f cm (min attendu: 1 cm)\n"
            "- Aqua: %d cm, Réserve: %d cm\n"
            "- Seuils: aqThreshold=%d, tankThreshold=%d cm\n"
            "\nCe qui permettra le déblocage:\n"
            "- Déblocage automatique: après ~30 s, si le niveau de réserve < %d cm\n"
            "- Ou action manuelle: amorcer la pompe, vérifier tuyaux/filtres/prises d'air, s'assurer que la réserve est suffisante, puis relancer un cycle.\n",
            tankPumpRetries, (unsigned)MAX_PUMP_RETRIES, levelImprovement, r.wlAqua, r.wlTank, 
            aqThresholdCm, tankThresholdCm, (int)tankThresholdCm - 10);
          _mailer.sendAlert("Pompe réservoir bloquée", msg, mail.c_str());
          emailTankSent = true;
          Serial.println(F("[CRITIQUE] Email d'alerte envoyé"));
        }
      }
    } else {
      // Succès : le niveau a augmenté → on réinitialise le compteur
      tankPumpRetries = 0;
      Serial.printf("[CRITIQUE] Remplissage réussi: niveau amélioré de %d cm\n", levelImprovement);
    }
    
      const uint32_t stopExecMs = (uint32_t)(millis() - stopStartMs);
      Serial.printf("[CRITIQUE] Arrêt effectué en %u ms\n", (unsigned)stopExecMs);
      Serial.println(F("[CRITIQUE] Arrêt pompe réservoir (durée écoulée)"));
      Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE ==="));
    }
  }

  // ========================================
  // 3. ARRÊT ANTICIPÉ DÉSACTIVÉ - GARANTIE DE CYCLE COMPLET
  // ========================================
  // COMPORTEMENT ATTENDU: Une fois la pompe démarrée (auto ou manuel), 
  // elle doit terminer son cycle complet (refillDurationMs) même si le niveau 
  // d'eau repasse au-dessus de la limite de remplissage.
  // 
  // RAISON: Éviter les cycles courts qui peuvent causer des problèmes de stabilité
  // et garantir un remplissage efficace même si le niveau fluctue.
  // 
  // NOTE: En mode manuel, la pompe doit terminer son cycle complet
  // NOTE: En mode auto, la pompe doit aussi terminer son cycle complet une fois démarrée
  // L'arrêt anticipé n'est plus autorisé pour garantir la stabilité du cycle
  if (false && !_manualTankOverride && _acts.isTankPumpRunning() && r.wlAqua <= aqThresholdCm) {
    // Cette condition est désactivée pour garantir que la pompe termine toujours son cycle complet
    Serial.println(F("[CRITIQUE] === ARRÊT ANTICIPÉ REMPLISSAGE AUTO ==="));
    Serial.printf("[CRITIQUE] Niveau atteint: %d cm, Seuil: %d cm\n", r.wlAqua, aqThresholdCm);
    
    // ARRÊT IMMÉDIAT - OPTIMISATION (mode auto uniquement)
    uint32_t stopTime = millis();
    
    _acts.stopTankPump(_pumpStartMs);
    _countdownEnd = 0;
    _manualTankOverride = false;
    tankPumpRetries = 0; // remise à zéro – succès
    
    uint32_t stopExecutionTime = (uint32_t)(millis() - stopTime);
    Serial.printf("[CRITIQUE] Arrêt anticipé effectué en %u ms\n", (unsigned)stopExecutionTime);
    Serial.println(F("[CRITIQUE] === REMPLISSAGE AUTO TERMINÉ AVEC SUCCÈS ==="));
  }

  // ========================================
  // 4. SÉCURITÉ DE DÉBORDEMENT RÉSERVOIR - PROTECTION CRITIQUE
  // ========================================
  if (!_manualTankOverride) {
    // Hystérésis renforcée: nécessite 2 confirmations consécutives au-dessus du seuil pour lock,
    // et 3 confirmations consécutives en dessous (avec marge -5 cm) pour unlock.
    static uint8_t aboveCount = 0;
    static uint8_t belowCount = 0;
    if (r.wlTank > tankThresholdCm) {
      aboveCount = min<uint8_t>(aboveCount + 1, 3);
      belowCount = 0;
      if (!tankPumpLocked && aboveCount >= 2) {
        Serial.println(F("[CRITIQUE] === SÉCURITÉ RÉSERVE BASSE ==="));
        Serial.printf("[CRITIQUE] Niveau réservoir (distance): %d cm, Seuil: %d cm\n", r.wlTank, tankThresholdCm);
        tankPumpLocked = true;
        _tankPumpLockReason = TankPumpLockReason::RESERVOIR_LOW;
        _acts.stopTankPump(_pumpStartMs);
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
        _countdownEnd = 0;
        Serial.println(F("[CRITIQUE] Sécurité: pompe réservoir verrouillée (réserve trop basse confirmée)"));
        Serial.println(F("[CRITIQUE] === PROTECTION ACTIVÉE ==="));
        if (mailNotif && !emailTankSent) {
          char msg[512];
          snprintf(msg, sizeof(msg), 
            "La pompe de réserve a été VERROUILLÉE (réserve trop basse)\n"
            "- Réserve (distance): %d cm (seuil: %d cm)\n"
            "- Aqua: %d cm\n"
            "\nDéblocage automatique: lorsque la distance réservoir < %d cm avec 3 confirmations consécutives.\n"
            "Conseil: remplissez la réserve (éviter marche à sec), puis relancez.",
            r.wlTank, tankThresholdCm, r.wlAqua, (int)tankThresholdCm - 5);
          _mailer.sendAlert("Pompe réservoir verrouillée (réserve trop basse)", msg, mail.c_str());
          emailTankSent = true;
        }
      }
    } else if (r.wlTank < tankThresholdCm - 5) {
      belowCount = min<uint8_t>(belowCount + 1, 3);
      aboveCount = 0;
      if (tankPumpLocked && belowCount >= 3) {
        tankPumpLocked = false;
        _tankPumpLockReason = TankPumpLockReason::NONE;
        emailTankSent = false; // autoriser un prochain mail si re-verrouillage
        Serial.printf("[Auto] Pompe réservoir déverrouillée (réserve OK, distance: %d cm, confirmations)\n", r.wlTank);
      }
    } else {
      // Zone d'hystérésis: stabilise les compteurs
      aboveCount = min<uint8_t>(aboveCount, 2);
      belowCount = min<uint8_t>(belowCount, 2);
    }
  }
  
  // ========================================
  // 5. RÉCUPÉRATION AUTOMATIQUE APRÈS BLOCAGE
  // ========================================
  static unsigned long lastRecoveryAttempt = 0;
  if (tankPumpLocked && tankPumpRetries >= MAX_PUMP_RETRIES) {
    unsigned long currentMillis = millis();
    // Attendre 30 secondes avant de tenter une récupération
    if (currentMillis - lastRecoveryAttempt > 30 * 1000UL) {
      if (r.wlTank < tankThresholdCm - 10) { // Réservoir suffisamment plein
        Serial.println(F("[CRITIQUE] === RÉCUPÉRATION AUTOMATIQUE ==="));
        Serial.printf("[CRITIQUE] Niveau réservoir: %d cm, Seuil: %d cm\n", r.wlTank, tankThresholdCm);
        
        tankPumpLocked = false;
        tankPumpRetries = 0;
        emailTankSent = false; // Permettre une nouvelle notification
        _tankPumpLockReason = TankPumpLockReason::NONE;
        lastRecoveryAttempt = currentMillis;
        Serial.println(F("[CRITIQUE] Récupération automatique: pompe réservoir débloquée"));
        Serial.println(F("[CRITIQUE] === RÉCUPÉRATION EFFECTUÉE ==="));
      }
    }
  }
}

void Automatism::handleMaree(const SensorReadings& r) {
  // Délégation au module Sleep
  _sleep.handleMaree(r);
}

void Automatism::handleFeeding() {
  // Délégation complète au module Feeding avec callbacks
  time_t currentTime = _power.getCurrentEpoch();
  struct tm* timeinfo = localtime(&currentTime);
  
  if (timeinfo != nullptr) {
    int currentHour = timeinfo->tm_hour;
    int currentMinute = timeinfo->tm_min;
    
    // Callback pour feedback OLED (utilisé dans module Feeding)
    auto mailBlinkCallback = [this]() { armMailBlink(); };
    
    // Diagnostic email avant délégation
    Serial.printf("[Auto] 📧 DIAGNOSTIC AVANT FEEDING - mailNotif: %s, mail: '%s'\n", 
                 mailNotif ? "TRUE" : "FALSE", mail.c_str());
    
    // Délégation au module Feeding
    _feeding.handleSchedule(currentHour, currentMinute, mail, mailNotif, mailBlinkCallback);
    
    // Note: Le module gère lui-même les phases servo (_currentPhase, etc.)
    // et appelle les servos, emails, traçage, etc.
  }
}

// ANCIEN CODE handleFeeding() COMMENTÉ TEMPORAIREMENT POUR RÉFÉRENCE
/*void Automatism::handleFeeding_OLD() {
  time_t currentTime = _power.getCurrentEpoch();
  struct tm* timeinfo = localtime(&currentTime);
  
  if (timeinfo != nullptr) {
    int currentHour = timeinfo->tm_hour;
    int currentMinute = timeinfo->tm_min;
    
    // ========================================
    // BOUFFE DU MATIN - PRIORITÉ ABSOLUE
    // ========================================
    // [ANCIEN CODE SUPPRIMÉ - Délégué au module Feeding]
    // Les 3 blocs matin/midi/soir (~170 lignes) sont maintenant gérés par
    // le module AutomatismFeeding via handleSchedule()
  }
}

/*
// ANCIEN CODE handleFeeding() CONSERVÉ POUR RÉFÉRENCE (à supprimer plus tard)
// Ce code (lignes 940-1100, ~160 lignes) a été déplacé dans automatism_feeding.cpp
// Le module gère maintenant:
// - Vérification horaires matin/midi/soir
// - Exécution servos (feedSequential)
// - Traçage événements
// - Gestion phases (pour OLED)
// - Notifications email
// - Mise à jour flags (BouffeMatinOk, etc.)
    if (currentHour == bouffeMatin) {
      if (!_config.getBouffeMatinOk()) {
        Serial.println(F("[CRITIQUE] === DÉBUT NOURRISSAGE MATIN ==="));
        Serial.printf("[CRITIQUE] Heure actuelle: %02d:%02d, Heure configurée: %02d:00\n", 
                     currentHour, currentMinute, bouffeMatin);
        Serial.printf("[CRITIQUE] Durées configurées - Gros: %u s, Petits: %u s\n", 
                     tempsGros, tempsPetits);
        
        // EXÉCUTION SÉQUENTIELLE - PRIORITÉ ABSOLUE
        uint32_t startTime = millis();
        
        // Nourrissage séquentiel gros puis petits (automatique)
        _acts.feedSequential(tempsGros, tempsPetits, 2); // 2s de délai entre les servos
        
        uint32_t executionTime = (uint32_t)(millis() - startTime);
        Serial.printf("[CRITIQUE] Temps d'exécution total: %u ms\n", (unsigned)executionTime);
        
        // Traçage de l'événement (sélectif: gros puis petits)
        traceFeedingEventSelective(true, true);
        
        // Initialisation des phases de nourrissage avec protection
        _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
        // Automatique: s'assurer que le flag manuel est à false
        _manualFeedingActive = false;
        uint16_t maxDur = std::max(tempsGros, tempsPetits);
        _feedingPhaseEnd = millis() + static_cast<unsigned long>(maxDur) * 1000UL;
        _feedingTotalEnd = millis() + static_cast<unsigned long>(maxDur + maxDur/2) * 1000UL;
        
        // Force un nettoyage complet de l'écran pour éviter les superpositions
        _disp.forceClear();
        
        // Pour compatibilité avec l'ancien système d'affichage
        _countdownLabel = "Feed GP";
        _countdownEnd = _feedingTotalEnd;
        
        // Marquage comme effectué - CRITIQUE
        _config.setBouffeMatinOk(true);
        LOG_TIME(LOG_INFO, "[CRITIQUE] Bouffe matin MARQUÉE COMME EFFECTUÉE");
        
        // Notification email si activée
        if (mailNotif) {
          String message = createFeedingMessage("Bouffe matin", tempsGros, tempsPetits);
          _mailer.sendAlert("Bouffe matin", message, mail.c_str());
          armMailBlink();
          Serial.println(F("[CRITIQUE] Email de notification envoyé"));
        }
        
        LOG_TIME(LOG_INFO, "[CRITIQUE] === FIN NOURRISSAGE MATIN ===");
      } else {
        LOG_TIME(LOG_INFO, "[Auto] Bouffe matin déjà effectuée (heure: %02d:%02d)", currentHour, currentMinute);
      }
    }
    
    // ========================================
    // BOUFFE DU MIDI - PRIORITÉ ABSOLUE
    // ========================================
    if (currentHour == bouffeMidi) {
      if (!_config.getBouffeMidiOk()) {
        Serial.println(F("[CRITIQUE] === DÉBUT NOURRISSAGE MIDI ==="));
        Serial.printf("[CRITIQUE] Heure actuelle: %02d:%02d, Heure configurée: %02d:00\n", 
                     currentHour, currentMinute, bouffeMidi);
        Serial.printf("[CRITIQUE] Durées configurées - Gros: %u s, Petits: %u s\n", 
                     tempsGros, tempsPetits);
        
        // EXÉCUTION SÉQUENTIELLE - PRIORITÉ ABSOLUE
        uint32_t startTime = millis();
        
        // Nourrissage séquentiel gros puis petits (automatique)
        _acts.feedSequential(tempsGros, tempsPetits, 2); // 2s de délai entre les servos
        
        uint32_t executionTime = (uint32_t)(millis() - startTime);
        Serial.printf("[CRITIQUE] Temps d'exécution total: %u ms\n", (unsigned)executionTime);
        
        // Traçage de l'événement (sélectif: gros puis petits)
        traceFeedingEventSelective(true, true);
        
        // Initialisation des phases de nourrissage avec protection
        _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
        // Automatique: s'assurer que le flag manuel est à false
        _manualFeedingActive = false;
        uint16_t maxDur = std::max(tempsGros, tempsPetits);
        _feedingPhaseEnd = millis() + static_cast<unsigned long>(maxDur) * 1000UL;
        _feedingTotalEnd = millis() + static_cast<unsigned long>(maxDur + maxDur/2) * 1000UL;
        
        // Force un nettoyage complet de l'écran pour éviter les superpositions
        _disp.forceClear();
        
        // Pour compatibilité avec l'ancien système d'affichage
        _countdownLabel = "Feed GP";
        _countdownEnd = _feedingTotalEnd;
        
        // Marquage comme effectué - CRITIQUE
        _config.setBouffeMidiOk(true);
        Serial.println(F("[CRITIQUE] Bouffe midi MARQUÉE COMME EFFECTUÉE"));
        
        // Notification email si activée
        if (mailNotif) {
          String message = createFeedingMessage("Bouffe midi", tempsGros, tempsPetits);
          _mailer.sendAlert("Bouffe midi", message, mail.c_str());
          armMailBlink();
          Serial.println(F("[CRITIQUE] Email de notification envoyé"));
        }
        
        Serial.println(F("[CRITIQUE] === FIN NOURRISSAGE MIDI ==="));
      } else {
        Serial.printf("[Auto] Bouffe midi déjà effectuée (heure: %02d:%02d)\n", currentHour, currentMinute);
      }
    }
    
    // ========================================
    // BOUFFE DU SOIR - PRIORITÉ ABSOLUE
    // ========================================
    if (currentHour == bouffeSoir) {
      if (!_config.getBouffeSoirOk()) {
        Serial.println(F("[CRITIQUE] === DÉBUT NOURRISSAGE SOIR ==="));
        Serial.printf("[CRITIQUE] Heure actuelle: %02d:%02d, Heure configurée: %02d:00\n", 
                     currentHour, currentMinute, bouffeSoir);
        Serial.printf("[CRITIQUE] Durées configurées - Gros: %u s, Petits: %u s\n", 
                     tempsGros, tempsPetits);
        
        // EXÉCUTION SÉQUENTIELLE - PRIORITÉ ABSOLUE
        uint32_t startTime = millis();
        
        // Nourrissage séquentiel pour éviter les conflits de puissance
        _acts.feedSequential(tempsGros, tempsPetits, 2); // 2s de délai entre les servos
        
        uint32_t executionTime = (uint32_t)(millis() - startTime);
        Serial.printf("[CRITIQUE] Temps d'exécution total: %u ms\n", (unsigned)executionTime);
        
        // Traçage de l'événement
        traceFeedingEvent();
        
        // Initialisation des phases de nourrissage avec protection
        _currentFeedingPhase = FeedingPhase::FEEDING_FORWARD;
        // Automatique: s'assurer que le flag manuel est à false
        _manualFeedingActive = false;
        uint16_t maxDur = std::max(tempsGros, tempsPetits);
        _feedingPhaseEnd = millis() + static_cast<unsigned long>(maxDur) * 1000UL;
        _feedingTotalEnd = millis() + static_cast<unsigned long>(maxDur + maxDur/2) * 1000UL;
        
        // Force un nettoyage complet de l'écran pour éviter les superpositions
        _disp.forceClear();
        
        // Pour compatibilité avec l'ancien système d'affichage
        _countdownLabel = "Feed GP";
        _countdownEnd = _feedingTotalEnd;
        
        // Marquage comme effectué - CRITIQUE
        _config.setBouffeSoirOk(true);
        Serial.println(F("[CRITIQUE] Bouffe soir MARQUÉE COMME EFFECTUÉE"));
        
        // Notification email si activée
        if (mailNotif) {
          String message = createFeedingMessage("Bouffe soir", tempsGros, tempsPetits);
          _mailer.sendAlert("Bouffe soir", message, mail.c_str());
          armMailBlink();
          Serial.println(F("[CRITIQUE] Email de notification envoyé"));
        }
        
        Serial.println(F("[CRITIQUE] === FIN NOURRISSAGE SOIR ==="));
      } else {
        Serial.printf("[Auto] Bouffe soir déjà effectuée (heure: %02d:%02d)\n", currentHour, currentMinute);
      }
    }
  }
}
*/

void Automatism::handleAlerts(const SensorReadings& r) {
  // --- Gestion alertes niveau aquarium ---
  // Plus la distance est grande, plus le niveau est BAS (manque d'eau)
  // Inversement, une petite distance signifie que l'aquarium est TROP PLEIN (risque de débordement)

  static bool lowAquaSent  = false; // alerte "trop bas"
  static bool highAquaSent = false; // alerte "trop plein"

  // 1) Aquarium trop BAS (distance > seuil)
  if (r.wlAqua > aqThresholdCm && !lowAquaSent && mailNotif) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", r.wlAqua, " cm (> ", aqThresholdCm);
    _mailer.sendAlert("Alerte - Niveau aquarium BAS", msgBuffer, mail.c_str());
    lowAquaSent = true;
    armMailBlink();
  }
  // Reset après retour sous le seuil avec hystérésis (-5 cm)
  if (r.wlAqua <= aqThresholdCm - 5) lowAquaSent = false;

  // 2) Aquarium trop PLEIN (distance < limFlood) - anti-spam avec debounce, hystérésis et cooldown persistant
  {
    time_t nowEpoch = _power.getCurrentEpoch();
    // Debounce entrée en trop-plein
    if (r.wlAqua < limFlood) {
      if (floodEnterSinceEpoch == 0) floodEnterSinceEpoch = nowEpoch;
      // sortir de la phase de reset stable
      aboveResetSinceEpoch = 0;
      // Si pas encore enFlood et email activé, vérifier debounce et cooldown
      if (!inFlood && mailNotif) {
        uint32_t elapsedUnder = (nowEpoch >= floodEnterSinceEpoch) ? (nowEpoch - floodEnterSinceEpoch) : 0;
        bool debounceOk = elapsedUnder >= (floodDebounceMin * 60UL);
        bool cooldownOk = (lastFloodEmailEpoch == 0) || ((nowEpoch - lastFloodEmailEpoch) >= (floodCooldownMin * 60UL));
        if (debounceOk && cooldownOk) {
          char msgBuffer[128];
          formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", r.wlAqua, " cm (< ", limFlood);
          if (tankPumpLocked || _config.getPompeAquaLocked()) {
            strncat(msgBuffer, " / Pompe verrouillée", sizeof(msgBuffer) - strlen(msgBuffer) - 1);
          }
          bool sent = _mailer.sendAlert("Alerte - Aquarium TROP PLEIN", msgBuffer, mail.c_str());
          if (sent) {
            inFlood = true;
            highAquaSent = true; // conserve l'ancien flag pour compat
            armMailBlink();
            lastFloodEmailEpoch = nowEpoch;
            // Persister immédiatement pour survivre aux reboots
            Preferences prefs; prefs.begin("alerts", false); prefs.putULong("floodLast", lastFloodEmailEpoch); prefs.end();
            Serial.println(F("[Auto] Email TROP PLEIN envoyé (anti-spam actif)"));
          } else {
            Serial.println(F("[Auto] Échec envoi email TROP PLEIN"));
          }
        }
      }
    } else {
      // Pas sous limFlood → reset du chrono d'entrée
      floodEnterSinceEpoch = 0;
      // Gestion de la sortie avec hystérésis et stabilité temporelle
      if (r.wlAqua >= limFlood + floodHystCm) {
        if (aboveResetSinceEpoch == 0) aboveResetSinceEpoch = nowEpoch;
        uint32_t elapsedAbove = (nowEpoch >= aboveResetSinceEpoch) ? (nowEpoch - aboveResetSinceEpoch) : 0;
        if (elapsedAbove >= (floodResetStableMin * 60UL)) {
          inFlood = false;
          highAquaSent = false;
        }
      } else {
        // Dans la zone d'hystérésis, maintenir l'état mais réinitialiser le timer de stabilité
        aboveResetSinceEpoch = 0;
      }
    }
  }

  // --- Niveau réserve ---
  static bool lowTankSent = false;     // flag "réserve trop vide" déjà notifié

  // 1) Réserve trop basse (distance > threshold)
  if (r.wlTank > tankThresholdCm && !lowTankSent && mailNotif) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", r.wlTank, " cm (> ", tankThresholdCm);
    _mailer.sendAlert("Alerte - Réserve BASSE", msgBuffer, mail.c_str());
    lowTankSent = true;
    armMailBlink();
  }

  // 2) Réserve de nouveau OK (distance < threshold - hysteresis)
  else if (lowTankSent && r.wlTank <= tankThresholdCm - 5 && mailNotif) {
    char msgBuffer[128];
    formatDistanceAlert(msgBuffer, sizeof(msgBuffer), "Distance: ", r.wlTank, " cm (<= ", tankThresholdCm - 5);
    _mailer.sendAlert("Info - Réserve OK", msgBuffer, mail.c_str());
    lowTankSent = false;
    armMailBlink();
  }

  // Chauffage ON/OFF
  if (r.tempWater < heaterThresholdC && !heaterPrevState) {
    _acts.startHeater();
    heaterPrevState = true;
    if (mailNotif) {
      char msgBuffer[64];
      formatTemperatureAlert(msgBuffer, sizeof(msgBuffer), "Temp eau: ", r.tempWater);
      _mailer.sendAlert("Chauffage ON", msgBuffer, mail.c_str());
      armMailBlink();
    }
  } else if (r.tempWater > heaterThresholdC + 2 && heaterPrevState) {
    _acts.stopHeater();
    heaterPrevState = false;
    if (mailNotif) {
      char msgBuffer[64];
      formatTemperatureAlert(msgBuffer, sizeof(msgBuffer), "Temp eau: ", r.tempWater);
      _mailer.sendAlert("Chauffage OFF", msgBuffer, mail.c_str());
      armMailBlink();
    }
  }
} 

void Automatism::handleRemoteState() {
  // === REFACTORED: Délégation au module Network (Phase 2.11) ===
  // Cette méthode de 635 lignes a été divisée en 5 sous-méthodes modulaires
  
  uint32_t currentMillis = millis();
  _power.resetWatchdog();

  // Mise à jour affichage OLED - indique téléchargement en cours (flèche vide)
  recvState = 0;
  if (_disp.isPresent()) {
    int diffNow = _sensors.diffMaree(_lastReadings.wlAqua);
    int8_t tideDir = 0;
    if (diffNow > 1) tideDir = 1;        // montante ~10s
    else if (diffNow < -1) tideDir = -1; // descendante ~10s
    _lastDiffMaree = diffNow;
    
    // Log pour debug affichage marée (section remote)
    if (diffNow > 10) { // Seulement si valeur suspecte
      Serial.printf("[OLED-Remote] Affichage marée: diff=%d, dir=%d\n", diffNow, tideDir);
    }
    
    {
      bool blinkNow3 = (mailBlinkUntil && isStillPending(mailBlinkUntil, currentMillis) && ((currentMillis/300)%2));
      _disp.drawStatus(sendState, recvState, WiFi.isConnected()?WiFi.RSSI():-127,
                       blinkNow3, tideDir, diffNow);
    }
    _disp.flush();
  }
  
  // === ÉTAPE 1: Polling serveur distant ===
  StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
  if (!_network.pollRemoteState(doc, currentMillis, *this)) {
    // Pas de données reçues (intervalle non atteint, WiFi déconnecté, ou erreur)
    return;
  }
  
  // Synchroniser états UI depuis module Network
  recvState = _network.getRecvState();
  serverOk = _network.isServerOk();
  
  // === ÉTAPE 2: Parsing unifié GPIO ===
  // Version 11.68: Remplacement des 6 étapes par un seul parser unifié
  GPIOParser::parseAndApply(doc, *this);
}

void Automatism::manualFeedSmall(){
  // Délégation au module Feeding avec callback countdown OLED
  auto countdownCallback = [this](const char* label, unsigned long endTime) {
    _countdownLabel = label;
    _countdownEnd = endTime;
  };
  // Marquer le cycle comme MANUEL pour l'affichage OLED (M)
  _manualFeedingActive = true;

  _feeding.feedSmallManual(countdownCallback);
  
  // Traçage (si pas récent)
  static unsigned long lastManualFeedSmallMs = 0;
  unsigned long now = millis();
  bool isRecentLocalCommand = (now - lastManualFeedSmallMs) < 2000;
  
  if (!isRecentLocalCommand) {
    traceFeedingEventSelective(true, false);
  }
  lastManualFeedSmallMs = now;
}

void Automatism::manualFeedBig(){
  // Délégation au module Feeding avec callback countdown OLED
  auto countdownCallback = [this](const char* label, unsigned long endTime) {
    _countdownLabel = label;
    _countdownEnd = endTime;
  };
  // Marquer le cycle comme MANUEL pour l'affichage OLED (M)
  _manualFeedingActive = true;

  _feeding.feedBigManual(countdownCallback);
  
  // Traçage (si pas récent)
  static unsigned long lastManualFeedBigMs = 0;
  unsigned long now = millis();
  bool isRecentLocalCommand = (now - lastManualFeedBigMs) < 2000;
  
  if (!isRecentLocalCommand) {
    traceFeedingEventSelective(false, true);
  }
  lastManualFeedBigMs = now;
}

bool Automatism::fetchRemoteState(ArduinoJson::JsonDocument& doc){
  bool ok = _web.fetchRemoteState(doc);
  if(ok && doc.size() > 0){
    String jsonStr;
    serializeJson(doc, jsonStr);
    _config.saveRemoteVars(jsonStr);
  }
  return ok;
} 

void Automatism::applyConfigFromJson(const ArduinoJson::JsonDocument& doc){
  auto assignIfPresent = [&doc](const char* key, auto& var){
    if(!doc.containsKey(key)) return;
    using T = std::decay_t<decltype(var)>;
    T v = doc[key].as<T>();
    if constexpr (std::is_arithmetic_v<T>) {
      // ignore 0 pour éviter d'écraser par défaut
      if (v == 0) return;
    }
    var = v;
  };
  // Heures nourrissage
  assignIfPresent("bouffeMatin", bouffeMatin);
  assignIfPresent("bouffeMidi",    bouffeMidi);
  assignIfPresent("bouffeSoir", bouffeSoir);
  // Durées - accepter toutes les valeurs >= 0 depuis la BDD distante
  if (doc.containsKey("tempsGros")) { 
    int v = doc["tempsGros"].as<int>(); 
    if (v >= 0) tempsGros = v;  // Accepter 0 et valeurs positives
  }
  if (doc.containsKey("tempsPetits")) { 
    int v = doc["tempsPetits"].as<int>(); 
    if (v >= 0) tempsPetits = v;  // Accepter 0 et valeurs positives
  }
  // Seuils
  assignIfPresent("aqThreshold", aqThresholdCm);
  assignIfPresent("tankThreshold", tankThresholdCm);
  // Seuil chauffage - gestion avec logging pour diagnostic
  if (doc.containsKey("heaterThreshold")) {
    float newThreshold = doc["heaterThreshold"].as<float>();
    if (newThreshold > 0.0f) { // Protection contre valeurs invalides
      heaterThresholdC = newThreshold;
      Serial.printf("[Auto] 🔥 Seuil chauffage mis à jour: %.1f°C\n", heaterThresholdC);
    }
  }
  // Alias serveur (compat): chauffageThreshold
  if (doc.containsKey("chauffageThreshold")) {
    float newThreshold = doc["chauffageThreshold"].as<float>();
    if (newThreshold > 0.0f) { // Protection contre valeurs invalides
      heaterThresholdC = newThreshold;
      Serial.printf("[Auto] 🔥 Seuil chauffage mis à jour (alias): %.1f°C\n", heaterThresholdC);
    }
  }
  if (doc.containsKey("refillDuration")) refillDurationMs = (uint32_t)doc["refillDuration"].as<int>() * 1000UL;
  // Alias serveur (compat): tempsRemplissageSec
  if (doc.containsKey("tempsRemplissageSec")) refillDurationMs = (uint32_t)doc["tempsRemplissageSec"].as<int>() * 1000UL;
  assignIfPresent("limFlood", limFlood);
  // Marée montante (réglages)
  if (doc.containsKey("tideTrigger")) {
    int v = doc["tideTrigger"].as<int>();
    if (v > 0) tideTriggerCm = (int16_t)v;
  }
  if (doc.containsKey("tideWindowMs")) {
    uint32_t w = doc["tideWindowMs"].as<uint32_t>();
    if (w >= 2000 && w <= 60000) { // bornes de sécurité 2s..60s
      _sensors.setTideWindowMs(w);
    }
  }
  // Email
  if (doc.containsKey("mail")) {
    // v11.78: Sécurisation de la copie mail pour éviter les pointeurs invalides
    const char* emailPtr = doc["mail"].as<const char*>();
    if (emailPtr != nullptr) {
      mail = String(emailPtr);
      Serial.printf("[Auto] 📧 mail chargé: '%s'\n", mail.c_str());
    } else {
      Serial.println("[Auto] ⚠️ mail null, ignoré");
    }
  }
  // Alias serveur (compat): mail
  if (!doc.containsKey("mail") && doc.containsKey("mail")) {
    const char* emailPtr = doc["mail"].as<const char*>();
    if (emailPtr != nullptr) {
      mail = String(emailPtr);
      Serial.printf("[Auto] 📧 mail chargé (alias): '%s'\n", mail.c_str());
    }
  }
  if (doc.containsKey("mailNotif")) {
    mailNotif = parseTruthy(doc["mailNotif"]);
    const char* mailNotifStr = nullptr;
    auto v = doc["mailNotif"];
    if (v.is<const char*>()) {
      mailNotifStr = v.as<const char*>();
    }
    Serial.printf("[Auto] 📧 mailNotif chargé: '%s' -> %s\n",
                 mailNotifStr ? mailNotifStr : (mailNotif ? "1" : "0"),
                 mailNotif ? "TRUE" : "FALSE");
  }
  // Alias serveur (compat): mailNotif
  if (doc.containsKey("mailNotif")) {
    mailNotif = parseTruthy(doc["mailNotif"]);
    Serial.printf("[Auto] 📧 mailNotif chargé (alias) -> %s\n", mailNotif ? "TRUE" : "FALSE");
  }

  // Wake flags
  if (doc.containsKey("forceWakeUp")) {
    bool v = parseTruthy(doc["forceWakeUp"]);
    forceWakeUp = v;
    _config.setForceWakeUp(v);
  }
  // Alias serveur (compat): WakeUp
  if (doc.containsKey("WakeUp")) {
    bool v = parseTruthy(doc["WakeUp"]);
    forceWakeUp = v;
    _config.setForceWakeUp(v);
  }
  // Fréquence de wakeup
  if (doc.containsKey("FreqWakeUp")) {
    int fv = doc["FreqWakeUp"].as<int>();
    if (fv > 0) freqWakeSec = fv;
  }
  
  // v11.41: Propager immédiatement au module Feeding
  _feeding.setDurations(tempsGros, tempsPetits);
  _feeding.setSchedule(bouffeMatin, bouffeMidi, bouffeSoir);
  Serial.printf("[Auto] Config Feeding appliquée - Durées: %us/%us, Horaires: %uh/%uh/%uh\n",
               tempsGros, tempsPetits, bouffeMatin, bouffeMidi, bouffeSoir);
}

void Automatism::checkCriticalChanges() {
  // Envoie désormais la trame complète sans paire supplémentaire :
  // la clé (etatPompeAqua / etatPompeTank) est déjà dans la partie
  // principale de la trame, ce qui évite toute duplication.
  auto sendChange = [this](const char* /*key*/, int /*value*/){
    SensorReadings cur = _sensors.read();
    sendFullUpdate(cur, nullptr);
  };

  // Pompes
  bool curPumpTank = _acts.isTankPumpRunning();
  if (curPumpTank != _prevPumpTankState) {
    sendChange("etatPompeTank", curPumpTank ? 1 : 0);

    // ------------------------------------------------------------------
    // Protection : ignorer le tout premier événement OFF juste après le démarrage qui n'est
    // que la synchronisation initiale (_pumpStartMs est encore à zéro).
    // Cela évite le message parasite "Fin du remplissage automatique"
    // observé après un reset.
    // ------------------------------------------------------------------
    if (!curPumpTank && _pumpStartMs == 0) {
      _prevPumpTankState = curPumpTank; // resynchroniser simplement le flag interne
      return;                           // ignorer la notification
    }

    // ------------------------------------------------------------------
    // Protection : ne pas envoyer de mail si l'arrêt est dû à la sécurité de débordement
    // AMÉLIORATION : Vérifier seulement si la pompe était verrouillée pour sécurité
    // ------------------------------------------------------------------
    bool isSecurityStop = false;
    if (!curPumpTank && _prevPumpTankState) {
      // Pompe vient de s'arrêter, vérifier si c'est à cause de la sécurité
      // CORRECTION : Ne bloquer que si la pompe était verrouillée pour sécurité
      if (_tankPumpLockReason == TankPumpLockReason::AQUARIUM_OVERFILL || 
          _tankPumpLockReason == TankPumpLockReason::RESERVOIR_LOW) {
        isSecurityStop = true;
        Serial.println(F("[Auto] Arrêt pompe détecté - sécurité, mail ignoré"));
      }
    }

    // Détermine si l'action est manuelle (override)
    bool isManual = _manualTankOverride; // fiable au moment du démarrage

    // Ajout de messages pour le moniteur série avec variables pertinentes
    if (curPumpTank) {
      // Pompe vient d'être allumée
      if (isManual) {
        Serial.println(F("[Manuel] Démarrage pompe réservoir"));
      } else {
        Serial.println(F("[Auto] Démarrage pompe réservoir"));
      }
      SensorReadings lv = _sensors.read();
      Serial.printf("[Pompe] wlAqua=%u cm, wlTank=%u cm, aqTh=%u, tankTh=%u, locked=%s, retries=%u/%u, durMax=%lus\n",
                    lv.wlAqua, lv.wlTank, aqThresholdCm, tankThresholdCm,
                    tankPumpLocked?"OUI":"NON", tankPumpRetries, (unsigned)MAX_PUMP_RETRIES, refillDurationMs/1000UL);
    } else {
      // Pompe vient d'être arrêtée
      uint32_t durSec = (_pumpStartMs > 0) ? ((uint32_t)(millis() - _pumpStartMs) / 1000U) : 0U;
      if (_lastPumpManual) {
        Serial.printf("[Manuel] Fin remplissage (durée: %lu s)\n", durSec);
      } else {
        Serial.printf("[Auto] Fin remplissage (durée: %lu s)\n", durSec);
      }
      SensorReadings lv = _sensors.read();
      Serial.printf("[Pompe] fin: wlAqua=%u cm, wlTank=%u cm, aqTh=%u, tankTh=%u, locked=%s, retries=%u/%u\n",
                    lv.wlAqua, lv.wlTank, aqThresholdCm, tankThresholdCm,
                    tankPumpLocked?"OUI":"NON", tankPumpRetries, (unsigned)MAX_PUMP_RETRIES);

      // CORRECTION CRITIQUE v11.10: Réinitialisation systématique du mode manuel
      // pour éviter les cycles infinis après activation manuelle
      if (_manualTankOverride) {
        Serial.println(F("[CRITIQUE] Réinitialisation mode manuel - pompe arrêtée"));
        _manualTankOverride = false;
        _countdownEnd = 0;
        _pumpStartMs = 0;
      }

      // Déterminer raison si non déjà posée
      if (isSecurityStop) {
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
      } else if (_lastPumpManual) {
        _lastTankStopReason = TankPumpStopReason::MANUAL;
      } else if (hasExpired(_countdownEnd)) {
        _lastTankStopReason = TankPumpStopReason::MAX_DURATION;
      } else if (_lastTankStopReason == TankPumpStopReason::UNKNOWN) {
        // AMÉLIORATION : Déterminer la raison d'arrêt si pas encore définie
        if (_lastPumpManual) {
          _lastTankStopReason = TankPumpStopReason::MANUAL;
        } else if (hasExpired(_countdownEnd)) {
          _lastTankStopReason = TankPumpStopReason::MAX_DURATION;
        } else {
          // fallback pour sleep ou autres arrêts
          _lastTankStopReason = TankPumpStopReason::SLEEP;
        }
      }
    }

    // Envoi du mail à chaque changement (inclut les arrêts de sécurité avec raison)
    // AMÉLIORATION : Éviter les mails en double avec les nouveaux flags
    if (mailNotif && !isSecurityStop) {
      // Vérifier si on doit envoyer le mail (éviter les doublons)
      bool shouldSendMail = false;
      
      if (curPumpTank && !emailTankStartSent) {
        // Démarrage : envoyer seulement si pas déjà envoyé
        shouldSendMail = true;
        emailTankStartSent = true;
        emailTankStopSent = false; // Reset pour le prochain arrêt
      } else if (!curPumpTank && !emailTankStopSent) {
        // Arrêt : envoyer seulement si pas déjà envoyé
        shouldSendMail = true;
        emailTankStopSent = true;
        emailTankStartSent = false; // Reset pour le prochain démarrage
      }
      
      if (shouldSendMail) {
        // Sujet différent selon manuel/auto + état
        const char* subj;
      if (curPumpTank) { // ON
        subj = isManual ? "Pompe réservoir MANUELLE ON" : "Pompe réservoir AUTO ON";
      } else {           // OFF
        subj = _lastPumpManual ? "Pompe réservoir MANUELLE OFF" : "Pompe réservoir AUTO OFF";
        if (isSecurityStop) {
          if (_tankPumpLockReason == TankPumpLockReason::RESERVOIR_LOW) {
            subj = "Pompe réservoir OFF (sécurité réserve basse)";
          } else if (_tankPumpLockReason == TankPumpLockReason::AQUARIUM_OVERFILL) {
            subj = "Pompe réservoir OFF (sécurité aquarium trop plein)";
          } else {
            subj = "Pompe réservoir OFF (sécurité)";
          }
        }
      }

      char body[1024];
      SensorReadings curLevels = _sensors.read(); // relevé instantané des niveaux
      if (curPumpTank) {
        const char* origin = (_lastTankManualOrigin == ManualOrigin::REMOTE_SERVER) ? "Serveur distant" : "Serveur local";
        int diffMaree = _sensors.diffMaree(curLevels.wlAqua);
        
        if (isManual) {
          snprintf(body, sizeof(body), 
            "Démarrage MANUEL du remplissage de l'aquarium\n"
            "- Origine: %s\n"
            "- Aqua: %d cm\n"
            "- Réserve: %d cm\n"
            "- Variation marée: %d cm\n"
            "- Seuils: aqThreshold=%d, tankThreshold=%d\n"
            "- Durée max configurée: %d s\n"
            "- Délai d'activité: %d s\n"
            "- Sécurité verrouillée: %s\n"
            "- Tentatives: %d/%d",
            origin, curLevels.wlAqua, curLevels.wlTank, diffMaree, 
            aqThresholdCm, tankThresholdCm, refillDurationMs/1000UL, 
            calculateAdaptiveSleepDelay(), tankPumpLocked ? "OUI" : "NON", 
            tankPumpRetries, MAX_PUMP_RETRIES);
        } else {
          snprintf(body, sizeof(body), 
            "Démarrage automatique du remplissage de l'aquarium\n"
            "- Aqua: %d cm\n"
            "- Réserve: %d cm\n"
            "- Variation marée: %d cm\n"
            "- Seuils: aqThreshold=%d, tankThreshold=%d\n"
            "- Durée max configurée: %d s\n"
            "- Délai d'activité: %d s\n"
            "- Sécurité verrouillée: %s\n"
            "- Tentatives: %d/%d",
            curLevels.wlAqua, curLevels.wlTank, diffMaree, 
            aqThresholdCm, tankThresholdCm, refillDurationMs/1000UL, 
            calculateAdaptiveSleepDelay(), tankPumpLocked ? "OUI" : "NON", 
            tankPumpRetries, MAX_PUMP_RETRIES);
        }
        if (tankPumpLocked) {
          char lockReason[256];
          const char* reason;
          switch (_tankPumpLockReason) {
            case TankPumpLockReason::RESERVOIR_LOW:
              reason = "Réserve trop basse (sécurité anti-marche à sec)"; 
              snprintf(lockReason, sizeof(lockReason), 
                "\n- Raison verrouillage: %s\n- Déblocage: automatique quand la distance réservoir < %d cm (confirmé).",
                reason, (int)tankThresholdCm - 5);
              break;
            case TankPumpLockReason::AQUARIUM_OVERFILL:
              reason = "Aquarium trop plein (sécurité)";
              snprintf(lockReason, sizeof(lockReason), 
                "\n- Raison verrouillage: %s\n- Déblocage: automatique quand l'aquarium n'est plus en trop-plein (stabilité).",
                reason);
              break;
            case TankPumpLockReason::INEFFICIENT:
              reason = "Pompe inefficace (pas d'amélioration de niveau)";
              snprintf(lockReason, sizeof(lockReason), 
                "\n- Raison verrouillage: %s\n- Déblocage: amorçage/vérification tuyaux puis relancer.",
                reason);
              break;
            default:
              reason = "Inconnue";
              snprintf(lockReason, sizeof(lockReason), 
                "\n- Raison verrouillage: %s\n- Déblocage: amorçage/vérification tuyaux puis relancer.",
                reason);
              break;
          }
          strncat(body, lockReason, sizeof(body) - strlen(body) - 1);
        }
        // Conserver l'horodatage initial du démarrage (_pumpStartMs fixé lors de l'ordre ON)
        _lastPumpManual = isManual; // mémorise le mode pour le message OFF
      } else {
        uint32_t durSec = (_pumpStartMs > 0) ? ((uint32_t)(millis() - _pumpStartMs) / 1000U) : 0U;
        const char* origin = (_lastTankManualOrigin == ManualOrigin::REMOTE_SERVER) ? "Serveur distant" : "Serveur local";
        int diffMaree = _sensors.diffMaree(curLevels.wlAqua);
        
        if (_lastPumpManual) {
          snprintf(body, sizeof(body), 
            "Fin du remplissage MANUEL\n"
            "- Origine: %s\n"
            "- Durée: %d s\n"
            "- Aqua: %d cm\n"
            "- Réserve: %d cm\n"
            "- Variation marée: %d cm\n"
            "- Seuils: aqThreshold=%d, tankThreshold=%d\n"
            "- Délai d'activité: %d s\n"
            "- Sécurité verrouillée: %s\n"
            "- Tentatives: %d/%d",
            origin, durSec, curLevels.wlAqua, curLevels.wlTank, diffMaree, 
            aqThresholdCm, tankThresholdCm, calculateAdaptiveSleepDelay(), 
            tankPumpLocked ? "OUI" : "NON", tankPumpRetries, MAX_PUMP_RETRIES);
        } else {
          snprintf(body, sizeof(body), 
            "Fin du remplissage automatique\n"
            "- Durée: %d s\n"
            "- Aqua: %d cm\n"
            "- Réserve: %d cm\n"
            "- Variation marée: %d cm\n"
            "- Seuils: aqThreshold=%d, tankThreshold=%d\n"
            "- Délai d'activité: %d s\n"
            "- Sécurité verrouillée: %s\n"
            "- Tentatives: %d/%d",
            durSec, curLevels.wlAqua, curLevels.wlTank, diffMaree, 
            aqThresholdCm, tankThresholdCm, calculateAdaptiveSleepDelay(), 
            tankPumpLocked ? "OUI" : "NON", tankPumpRetries, MAX_PUMP_RETRIES);
        }
        // Ajout de la raison d'arrêt
        char stopReason[128];
        const char* reason;
        switch (_lastTankStopReason) {
          case TankPumpStopReason::MAX_DURATION: reason = "Durée maximale atteinte"; break;
          case TankPumpStopReason::MANUAL: reason = "Arrêt manuel"; break;
          case TankPumpStopReason::OVERFLOW_SECURITY: reason = "Sécurité débordement"; break;
          case TankPumpStopReason::SLEEP: reason = "Mise en veille"; break;
          default: reason = "Inconnue"; break;
        }
        snprintf(stopReason, sizeof(stopReason), "\n- Raison d'arrêt: %s", reason);
        strncat(body, stopReason, sizeof(body) - strlen(body) - 1);
        if (tankPumpLocked) {
          char lockReason2[256];
          const char* reason2;
          switch (_tankPumpLockReason) {
            case TankPumpLockReason::RESERVOIR_LOW:
              reason2 = "Réserve trop basse (sécurité anti-marche à sec)"; 
              snprintf(lockReason2, sizeof(lockReason2), 
                "\n- Raison verrouillage: %s\n- Déblocage: automatique quand la distance réservoir < %d cm (confirmé).",
                reason2, (int)tankThresholdCm - 5);
              break;
            case TankPumpLockReason::AQUARIUM_OVERFILL:
              reason2 = "Aquarium trop plein (sécurité)";
              snprintf(lockReason2, sizeof(lockReason2), 
                "\n- Raison verrouillage: %s\n- Déblocage: automatique quand l'aquarium n'est plus en trop-plein (stabilité).",
                reason2);
              break;
            case TankPumpLockReason::INEFFICIENT:
              reason2 = "Pompe inefficace (pas d'amélioration de niveau)";
              snprintf(lockReason2, sizeof(lockReason2), 
                "\n- Raison verrouillage: %s\n- Déblocage: amorçage/vérification tuyaux puis relancer.",
                reason2);
              break;
            default:
              reason2 = "Inconnue";
              snprintf(lockReason2, sizeof(lockReason2), 
                "\n- Raison verrouillage: %s\n- Déblocage: amorçage/vérification tuyaux puis relancer.",
                reason2);
              break;
          }
          strncat(body, lockReason2, sizeof(body) - strlen(body) - 1);
        }
        // Le cycle est terminé, réinitialise flag
        _lastPumpManual = false;
      }

        _mailer.sendAlert(subj, body, mail.c_str());
        armMailBlink();
      }
    }

    _prevPumpTankState = curPumpTank;
  }
  bool curPumpAqua = _acts.isAquaPumpRunning();
      if (curPumpAqua != _prevPumpAquaState) {
    sendChange("etatPompeAqua", curPumpAqua ? 1 : 0);
    // Email d'arrêt/démarrage pompe aquarium avec raison
    if (mailNotif) {
      const char* subjA;
      char bodyA[1024];
      SensorReadings readings = _sensors.read();
      int diffMaree = _sensors.diffMaree(readings.wlAqua);
      
      if (curPumpAqua) {
        subjA = "Pompe aquarium ON";
        const char* origin = (_lastAquaManualOrigin == ManualOrigin::REMOTE_SERVER) ? "Serveur distant" : "Serveur local";
        snprintf(bodyA, sizeof(bodyA), 
          "La pompe aquarium a été démarrée.\n"
          "- Origine: %s\n"
          "- Aquarium: %d cm\n"
          "- Variation marée: %d cm\n"
          "- Délai d'activité: %d s\n"
          "- Aqua: %d cm\n"
          "- Réserve: %d cm\n"
          "- Seuils: aqThreshold=%d, tankThreshold=%d",
          (_lastAquaManualOrigin != ManualOrigin::NONE) ? origin : "Automatique",
          readings.wlAqua, diffMaree, calculateAdaptiveSleepDelay(),
          readings.wlAqua, readings.wlTank, aqThresholdCm, tankThresholdCm);
      } else {
        subjA = "Pompe aquarium OFF";
        const char* reason;
        switch (_lastAquaStopReason) {
          case AquaPumpStopReason::MANUAL: reason = "Arrêt manuel"; break;
          case AquaPumpStopReason::SLEEP: reason = "Mise en veille"; break;
          default: reason = "Inconnue"; break;
        }
        const char* origin = (_lastAquaManualOrigin == ManualOrigin::REMOTE_SERVER) ? "Serveur distant" : "Serveur local";
        snprintf(bodyA, sizeof(bodyA), 
          "La pompe aquarium a été arrêtée.\n"
          "- Raison d'arrêt: %s\n"
          "- Aquarium: %d cm\n"
          "- Variation marée: %d cm\n"
          "- Délai d'activité: %d s\n"
          "%s\n"
          "- Aqua: %d cm\n"
          "- Réserve: %d cm\n"
          "- Seuils: aqThreshold=%d, tankThreshold=%d",
          reason, readings.wlAqua, diffMaree, calculateAdaptiveSleepDelay(),
          (_lastAquaStopReason == AquaPumpStopReason::MANUAL && _lastAquaManualOrigin != ManualOrigin::NONE) ? 
            origin : "",
          readings.wlAqua, readings.wlTank, aqThresholdCm, tankThresholdCm);
      }
      _mailer.sendAlert(subjA, bodyA, mail.c_str());
      armMailBlink();
      if (!curPumpAqua) _lastAquaStopReason = AquaPumpStopReason::UNKNOWN; // reset après notification
    }
    _prevPumpAquaState = curPumpAqua;
  }

  // (Les flags de nourrissage ne sont plus envoyés ici pour éviter d'écraser
  //  les heures bouffeMatin/bouffeMidi/bouffeSoir stockées dans la BDD.)
} 

/* ------------------------------------------------------------------
 *  Construction et envoi d'une trame COMPLETE (mesures + variables)
 * ------------------------------------------------------------------*/
bool Automatism::sendFullUpdate(const SensorReadings& readings, const char* extraPairs) {
  // Délégation au module Network avec toutes les variables nécessaires
  return _network.sendFullUpdate(
    readings,
    _acts,
    _sensors.diffMaree(readings.wlAqua),
    bouffeMatin, bouffeMidi, bouffeSoir,
    tempsGros, tempsPetits,
    bouffePetits, bouffeGros,
    forceWakeUp, freqWakeSec,
    refillDurationMs / 1000,
    extraPairs
  );
} 

/* ------------------------------------------------------------------
 *  Commandes manuelles exposées au serveur Web local
 * ------------------------------------------------------------------*/

void Automatism::startTankPumpManual() {
  // FONCTION CRITIQUE : DÉMARRAGE MANUEL POMPE REMPLISSAGE
  if (!_acts.isTankPumpRunning()) {
    Serial.println(F("[CRITIQUE] === DÉBUT REMPLISSAGE MANUEL ==="));
    
    // Tracking local
    _manualTankOverride = true;
    _lastTankManualOrigin = ManualOrigin::LOCAL_SERVER;
    _countdownLabel = "Refill";
    _countdownEnd = millis() + refillDurationMs;
    _pumpStartMs = millis();
    
    SensorReadings cur = _sensors.read();
    _levelAtPumpStart = cur.wlAqua;
    
    Serial.printf("[CRITIQUE] Niveau aquarium: %d cm, Niveau réservoir: %d cm\n", cur.wlAqua, cur.wlTank);
    
    // Délégation au module Actuators (démarrage + sync)
    AutomatismActuators actuators(_acts, _config);
    actuators.startTankPumpManual();
    
    Serial.println(F("[CRITIQUE] === REMPLISSAGE MANUEL EN COURS ==="));
  } else {
    Serial.println(F("[Auto] Pompe réservoir déjà en cours d'exécution"));
  }
}

void Automatism::stopTankPumpManual() {
  // FONCTION CRITIQUE : ARRÊT MANUEL POMPE REMPLISSAGE
  if (_acts.isTankPumpRunning()) {
    Serial.println(F("[CRITIQUE] === ARRÊT MANUEL REMPLISSAGE ==="));
    
    // Tracking local (avant arrêt pour checkCriticalChanges)
    _lastPumpManual = _manualTankOverride;
    _lastTankStopReason = TankPumpStopReason::MANUAL;
    _lastTankManualOrigin = ManualOrigin::LOCAL_SERVER;
    _manualTankOverride = false;
    _countdownEnd = 0;
    
    uint32_t pumpStart = _pumpStartMs;
    _pumpStartMs = 0;
    
    // Diagnostic niveau
    SensorReadings cur = _sensors.read();
    int levelImprovement = _levelAtPumpStart - cur.wlAqua;
    Serial.printf("[CRITIQUE] Amélioration niveau: %d cm\n", levelImprovement);
    
    // Délégation au module Actuators (arrêt + sync)
    _acts.stopTankPump(pumpStart);
    AutomatismActuators actuators(_acts, _config);
    actuators.stopTankPumpManual();
    
    vTaskDelay(pdMS_TO_TICKS(100)); // Délai sécurité
    Serial.println(F("[CRITIQUE] === REMPLISSAGE MANUEL TERMINÉ ==="));

    // Notification serveur immédiate pour remettre les variables à 0
    if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
      bool ok = sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
      if (ok) {
        Serial.println(F("[Auto] ✅ Fin remplissage manuel notifiée au serveur - pump_tank=0"));
      } else {
        Serial.println(F("[Auto] ⚠️ Échec notification fin remplissage manuel au serveur"));
      }
    }
  } else {
    Serial.println(F("[Auto] Pompe réservoir déjà arrêtée"));
  }
}

/* ------------------------------------------------------------------
 *  Trace nourrissage : envoie bouffePetits/bouffeGros à 1 puis 0
 * ------------------------------------------------------------------*/
void Automatism::traceFeedingEvent(){
  // Délégation au module Feeding avec callback sendFullUpdate
  auto sendCallback = [this](const char* params) {
    SensorReadings cur = _sensors.read();
    sendFullUpdate(cur, params);
  };
  _feeding.traceFeedingEvent(sendCallback);
}

// Variante sélective: permet d'indiquer séparément petits/gros
void Automatism::traceFeedingEventSelective(bool feedSmall, bool feedBig){
  // Délégation au module Feeding avec callback sendFullUpdate
  auto sendCallback = [this](const char* params) {
    SensorReadings cur = _sensors.read();
    sendFullUpdate(cur, params);
  };
  _feeding.traceFeedingEventSelective(feedSmall, feedBig, sendCallback);
}

/* ------------------------------------------------------------------
 *  Vérification précoce pour l'entrée en sleep - PRIORITÉ HAUTE
 *  Vérifie uniquement les conditions critiques (nourrissage/remplissage)
 * ------------------------------------------------------------------*/
bool Automatism::shouldEnterSleepEarly(const SensorReadings& r) {
  bool feedingInProgress = (_currentFeedingPhase != FeedingPhase::NONE);
  bool tankPumpRunning = _acts.isTankPumpRunning();
  int diff10s = _sensors.diffMaree(_lastReadings.wlAqua);
  return _sleep.shouldEnterSleepEarly(r,
                                      forceWakeUp,
                                      _forceWakeFromWeb,
                                      _lastWebActivityMs,
                                      feedingInProgress,
                                      tankPumpRunning,
                                      _countdownEnd,
                                      _lastWakeMs,
                                      diff10s,
                                      tideTriggerCm);
}

/* ------------------------------------------------------------------
 *  Mise en veille légère automatique après un délai fixe
 * ------------------------------------------------------------------*/
void Automatism::handleAutoSleep(const SensorReadings& r) {
  // ========================================
  // FONCTION DE MOINDRE PRIORITÉ : MISE EN VEILLE AUTOMATIQUE
  // NE DOIT PAS INTERFÉRER AVEC LES FONCTIONS CRITIQUES
  // ========================================
  
  // ========================================
  // 1. VÉRIFICATION CLIENTS WEBSOCKET (v11.50 - NON-BLOQUANT)
  // Timeout pour éviter le blocage indéfini du sleep
  // ========================================
  if (!_sleep.handleBlockingConditions(_acts,
                                       forceWakeUp,
                                       _forceWakeFromWeb,
                                       _lastWebActivityMs,
                                       _countdownEnd,
                                       _lastWakeMs,
                                       (_currentFeedingPhase != FeedingPhase::NONE),
                                       _acts.isTankPumpRunning(),
                                       0)) {
    return;
  }
  
  // ========================================
  // 2. VÉRIFICATION ACTIVITÉ WEB TEMPORAIRE (FALLBACK)
  // Empêche le sleep pendant consultation interface web, sans modifier GPIO 115
  // ========================================
  if (_forceWakeFromWeb) {
    unsigned long now = millis();
    if (_lastWebActivityMs > 0 && (now - _lastWebActivityMs > TimingConfig::WEB_ACTIVITY_TIMEOUT_MS)) {
      // Timeout atteint -> libérer le blocage temporaire
      _forceWakeFromWeb = false;
      Serial.println(F("[Auto] Activité web expirée - sleep autorisé à nouveau"));
    } else {
      // Activité web récente -> empêcher sleep temporairement
      static unsigned long lastWebLog = 0;
      unsigned long currentMillis = millis();
      if (currentMillis - lastWebLog > 30000) {
        lastWebLog = currentMillis;
        Serial.println(F("[Auto] Auto-sleep bloqué temporairement (activité web récente)"));
      }
      return;
    }
  }
  
  // ========================================
  // 3. VÉRIFICATION FORCEWAKEUP PERSISTANT (GPIO 115)
  // Contrôlé UNIQUEMENT par l'utilisateur via toggle ou serveur distant
  // ========================================
  if (forceWakeUp) {
    static unsigned long lastWakeUpLog = 0;
    unsigned long currentMillis = millis();
    if (currentMillis - lastWakeUpLog > 30000) { // Log toutes les 30 secondes
      lastWakeUpLog = currentMillis;
      Serial.println(F("[Auto] Auto-sleep désactivé (forceWakeUp GPIO 115 = true)"));
    }
    return;
  }

  // ========================================
  // PROTECTION CRITIQUE : NE PAS DORMIR PENDANT LES FONCTIONS CRITIQUES
  // ========================================
  
  // Ne pas dormir si la pompe de remplissage fonctionne
  if (_acts.isTankPumpRunning()) {
    // Réinitialise le chrono pour patienter 10 min après l'arrêt
    _lastWakeMs = millis();
    Serial.println(F("[Auto] Auto-sleep différé - pompe de remplissage active"));
    return;
  }
  
  // Ne pas dormir si un nourrissage est en cours
  if (_currentFeedingPhase != FeedingPhase::NONE) {
    // Réinitialise le chrono pour patienter 10 min après la fin du nourrissage
    _lastWakeMs = millis();
    Serial.println(F("[Auto] Auto-sleep différé - nourrissage en cours"));
    return;
  }
  
  // Ne pas dormir si un décompte est en cours (remplissage ou nourrissage)
  if (isStillPending(_countdownEnd, millis())) {
    uint32_t remainingCountdownMs = remainingMs(_countdownEnd, millis());
    uint32_t remainingCountdownSec = remainingCountdownMs / 1000UL;
    
    // Ne reset le chronomètre que pour les décomptes longs (remplissage)
    // Les décomptes courts (post-nourrissage) ne bloquent pas le chronomètre
    if (remainingCountdownSec > 300) { // Plus de 5 minutes = décompte long
      _lastWakeMs = millis();
      Serial.printf("[Auto] Auto-sleep différé - décompte long en cours (%u s restants)\n", remainingCountdownSec);
    } else {
      Serial.printf("[Auto] Décompte court en cours (%u s restants) - chronomètre préservé\n", remainingCountdownSec);
    }
    return;
  }

  // Détection d'activité fine - priorité réduite pour sleep
  if (hasSignificantActivity()) {
    // Reset minimal pour permettre le sleep plus rapidement
    unsigned long currentMillis = millis();
    static unsigned long lastActivityCheck = 0;
    if (currentMillis - lastActivityCheck > 5000) { // Vérification toutes les 5s au lieu de bloquer
      lastActivityCheck = currentMillis;
      Serial.println(F("[Auto] Activité détectée - sleep différé (priorité réduite)"));
      _lastWakeMs = currentMillis; // Reset minimal
    }
    return;
  }

  // ========================================
  // VÉRIFICATION WEBSOCKET RÉDUITE (NON-CRITIQUE)
  // ========================================
  // Vérifier si des clients WebSocket sont actifs - délai réduit pour priorité sleep
  if (false) { // WebSocket removed for v11.68
    // Délai réduit pour permettre le sleep plus rapidement
    unsigned long currentMillis = millis();
    static unsigned long lastWebSocketCheck = 0;
    if (currentMillis - lastWebSocketCheck > 10000) { // Vérification toutes les 10s au lieu de bloquer
      lastWebSocketCheck = currentMillis;
      Serial.println(F("[Auto] WebSocket actif - sleep différé (priorité réduite)"));
      _lastWakeMs = currentMillis; // Reset minimal
    }
    return;
  }

  // Calcul du délai adaptatif
  const unsigned long currentMillis = millis();
  const int diff10s = _sensors.diffMaree(_lastReadings.wlAqua);
  AutomatismSleep::AutoSleepContext sleepCtx{};
  sleepCtx.forceWakeUp = forceWakeUp;
  sleepCtx.forceWakeFromWeb = &_forceWakeFromWeb;
  sleepCtx.lastWebActivityMs = &_lastWebActivityMs;
  sleepCtx.feedingInProgress = (_currentFeedingPhase != FeedingPhase::NONE);
  sleepCtx.tankPumpRunning = _acts.isTankPumpRunning();
  sleepCtx.countdownEnd = _countdownEnd;
  sleepCtx.lastWakeMs = &_lastWakeMs;
  sleepCtx.diff10s = diff10s;
  sleepCtx.tideTriggerCm = tideTriggerCm;
  sleepCtx.currentMillis = currentMillis;

  AutomatismSleep::AutoSleepDecision sleepDecision{};
  if (!_sleep.evaluateAutoSleep(sleepCtx, sleepDecision)) {
    return;
  }

  // Informations détaillées sur le passage en veille
  bool pumpReservoirOn = _acts.isTankPumpRunning();
  bool feedingActive = (_currentFeedingPhase != FeedingPhase::NONE);
  bool countdownActive = isStillPending(_countdownEnd, millis());
  _sleep.logSleepDecision(pumpReservoirOn,
                          feedingActive,
                          countdownActive,
                          sleepDecision.tideAscending,
                          sleepDecision.diff10s,
                          sleepDecision.awakeSec,
                          sleepDecision.adaptiveDelaySec,
                          static_cast<uint16_t>(freqWakeSec));

  // ========================================
  // PRÉPARATION AVANT MISE EN VEILLE LÉGÈRE
  // ========================================
  
  // Validation de l'état système avant sleep
  if (!validateSystemStateBeforeSleep()) {
    Serial.println(F("[Auto] Sleep annulé - état système invalide"));
    _lastWakeMs = millis(); // Reset pour réessayer plus tard
    return;
  }
  
  // 1) Envoi d'une dernière série de mesures à la BDD distante
  //    (uniquement si la connexion Wi-Fi est active ET mémoire suffisante)
  // OPTIMISATION: Skip sendFullUpdate si heap critique pour éviter PANIC
  uint32_t heapBeforeSend = ESP.getFreeHeap();
  const uint32_t MIN_HEAP_FOR_SEND = 50000; // 50KB minimum pour sendFullUpdate
  
  if (WiFi.status() == WL_CONNECTED) {
    if (heapBeforeSend >= MIN_HEAP_FOR_SEND) {
      Serial.printf("[Auto] 📤 Envoi mise à jour avant sleep (heap: %u bytes)\n", heapBeforeSend);
      sendFullUpdate(r, "resetMode=0");
      // Petit délai pour laisser la requête HTTP se terminer proprement
      vTaskDelay(pdMS_TO_TICKS(150));
      
      // Log mémoire après envoi
      uint32_t heapAfterSend = ESP.getFreeHeap();
      Serial.printf("[Auto] 📊 Heap après envoi: %u bytes (delta: %d bytes)\n", 
                    heapAfterSend, (int)(heapAfterSend - heapBeforeSend));
    } else {
      Serial.printf("[Auto] ⚠️ Skip sendFullUpdate avant sleep: heap insuffisant (%u < %u bytes)\n",
                    heapBeforeSend, MIN_HEAP_FOR_SEND);
      Serial.println(F("[Auto] ⚠️ Données seront synchronisées au prochain réveil"));
    }
  }

  // 2) Sauvegarde des flags/états critiques dans la NVS (en plus de l'heure gérée par Power)
  saveFeedingState();          // hour-based feeding flags
  _config.saveBouffeFlags();   // autres flags persistant

  // 3) Mémorise l'état courant des actionneurs pour le restaurer après le réveil
  bool pumpAquaWasOn = _acts.isAquaPumpRunning();
  bool heaterWasOn   = _acts.isHeaterOn();
  bool lightWasOn    = _acts.isLightOn();
  // Persister ce snapshot dans la NVS pour couvrir les reboot inopinés pendant le sleep
  saveActuatorSnapshotToNVS(pumpAquaWasOn, heaterWasOn, lightWasOn);

  // 4) Message raison détaillée + OLED (si écran présent)
  {
    unsigned long nowMs = millis();
    bool pumpReservoirOn = _acts.isTankPumpRunning();
    bool feedingActive = (_currentFeedingPhase != FeedingPhase::NONE);
    bool countdownActive = isStillPending(_countdownEnd, nowMs);
    bool hadCountdown = (_countdownEnd != 0);
    long secToCountdown = hadCountdown ? static_cast<long>(remainingMs(_countdownEnd, nowMs) / 1000UL) : 0;
    bool justAfterCountdown = hadCountdown && !countdownActive && (static_cast<uint32_t>(nowMs - _countdownEnd) <= 60000UL);

    // Marée actuelle (diff) — ici sans dernier readings, utiliser capteur direct
    int diffNow = _sensors.diffMaree(_lastReadings.wlAqua);

    // Éveil courant estimé
    unsigned long awakeSecLocal = (nowMs - _lastWakeMs) / 1000UL;

    // Détection marée montante locale
    int diffNowLocal = diffNow;
    bool tideAscendingLocal = (diffNowLocal > 1);

    if (tideAscendingLocal) {
      int delta = diffNowLocal;
      Serial.printf("[Auto] Sleep: marée montante (~10s, +%d cm), diff10s=%d, awake=%lus, next=%us\n",
                    delta, diffNow, awakeSecLocal, (uint32_t)freqWakeSec);
    } else if (justAfterCountdown) {
      Serial.printf("[Auto] Sleep: fin de décompte '%s' (il y a %lds), diffMarée=%d, awake=%lus, next=%us\n",
                    _countdownLabel.c_str(), -(secToCountdown), diffNow, awakeSecLocal, (uint32_t)freqWakeSec);
    } else {
      Serial.printf("[Auto] Sleep: inactivité (pompeReserv=%s, nourrissage=%s, decompteActif=%s), diffMarée=%d, awake=%lus, next=%us\n",
                    pumpReservoirOn ? "ON" : "OFF",
                    feedingActive ? "OUI" : "NON",
                    countdownActive ? "OUI" : "NON",
                    diffNow, awakeSecLocal, (uint32_t)freqWakeSec);
    }

    if (_disp.isPresent()) {
      if (tideAscendingLocal) {
        int delta = diffNowLocal;
        char d1[48];
        snprintf(d1, sizeof(d1), "+%d en ~10s", delta);
        char d2[48];
        snprintf(d2, sizeof(d2), "Maree diff10s:%d", diffNow);
        bool blink = (mailBlinkUntil && isStillPending(mailBlinkUntil, nowMs));
        _disp.showSleepReason("Cause: maree montante", d1, d2, 2500, blink);
      } else if (justAfterCountdown) {
        char d1[48];
        snprintf(d1, sizeof(d1), "Fin decompte: %s", _countdownLabel.c_str());
        char d2[48];
        snprintf(d2, sizeof(d2), "Il y a %lds - diff10s:%d", -(secToCountdown), diffNow);
        bool blink = (mailBlinkUntil && isStillPending(mailBlinkUntil, nowMs));
        _disp.showSleepReason("Cause: decompte termine", d1, d2, 2500, blink);
      } else {
        char d1[48];
        snprintf(d1, sizeof(d1), "Inactivite depuis %lus", awakeSecLocal);
        char d2[48];
        snprintf(d2, sizeof(d2), "Maree diff10s:%d", diffNow);
        bool blink = (mailBlinkUntil && isStillPending(mailBlinkUntil, nowMs));
        _disp.showSleepReason("Cause: inactivite", d1, d2, 2500, blink);
      }
    }
  }

  // ========================================
  // ENTRÉE EN LIGHT-SLEEP
  // ========================================
  // Garde minimale: rester éveillé au moins le délai minimum depuis le dernier réveil
  if ((uint32_t)(millis() - _lastWakeMs) < ::SleepConfig::MIN_AWAKE_TIME_MS) {
    unsigned long remain = (::SleepConfig::MIN_AWAKE_TIME_MS - (uint32_t)(millis() - _lastWakeMs)) / 1000UL;
    Serial.printf("[Auto] Sleep différé: éveil < %lus (reste ~%lus)\n", ::SleepConfig::MIN_AWAKE_TIME_MS/1000, remain);
    return;
  }

  // ========================================
  // 5) MISE AU REPOS DES ACTIONNEURS (DERNIÈRE ÉTAPE AVANT SLEEP)
  // ========================================
  _acts.stopTankPump(_pumpStartMs);
  _lastTankStopReason = TankPumpStopReason::SLEEP;
  Serial.printf("[Auto] Aqua OFF (auto-sleep) - etat_avant=%s\n", pumpAquaWasOn ? "ON" : "OFF");
  _acts.stopAquaPump();
  _lastAquaStopReason = AquaPumpStopReason::SLEEP;
  _acts.stopHeater();
  _acts.stopLight();

  // Calcul de la durée de sommeil selon la période et la configuration
  uint32_t actualSleepDuration = freqWakeSec; // Durée par défaut (serveur)
  
  if (::SleepConfig::LOCAL_SLEEP_DURATION_CONTROL) {
    if (isNightTime()) {
      actualSleepDuration = freqWakeSec * ::SleepConfig::NIGHT_SLEEP_MULTIPLIER; // Serveur × 3
      Serial.printf("[Auto] Durée sommeil nocturne: %u s (serveur: %u s × %d)\n", 
                    actualSleepDuration, freqWakeSec, (int)::SleepConfig::NIGHT_SLEEP_MULTIPLIER);
    } else {
      actualSleepDuration = freqWakeSec; // Serveur normal le jour
      Serial.printf("[Auto] Durée sommeil diurne: %u s (serveur normal)\n", freqWakeSec);
    }
  } else {
    Serial.printf("[Auto] Durée sommeil serveur: %u s\n", freqWakeSec);
  }
  
  // Journalise la durée de veille prévue avant d'entrer en light-sleep
  Serial.printf("[Auto] Entrée en light-sleep pour ~%u s\n", actualSleepDuration);
  
  // Envoi du mail de mise en veille (si mémoire suffisante)
  #if FEATURE_MAIL
  uint32_t heapBeforeMail = ESP.getFreeHeap();
  const uint32_t MIN_HEAP_FOR_MAIL = 45000; // 45KB minimum pour envoi email
  
  if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
    if (heapBeforeMail >= MIN_HEAP_FOR_MAIL) {
      char sleepReason[512];
      if (sleepDecision.tideAscending) {
        snprintf(sleepReason, sizeof(sleepReason), 
          "Délai écoulé (éveillé %ds, cible %ds) + marée montante (+%dcm) - aucune activité bloquante (pompeReserv=%s, nourrissage=%s, decompte=%s)",
          static_cast<int>(sleepDecision.awakeSec), sleepDecision.adaptiveDelaySec, sleepDecision.diff10s, 
          pumpReservoirOn ? "ON" : "OFF", feedingActive ? "OUI" : "NON", countdownActive ? "OUI" : "NON");
      } else {
        snprintf(sleepReason, sizeof(sleepReason), 
          "Délai écoulé (éveillé %ds, cible %ds) - aucune activité bloquante (pompeReserv=%s, nourrissage=%s, decompte=%s)",
          static_cast<int>(sleepDecision.awakeSec), sleepDecision.adaptiveDelaySec, 
          pumpReservoirOn ? "ON" : "OFF", feedingActive ? "OUI" : "NON", countdownActive ? "OUI" : "NON");
      }
      
      Serial.printf("[Auto] 📧 Envoi du mail de mise en veille (heap: %u bytes)...\n", heapBeforeMail);
      bool mailSent = _mailer.sendSleepMail(sleepReason, actualSleepDuration, _lastReadings);
      if (mailSent) {
        Serial.println("[Auto] Mail de mise en veille envoyé avec succès ✔");
      } else {
        Serial.println("[Auto] Échec de l'envoi du mail de mise en veille ✗");
      }
    } else {
      Serial.printf("[Auto] ⚠️ Skip mail de mise en veille: heap insuffisant (%u < %u bytes)\n",
                    heapBeforeMail, MIN_HEAP_FOR_MAIL);
    }
  }
  #endif
  
  // ========================================
  // NETTOYAGE MÉMOIRE AVANT SLEEP
  // ========================================
  Serial.println(F("[Auto] 🧹 Nettoyage mémoire avant sleep..."));
  uint32_t heapBeforeCleanup = ESP.getFreeHeap();
  
  // Force garbage collection et libération mémoire
  yield();
  vTaskDelay(pdMS_TO_TICKS(100)); // Délai pour permettre le nettoyage
  
  uint32_t heapAfterCleanup = ESP.getFreeHeap();
  Serial.printf("[Auto] 📊 Heap avant sleep: %u bytes (nettoyé: %d bytes)\n", 
                heapAfterCleanup, (int)(heapAfterCleanup - heapBeforeCleanup));
  
  // Vérification finale de sécurité
  if (heapAfterCleanup < 30000) {
    Serial.printf("[Auto] ⚠️ ATTENTION: Heap critique avant sleep (%u bytes) - risque de PANIC\n", 
                  heapAfterCleanup);
  }
  
  uint32_t actualSleepSeconds = _power.goToLightSleep(actualSleepDuration);

  // ========================================
  // AU RÉVEIL
  // ========================================
  
  // DIAGNOSTIC MÉMOIRE AU RÉVEIL
  uint32_t heapAfterWake = ESP.getFreeHeap();
  uint32_t minHeapEver = ESP.getMinFreeHeap();
  Serial.printf("[Auto] 📊 Réveil - Heap actuel: %u bytes, minimum historique: %u bytes\n", 
                heapAfterWake, minHeapEver);
  
  // Alerte si mémoire critique
  if (heapAfterWake < 30000) {
    Serial.printf("[Auto] 🚨 ALERTE: Heap critique au réveil (%u bytes)\n", heapAfterWake);
  }
  if (minHeapEver < 20000) {
    Serial.printf("[Auto] 🚨 ALERTE: Heap minimum historique critique (%u bytes) - risque de PANIC\n", minHeapEver);
  }
  
  // Envoi du mail de réveil (si mémoire suffisante)
  #if FEATURE_MAIL
  uint32_t heapBeforeWakeMail = ESP.getFreeHeap();
  const uint32_t MIN_HEAP_FOR_WAKE_MAIL = 45000; // 45KB minimum
  
  if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
    if (heapBeforeWakeMail >= MIN_HEAP_FOR_WAKE_MAIL) {
      String wakeReason = "Réveil automatique par timer";
      
      Serial.printf("[Auto] 📧 Envoi du mail de réveil (heap: %u bytes)...\n", heapBeforeWakeMail);
      bool mailSent = _mailer.sendWakeMail(wakeReason.c_str(), actualSleepSeconds, _lastReadings);
      if (mailSent) {
        Serial.println("[Auto] Mail de réveil envoyé avec succès ✔");
      } else {
        Serial.println("[Auto] Échec de l'envoi du mail de réveil ✗");
      }
    } else {
      Serial.printf("[Auto] ⚠️ Skip mail de réveil: heap insuffisant (%u < %u bytes)\n",
                    heapBeforeWakeMail, MIN_HEAP_FOR_WAKE_MAIL);
    }
  }
  #endif
  // Restauration des états actionneurs depuis la NVS (préféré)
  {
    bool snapAqua=false, snapHeater=false, snapLight=false;
    if (loadActuatorSnapshotFromNVS(snapAqua, snapHeater, snapLight)) {
      Serial.printf("[Auto] Wake(auto): restauration NVS aqua=%s heater=%s light=%s\n",
                    snapAqua?"ON":"OFF", snapHeater?"ON":"OFF", snapLight?"ON":"OFF");
      if (snapAqua)   _acts.startAquaPump(); else _acts.stopAquaPump();
      if (snapHeater) _acts.startHeater();   else _acts.stopHeater();
      if (snapLight)  _acts.startLight();      else _acts.stopLight();
      _prevPumpAquaState = snapAqua;
      clearActuatorSnapshotInNVS();
    } else {
      // Fallback RAM (compat)
      if (pumpAquaWasOn) _acts.startAquaPump();
      if (heaterWasOn)   _acts.startHeater();
      if (lightWasOn)    _acts.startLight();
      _prevPumpAquaState = pumpAquaWasOn;
    }
    // Sécurité: pompe réservoir reste arrêtée
    _acts.stopTankPump(_pumpStartMs);
    _prevPumpTankState = _acts.isTankPumpRunning();
  }
  Serial.println(F("[Auto] Réveil du light-sleep - reprise des opérations"));
  
  // Vérification de l'état système après réveil
  verifySystemStateAfterWakeup();

  // Nettoyage de sécurité (si non nettoyé plus haut)
  clearActuatorSnapshotInNVS();
  
  // Détection d'anomalies de sleep
  detectSleepAnomalies();
  
  // Rechargement des données NVS qui auraient pu être mises à jour pendant le sommeil
  _config.loadBouffeFlags();
  // Ré-initialisation éventuelle des capteurs
  _sensors.begin();
  // Récupération immédiate des variables distantes si Wi‑Fi connecté
  if (WiFi.status() == WL_CONNECTED) {
    StaticJsonDocument<4096> tmpDoc;
    fetchRemoteState(tmpDoc);
  }
  // Restauration des états actionneurs sauvegardés
  if (pumpAquaWasOn) _acts.startAquaPump();
  if (heaterWasOn)   _acts.startHeater();
  if (lightWasOn)    _acts.startLight();
  _prevPumpAquaState = pumpAquaWasOn;
  // La pompe de réserve est volontairement forcée à l'arrêt
  // ---------------------------------------------------------
  // Pompe réservoir reste arrêtée -> sécurité
  _acts.stopTankPump(_pumpStartMs);

  // Avant d'arrêter les actionneurs, on mémorise leur état pour pouvoir le restaurer au réveil
  // À la sortie du light-sleep la pompe est toujours arrêtée. Ne pas
  // forcer d'événement artificiel OFF.
  _prevPumpTankState = _acts.isTankPumpRunning();

  // Redémarre le chronomètre pour le prochain auto-sleep
  _lastWakeMs = millis();
  
  // Mise à jour des timestamps d'activité à la sortie de veille
  updateActivityTimestamp();
  
  Serial.println(F("[Auto] Réveil du light-sleep - états restaurés"));
} 

/* ------------------------------------------------------------------
 *  Helper pour créer le message de nourrissage avec temps effectif
 * ------------------------------------------------------------------*/
String Automatism::createFeedingMessage(const char* type, uint16_t bigDur, uint16_t smallDur) {
  // Délégation au module Feeding + ajout info système
  String baseMessage = _feeding.createMessage(type);
  
  // Ajouter les informations de niveau d'eau et variation de marée
  SensorReadings readings = _sensors.read();
  int diffMaree = _sensors.diffMaree(readings.wlAqua);
  
  char waterInfo[256];
  snprintf(waterInfo, sizeof(waterInfo), 
    "\nÉtat de l'eau:\n"
    "- Aquarium: %d cm\n"
    "- Réserve: %d cm\n"
    "- Variation marée: %d cm\n"
    "- Délai d'activité: %d s",
    readings.wlAqua, readings.wlTank, diffMaree, calculateAdaptiveSleepDelay());
  
  return baseMessage + waterInfo;
}

/* ------------------------------------------------------------------
 *  Nouvelles méthodes pour la détection d'activité fine et sleep adaptatif
 * ------------------------------------------------------------------*/

bool Automatism::hasSignificantActivity() {
  return _sleep.hasSignificantActivity();
}

void Automatism::updateActivityTimestamp() {
  _sleep.updateActivityTimestamp();
}

void Automatism::logActivity(const char* activity) {
  _sleep.logActivity(activity);
}

void Automatism::notifyLocalWebActivity() {
  _sleep.notifyLocalWebActivity();
  // Synchroniser la variable locale forceWakeUp
  forceWakeUp = _sleep.getForceWakeUp();
}

uint32_t Automatism::calculateAdaptiveSleepDelay() {
  return _sleep.calculateAdaptiveSleepDelay();
}

bool Automatism::isNightTime() {
  return _sleep.isNightTime();
}

bool Automatism::hasRecentErrors() {
  return _sleep.hasRecentErrors();
}

bool Automatism::verifySystemStateAfterWakeup() {
  // Délégation au module Sleep
  return _sleep.verifySystemStateAfterWakeup();
}

void Automatism::detectSleepAnomalies() {
  static uint64_t lastSleepStartTime = 0;
  static uint32_t expectedSleepDuration = 0;
  
  // Détecter les cycles de sleep trop courts
  if (lastSleepStartTime > 0) {
    uint64_t actualSleepTime = esp_timer_get_time() - lastSleepStartTime;
    uint64_t actualSleepSeconds = actualSleepTime / 1000000ULL;
    
    if (expectedSleepDuration > 0 && actualSleepSeconds < expectedSleepDuration * 0.8) {
      Serial.printf("[Auto] ⚠️ Anomalie: sleep interrompu prématurément (%u/%u s)\n", 
                   (uint32_t)actualSleepSeconds, expectedSleepDuration);
      _hasRecentErrors = true;
    }
  }
  
  // Détecter les échecs de reconnexion WiFi répétés
  if (_consecutiveWakeupFailures > ::SleepConfig::WAKEUP_FAILURE_ALERT_THRESHOLD) {
    Serial.printf("[Auto] ⚠️ Anomalie: %d échecs de réveil consécutifs\n", _consecutiveWakeupFailures);
  }
}

bool Automatism::validateSystemStateBeforeSleep() {
  Serial.println(F("[Auto] Validation de l'état système avant sleep"));
  
  // Vérifier qu'aucun actionneur critique n'est actif
  if (_acts.isTankPumpRunning()) {
    Serial.println(F("[Auto] ⚠️ Sleep annulé: pompe réservoir active"));
    return false;
  }
  
  // CRITIQUE: Vérifier la mémoire disponible avant sleep
  // Un heap trop bas peut causer un PANIC pendant ou après le sleep
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minFreeHeap = ESP.getMinFreeHeap();
  
  Serial.printf("[Auto] 📊 Heap libre: %u bytes (minimum historique: %u bytes)\n", 
                freeHeap, minFreeHeap);
  
  // Seuil de sécurité: minimum 60KB de heap libre pour entrer en sleep (v11.50)
  // En dessous de ce seuil, risque de PANIC critique
  const uint32_t MIN_HEAP_FOR_SLEEP = 60000; // AUGMENTÉ de 40KB à 60KB
  
  if (freeHeap < MIN_HEAP_FOR_SLEEP) {
    Serial.printf("[Auto] ⚠️ Sleep annulé: heap insuffisant (%u < %u bytes)\n", 
                  freeHeap, MIN_HEAP_FOR_SLEEP);
    Serial.println(F("[Auto] ⚠️ RISQUE DE PANIC - Mémoire critique détectée"));
    
    // Tentative de libération de mémoire d'urgence
    Serial.println(F("[Auto] 🧹 Tentative de nettoyage mémoire d'urgence..."));
    
    // Force un garbage collection si possible
    yield();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Vérifier à nouveau
    uint32_t freeHeapAfter = ESP.getFreeHeap();
    Serial.printf("[Auto] 📊 Heap après nettoyage: %u bytes (gain: %d bytes)\n", 
                  freeHeapAfter, (int)(freeHeapAfter - freeHeap));
    
    if (freeHeapAfter < MIN_HEAP_FOR_SLEEP) {
      Serial.println(F("[Auto] ⚠️ Mémoire toujours insuffisante - Sleep annulé"));
      return false;
    } else {
      Serial.println(F("[Auto] ✅ Mémoire récupérée - Sleep autorisé"));
    }
  }
  
  Serial.println(F("[Auto] ✅ État système validé pour sleep"));
  return true;
}

// v11.59: Indicateurs de mode pour OLED (A=Automatique, M=Manuel)
bool Automatism::isFeedingInManualMode() const {
  // Le nourrissage est en mode manuel si :
  // 1. Une phase de nourrissage est en cours (déclenchée manuellement)
  // 2. Les dernières commandes de nourrissage étaient manuelles
  return (_currentFeedingPhase != FeedingPhase::NONE) || 
         (_lastAquaManualOrigin != ManualOrigin::NONE);
}

bool Automatism::isRefillingInManualMode() const {
  // Le remplissage est en mode manuel si :
  // 1. La pompe réservoir est en cours d'exécution en mode manuel
  // 2. La dernière commande de pompe réservoir était manuelle
  return _manualTankOverride || 
         (_lastTankManualOrigin != ManualOrigin::NONE);
}

