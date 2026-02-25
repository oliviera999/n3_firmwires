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
#include "boot_log.h"   // BOOT_LOG : un seul #if centralisé (ets_printf vs Serial)
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/wdt_hal.h"
#include "hal/wdt_types.h"
#endif

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
#include <ArduinoOTA.h>
#endif

// S3 TG0WDT: appelé au tout début de initArduino() (patch core via tools/patch_arduino_early_init_variant.py).
// Avec CONFIG_ESP_TASK_WDT_INIT=n (sdkconfig_s3_wdt), le TWDT ne démarre pas — pas de deinit nécessaire.
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
extern "C" void earlyInitVariant(void) {
  // IWDT (MWDT1) : désactivation au plus tôt pour que PSRAM/NVS (>300 ms) ne déclenchent pas TG1WDT
  wdt_hal_context_t iwdt_hal = {};
  wdt_hal_init(&iwdt_hal, WDT_MWDT1, 40000, false);
  wdt_hal_write_protect_disable(&iwdt_hal);
  wdt_hal_disable(&iwdt_hal);
  wdt_hal_write_protect_enable(&iwdt_hal);
  BOOT_LOG("[FFP5CS] earlyInitVariant\n");
}

extern "C" void ffp5cs_diag_after_psram(void) {
  BOOT_LOG("[FFP5CS] after PSRAM block\n");
}

extern "C" void ffp5cs_diag_before_nvs(void) {
  BOOT_LOG("[FFP5CS] before nvs_flash_init\n");
}

extern "C" void ffp5cs_diag_after_nvs(void) {
  BOOT_LOG("[FFP5CS] after nvs_flash_init\n");
}
#endif

