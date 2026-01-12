#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "wifi_manager.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "display_view.h"
#include "mailer.h"
#include "power.h"
#include "diagnostics.h"
#include "pins.h"
#include "config.h"
#include "secrets.h"
#include "automatism.h"
#include "web_client.h"
#include "web_server.h"
#include "nvs_manager.h"
#include "event_log.h"
#include "time_drift_monitor.h"
#include "timer_manager.h"
#include "gpio_parser.h"
#include "task_monitor.h"
#include "app_context.h"
#include "app_tasks.h"
#include "system_boot.h"

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
#include <ArduinoOTA.h>
#endif

WifiManager wifi(Secrets::WIFI_LIST,
                Secrets::WIFI_COUNT,
                TimingConfig::WIFI_CONNECT_TIMEOUT_MS,
                TimingConfig::WIFI_RETRY_INTERVAL_MS);
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

AppContext g_appContext{wifi,
                        oled,
                        power,
                        mailer,
                        web,
                        config,
                        otaManager,
                        sensors,
                        acts,
                        diag,
                        webSrv,
                        autoCtrl,
                        timeDriftMonitor};

static char g_hostname[SystemConfig::HOSTNAME_BUFFER_SIZE];
static bool g_otaJustUpdated = false;
static String g_previousVersion = "";
static unsigned long g_lastOtaCheck = 0;

static const unsigned long OTA_CHECK_INTERVAL = TimingConfig::OTA_CHECK_INTERVAL_MS;
static const unsigned long DIGEST_INTERVAL_MS = TimingConfig::DIGEST_INTERVAL_MS;

static unsigned long g_lastDigestMs = 0;
static uint32_t g_lastDigestSeq = 0;

void setup() {
  esp_task_wdt_deinit();
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = 60000;
  cfg.idle_core_mask = 0;  // Idle tasks non surveillées pour éviter les faux positifs
  cfg.trigger_panic = true;
  esp_task_wdt_init(&cfg);
  delay(SystemConfig::INITIAL_DELAY_MS);
  
  #if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  Serial.begin(SystemConfig::SERIAL_BAUD_RATE);
  Serial.println("[INIT] Moniteur série activé");
  #endif

  // Delay conditionnel pour debug uniquement
  #if defined(DEBUG_MODE) || !defined(PROFILE_PROD)
  delay(1000);  // 1 seconde pour stabiliser Serial Monitor en debug
  #endif
  
  Serial.printf("\n\n=== BOOT FFP5CS v%s ===\n", ProjectConfig::VERSION);

  SystemBoot::setupHostname(g_hostname, sizeof(g_hostname));

  SystemBoot::initializeStorage(g_appContext, g_lastDigestMs, g_lastDigestSeq);
  
  LOG_INFO("Démarrage FFP5CS v%s", ProjectConfig::VERSION);
  
  time_t bootTime = time(nullptr);
  struct tm bootTimeInfo;
  if (localtime_r(&bootTime, &bootTimeInfo)) {
    LOG_TIME(LOG_INFO,
             "Démarrage système - Heure: %02d:%02d:%02d, Date: %02d/%02d/%04d",
             bootTimeInfo.tm_hour,
             bootTimeInfo.tm_min,
             bootTimeInfo.tm_sec,
             bootTimeInfo.tm_mday,
             bootTimeInfo.tm_mon + 1,
             bootTimeInfo.tm_year + 1900);
    LOG_TIME(LOG_INFO, "Epoch au démarrage: %lu", bootTime);
  } else {
    LOG_TIME(LOG_WARN, "Impossible de récupérer l'heure au démarrage");
  }
  EventLog::addf("App start v%s", ProjectConfig::VERSION);
  
  SystemBoot::OtaState otaState{g_otaJustUpdated, g_previousVersion, g_lastOtaCheck};
  SystemBoot::validatePendingOta(otaState);
  EventLog::add("OTA validation done");
  
  SystemBoot::initializeTimekeeping(g_appContext);

  SystemBoot::initializeDisplay(g_appContext);

  bool wifiConnected = SystemBoot::connectWifi(g_appContext, g_hostname);
  if (wifiConnected) {
    SystemBoot::onWifiReady(g_appContext, g_hostname, otaState);
  }

  SystemBoot::initializePeripherals(g_appContext);
  SystemBoot::loadConfiguration(g_appContext);

  if (wifiConnected) {
    SystemBoot::postConfiguration(g_appContext, g_hostname, otaState);
  }

  SystemBoot::finalizeDisplay(g_appContext);

  if (!AppTasks::start(g_appContext)) {
    LOG_WARN("Système dégradé: pas de tâches FreeRTOS");
  }
  
  // Notification de démarrage par mail (Diagnostic "fix mail")
  if (g_appContext.wifi.isConnected()) {
    LOG_INFO("=== TEST MAIL AU DÉMARRAGE ===");
    
    // Récupérer l'adresse configurée ou utiliser fallback
    String targetEmail = g_appContext.automatism.getEmailAddress();
    if (targetEmail.length() == 0) {
      LOG_WARN("Email configuré vide, utilisation adresse fallback");
      targetEmail = "oliv.arn.lau@gmail.com"; // HARDCODED pour test
    }
    
    LOG_INFO("Cible: %s", targetEmail.c_str());
    
    String bootMsg = "Système démarré avec succès (v" + String(ProjectConfig::VERSION) + ").\n";
    bootMsg += "IP: " + WiFi.localIP().toString() + "\n";
    bootMsg += "SSID: " + g_appContext.wifi.currentSSID() + "\n";
    bootMsg += "Raison: Test forcé au boot";
    
    // Reset watchdog avant envoi bloquant
    g_appContext.power.resetWatchdog();
    String emailSubject = "Démarrage système v" + String(ProjectConfig::VERSION);
    // Utiliser sendAlert() pour inclure automatiquement le rapport détaillé (incluant generateRestartReport())
    bool sent = g_appContext.mailer.sendAlert(emailSubject.c_str(), bootMsg, targetEmail.c_str());
    
    if (sent) {
      LOG_INFO("✅ Mail de démarrage ENVOYÉ avec succès");
    } else {
      LOG_ERROR("❌ ÉCHEC envoi mail de démarrage");
    }
    g_appContext.power.resetWatchdog();
  } else {
    LOG_ERROR("❌ Impossible d'envoyer mail: WiFi non connecté au boot");
  }

  LOG_INFO("Initialisation terminée");
  EventLog::add("Init done");
}

