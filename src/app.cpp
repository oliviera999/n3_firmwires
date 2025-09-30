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
#include <vector>
#include <algorithm>
#include "ota_config.h"
#include "ota_manager.h"
#include <LittleFS.h>
#include <Preferences.h>
#include "event_log.h"
#include "time_drift_monitor.h"
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
// Fonction de comparaison de versions sémantiques
static int compareVersions(const String& version1, const String& version2) {
  // Divise les versions en composants (ex: "5.1.0" -> [5, 1, 0])
  std::vector<int> v1_parts, v2_parts;
  
  // Parse version1
  String v1 = version1;
  while (v1.length() > 0) {
    int dotPos = v1.indexOf('.');
    if (dotPos == -1) {
      v1_parts.push_back(v1.toInt());
      break;
    }
    v1_parts.push_back(v1.substring(0, dotPos).toInt());
    v1 = v1.substring(dotPos + 1);
  }
  
  // Parse version2
  String v2 = version2;
  while (v2.length() > 0) {
    int dotPos = v2.indexOf('.');
    if (dotPos == -1) {
      v2_parts.push_back(v2.toInt());
      break;
    }
    v2_parts.push_back(v2.substring(0, dotPos).toInt());
    v2 = v2.substring(dotPos + 1);
  }
  
  // Compare les composants
  int maxParts = max(v1_parts.size(), v2_parts.size());
  for (int i = 0; i < maxParts; i++) {
    int v1_part = (i < v1_parts.size()) ? v1_parts[i] : 0;
    int v2_part = (i < v2_parts.size()) ? v2_parts[i] : 0;
    
    if (v1_part < v2_part) return -1;  // version1 < version2
    if (v1_part > v2_part) return 1;   // version1 > version2
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
    
    // Affichage sur l'OLED
    String otaMessage = "OTA dispo: " + otaManager.getRemoteVersion();
    oled.showDiagnostic(otaMessage.c_str());
    
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
  
  // Log des informations système pour diagnostic
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
  
  Serial.println(F("[Sensor] Tâche sensorTask démarrée - fréquence 2000ms pour stabilité"));
  
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(2000);
  for (;;) {
    // Suspendre la lecture des capteurs pendant l'OTA pour prioriser le réseau
    if (otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&lastWake, period);
      continue;
    }
    // Reset watchdog before starting sensor reading
    esp_task_wdt_reset();
    
    // Lecture des capteurs avec gestion d'erreur sans exceptions
    r = sensors.read();
    
    // Vérification des valeurs pour détecter les erreurs
    if (r.tempWater < ExtendedSensorConfig::ValidationRanges::TEMP_MIN || r.tempWater > ExtendedSensorConfig::ValidationRanges::TEMP_MAX ||
        r.tempAir < ExtendedSensorConfig::ValidationRanges::TEMP_MIN || r.tempAir > ExtendedSensorConfig::ValidationRanges::TEMP_MAX ||
        r.humidity < ExtendedSensorConfig::ValidationRanges::HUMIDITY_MIN || r.humidity > ExtendedSensorConfig::ValidationRanges::HUMIDITY_MAX) {
      Serial.println(F("[Sensor] Erreur lors de la lecture des capteurs"));
      // Utiliser des valeurs par défaut en cas d'erreur
      r.tempAir = ExtendedSensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
      r.humidity = ExtendedSensorConfig::DefaultValues::HUMIDITY_DEFAULT;
      r.tempWater = ExtendedSensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
      r.wlPota = 0;
      r.wlAqua = 0;
      r.wlTank = 0;
      r.luminosite = 0;
      r.voltageMv = 0;
    }
    
    // Reset watchdog after sensor reading
    esp_task_wdt_reset();
    
    if (sensorQueue) xQueueSendToBack(sensorQueue, &r, pdMS_TO_TICKS(20));
    
    // Reset watchdog before delay
    esp_task_wdt_reset();
    vTaskDelayUntil(&lastWake, period); // période stable 2 s
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
  
  for (;;) {
    // Reset watchdog au début de chaque itération
    esp_task_wdt_reset();
    
    if (xQueueReceive(sensorQueue, &r, portMAX_DELAY) == pdTRUE) {
      static bool wdtReg=false;
      if(!wdtReg){ esp_task_wdt_add(NULL); wdtReg=true; }
      
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
        Serial.println(F("=== INFORMATIONS DE DÉRIVE TEMPORELLE ==="));
        timeDriftMonitor.printDriftInfo();
        lastDriftDisplay = now;
      }
    }
  }
}

