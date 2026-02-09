#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <cstring>

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
#include "gpio_parser.h"
#include "task_monitor.h"
#include "app_context.h"
#include "app_tasks.h"
#include "system_boot.h"
#include "tls_mutex.h"  // v11.149: Mutex pour sérialiser TLS (SMTP/HTTPS)

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
#include <ArduinoOTA.h>
#endif

// S3 TG0WDT: appelé au tout début de initArduino() (patch core via tools/patch_arduino_early_init_variant.py).
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
extern "C" void earlyInitVariant(void) {
  esp_task_wdt_deinit();
  // Diagnostic boot: premier point (avant nvs_flash_init etc.)
  Serial.begin(115200);
  Serial.println("[FFP5CS] earlyInitVariant");
}

// Diagnostic boot: appelé juste après nvs_flash_init() dans initArduino() (patch core via tools/patch_arduino_diag_after_nvs.py).
extern "C" void ffp5cs_diag_after_nvs(void) {
  Serial.println("[FFP5CS] after nvs_flash_init");
}
#endif

// S3 TG0WDT: secours dans initVariant() (fin de initArduino). Réinit 300s en setup().
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
void initVariant(void) {
  esp_task_wdt_deinit();
  esp_task_wdt_reset();
  yield();
  esp_task_wdt_reset();
  Serial.println("[FFP5CS] initVariant");
}
#endif

WifiManager wifi(Secrets::WIFI_LIST,
                Secrets::WIFI_COUNT,
                TimingConfig::WIFI_CONNECT_ATTEMPT_TIMEOUT_MS,
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
Automatism g_autoCtrl(sensors, acts, web, oled, power, mailer, config);
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
                        g_autoCtrl};

static char g_hostname[SystemConfig::HOSTNAME_BUFFER_SIZE];
static bool g_otaJustUpdated = false;
static char g_previousVersion[32] = "";
static unsigned long g_lastOtaCheck = 0;