#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
void initVariant(void) {
  BOOT_LOG("[FFP5CS] initVariant\n");
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
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  // TWDT : désactiver au plus tôt si le framework l'a démarré (évite TG0WDT avant reconfig 300s)
  esp_task_wdt_deinit();
  // IWDT (MWDT1) : filet de sécurité si bootloader/lib l'a réactivé après earlyInitVariant()
  {
    wdt_hal_context_t iwdt_hal = {};
    wdt_hal_init(&iwdt_hal, WDT_MWDT1, 40000, false);
    wdt_hal_write_protect_disable(&iwdt_hal);
    wdt_hal_disable(&iwdt_hal);
    wdt_hal_write_protect_enable(&iwdt_hal);
  }
  BOOT_LOG("[FFP5CS] setup after IWDT\n");
  // TWDT 300s immédiatement, sans delay/yield (évite TG0WDT si framework a armé un timeout court)
  esp_task_wdt_deinit();
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  esp_task_wdt_config_t wdtCfg = {};
  wdtCfg.timeout_ms = 300000;
  wdtCfg.idle_core_mask = 0;
  wdtCfg.trigger_panic = true;
  esp_task_wdt_init(&wdtCfg);
#else
  esp_task_wdt_init(300, true);  // API IDF 4.x : timeout_sec, panic
#endif
  esp_task_wdt_add(NULL);  // souscrire la tâche setup/loop au TWDT 300s
  BOOT_LOG("[FFP5CS] TWDT 300s ok\n");
#endif
  // Task WDT 300s (IDF 5+ uniquement; IDF 4.x garde la config par défaut)
  #if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  #if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  // S3: déjà fait ci-dessus (pas de delay/yield avant reconfig)
  #else
  delay(1);
  yield();
  esp_task_wdt_deinit();  // No-op si TWDT jamais init, sinon permet reconfig
  esp_task_wdt_config_t wdtCfg = {};
  wdtCfg.timeout_ms = 300000;
  wdtCfg.idle_core_mask = 0;
  wdtCfg.trigger_panic = true;
  esp_task_wdt_init(&wdtCfg);
  #endif
  #endif
  // Log de la cause du redémarrage AVANT toute initialisation (BOOT_LOG = ets_printf sur S3 PSRAM, pas de blocage UART)
  #if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  #if !defined(BOARD_S3) || !defined(BOARD_HAS_PSRAM)
  Serial.begin(SystemConfig::SERIAL_BAUD_RATE);
  delay(100);  // Petit délai pour stabiliser Serial
  #endif
  #endif

  esp_reset_reason_t resetReason = esp_reset_reason();
  BOOT_LOG("\n\n=== BOOT FFP5CS v%s ===\n", ProjectConfig::VERSION);
  #if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM) && defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  BOOT_LOG("[BOOT] Watchdog 300s (S3 early reconfig)\n");
  #endif
  BOOT_LOG("[BOOT] Reset reason: %d - ", resetReason);
  switch(resetReason) {
    case ESP_RST_POWERON: BOOT_LOG("Power-on reset\n"); break;
    case ESP_RST_EXT: BOOT_LOG("External reset (pin)\n"); break;
    case ESP_RST_SW: BOOT_LOG("Software reset (ESP.restart())\n"); break;
    case ESP_RST_PANIC: BOOT_LOG("PANIC reset (crash)\n"); break;
    case ESP_RST_INT_WDT: BOOT_LOG("Interrupt watchdog timeout\n"); break;
    case ESP_RST_TASK_WDT: BOOT_LOG("Task watchdog timeout\n"); break;
    case ESP_RST_WDT: BOOT_LOG("Other watchdog timeout\n"); break;
    case ESP_RST_DEEPSLEEP: BOOT_LOG("Deep sleep wake\n"); break;
    case ESP_RST_BROWNOUT: BOOT_LOG("Brownout (alimentation)\n"); break;
    case ESP_RST_SDIO: BOOT_LOG("SDIO reset\n"); break;
    default: BOOT_LOG("Unknown reset (%d)\n", resetReason); break;
  }

  // S3 PSRAM : Serial.begin désactivé pour test (boot via BOOT_LOG/ets_printf uniquement)
  #if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  (void)0;  // aucun Serial.begin pour cet env (test)
  #else
  #if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  Serial.begin(SystemConfig::SERIAL_BAUD_RATE);
  delay(100);
  #endif
  #endif
  
  // WROOM (non-S3): reconfig TWDT au démarrage (évite reboot pendant POST 8s, OTA HTTPS).
  // USE_TEST_ENDPOINTS (wroom-beta): 60s pour marge OTA metadata HTTPS (handshake TLS peut bloquer ~20s).
  // IDF 5: esp_task_wdt_config_t + timeout_ms. IDF 4 (Arduino wroom-test): esp_task_wdt_init(timeout_sec).
  #if !defined(BOARD_S3) || !defined(BOARD_HAS_PSRAM)
    #if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  esp_task_wdt_deinit();
  esp_task_wdt_config_t cfg = {};
  #if defined(USE_TEST_ENDPOINTS)
  cfg.timeout_ms = 60000;   // 60s wroom-beta (OTA metadata HTTPS peut bloquer ~20s)
  #else
  cfg.timeout_ms = 30000;   // 30s prod/test (marge POST 8s + async_tcp)
  #endif
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  esp_task_wdt_init(&cfg);
  Serial.printf("[BOOT] Watchdog configuré: timeout=%lu ms (WROOM)\n", (unsigned long)cfg.timeout_ms);
    #else
  // API IDF 4.x: esp_task_wdt_init(timeout_sec, panic)
  esp_task_wdt_deinit();
  #if defined(USE_TEST_ENDPOINTS)
  esp_task_wdt_init(60, true);   // 60s wroom-beta (OTA TLS)
  Serial.println("[BOOT] Watchdog configuré: 60 s (WROOM-beta, API IDF4)");
  #else
  esp_task_wdt_init(30, true);   // 30s prod/test
  Serial.println("[BOOT] Watchdog configuré: 30 s (WROOM, API IDF4)");
  #endif
    #endif
  #endif
  
  // v11.149: Initialisation du mutex TLS (avant toute opération réseau)
  TLSMutex::init();
  
  delay(SystemConfig::INITIAL_DELAY_MS);
  
  #if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  BOOT_LOG("[INIT] Moniteur série activé\n");
  #endif

  // Delay conditionnel pour debug uniquement
  #if defined(DEBUG_MODE) || !defined(PROFILE_PROD)
  delay(1000);  // 1 seconde pour stabiliser Serial Monitor en debug
  #endif

  SystemBoot::setupHostname(g_hostname, sizeof(g_hostname));

  SystemBoot::initializeStorage(g_appContext);
  
  BOOT_LOG("[INFO] Démarrage FFP5CS v%s\n", ProjectConfig::VERSION);
  BOOT_LOG("[Event] App start v%s\n", ProjectConfig::VERSION);
  
  SystemBoot::OtaState otaState{};
  otaState.justUpdated = g_otaJustUpdated;
  strncpy(otaState.previousVersion, g_previousVersion, sizeof(otaState.previousVersion) - 1);
  otaState.previousVersion[sizeof(otaState.previousVersion) - 1] = '\0';
  otaState.lastCheck = g_lastOtaCheck;
  SystemBoot::validatePendingOta(otaState);
  BOOT_LOG("[Event] OTA validation done\n");
  
  SystemBoot::initializeTimekeeping(g_appContext);
  g_appContext.mailer.setPowerManager(&g_appContext.power);

  time_t bootTime = g_appContext.power.getCurrentEpochSafe();
  struct tm bootTimeInfo;
  if (localtime_r(&bootTime, &bootTimeInfo)) {
    BOOT_LOG("[TIME][INFO] Démarrage système - Heure: %02d:%02d:%02d, Date: %02d/%02d/%04d\n",
             bootTimeInfo.tm_hour,
             bootTimeInfo.tm_min,
             bootTimeInfo.tm_sec,
             bootTimeInfo.tm_mday,
             bootTimeInfo.tm_mon + 1,
             bootTimeInfo.tm_year + 1900);
    BOOT_LOG("[TIME][INFO] Epoch au démarrage: %lu\n", (unsigned long)bootTime);
  } else {
    BOOT_LOG("[TIME][WARN] Impossible de récupérer l'heure au démarrage\n");
  }

  SystemBoot::initializeDisplay(g_appContext);
  g_appContext.otaManager.setDisplay(&g_appContext.display);

  bool wifiConnected = SystemBoot::connectWifi(g_appContext, g_hostname);
  if (wifiConnected) {
    SystemBoot::onWifiReady(g_appContext, g_hostname, otaState);
    if (otaState.lastCheck > 0) {
      g_lastOtaCheck = otaState.lastCheck;
    }
#if FEATURE_MAIL
    // Réserve 32 KB pour SMTP tant que le heap est peu fragmenté (libérée au moment de l'envoi)
    AppTasks::reserveMailBlockAtBoot();
#endif
  }

  SystemBoot::initializePeripherals(g_appContext);
  SystemBoot::loadConfiguration(g_appContext);

  if (wifiConnected) {
    SystemBoot::postConfiguration(g_appContext, g_hostname, otaState);
  }

  SystemBoot::finalizeDisplay(g_appContext);

  if (!AppTasks::start(g_appContext)) {
    BOOT_LOG("[WARN] Système dégradé: pas de tâches FreeRTOS\n");
  }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  vTaskDelay(pdMS_TO_TICKS(20));
  BOOT_LOG("[BOOT] setup done, loop starts\n");
  BOOT_LOG("[BOOT] init done\n");
  wifi.startDelayedModeInitTask();
#else
  // Notification de démarrage par mail (Diagnostic "fix mail")
  if (g_appContext.wifi.isConnected()) {
    LOG_INFO("=== TEST MAIL AU DÉMARRAGE ===");
    
    // Buffers statiques pour éviter fragmentation mémoire
    static char targetEmail[EmailConfig::MAX_EMAIL_LENGTH];
    static char bootMsg[BufferConfig::EMAIL_MAX_SIZE_BYTES];
    static char emailSubject[128];
    
    // Récupérer l'adresse configurée ou utiliser la constante par défaut (boot avant 1er sync → NVS vide)
    const char* emailFromConfig = g_appContext.automatism.getEmailAddress();
    if (!emailFromConfig || strlen(emailFromConfig) == 0) {
      LOG_INFO("Email non configuré (boot avant sync), utilisation adresse par défaut");
      strncpy(targetEmail, EmailConfig::DEFAULT_RECIPIENT, sizeof(targetEmail) - 1);
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
  #endif  // !BOARD_S3 || !BOARD_HAS_PSRAM
}

void loop() {
  power.updateTime();
  unsigned long now = millis();
  
  #if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
  ArduinoOTA.handle();
  #endif

  // S3 PSRAM : init WiFi.mode() après 3 s (évite blocage au boot) ; sans effet sur les autres envs
  wifi.tryDelayedModeInit();

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