void setup() {
  // Configure the native TWDT to 300 s before anything lengthy
  esp_task_wdt_deinit();
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = TimingConfig::WATCHDOG_TIMEOUT_MS; // Increased to 300s (5 minutes) for stability
  cfg.idle_core_mask = (1 << 0) | (1 << 1);
  cfg.trigger_panic = true;
  esp_task_wdt_init(&cfg);
  delay(SystemConfig::INITIAL_DELAY_MS);
  Serial.begin(SystemConfig::SERIAL_BAUD_RATE);
  // Définit un hostname unique et cohérent pour DHCP/DNS (ffp3-XXXX)
  {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(g_hostname, sizeof(g_hostname), "%s-%04X", SystemConfig::HOSTNAME_PREFIX, (uint16_t)(chipId & 0xFFFF));
    WiFi.setHostname(g_hostname);
  }
  EventLog::begin();
  EventLog::add("Boot start");
  
  // Monter LittleFS globalement (format si nécessaire)
  Serial.println("[FS] Mounting LittleFS...");
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed");
  } else {
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    Serial.printf("[FS] LittleFS ok: %u/%u bytes\n", (unsigned)used, (unsigned)total);
  }
  
  Serial.printf("[App] Démarrage FFP3CS v%s\n", Config::VERSION);
  EventLog::addf("App start v%s", Config::VERSION);
  
  // Valide la nouvelle image si on vient juste d'être mis à jour
  validatePendingOta();
  EventLog::add("OTA validation done");
  
  // Initialisation de la gestion du temps (avec restauration depuis la flash)
  power.initTime();
  EventLog::add("Time init");
  
  // Configuration NTP
  power.setNTPConfig(SystemConfig::NTP_GMT_OFFSET_SEC, SystemConfig::NTP_DAYLIGHT_OFFSET_SEC, SystemConfig::NTP_SERVER);
  
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
    const char* namespaces[] = {"bouffe","ota","remoteVars","rtc","diagnostics","alerts","timeDrift"};
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
    // Synchronisation NTP si WiFi disponible
    oled.showDiagnostic("NTP sync");
    power.syncTimeFromNTP();

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
                      "- Hostname: " + String(g_hostname) + "\n" +
                      "- Hôte: " + String(g_hostname) + ":" + String(SystemConfig::ARDUINO_OTA_PORT);
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
  
  // Envoi d'un mail de test au démarrage (borné et conditionné par variables distantes)
  #if FEATURE_MAIL
  if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
    Serial.println("[App] Envoi du mail de test au démarrage...");
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
      prefs.end();
      testMessage += "- diagnostics.rebootCnt: "; testMessage += String(rebootCnt); testMessage += "\n";
      testMessage += "- diagnostics.minHeap: "; testMessage += String(minHeap); testMessage += "\n";
    }
    
    // Borne de sécurité: 6000 bytes
    if (testMessage.length() > BufferConfig::EMAIL_MAX_SIZE_BYTES) {
      testMessage = testMessage.substring(0, BufferConfig::EMAIL_MAX_SIZE_BYTES - 3);
      testMessage += "...";
    }
    String subjBoot = String("FFP3CS - Test de démarrage [") + g_hostname + "]";
    bool mailSent = mailer.send(subjBoot.c_str(), testMessage.c_str(), "Test User", autoCtrl.getEmailAddress().c_str());
    if (mailSent) { EventLog::add("Boot test email sent"); }
    
    if (mailSent) {
      Serial.println("[App] Mail de test envoyé avec succès ✔");
    } else {
      Serial.println("[App] Échec de l'envoi du mail de test ✗");
    }
  } else {
    Serial.println("[App] WiFi non connecté - mail de test ignoré");
  }
  #else
  Serial.println("[App] Mail désactivé (FEATURE_MAIL=0) - mail de test ignoré");
  #endif
  
  // Chargement des flags de bouffe AVANT l'initialisation des automatismes
  config.loadBouffeFlags();
  autoCtrl.begin();

  // --- Première synchronisation distante -----------------------------
  if (WiFi.status() == WL_CONNECTED) {
    // 1) Récupération des variables distantes AVANT la vérification OTA
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;  // taille confortable
    autoCtrl.fetchRemoteState(tmp);

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
      body += "- Compilé le: " + String(__DATE__) + " " + String(__TIME__) + "\n";
      body += "- Redémarrage automatique effectué";
      // Borne de sécurité: 6 KB max
      if (body.length() > BufferConfig::EMAIL_MAX_SIZE_BYTES) { body = body.substring(0, BufferConfig::EMAIL_MAX_SIZE_BYTES - 3) + "..."; }
      String subjOtaSrv = String("OTA mise à jour - Serveur distant [") + g_hostname + "]";
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
      body += "- Compilé le: " + String(__DATE__) + " " + String(__TIME__) + "\n";
      body += "- Redémarrage automatique effectué";
      // Borne de sécurité: 6 KB max
      if (body.length() > BufferConfig::EMAIL_MAX_SIZE_BYTES) { body = body.substring(0, BufferConfig::EMAIL_MAX_SIZE_BYTES - 3) + "..."; }
      String subjOtaWeb = String("OTA mise à jour - Interface web [") + g_hostname + "]";
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
    ms.voltage   = rs.voltageMv;
    ms.pumpAqua  = acts.isAquaPumpRunning();
    ms.pumpTank  = acts.isTankPumpRunning();
    ms.heater    = acts.isHeaterOn();
    ms.light     = acts.isLightOn();
    autoCtrl.sendFullUpdate(rs, "resetMode=0");
  }

  // Affichage final propre - effacer l'écran et afficher juste "Init ok"
  if (oled.isPresent()) {
    oled.clear();
    oled.showDiagnostic("Init ok");
    delay(SystemConfig::FINAL_INIT_DELAY_MS); // 1 seconde propre pour lire le message final
  }
  
  // Création queue & tasks FreeRTOS
  sensorQueue = xQueueCreate(5, sizeof(SensorReadings));
  if (sensorQueue) {
    xTaskCreatePinnedToCore(sensorTask, "sensorTask", TaskConfig::SENSOR_TASK_STACK_SIZE, nullptr, TaskConfig::SENSOR_TASK_PRIORITY, &g_sensorTaskHandle, TaskConfig::TASK_CORE_ID); // core 1 - priorité haute pour l'acquisition
    xTaskCreatePinnedToCore(automationTask, "autoTask", TaskConfig::AUTOMATION_TASK_STACK_SIZE, nullptr, TaskConfig::AUTOMATION_TASK_PRIORITY, &g_autoTaskHandle, TaskConfig::TASK_CORE_ID); // priorité intermédiaire pour la logique
    // Tâche d'affichage OLED avec stack augmenté et fréquence réduite
    xTaskCreatePinnedToCore(displayTask, "displayTask", TaskConfig::DISPLAY_TASK_STACK_SIZE, nullptr, TaskConfig::DISPLAY_TASK_PRIORITY, &g_displayTaskHandle, TaskConfig::TASK_CORE_ID); // priorité basse pour l'affichage
    // Log unique des marges de stack au boot
    vTaskDelay(pdMS_TO_TICKS(50)); // petite latence pour laisser démarrer
    UBaseType_t hwmSensor  = g_sensorTaskHandle  ? uxTaskGetStackHighWaterMark(g_sensorTaskHandle)  : 0;
    UBaseType_t hwmAuto    = g_autoTaskHandle    ? uxTaskGetStackHighWaterMark(g_autoTaskHandle)    : 0;
    UBaseType_t hwmDisplay = g_displayTaskHandle ? uxTaskGetStackHighWaterMark(g_displayTaskHandle) : 0;
    UBaseType_t hwmLoop    = uxTaskGetStackHighWaterMark(NULL);
    Serial.printf("[Stacks] HWM at boot - sensor:%u auto:%u display:%u loop:%u bytes\n",
                  (unsigned)hwmSensor, (unsigned)hwmAuto, (unsigned)hwmDisplay, (unsigned)hwmLoop);
    EventLog::addf("Stacks HWM boot sensor=%u auto=%u display=%u loop=%u",
                   (unsigned)hwmSensor, (unsigned)hwmAuto, (unsigned)hwmDisplay, (unsigned)hwmLoop);
  }
  
  Serial.println(F("[App] Initialisation terminée"));
  EventLog::add("Init done");
}

