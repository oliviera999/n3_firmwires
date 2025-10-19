#include <Arduino.h>
#include "wifi_manager.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "display_view.h"
#include "mailer.h"
#include "power.h"
#include "diagnostics.h"
#include "pins.h"
#include "project_config.h"
#include "secrets.h"
#include "automatism.h"
#include "web_client.h"
#include "web_server.h"
#include <ArduinoJson.h>
#if FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
#include <ArduinoOTA.h>
#endif
#include <esp_task_wdt.h>
// OTA (pull) helpers
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include "ota_config.h"
#include "ota_manager.h"
#include <LittleFS.h>
#include <Preferences.h>
#include "event_log.h"
#include "time_drift_monitor.h"
#include "email_buffer_pool.h"
#include "timer_manager.h"
#include "optimized_logger.h"
#include "gpio_parser.h"
// Pour mesurer les marges de stack FreeRTOS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ---------- Globals ----------
// Timeout connexion WiFi ramené à 12 s et intervalle de retry configurable
WifiManager wifi(Secrets::WIFI_LIST, Secrets::WIFI_COUNT, TimingConfig::WIFI_CONNECT_TIMEOUT_MS, TimingConfig::WIFI_RETRY_INTERVAL_MS);
DisplayView oled;
PowerManager power;
Mailer mailer;
WebClient web;
ConfigManager config;
OTAManager otaManager;

SystemSensors sensors;
SystemActuators acts;
Diagnostics diag;
WebServerManager webSrv(sensors, acts, diag);
Automatism autoCtrl(sensors, acts, web, oled, power, mailer, config);
TimeDriftMonitor timeDriftMonitor;

QueueHandle_t sensorQueue;
// Handles des tâches pour mesurer la marge de stack (HWM)
static TaskHandle_t g_sensorTaskHandle = nullptr;
static TaskHandle_t g_webTaskHandle = nullptr;              // Nouvelle tâche web dédiée
static TaskHandle_t g_autoTaskHandle = nullptr;
static TaskHandle_t g_displayTaskHandle = nullptr;

// Hostname unique pour WiFi/OTA
static char g_hostname[SystemConfig::HOSTNAME_BUFFER_SIZE];

// NEW: Flag pour indiquer qu'on vient tout juste d'appliquer une mise à jour OTA
static bool g_otaJustUpdated = false;
static String g_previousVersion = "";
// NEW: Variables pour la vérification périodique des mises à jour OTA
static unsigned long g_lastOtaCheck = 0;
static const unsigned long OTA_CHECK_INTERVAL = TimingConfig::OTA_CHECK_INTERVAL_MS; // 2 heures en millisecondes - optimisé

// Digest email scheduler state
static unsigned long g_lastDigestMs = 0;
static uint32_t g_lastDigestSeq = 0;
static const unsigned long DIGEST_INTERVAL_MS = TimingConfig::DIGEST_INTERVAL_MS; // 15 min par défaut (faible charge)
static Preferences g_prefsDigest;

/**************  OTA helpers  ****************/
// Fonction de comparaison de versions sémantiques optimisée (sans allocations dynamiques)
static int compareVersions(const String& version1, const String& version2) {
  // Parse les versions directement avec sscanf (plus efficace)
  int v1_parts[4] = {0, 0, 0, 0};
  int v2_parts[4] = {0, 0, 0, 0};
  
  // Parse version1 (max 4 composants)
  const char* v1_str = version1.c_str();
  sscanf(v1_str, "%d.%d.%d.%d", &v1_parts[0], &v1_parts[1], &v1_parts[2], &v1_parts[3]);
  
  // Parse version2 (max 4 composants)
  const char* v2_str = version2.c_str();
  sscanf(v2_str, "%d.%d.%d.%d", &v2_parts[0], &v2_parts[1], &v2_parts[2], &v2_parts[3]);
  
  // Compare les composants
  for (int i = 0; i < 4; i++) {
    if (v1_parts[i] < v2_parts[i]) return -1;  // version1 < version2
    if (v1_parts[i] > v2_parts[i]) return 1;   // version1 > version2
  }
  
  return 0; // versions égales
}

static void validatePendingOta() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  
  Serial.println("\n=== VALIDATION OTA AU DÉMARRAGE ===");
  if (running) {
    Serial.printf("[OTA] Partition en cours: %s (0x%08x)\n", running->label, running->address);
  }
  if (boot) {
    Serial.printf("[OTA] Partition de boot: %s (0x%08x)\n", boot->label, boot->address);
  }
  
  // Vérifier si les partitions sont différentes (indique qu'on a changé de partition)
  if (running && boot && running->address != boot->address) {
    Serial.println("[OTA] ⚠️ Partition courante différente de la partition de boot!");
  }
  
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
    switch(state) {
      case ESP_OTA_IMG_NEW:
        Serial.println("[OTA] État: NOUVELLE IMAGE (jamais démarrée)");
        break;
      case ESP_OTA_IMG_PENDING_VERIFY:
        Serial.println("[OTA] État: IMAGE EN ATTENTE DE VALIDATION");
        // On valide l'image et annule un éventuel rollback
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("[OTA] ✅ Image validée et rollback annulé");
        // Marque qu'une mise à jour vient d'être appliquée
        g_otaJustUpdated = true;
        // Charger l'ancienne version persistée dans NVS (namespace 'ota')
        {
          Preferences prefs;
          prefs.begin("ota", true);
          g_previousVersion = prefs.getString("prevVer", "");
          prefs.end();
        }
        break;
      case ESP_OTA_IMG_VALID:
        Serial.println("[OTA] État: IMAGE VALIDÉE");
        break;
      case ESP_OTA_IMG_INVALID:
        Serial.println("[OTA] État: IMAGE INVALIDE");
        break;
      case ESP_OTA_IMG_ABORTED:
        Serial.println("[OTA] État: IMAGE ABANDONNÉE");
        break;
      case ESP_OTA_IMG_UNDEFINED:
        Serial.println("[OTA] État: IMAGE NON DÉFINIE");
        break;
      default:
        Serial.printf("[OTA] État inconnu: %d\n", state);
    }
  } else {
    Serial.println("[OTA] Impossible de récupérer l'état de la partition");
  }
  
  Serial.println("==================================\n");
}

