#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <time.h>

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
#include "nvs_manager.h"
#include "event_log.h"
#include "time_drift_monitor.h"
#include "email_buffer_pool.h"
#include "timer_manager.h"
#include "optimized_logger.h"
#include "gpio_parser.h"
#include "task_monitor.h"
#include "app_context.h"
#include "app_tasks.h"
#include "bootstrap_storage.h"
#include "bootstrap_services.h"
#include "bootstrap_network.h"

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
static Preferences g_prefsDigest;

void setup() {
  esp_task_wdt_deinit();
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = 60000;
  cfg.idle_core_mask = (1 << 0) | (1 << 1);
  cfg.trigger_panic = true;
  esp_task_wdt_init(&cfg);
  delay(SystemConfig::INITIAL_DELAY_MS);
  
  #if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  Serial.begin(SystemConfig::SERIAL_BAUD_RATE);
  Serial.println("[INIT] Moniteur série activé");
  #endif

  BootstrapNetwork::setupHostname(g_hostname, sizeof(g_hostname));

  BootstrapStorage::initialize(g_appContext, g_lastDigestMs, g_lastDigestSeq);
  
  Serial.printf("[App] Démarrage FFP3CS v%s\n", Config::VERSION);
  
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
  EventLog::addf("App start v%s", Config::VERSION);
  
  BootstrapNetwork::OtaState otaState{g_otaJustUpdated, g_previousVersion, g_lastOtaCheck};
  BootstrapNetwork::validatePendingOta(otaState);
  EventLog::add("OTA validation done");
  
  BootstrapServices::initializeTimekeeping(g_appContext);

  BootstrapServices::initializeDisplay(g_appContext);

  bool wifiConnected = BootstrapNetwork::connect(g_appContext, g_hostname);
  if (wifiConnected) {
    BootstrapNetwork::onWifiReady(g_appContext, g_hostname, otaState);
  }

  BootstrapServices::initializePeripherals(g_appContext);
  BootstrapServices::loadConfiguration(g_appContext);

  if (wifiConnected) {
    BootstrapNetwork::postConfiguration(g_appContext, g_hostname, otaState);
  }

  BootstrapServices::finalizeDisplay(g_appContext);

  if (!AppTasks::start(g_appContext)) {
    Serial.println(F("[App] ⚠️ Système dégradé: pas de tâches FreeRTOS"));
  }
  
  Serial.println(F("[App] Initialisation terminée"));
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
      Serial.println("[OTA] Vérification périodique des mises à jour...");
      BootstrapNetwork::checkForOtaUpdate(g_appContext);
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
        String body;
        body.reserve(BufferConfig::JSON_DOCUMENT_SIZE);
        body += "Résumé des événements récents (";
        char systemInfo[128];
        Utils::getSystemInfo(systemInfo, sizeof(systemInfo));
        body += systemInfo;
        body += ")\n\n";
        body += "[Stacks] Marges (bytes):\n";

        AppTasks::Handles handles = AppTasks::getHandles();
        if (handles.sensor) {
          body += "- sensorTask: ";
          body += uxTaskGetStackHighWaterMark(handles.sensor);
          body += "\n";
        }
        if (handles.web) {
          body += "- webTask: ";
          body += uxTaskGetStackHighWaterMark(handles.web);
          body += "\n";
        }
        if (handles.automation) {
          body += "- autoTask: ";
          body += uxTaskGetStackHighWaterMark(handles.automation);
          body += "\n";
        }
        if (handles.display) {
          body += "- displayTask: ";
          body += uxTaskGetStackHighWaterMark(handles.display);
          body += "\n";
        }
        body += "- loop(): ";
        body += uxTaskGetStackHighWaterMark(nullptr);
        body += "\n\n";

        body += "[TIME DRIFT] Dérive temporelle:\n";
        body += timeDriftMonitor.generateDriftReport();
        body += "\n";

        uint32_t newSeq = EventLog::dumpSince(g_lastDigestSeq,
                                              body,
                                              BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES);
        if (newSeq != g_lastDigestSeq) {
          String subjDigest = "FFP3CS - Digest événements [";
          subjDigest += g_hostname;
          subjDigest += "]";
          if (body.length() > BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES) {
            body = body.substring(0, BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES - 3);
            body += "...";
          }
          bool ok = mailer.send(subjDigest.c_str(),
                                body.c_str(),
                                "User",
                                autoCtrl.getEmailAddress());
          EventLog::add(ok ? "Digest email sent" : "Digest email failed");
          g_lastDigestSeq = newSeq;
        } else {
          EventLog::add("Digest: no new events");
        }
        g_lastDigestMs = currentTime;
        g_prefsDigest.begin("digest", false);
        g_prefsDigest.putUInt("lastSeq", g_lastDigestSeq);
        g_prefsDigest.putULong("lastMs", g_lastDigestMs);
        g_prefsDigest.end();
      }
    });
    digestTimerAdded = true;
  }
  
  static bool statsTimerAdded = false;
  if (!statsTimerAdded) {
    TimerManager::addTimer("STATS_REPORT", 300000, []() {
      TimerManager::logStats();
      OptimizedLogger::logStats();
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
    Serial.println(F("[App] 🧹 Démarrage nettoyage périodique NVS"));
    g_nvsManager.rotateLogs(NVS_NAMESPACES::LOGS, 50);
    g_nvsManager.cleanupOldData(NVS_NAMESPACES::STATE, 604800000UL);
    g_nvsManager.schedulePeriodicCleanup();
    cleanupScheduled = true;
    Serial.println(F("[App] ✅ Nettoyage périodique NVS terminé"));
  }
  
  vTaskDelay(pdMS_TO_TICKS(500));
} 

 