void loop() {
  power.updateTime();
  
  // Mise à jour du moniteur de dérive temporelle
  timeDriftMonitor.update();
  
  #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
  ArduinoOTA.handle();
  #endif
  wifi.loop(&oled);
  
  // Vérification périodique des mises à jour OTA (toutes les heures)
  unsigned long currentTime = millis();
  #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  if (currentTime - g_lastOtaCheck >= OTA_CHECK_INTERVAL) {
    Serial.println("[OTA] Vérification périodique des mises à jour...");
    checkForOtaUpdate();
    g_lastOtaCheck = currentTime;
  }
  #endif
  
  power.resetWatchdog();

  // Periodic log digest by email (lightweight, bounded)
  if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
    if (currentTime - g_lastDigestMs >= DIGEST_INTERVAL_MS) {
      String body;
      body.reserve(BufferConfig::JSON_DOCUMENT_SIZE);
      body += "Résumé des événements récents (";
      body += Utils::getSystemInfo();
      body += ")\n\n";
      // Ajouter marges de stack des tâches si disponibles
      body += "[Stacks] Marges (bytes):\n";
      if (g_sensorTaskHandle) { body += "- sensorTask: "; body += String(uxTaskGetStackHighWaterMark(g_sensorTaskHandle)); body += "\n"; }
      if (g_autoTaskHandle)   { body += "- autoTask: ";   body += String(uxTaskGetStackHighWaterMark(g_autoTaskHandle));   body += "\n"; }
      if (g_displayTaskHandle){ body += "- displayTask: ";body += String(uxTaskGetStackHighWaterMark(g_displayTaskHandle));body += "\n"; }
      // Marge de la loop() actuelle
      body += "- loop(): "; body += String(uxTaskGetStackHighWaterMark(NULL)); body += "\n\n";
      
      // Ajouter informations de dérive temporelle
      body += "[TIME DRIFT] Dérive temporelle:\n";
      body += timeDriftMonitor.generateDriftReport();
      body += "\n";
      uint32_t newSeq = EventLog::dumpSince(g_lastDigestSeq, body, BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES);
      if (newSeq != g_lastDigestSeq) {
        String subjDigest = String("FFP3CS - Digest événements [") + g_hostname + "]";
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
  }
  vTaskDelay(pdMS_TO_TICKS(500)); // Délai augmenté pour réduire la charge CPU
} 