static void checkForOtaUpdate() {
  // OTA RÉACTIVÉ - Nouveau gestionnaire robuste
  Serial.println("\n=== OTA RÉACTIVÉ - NOUVEAU GESTIONNAIRE ROBUSTE ===");
  Serial.println("[OTA] ✅ OTA RÉACTIVÉ - Gestionnaire moderne et stable");
  Serial.println("[OTA] ✅ Fonctionnalités: Tâche dédiée, esp_http_client, fallback");
  Serial.println("[OTA] ✅ Sécurité: Watchdog désactivé, validation complète");
  
  // Configuration du gestionnaire OTA
  otaManager.setCurrentVersion(Config::VERSION);
  otaManager.setCheckInterval(TimingConfig::OTA_CHECK_INTERVAL_MS); // 2 heures
  
  // Callbacks pour le feedback
  otaManager.setStatusCallback([](const String& message) {
    Serial.printf("[OTA] %s\n", message.c_str());
    if (oled.isPresent()) {
      // Si c'est le début du téléchargement, afficher l'overlay
      if (message.indexOf("Téléchargement") >= 0 || message.indexOf("Début") >= 0) {
        oled.showOtaProgressOverlay(0);
      }
    }
  });
  
  otaManager.setErrorCallback([](const String& error) {
    Serial.printf("[OTA] ❌ %s\n", error.c_str());
    if (oled.isPresent()) {
      // Masquer l'overlay OTA en cas d'erreur
      oled.hideOtaProgressOverlay();
    }
  });
  
  otaManager.setProgressCallback([](int progress) {
    Serial.printf("[OTA] 📊 Progression: %d%%\n", progress);
    if (oled.isPresent()) {
      // Afficher l'overlay de progression en haut à droite
      oled.showOtaProgressOverlay((uint8_t)progress);
    }
  });
  
  // Vérification des mises à jour
  if (otaManager.checkForUpdate()) {
    Serial.println("[OTA] 🆕 Mise à jour disponible !");
    Serial.printf("[OTA] 📋 Version courante: %s\n", otaManager.getCurrentVersion().c_str());
    Serial.printf("[OTA] 📋 Version distante: %s\n", otaManager.getRemoteVersion().c_str());
    Serial.printf("[OTA] 📋 URL firmware: %s\n", otaManager.getFirmwareUrl().c_str());
    Serial.printf("[OTA] 📋 Taille: %s\n", OTAManager::formatBytes(otaManager.getFirmwareSize()).c_str());
    
    // Affichage sur l'OLED (optimisé - évite concaténation String)
    char otaMessage[32];
    snprintf(otaMessage, sizeof(otaMessage), "OTA dispo: %s", otaManager.getRemoteVersion().c_str());
    oled.showDiagnostic(otaMessage);
    
    // Déclenchement automatique de la mise à jour
    Serial.println("[OTA] 🚀 Déclenchement automatique de la mise à jour...");
    if (otaManager.performUpdate()) {
      Serial.println("[OTA] ✅ Mise à jour lancée avec succès");
    } else {
      Serial.println("[OTA] ❌ Échec du lancement de la mise à jour");
    }
  } else {
    Serial.println("[OTA] ✅ Aucune mise à jour disponible");
    Serial.printf("[OTA] 📋 Version courante: %s\n", otaManager.getCurrentVersion().c_str());
    Serial.printf("[OTA] 📋 Version distante: %s\n", otaManager.getRemoteVersion().c_str());
  }
  
  // Log des informations système pour diagnostic (optimisé - évite les String temporaires)
  Serial.printf("[OTA] 📊 Espace libre sketch: %d bytes\n", ESP.getFreeSketchSpace());
  Serial.printf("[OTA] 📊 Heap libre: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("[OTA] 📊 Version courante: %s\n", Config::VERSION);
  
  // Vérification de la partition courante
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) {
    Serial.printf("[OTA] 📊 Partition courante: %s (0x%08x)\n", running->label, running->address);
  }
}
/**********************************************/

void sensorTask(void* pv) {
  SensorReadings r;
  static bool wdtReg=false;
  if(!wdtReg){ esp_task_wdt_add(NULL); wdtReg=true; }
  
  SENSOR_LOG_PRINTLN(F("[Sensor] Tâche sensorTask démarrée - exécution à rythme naturel avec repos de 500ms"));
  
  for (;;) {
    // Suspendre la lecture des capteurs pendant l'OTA pour prioriser le réseau
    if (otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(4000)); // Attente simple pendant l'OTA
      continue;
    }
    // Reset watchdog before starting sensor reading
    esp_task_wdt_reset();
    
    // NOUVELLE PROTECTION CONTRE LES LECTURES LONGUES (v11.50)
    uint32_t sensorStartTime = millis();
    const uint32_t MAX_SENSOR_TIME_MS = 30000; // 30s max pour lecture capteurs
    
    // Lecture des capteurs avec gestion d'erreur sans exceptions
    r = sensors.read();
    
    // Vérification du temps de lecture
    uint32_t sensorDuration = millis() - sensorStartTime;
    if (sensorDuration > MAX_SENSOR_TIME_MS) {
      SENSOR_LOG_PRINTF("[Sensor] ⚠️ LECTURE CAPTEURS TROP LENTE: %u ms (limite: %u ms)\n", 
                    sensorDuration, MAX_SENSOR_TIME_MS);
    }
    
    // Reset watchdog pendant la lecture
    esp_task_wdt_reset();
    
    // Vérification des valeurs pour détecter les erreurs
    if (r.tempWater < SensorConfig::WaterTemp::MIN_VALID || r.tempWater > SensorConfig::WaterTemp::MAX_VALID ||
        r.tempAir < SensorConfig::AirSensor::TEMP_MIN || r.tempAir > SensorConfig::AirSensor::TEMP_MAX ||
        r.humidity < SensorConfig::AirSensor::HUMIDITY_MIN || r.humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
      SENSOR_LOG_PRINTLN(F("[Sensor] Erreur lors de la lecture des capteurs"));
      // Utiliser des valeurs par défaut en cas d'erreur
      r.tempAir = ExtendedSensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
      r.humidity = ExtendedSensorConfig::DefaultValues::HUMIDITY_DEFAULT;
      r.tempWater = ExtendedSensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
      r.wlPota = 0;
      r.wlAqua = 0;
      r.wlTank = 0;
      r.luminosite = 0;
    }
    
    // Reset watchdog after sensor reading
    esp_task_wdt_reset();
    
    if (sensorQueue) {
      BaseType_t result = xQueueSendToBack(sensorQueue, &r, pdMS_TO_TICKS(200)); // v11.34: Augmenté 20ms → 200ms
      if (result != pdTRUE) {
        SENSOR_LOG_PRINTLN(F("[Sensor] ⚠️ Queue pleine - donnée de capteur perdue!"));
      }
    } else {
      SENSOR_LOG_PRINTLN(F("[Sensor] ❌ Queue non disponible - donnée ignorée"));
    }
    
    // Repos pour permettre au système de gérer les autres tâches (WiFi, WebSocket, etc.)
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(500)); // 500ms de repos optimal pour l'ESP32
  }
}