void loop() {
  power.updateTime();
  TimerManager::process();
  timeDriftMonitor.update();
  
  #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
  ArduinoOTA.handle();
  #endif

  wifi.loop(&oled);
  
  #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  static bool otaTimerAdded = false;
  if (!otaTimerAdded) {
    TimerManager::addTimer("OTA_CHECK", OTA_CHECK_INTERVAL, []() {
      LOG_INFO("Vérification périodique des mises à jour OTA...");
      SystemBoot::checkForOtaUpdate(g_appContext);
    });
    otaTimerAdded = true;
  }
  #endif
  
  power.resetWatchdog();

  static bool digestTimerAdded = false;
  if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled() && !digestTimerAdded) {
    TimerManager::addTimer("EMAIL_DIGEST", DIGEST_INTERVAL_MS, []() {
      if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
        unsigned long currentTime = millis();
        
        // Optimisation: Utilisation directe de String pour éviter allocation stack massive (Fix audit)
        String body;
        if (!body.reserve(BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES)) {
             LOG_WARN("Echec reservation memoire digest");
             return;
        }

        // Construire l'en-tête de l'e-mail
        char systemInfo[128];
        Utils::getSystemInfo(systemInfo, sizeof(systemInfo));
        
        // Petit buffer temporaire pour les formatages (safe stack size)
        char lineBuf[256]; 
        
        snprintf(lineBuf, sizeof(lineBuf), "Résumé des événements récents (%s)\n\n", systemInfo);
        body += lineBuf;
        
        body += "[Stacks] Marges (bytes):\n";

        // Récupérer les informations de stack
        AppTasks::Handles handles = AppTasks::getHandles();
        if (handles.sensor) {
          snprintf(lineBuf, sizeof(lineBuf), "- sensorTask: %u\n", uxTaskGetStackHighWaterMark(handles.sensor));
          body += lineBuf;
        }
        if (handles.web) {
          snprintf(lineBuf, sizeof(lineBuf), "- webTask: %u\n", uxTaskGetStackHighWaterMark(handles.web));
          body += lineBuf;
        }
        if (handles.automation) {
          snprintf(lineBuf, sizeof(lineBuf), "- autoTask: %u\n", uxTaskGetStackHighWaterMark(handles.automation));
          body += lineBuf;
        }
        if (handles.display) {
          snprintf(lineBuf, sizeof(lineBuf), "- displayTask: %u\n", uxTaskGetStackHighWaterMark(handles.display));
          body += lineBuf;
        }
        snprintf(lineBuf, sizeof(lineBuf), "- loop(): %u\n\n", uxTaskGetStackHighWaterMark(nullptr));
        body += lineBuf;
        
        body += "[TIME DRIFT] Dérive temporelle:\n";
        body += timeDriftMonitor.generateDriftReport();
        body += "\n";

        uint32_t newSeq = EventLog::dumpSince(g_lastDigestSeq,
                                              body,
                                              BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES - body.length());
        if (newSeq != g_lastDigestSeq) {
          char subjDigest[128];
          snprintf(subjDigest, sizeof(subjDigest), "FFP5CS - Digest événements [%s]", g_hostname);

          // Pas besoin de vérifier la longueur/substring, car `dumpSince` a déjà respecté la limite
          bool ok = mailer.send(subjDigest,
                                body.c_str(),
                                "User",
                                autoCtrl.getEmailAddress());
          EventLog::add(ok ? "Digest email sent" : "Digest email failed");
          g_lastDigestSeq = newSeq;
        } else {
          EventLog::add("Digest: no new events");
        }
        g_lastDigestMs = currentTime;
        g_nvsManager.saveInt(NVS_NAMESPACES::SENSORS, "digest_lastSeq", g_lastDigestSeq);
        g_nvsManager.saveULong(NVS_NAMESPACES::SENSORS, "digest_lastMs", g_lastDigestMs);
      }
    });
    digestTimerAdded = true;
  }
  
  static bool statsTimerAdded = false;
  if (!statsTimerAdded) {
    TimerManager::addTimer("STATS_REPORT", 300000, []() {
      TimerManager::logStats();
    });
    statsTimerAdded = true;
  }
  
  static unsigned long lastNvsCheck = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastNvsCheck >= 1000) {
    g_nvsManager.checkDeferredFlush();
    lastNvsCheck = currentTime;
  }
  
  static bool cleanupScheduled = false;
  if (!cleanupScheduled && g_nvsManager.shouldPerformCleanup()) {
    LOG_INFO("Démarrage nettoyage périodique NVS");
    g_nvsManager.rotateLogs(NVS_NAMESPACES::LOGS, 50);
    g_nvsManager.cleanupOldData(NVS_NAMESPACES::STATE, 604800000UL);
    g_nvsManager.schedulePeriodicCleanup();
    cleanupScheduled = true;
    LOG_INFO("Nettoyage périodique NVS terminé");
  }
  
  vTaskDelay(pdMS_TO_TICKS(500));
} 