void setup() {
  // S3: nourrir le WDT immédiatement (au cas où timeout court avant reconfig)
  #if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  esp_task_wdt_reset();
  #endif
  // Task WDT 300s (IDF 5+ uniquement; IDF 4.x garde la config par défaut)
  #if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  #if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  // S3: reconfig 300s (initVariant() + feed ci-dessus ont pu nourrir le WDT)
  delay(1);
  yield();
  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdtCfg = {};
  wdtCfg.timeout_ms = 300000;
  wdtCfg.idle_core_mask = 0;
  wdtCfg.trigger_panic = true;
  esp_task_wdt_init(&wdtCfg);
  #endif
  #endif
  // Log de la cause du redémarrage AVANT toute initialisation
  #if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  Serial.begin(SystemConfig::SERIAL_BAUD_RATE);
  delay(100);  // Petit délai pour stabiliser Serial
  #endif
  
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("\n\n=== BOOT FFP5CS v%s ===\n", ProjectConfig::VERSION);
  #if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM) && defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  Serial.println("[BOOT] Watchdog 300s (S3 early reconfig)");
  #endif
  Serial.printf("[BOOT] Reset reason: %d - ", resetReason);
  switch(resetReason) {
    case ESP_RST_POWERON: Serial.println("Power-on reset"); break;
    case ESP_RST_EXT: Serial.println("External reset (pin)"); break;
    case ESP_RST_SW: Serial.println("Software reset (ESP.restart())"); break;
    case ESP_RST_PANIC: Serial.println("⚠️ PANIC reset (crash)"); break;
    case ESP_RST_INT_WDT: Serial.println("⚠️ Interrupt watchdog timeout"); break;
    case ESP_RST_TASK_WDT: Serial.println("⚠️ Task watchdog timeout"); break;
    case ESP_RST_WDT: Serial.println("⚠️ Other watchdog timeout"); break;
    case ESP_RST_DEEPSLEEP: Serial.println("Deep sleep wake"); break;
    case ESP_RST_BROWNOUT: Serial.println("⚠️ Brownout (alimentation)"); break;
    case ESP_RST_SDIO: Serial.println("SDIO reset"); break;
    default: Serial.printf("Unknown reset (%d)\n", resetReason); break;
  }
  
  // v11.149: Initialisation du mutex TLS (avant toute opération réseau)
  TLSMutex::init();
  
  #if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  #if !defined(BOARD_S3) || !defined(BOARD_HAS_PSRAM)
  esp_task_wdt_deinit();
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = 300000;  // 300 secondes (5 minutes)
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  esp_task_wdt_init(&cfg);
  Serial.printf("[BOOT] Watchdog configuré: timeout=%d ms\n", (int)cfg.timeout_ms);
  #endif
  #endif
  delay(SystemConfig::INITIAL_DELAY_MS);
  
  #if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  Serial.println("[INIT] Moniteur série activé");
  #endif

  // Delay conditionnel pour debug uniquement
  #if defined(DEBUG_MODE) || !defined(PROFILE_PROD)
  delay(1000);  // 1 seconde pour stabiliser Serial Monitor en debug
  #endif

  SystemBoot::setupHostname(g_hostname, sizeof(g_hostname));

  SystemBoot::initializeStorage(g_appContext);
  
  LOG_INFO("Démarrage FFP5CS v%s", ProjectConfig::VERSION);
  Serial.printf("[Event] App start v%s\n", ProjectConfig::VERSION);
  
  SystemBoot::OtaState otaState{};
  otaState.justUpdated = g_otaJustUpdated;
  strncpy(otaState.previousVersion, g_previousVersion, sizeof(otaState.previousVersion) - 1);
  otaState.previousVersion[sizeof(otaState.previousVersion) - 1] = '\0';
  otaState.lastCheck = g_lastOtaCheck;
  SystemBoot::validatePendingOta(otaState);
  Serial.println("[Event] OTA validation done");
  
  SystemBoot::initializeTimekeeping(g_appContext);

  time_t bootTime = g_appContext.power.getCurrentEpochSafe();
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

  SystemBoot::initializeDisplay(g_appContext);

  bool wifiConnected = SystemBoot::connectWifi(g_appContext, g_hostname);
  if (wifiConnected) {
    SystemBoot::onWifiReady(g_appContext, g_hostname, otaState);
    if (otaState.lastCheck > 0) {
      g_lastOtaCheck = otaState.lastCheck;
    }
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
    
    // Buffers statiques pour éviter fragmentation mémoire
    static char targetEmail[EmailConfig::MAX_EMAIL_LENGTH];
    static char bootMsg[BufferConfig::EMAIL_MAX_SIZE_BYTES];
    static char emailSubject[128];
    
    // Récupérer l'adresse configurée ou utiliser fallback
    const char* emailFromConfig = g_appContext.automatism.getEmailAddress();
    if (!emailFromConfig || strlen(emailFromConfig) == 0) {
      LOG_WARN("Email configuré vide, utilisation adresse fallback");
      strncpy(targetEmail, "oliv.arn.lau@gmail.com", sizeof(targetEmail) - 1);
      targetEmail[sizeof(targetEmail) - 1] = '\0';
    } else {
      strncpy(targetEmail, emailFromConfig, sizeof(targetEmail) - 1);
      targetEmail[sizeof(targetEmail) - 1] = '\0';
    }
    
    LOG_INFO("Cible: %s", targetEmail);
    
    // Construire le message avec snprintf pour éviter allocations
    IPAddress ip = WiFi.localIP();
    char ssid[64];
    g_appContext.wifi.currentSSID(ssid, sizeof(ssid));
    int written = snprintf(bootMsg, sizeof(bootMsg),
                          "Système démarré avec succès (v%s).\n"
                          "IP: %d.%d.%d.%d\n"
                          "SSID: %s\n"
                          "Raison: Test forcé au boot",
                          ProjectConfig::VERSION,
                          ip[0], ip[1], ip[2], ip[3],
                          ssid);
    if (written < 0 || (size_t)written >= sizeof(bootMsg)) {
      bootMsg[sizeof(bootMsg) - 1] = '\0';
    }
    
    // Construire le sujet
    snprintf(emailSubject, sizeof(emailSubject), "Démarrage système v%s", ProjectConfig::VERSION);
    
    // Reset watchdog avant envoi bloquant
    g_appContext.power.resetWatchdog();
    // sendAlert(..., true) = alerte diagnostic (rapport temporel détaillé)
    bool queued = g_appContext.mailer.sendAlert(emailSubject, bootMsg, targetEmail, true);
    
    if (queued) {
      LOG_INFO("✅ Mail de démarrage ajouté à la queue (envoi différé)");
    } else {
      LOG_ERROR("❌ Échec ajout à la queue mail de démarrage");
    }
    g_appContext.power.resetWatchdog();
  } else {
    LOG_ERROR("❌ Impossible d'envoyer mail: WiFi non connecté au boot");
  }

  LOG_INFO("Initialisation terminée");
  Serial.println("[Event] Init done");
}

void loop() {
  power.updateTime();
  unsigned long now = millis();
  
  #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
  ArduinoOTA.handle();
  #endif

  // Centraliser les opérations WiFi dans le loop Arduino (évite concurrence multi-tâches)
  wifi.checkConnectionStability();
  wifi.loop(&oled);

  power.resetWatchdog();
  
  #if FEATURE_DIAG_STATS
  static unsigned long lastStatsReportMs = 0;
  if (now - lastStatsReportMs >= TimingConfig::STATS_REPORT_INTERVAL_MS) {
    LOG_INFO("Diag stats: heap=%u min=%u", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    lastStatsReportMs = now;
  }
  #endif
  
  
  vTaskDelay(pdMS_TO_TICKS(500));
} 