// Tâche dédiée à l'affichage OLED (fréquence augmentée avec garde WDT)
void displayTask(void* pv) {
  static bool wdtReg = false;
  if (!wdtReg) { esp_task_wdt_add(NULL); wdtReg = true; }
  
  Serial.println(F("[Display] Tâche displayTask démarrée - cadence dynamique"));
  
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    // Pendant l'OTA, laisser l'écran aux callbacks OTA (progress/messages)
    if (otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(250)); // cadence fixe légère pendant OTA
      continue;
    }
    autoCtrl.updateDisplay();
    esp_task_wdt_reset();
    uint32_t intervalMs = autoCtrl.getRecommendedDisplayIntervalMs();
    if (intervalMs < TimingConfig::MIN_DISPLAY_INTERVAL_MS) intervalMs = TimingConfig::MIN_DISPLAY_INTERVAL_MS; // garde minimale
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(intervalMs));
  }
}

// Tâche dédiée au serveur web - Stratégie Web Dédié
void webTask(void* pv) {
  static bool wdtReg = false;
  if (!wdtReg) { esp_task_wdt_add(NULL); wdtReg = true; }
  
  Serial.println(F("[Web] Tâche webTask démarrée - interface web dédiée"));
  
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(100); // 100ms pour réactivité web maximale
  
  for (;;) {
    // Suspendre le traitement web pendant l'OTA pour libérer le réseau
    if (otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500)); // Cadence réduite pendant OTA
      continue;
    }
    
    // Reset watchdog avant traitement web
    esp_task_wdt_reset();
    
    // Traitement des requêtes web et WebSocket
    webSrv.loop();
    
    // Délai pour éviter la surcharge CPU
    vTaskDelayUntil(&lastWake, period);
  }
}

void automationTask(void* pv) {
  SensorReadings r;
  unsigned long lastHb=0;
  // Ajout pour l'affichage périodique des flags de bouffe
  unsigned long lastBouffeDisplay = 0;
  const unsigned long BOUFFE_DISPLAY_INTERVAL = TimingConfig::BOUFFE_DISPLAY_INTERVAL_MS; // 120 secondes (réduit la fréquence)
  
  // Ajout pour l'affichage périodique des statistiques de la pompe de réserve
  unsigned long lastPumpStatsDisplay = 0;
  const unsigned long PUMP_STATS_DISPLAY_INTERVAL = TimingConfig::PUMP_STATS_DISPLAY_INTERVAL_MS; // 10 minutes (réduit la fréquence)
  
  // Ajout pour l'affichage périodique des informations de dérive temporelle
  unsigned long lastDriftDisplay = 0;
  const unsigned long DRIFT_DISPLAY_INTERVAL = TimingConfig::DRIFT_DISPLAY_INTERVAL_MS; // 30 minutes
  
  // Enregistrement dans le watchdog AU DÉBUT de la tâche
  static bool wdtReg=false;
  if(!wdtReg){ esp_task_wdt_add(NULL); wdtReg=true; }
  
  for (;;) {
    // Reset watchdog au début de chaque itération
    esp_task_wdt_reset();
    
    // NOUVEAU TIMEOUT NON-BLOQUANT (v11.50)
    if (xQueueReceive(sensorQueue, &r, pdMS_TO_TICKS(1000)) == pdTRUE) {
      
      // Reset watchdog avant les opérations critiques
      esp_task_wdt_reset();
      autoCtrl.update(r);
      power.resetWatchdog();
      diag.update();
      
      unsigned long now = millis();
      // Suspendre le heartbeat pendant l'OTA pour libérer le réseau
      if (!otaManager.isUpdating()) {
        if (now - lastHb > 30000) {
          // Reset watchdog avant l'envoi heartbeat
          esp_task_wdt_reset();
          web.sendHeartbeat(diag);
          lastHb = now;
        }
      }
      
      // Vérification de stabilité WiFi et reconnexion intelligente
      wifi.checkConnectionStability();
      wifi.loop(&oled); // Gestion automatique des reconnexions
      
      // Affichage périodique de l'état des flags de bouffe
      if (now - lastBouffeDisplay > BOUFFE_DISPLAY_INTERVAL) {
        Serial.println(F("=== ÉTAT DES FLAGS DE BOUFFE ==="));
        Serial.printf("Bouffe Matin: %s\n", config.getBouffeMatinOk() ? "✓ FAIT" : "✗ À FAIRE");
        Serial.printf("Bouffe Midi:  %s\n", config.getBouffeMidiOk() ? "✓ FAIT" : "✗ À FAIRE");
        Serial.printf("Bouffe Soir:  %s\n", config.getBouffeSoirOk() ? "✓ FAIT" : "✗ À FAIRE");
        Serial.printf("Dernier jour: %d\n", config.getLastJourBouf());
        Serial.printf("Pompe lock:   %s\n", config.getPompeAquaLocked() ? "VERROUILLÉE" : "LIBRE");
        Serial.println(F("==============================="));
        lastBouffeDisplay = now;
      }
      
      // Affichage périodique des statistiques de la pompe de réserve
      if (now - lastPumpStatsDisplay > PUMP_STATS_DISPLAY_INTERVAL) {
        Serial.println(F("=== STATISTIQUES POMPE DE RÉSERVE ==="));
        Serial.printf("État actuel: %s\n", acts.isTankPumpRunning() ? "EN COURS" : "ARRÊTÉE");
        if (acts.isTankPumpRunning()) {
          unsigned long currentRuntime = acts.getTankPumpCurrentRuntime();
          Serial.printf("Durée actuelle: %lu ms (%lu s)\n", currentRuntime, currentRuntime / 1000);
        }
        Serial.printf("Temps total d'activité: %lu ms (%lu s)\n", 
                     acts.getTankPumpTotalRuntime(), acts.getTankPumpTotalRuntime() / 1000);
        Serial.printf("Nombre total d'arrêts: %lu\n", acts.getTankPumpTotalStops());
        if (acts.getTankPumpLastStopTime() > 0) {
          unsigned long timeSinceLastStop = now - acts.getTankPumpLastStopTime();
          Serial.printf("Dernier arrêt: il y a %lu ms (%lu s)\n", 
                       timeSinceLastStop, timeSinceLastStop / 1000);
        }
        Serial.println(F("====================================="));
        lastPumpStatsDisplay = now;
      }
      
      // Affichage périodique des informations de dérive temporelle
      if (now - lastDriftDisplay > DRIFT_DISPLAY_INTERVAL) {
        LOG_TIME(LOG_INFO, "=== INFORMATIONS DE DÉRIVE TEMPORELLE ===");
        timeDriftMonitor.printDriftInfo();
        
        // Informations temporelles supplémentaires
        time_t currentEpoch = time(nullptr);
        struct tm timeinfo;
        if (localtime_r(&currentEpoch, &timeinfo)) {
          LOG_TIME(LOG_INFO, "État temporel - Heure: %02d:%02d:%02d, Date: %02d/%02d/%04d", 
                   timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                   timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
          LOG_TIME(LOG_INFO, "Jour semaine: %d, Jour année: %d, DST: %s", 
                   timeinfo.tm_wday, timeinfo.tm_yday, timeinfo.tm_isdst ? "OUI" : "NON");
        }
        
        lastDriftDisplay = now;
      }
      
      // Reset watchdog après traitement
      esp_task_wdt_reset();
    } else {
      // Timeout atteint - pas de données disponibles
      Serial.println(F("[Auto] Timeout queue capteurs - cycle continu"));
      // v11.74: Poll distant même sans nouvelles mesures capteurs pour ne pas bloquer les commandes
      if (WiFi.status() == WL_CONNECTED) {
        esp_task_wdt_reset();
        Serial.println(F("[Auto] ▶️ Poll distant (fallback sans capteurs)"));
        // Appeler la façade publique qui déclenche la récupération distante
        // via le pipeline existant
        StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmpDoc;
        bool ok = autoCtrl.fetchRemoteState(tmpDoc);
        Serial.printf("[Auto] Fetch distant fallback: %s, keys=%u\n", ok?"OK":"KO", (unsigned)tmpDoc.size());
        if (ok) {
          Serial.println(F("[Auto] ▶️ Application immédiate des GPIO (fallback)"));
          GPIOParser::parseAndApply(tmpDoc, autoCtrl);
        }
      }
    }
  }
}

void setup() {
  // Configure the native TWDT to 60s pour détecter les blocages plus rapidement
  esp_task_wdt_deinit();
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = 60000; // RÉDUIT de 300s à 60s (v11.50)
  cfg.idle_core_mask = (1 << 0) | (1 << 1);
  cfg.trigger_panic = true;
  esp_task_wdt_init(&cfg);
  delay(SystemConfig::INITIAL_DELAY_MS);
  
  // Initialisation conditionnelle du moniteur série basée sur le flag SERIAL_MONITOR_ENABLED
  // Ce flag peut être contrôlé via ENABLE_SERIAL_MONITOR dans platformio.ini
  // Voir la documentation dans project_config.h pour plus de détails
  if (LogConfig::SERIAL_MONITOR_ENABLED) {
    Serial.begin(SystemConfig::SERIAL_BAUD_RATE);
    Serial.println("[INIT] Moniteur série activé");
  } else {
    Serial.end(); // S'assurer que Serial est désactivé si le flag est à false
    // Note: Sur certains systèmes, Serial.begin() pourrait être appelé automatiquement,
    // mais on peut le désactiver explicitement avec Serial.end()
    // Les appels Serial.print dans le code seront également désactivés via les macros définies
  }
  
  // Définit un hostname unique et cohérent pour DHCP/DNS (ffp3-XXXX)
  {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(g_hostname, sizeof(g_hostname), "%s-%04X", SystemConfig::HOSTNAME_PREFIX, (uint16_t)(chipId & 0xFFFF));
    WiFi.setHostname(g_hostname);
  }
  EventLog::begin();
  EventLog::add("Boot start");
  
  // Monter LittleFS globalement avec vérification robuste (v11.50)
  Serial.println("[FS] Mounting LittleFS...");
  uint32_t fsStartTime = millis();
  const uint32_t FS_TIMEOUT_MS = 10000; // 10s timeout pour montage
  
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] ❌ LittleFS mount failed - tentative de format");
    // Tentative de format et remontage
    if (LittleFS.format()) {
      Serial.println("[FS] ✅ Format réussi, tentative de remontage");
      if (LittleFS.begin(false)) {
        Serial.println("[FS] ✅ Remontage après format réussi");
      } else {
        Serial.println("[FS] ❌ CRITIQUE: Impossible de monter LittleFS même après format");
        // Continuer sans LittleFS mais avec avertissement
      }
    } else {
      Serial.println("[FS] ❌ CRITIQUE: Format LittleFS échoué");
    }
  } else {
    uint32_t fsDuration = millis() - fsStartTime;
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    Serial.printf("[FS] ✅ LittleFS ok: %u/%u bytes (montage: %u ms)\n", 
                  (unsigned)used, (unsigned)total, fsDuration);
    
    // Vérification de l'intégrité des fichiers critiques
    if (!LittleFS.exists("/index.html")) {
      Serial.println("[FS] ⚠️ Fichier index.html manquant");
    }
    if (!LittleFS.exists("/shared/common.js")) {
      Serial.println("[FS] ⚠️ Fichier common.js manquant");
    }
  }
  
  Serial.printf("[App] Démarrage FFP3CS v%s\n", Config::VERSION);
  
  // Log des informations temporelles au démarrage
  time_t bootTime = time(nullptr);
  struct tm bootTimeInfo;
  if (localtime_r(&bootTime, &bootTimeInfo)) {
    LOG_TIME(LOG_INFO, "Démarrage système - Heure: %02d:%02d:%02d, Date: %02d/%02d/%04d", 
             bootTimeInfo.tm_hour, bootTimeInfo.tm_min, bootTimeInfo.tm_sec,
             bootTimeInfo.tm_mday, bootTimeInfo.tm_mon + 1, bootTimeInfo.tm_year + 1900);
    LOG_TIME(LOG_INFO, "Epoch au démarrage: %lu", bootTime);
  } else {
    LOG_TIME(LOG_WARN, "Impossible de récupérer l'heure au démarrage");
  }
  EventLog::addf("App start v%s", Config::VERSION);
  
  // Valide la nouvelle image si on vient juste d'être mis à jour
  validatePendingOta();
  EventLog::add("OTA validation done");
  
  // Initialisation de la gestion du temps (avec restauration depuis la flash)
  power.initTime();
  EventLog::add("Time init");
  
  // Configuration NTP
  power.setNTPConfig(SystemConfig::NTP_GMT_OFFSET_SEC, SystemConfig::NTP_DAYLIGHT_OFFSET_SEC, SystemConfig::NTP_SERVER);

  timeDriftMonitor.attachPowerManager(&power);
  // Initialisation du moniteur de dérive temporelle
  timeDriftMonitor.begin();
  EventLog::add("Time drift monitor init");

  // S'assurer que l'overlay OTA est masqué au démarrage
  if (oled.isPresent()) {
    oled.hideOtaProgressOverlay();
  }

  // Pré-créer les namespaces NVS critiques pour éviter NOT_FOUND en lecture seule
  {
    Preferences prefs;
    const char* namespaces[] = {
      "bouffe","ota","remoteVars","rtc","diagnostics","alerts","timeDrift",
      "actSnap","actState","pendingSync","waterTemp","digest","cmdLog","reset"
    };
    const size_t count = sizeof(namespaces)/sizeof(namespaces[0]);
    for (size_t i = 0; i < count; ++i) { prefs.begin(namespaces[i], false); prefs.end(); }
  }
  // Charger état digest depuis NVS
  g_prefsDigest.begin("digest", true);
  g_lastDigestSeq = g_prefsDigest.getUInt("lastSeq", 0);
  g_lastDigestMs  = g_prefsDigest.getULong("lastMs", 0);
  g_prefsDigest.end();
  
  // Init OLED early
  Serial.println("[DEBUG] Avant oled.begin()");
  bool oledResult = oled.begin();
  Serial.printf("[DEBUG] oled.begin() retourné: %s\n", oledResult ? "true" : "false");
  Serial.printf("[DEBUG] oled.isPresent(): %s\n", oled.isPresent() ? "true" : "false");
  
  if (oled.isPresent()) {
    oled.showDiagnostic("WiFi...");
    Serial.println("[DEBUG] oled.showDiagnostic() appelé");
  } else {
    Serial.println("[DEBUG] OLED non présent - pas d'affichage");
  }
  
  // Connexion WiFi
  if (!wifi.connect(&oled)) {
    Serial.println(F("[App] WiFi non connecté - lancement AP de secours"));
    oled.showDiagnostic("AP secours");
    wifi.startFallbackAP();
    EventLog::add("WiFi connect failed; fallback AP started");
  } else {
    // Synchronisation NTP FORCÉE au démarrage pour garantir l'heure correcte
    oled.showDiagnostic("NTP sync");
    power.syncTimeFromNTP();
    Serial.printf("[Time] Heure après sync NTP: %s\n", power.getCurrentTimeString().c_str());

    // Prépare le système de mail avant toute action OTA
    #if FEATURE_MAIL
    mailer.begin();
    EventLog::add("Mailer ready");
    #else
    EventLog::add("Mailer disabled (FEATURE_MAIL=0)");
    #endif

    #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
    // Vérifie s'il existe une mise à jour disponible sur le serveur OTA
    checkForOtaUpdate();
    // Initialise le timer pour les vérifications périodiques
    g_lastOtaCheck = millis();
    EventLog::add("OTA initial check done");
    #endif

    // Initialisation ArduinoOTA classique
    #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
    ArduinoOTA.setPort(SystemConfig::ARDUINO_OTA_PORT);
    ArduinoOTA.setHostname(g_hostname);
    ArduinoOTA
      .onStart([]() {
        Serial.println("[OTA] Début de la mise à jour");
        // Désactive le modem sleep pour la session OTA
        WiFi.setSleep(false);
        EventLog::add("ArduinoOTA start");
        // Afficher l'overlay de progression OTA sur l'OLED
        if (oled.isPresent()) {
          oled.showOtaProgressOverlay(0);
        }
        // Envoi d'un mail de début OTA via l'interface web
        bool emailEnabled = autoCtrl.isEmailEnabled();
        String to = emailEnabled ? autoCtrl.getEmailAddress() : String(Config::DEFAULT_MAIL_TO);
        String body = String("Début de mise à jour OTA (Interface web ArduinoOTA)\n\n") +
                      "Détails:\n" +
                      "- Méthode: Interface web ArduinoOTA\n" +
                      "- Version courante: " + String(Config::VERSION) + "\n" +
                      "- Hostname: " + String(g_hostname) + "\n";
                      body += "- Hôte: "; body += g_hostname; body += ":"; body += String(SystemConfig::ARDUINO_OTA_PORT);
        mailer.sendAlert("OTA début - Interface web", body, to.c_str());
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        int percent = (progress * 100) / total;
        Serial.printf("[OTA] 📊 Progression ArduinoOTA: %d%% (%u/%u)\n", percent, progress, total);
        if (oled.isPresent()) {
          oled.showOtaProgressOverlay((uint8_t)percent);
        }
      })
      .onEnd([]() { 
        Serial.println("[OTA] Fin de la mise à jour"); 
        WiFi.setSleep(false);
        // Masquer l'overlay de progression OTA
        if (oled.isPresent()) {
          oled.hideOtaProgressOverlay();
        }
      })
      .onError([](ota_error_t error) { 
        Serial.printf("[OTA] Erreur %u\n", error);
        // Masquer l'overlay de progression OTA en cas d'erreur
        if (oled.isPresent()) {
          oled.hideOtaProgressOverlay();
        }
      });
    ArduinoOTA.begin();
    #endif

    // Synchronisation initiale des variables distantes (avant lecture NVS)
    {
      StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
      autoCtrl.fetchRemoteState(tmp); // Enregistre les variables reçues dans la NVS
    }
  }
  
  // Initialisation des composants (déjà OLED ok)
  oled.showDiagnostic("Sensors");
  sensors.begin();
  oled.showDiagnostic("Actuators");
  acts.begin();
  oled.showDiagnostic("WebSrv");
  webSrv.begin();
  oled.showDiagnostic("Diag");
  diag.begin();
  
  // Initialisation des systèmes
  oled.showDiagnostic("Systems");
  power.initWatchdog();
  
  // Initialisation du système Modem Sleep + Light Sleep
  power.initModemSleep();
  
  // Initialiser le Timer Manager centralisé
  TimerManager::init();
  
  // Initialiser le logger optimisé (niveau INFO par défaut)
  OptimizedLogger::init(OptimizedLogger::LOG_INFO);
  
    // Envoi d'un mail de test au démarrage (optimisé avec buffer pooling)
  #if FEATURE_MAIL
  if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
    OPT_LOG_INFO("Envoi du mail de test au démarrage...");
    
    String testMessage;
    testMessage.reserve(1024);
    testMessage += "Test de démarrage FFP3CS v"; testMessage += String(Config::VERSION); testMessage += "\n";
    testMessage += "Environnement: "; testMessage += String(Config::SENSOR); testMessage += "\n";
    testMessage += "Hostname: "; testMessage += g_hostname; testMessage += "\n";
    testMessage += "IP: "; testMessage += WiFi.localIP().toString(); testMessage += "\n";
    testMessage += "MAC: "; testMessage += WiFi.macAddress(); testMessage += "\n";
    testMessage += "SSID: "; testMessage += WiFi.SSID(); testMessage += "\n";
    testMessage += "Uptime: "; testMessage += String(millis() / 1000); testMessage += "s\n";
    testMessage += "Timestamp: "; testMessage += power.getCurrentTimeString(); testMessage += "\n";

    // Ajouter les informations détaillées de redémarrage
    testMessage += "\n[RESTART INFO] Informations de redémarrage:\n";
    testMessage += diag.generateRestartReport();
    
    // Informations supplémentaires sur le sketch et la mémoire
    testMessage += "- Taille du sketch: "; testMessage += String(ESP.getSketchSize()); testMessage += " bytes\n";
    testMessage += "- Espace libre sketch: "; testMessage += String(ESP.getFreeSketchSpace()); testMessage += " bytes\n";

    // Ajouter les informations de dérive temporelle
    testMessage += "\n[TIME DRIFT] Informations de dérive:\n";
    testMessage += timeDriftMonitor.generateDriftReport();
    
    // Ajouter les variables NVS principales
    testMessage += "\n[NVS] Variables enregistrées:\n";
    {
      Preferences prefs; 
      // Namespace bouffe
      prefs.begin("bouffe", true);
      testMessage += "- bouffeMatinOk: "; testMessage += prefs.getBool("bouffeMatinOk", false) ? "true" : "false"; testMessage += "\n";
      testMessage += "- bouffeMidiOk: ";  testMessage += prefs.getBool("bouffeMidiOk", false)  ? "true" : "false"; testMessage += "\n";
      testMessage += "- bouffeSoirOk: ";  testMessage += prefs.getBool("bouffeSoirOk", false)  ? "true" : "false"; testMessage += "\n";
      testMessage += "- lastJourBouf: ";  testMessage += String(prefs.getInt("lastJourBouf", -1)); testMessage += "\n";
      testMessage += "- pompeAquaLocked: "; testMessage += prefs.getBool("pompeAquaLocked", false) ? "true" : "false"; testMessage += "\n";
      testMessage += "- forceWakeUp: "; testMessage += prefs.getBool("forceWakeUp", false) ? "true" : "false"; testMessage += "\n";
      prefs.end();

      // Namespace ota
      prefs.begin("ota", true);
      testMessage += "- ota.updateFlag: "; testMessage += prefs.getBool("updateFlag", true) ? "true" : "false"; testMessage += "\n";
      prefs.end();

      // Namespace remoteVars (truncate for safety)
      prefs.begin("remoteVars", true);
      String remoteJson = prefs.getString("json", "");
      prefs.end();
      if (remoteJson.length() > 0) {
        String preview = remoteJson.substring(0, min((size_t)BufferConfig::JSON_PREVIEW_MAX_SIZE, (size_t)remoteJson.length()));
        testMessage += "- remoteVars.json: ";
        testMessage += preview;
        if (remoteJson.length() > BufferConfig::JSON_PREVIEW_MAX_SIZE) testMessage += "...";
        testMessage += "\n";
      } else {
        testMessage += "- remoteVars.json: (vide)\n";
      }

      // Namespace rtc
      prefs.begin("rtc", true);
      unsigned long savedEpoch = prefs.getULong("epoch", 0);
      prefs.end();
      testMessage += "- rtc.epoch: "; testMessage += String(savedEpoch); testMessage += "\n";

      // Namespace diagnostics
      prefs.begin("diagnostics", true);
      unsigned int rebootCnt = prefs.getUInt("rebootCnt", 0);
      unsigned int minHeap = prefs.getUInt("minHeap", 0);
      unsigned int httpOk = prefs.getUInt("httpOk", 0);
      unsigned int httpKo = prefs.getUInt("httpKo", 0);
      unsigned int otaOk = prefs.getUInt("otaOk", 0);
      unsigned int otaKo = prefs.getUInt("otaKo", 0);
      prefs.end();
      testMessage += "- diagnostics.rebootCnt: "; testMessage += String(rebootCnt); testMessage += "\n";
      testMessage += "- diagnostics.minHeap: "; testMessage += String(minHeap); testMessage += " bytes\n";
      testMessage += "- diagnostics.httpOk: "; testMessage += String(httpOk); testMessage += "\n";
      testMessage += "- diagnostics.httpKo: "; testMessage += String(httpKo); testMessage += "\n";
      testMessage += "- diagnostics.otaOk: "; testMessage += String(otaOk); testMessage += "\n";
      testMessage += "- diagnostics.otaKo: "; testMessage += String(otaKo); testMessage += "\n";
    }
    
    // Borne de sécurité: 6000 bytes
    if (testMessage.length() > BufferConfig::EMAIL_MAX_SIZE_BYTES) {
      testMessage = testMessage.substring(0, BufferConfig::EMAIL_MAX_SIZE_BYTES - 3);
      testMessage += "...";
    }
    String subjBoot = "FFP3CS - Test de démarrage ["; subjBoot += g_hostname; subjBoot += "]";
    bool mailSent = mailer.send(subjBoot.c_str(), testMessage.c_str(), "Test User", autoCtrl.getEmailAddress().c_str());
    if (mailSent) { EventLog::add("Boot test email sent"); }
    
    if (mailSent) {
      OPT_LOG_INFO("Mail de test envoyé avec succès ✔");
    } else {
      OPT_LOG_ERROR("Échec de l'envoi du mail de test ✗");
    }
  } else {
    OPT_LOG_INFO("WiFi non connecté - mail de test ignoré");
  }
  #else
  Serial.println("[App] Mail désactivé (FEATURE_MAIL=0) - mail de test ignoré");
  #endif
  
  // ========================================
  // CHARGEMENT COMPLET CONFIGURATION NVS (v11.32)
  // ========================================
  // Charge TOUTES les variables depuis NVS au démarrage
  // Inclut: flags bouffe, email, seuils, durées, etc.
  // Fallback valeurs par défaut si NVS vide
  Serial.println(F("\n[App] Chargement configuration complète depuis NVS..."));
  config.loadConfigFromNVS();
  
  autoCtrl.begin();

  // --- Première synchronisation distante IMMÉDIATE -----------------------------
  if (WiFi.status() == WL_CONNECTED) {
    // 1) Récupération IMMÉDIATE des variables distantes pour un chargement web rapide
    Serial.println("[App] Chargement immédiat des variables distantes...");
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;  // taille confortable
    bool remoteOk = autoCtrl.fetchRemoteState(tmp);
    if (remoteOk) {
      Serial.println("[App] Variables distantes chargées avec succès");
    } else {
      Serial.println("[App] Échec chargement variables distantes - utilisation du cache");
    }

    // 2) Vérification des mises à jour OTA et envoi d'emails de notification
    // (maintenant que les variables distantes sont à jour)
    Serial.println("[App] Vérification des flags OTA...");
    Serial.printf("[App] g_otaJustUpdated: %s\n", g_otaJustUpdated ? "true" : "false");
    Serial.printf("[App] config.getOtaUpdateFlag(): %s\n", config.getOtaUpdateFlag() ? "true" : "false");
    Serial.printf("[App] autoCtrl.isEmailEnabled(): %s\n", autoCtrl.isEmailEnabled() ? "true" : "false");
    
    // NOTE: ancien code de test supprimé pour éviter l'activation OTA involontaire en production
    
    // 1. Mise à jour via serveur distant automatique
    if (g_otaJustUpdated && autoCtrl.isEmailEnabled()) {
      Serial.println("[App] Envoi email pour mise à jour OTA serveur distant...");
      String body = String("Mise à jour OTA effectuée avec succès.\n\n");
      body += "Détails de la mise à jour:\n";
      body += "- Méthode: Serveur distant automatique\n";
      body += "- Ancienne version: " + g_previousVersion + "\n";
      body += "- Nouvelle version: " + String(Config::VERSION) + "\n";
      body += "- Hostname: "; body += g_hostname; body += "\n";
      body += "- Compilé le: "; body += __DATE__; body += " "; body += __TIME__; body += "\n";
      body += "- Redémarrage automatique effectué";
      // Borne de sécurité: 6 KB max
      if (body.length() > BufferConfig::EMAIL_MAX_SIZE_BYTES) { body = body.substring(0, BufferConfig::EMAIL_MAX_SIZE_BYTES - 3) + "..."; }
      String subjOtaSrv = "OTA mise à jour - Serveur distant ["; subjOtaSrv += g_hostname; subjOtaSrv += "]";
      bool emailSent = mailer.sendAlert(subjOtaSrv.c_str(), body, autoCtrl.getEmailAddress().c_str());
      Serial.printf("[App] Email serveur distant %s\n", emailSent ? "envoyé" : "échoué");
      g_otaJustUpdated = false; // notification envoyée
      // Effacer l'ancienne version persistée et reset local
      {
        Preferences prefs;
        prefs.begin("ota", false);
        prefs.remove("prevVer");
        prefs.end();
      }
      g_previousVersion = "";
    }
    
    // 2. Mise à jour via interface web (activable/désactivable)
    #if FEATURE_OTA && FEATURE_OTA != 0
    if (config.getOtaUpdateFlag()) {
      // Vérifier si l'email est activé, sinon utiliser l'adresse par défaut
      bool emailEnabled = autoCtrl.isEmailEnabled();
      String emailAddress = emailEnabled ? autoCtrl.getEmailAddress() : String(Config::DEFAULT_MAIL_TO);
      
      if (emailEnabled) {
        Serial.println("[App] Envoi email pour mise à jour OTA interface web...");
      } else {
        Serial.println("[App] Email non activé dans les variables distantes, utilisation de l'adresse par défaut...");
      }
      
      String body = String("Mise à jour OTA effectuée avec succès.\n\n");
      body += "Détails de la mise à jour:\n";
      body += "- Méthode: Interface web ElegantOTA\n";
      body += "- Nouvelle version: " + String(Config::VERSION) + "\n";
      body += "- Hostname: "; body += g_hostname; body += "\n";
      body += "- Compilé le: "; body += __DATE__; body += " "; body += __TIME__; body += "\n";
      body += "- Redémarrage automatique effectué";
      // Borne de sécurité: 6 KB max
      if (body.length() > BufferConfig::EMAIL_MAX_SIZE_BYTES) { body = body.substring(0, BufferConfig::EMAIL_MAX_SIZE_BYTES - 3) + "..."; }
      String subjOtaWeb = "OTA mise à jour - Interface web ["; subjOtaWeb += g_hostname; subjOtaWeb += "]";
      bool emailSent = mailer.sendAlert(subjOtaWeb.c_str(), body, emailAddress.c_str());
      Serial.printf("[App] Email interface web %s\n", emailSent ? "envoyé" : "échoué");
      config.setOtaUpdateFlag(false); // reset du flag
    }
    #endif

    // 3) Première mesure envoyée, incluant resetMode=0 pour acquitter le flag distant
    SensorReadings rs = sensors.read();
    Measurements ms{};
    ms.tempAir   = rs.tempAir;
    ms.humid     = rs.humidity;
    ms.tempWater = rs.tempWater;
    ms.wlPota    = rs.wlPota;
    ms.wlAqua    = rs.wlAqua;
    ms.wlTank    = rs.wlTank;
    ms.diffMaree = sensors.diffMaree(rs.wlAqua);
    ms.luminosite = rs.luminosite;
    ms.pumpAqua  = acts.isAquaPumpRunning();
    ms.pumpTank  = acts.isTankPumpRunning();
    ms.heater    = acts.isHeaterOn();
    ms.light     = acts.isLightOn();
    autoCtrl.sendFullUpdate(rs, "resetMode=0");
  }

  // Affichage final propre - effacer l'écran et afficher juste "Init ok"
  if (oled.isPresent()) {
    // Forcer la fin du splash screen avant l'affichage final
    oled.forceEndSplash();
    oled.clear();
    oled.showDiagnostic("Init ok");
    delay(SystemConfig::FINAL_INIT_DELAY_MS); // 1 seconde propre pour lire le message final
  }
  
  // Création queue & tasks FreeRTOS - Stratégie Web Dédié
  sensorQueue = xQueueCreate(100, sizeof(SensorReadings)); // AUGMENTÉ de 30 à 100 slots (v11.50)
  if (sensorQueue) {
    Serial.printf("[App] ✅ Queue capteurs créée avec succès (100 slots)\n");
    xTaskCreatePinnedToCore(sensorTask, "sensorTask", TaskConfig::SENSOR_TASK_STACK_SIZE, nullptr, TaskConfig::SENSOR_TASK_PRIORITY, &g_sensorTaskHandle, TaskConfig::TASK_CORE_ID); // core 1 - priorité CRITIQUE pour l'acquisition
    xTaskCreatePinnedToCore(webTask, "webTask", TaskConfig::WEB_TASK_STACK_SIZE, nullptr, TaskConfig::WEB_TASK_PRIORITY, &g_webTaskHandle, TaskConfig::TASK_CORE_ID); // priorité HAUTE pour l'interface web
    xTaskCreatePinnedToCore(automationTask, "autoTask", TaskConfig::AUTOMATION_TASK_STACK_SIZE, nullptr, TaskConfig::AUTOMATION_TASK_PRIORITY, &g_autoTaskHandle, TaskConfig::TASK_CORE_ID); // priorité MOYENNE pour la logique pure
    // Tâche d'affichage OLED avec stack augmenté et fréquence réduite
    xTaskCreatePinnedToCore(displayTask, "displayTask", TaskConfig::DISPLAY_TASK_STACK_SIZE, nullptr, TaskConfig::DISPLAY_TASK_PRIORITY, &g_displayTaskHandle, TaskConfig::TASK_CORE_ID); // priorité BASSE pour l'affichage
    // Log unique des marges de stack au boot
    vTaskDelay(pdMS_TO_TICKS(50)); // petite latence pour laisser démarrer
    UBaseType_t hwmSensor  = g_sensorTaskHandle  ? uxTaskGetStackHighWaterMark(g_sensorTaskHandle)  : 0;
    UBaseType_t hwmWeb     = g_webTaskHandle     ? uxTaskGetStackHighWaterMark(g_webTaskHandle)     : 0;
    UBaseType_t hwmAuto    = g_autoTaskHandle    ? uxTaskGetStackHighWaterMark(g_autoTaskHandle)    : 0;
    UBaseType_t hwmDisplay = g_displayTaskHandle ? uxTaskGetStackHighWaterMark(g_displayTaskHandle) : 0;
    UBaseType_t hwmLoop    = uxTaskGetStackHighWaterMark(NULL);
    Serial.printf("[Stacks] HWM at boot - sensor:%u web:%u auto:%u display:%u loop:%u bytes\n",
                  (unsigned)hwmSensor, (unsigned)hwmWeb, (unsigned)hwmAuto, (unsigned)hwmDisplay, (unsigned)hwmLoop);
    EventLog::addf("Stacks HWM boot sensor=%u web=%u auto=%u display=%u loop=%u",
                   (unsigned)hwmSensor, (unsigned)hwmWeb, (unsigned)hwmAuto, (unsigned)hwmDisplay, (unsigned)hwmLoop);
  } else {
    Serial.println(F("[App] ❌ CRITIQUE: Échec création queue capteurs - arrêt système"));
    EventLog::add("CRITICAL: Failed to create sensor queue");
    // En cas d'échec, continuer sans tâches mais avec un warning
    Serial.println(F("[App] ⚠️ Système dégradé: pas de tâches FreeRTOS"));
  }
  
  Serial.println(F("[App] Initialisation terminée"));
  EventLog::add("Init done");
}

void loop() {
  power.updateTime();
  
  // Traitement des timers centralisés (priorité haute)
  TimerManager::process();
  
  // Mise à jour du moniteur de dérive temporelle
  timeDriftMonitor.update();
  
  #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
  ArduinoOTA.handle();
  #endif
  wifi.loop(&oled);
  
  // Vérification périodique des mises à jour OTA (gérée par Timer Manager)
  #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  static bool otaTimerAdded = false;
  if (!otaTimerAdded) {
    TimerManager::addTimer("OTA_CHECK", OTA_CHECK_INTERVAL, []() {
      Serial.println("[OTA] Vérification périodique des mises à jour...");
      checkForOtaUpdate();
    });
    otaTimerAdded = true;
  }
  #endif
  
  power.resetWatchdog();

  // Periodic log digest by email (géré par Timer Manager)
  static bool digestTimerAdded = false;
  if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled() && !digestTimerAdded) {
    TimerManager::addTimer("EMAIL_DIGEST", DIGEST_INTERVAL_MS, []() {
      if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
        unsigned long currentTime = millis();
        String body;
        body.reserve(BufferConfig::JSON_DOCUMENT_SIZE);
        body += "Résumé des événements récents ("; 
        body += Utils::getSystemInfo(); 
        body += ")\n\n";
        // Ajouter marges de stack des tâches si disponibles
        body += "[Stacks] Marges (bytes):\n";
        if (g_sensorTaskHandle) { body += "- sensorTask: "; body += uxTaskGetStackHighWaterMark(g_sensorTaskHandle); body += "\n"; }
        if (g_webTaskHandle)    { body += "- webTask: ";    body += uxTaskGetStackHighWaterMark(g_webTaskHandle);    body += "\n"; }
        if (g_autoTaskHandle)   { body += "- autoTask: ";   body += uxTaskGetStackHighWaterMark(g_autoTaskHandle);   body += "\n"; }
        if (g_displayTaskHandle){ body += "- displayTask: ";body += uxTaskGetStackHighWaterMark(g_displayTaskHandle);body += "\n"; }
        // Marge de la loop() actuelle
        body += "- loop(): "; body += uxTaskGetStackHighWaterMark(NULL); body += "\n\n";
        
        // Ajouter informations de dérive temporelle
        body += "[TIME DRIFT] Dérive temporelle:\n";
        body += timeDriftMonitor.generateDriftReport();
        body += "\n";
        uint32_t newSeq = EventLog::dumpSince(g_lastDigestSeq, body, BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES);
        if (newSeq != g_lastDigestSeq) {
          String subjDigest = "FFP3CS - Digest événements ["; subjDigest += g_hostname; subjDigest += "]";
          // Borne de sécurité: 5 KB max
          if (body.length() > BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES) {
            body = body.substring(0, BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES - 3);
            body += "...";
          }
          bool ok = mailer.send(subjDigest.c_str(), body.c_str(), "User", autoCtrl.getEmailAddress().c_str());
          EventLog::add(ok ? "Digest email sent" : "Digest email failed");
          g_lastDigestSeq = newSeq;
        } else {
          // Pas de nouveaux événements, espace les envois
          EventLog::add("Digest: no new events");
        }
        g_lastDigestMs = currentTime;
        // Persister dans NVS
        g_prefsDigest.begin("digest", false);
        g_prefsDigest.putUInt("lastSeq", g_lastDigestSeq);
        g_prefsDigest.putULong("lastMs", g_lastDigestMs);
        g_prefsDigest.end();
      }
    });
    digestTimerAdded = true;
  }
  
  // Timer pour les statistiques du Timer Manager et Logger (toutes les 5 minutes)
  static bool statsTimerAdded = false;
  if (!statsTimerAdded) {
    TimerManager::addTimer("STATS_REPORT", 300000, []() { // 5 minutes
      TimerManager::logStats();
      OptimizedLogger::logStats();
    });
    statsTimerAdded = true;
  }
  
  vTaskDelay(pdMS_TO_TICKS(500)); // Délai augmenté pour réduire la charge CPU
} 