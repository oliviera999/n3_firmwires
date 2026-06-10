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
#include "i2c_bus.h"    // Bus I2C partagé (OLED, BME280, scan)
#include "boot_log.h"   // BOOT_LOG : un seul #if centralisé (ets_printf vs Serial)
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/wdt_hal.h"
#include "hal/wdt_types.h"
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

// Accès sync pour footer mail (évite dépendance circulaire mailer -> automatism)
unsigned long ffp5csGetLastSendMsForMail() {
  return g_autoCtrl.getLastSendMs();
}
int ffp5csGetLastDataSkipReasonForMail() {
  return g_autoCtrl.getLastDataSkipReason();
}

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
  {
    esp_task_wdt_config_t wdtCfg = {};
    wdtCfg.timeout_ms = 300000;
    wdtCfg.idle_core_mask = 0;
    wdtCfg.trigger_panic = true;
    esp_err_t err = esp_task_wdt_init(&wdtCfg);
    if (err == ESP_ERR_INVALID_STATE) {
      esp_task_wdt_deinit();
      esp_task_wdt_init(&wdtCfg);
    }
  }
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
  
  // v13.53 (audit): init TWDT extrait dans SystemBoot::initWatchdog() — factorise les
  // 3 blocs WROOM/IDF4/IDF5/wroom-beta qui étaient en ligne ici. La cible S3+PSRAM
  // conserve son init précoce plus haut (earlyInitVariant + IWDT MWDT1).
  SystemBoot::initWatchdog();
  
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
#if defined(BOARD_S3)
  SystemBoot::initializeSdCard();
#endif
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
  g_appContext.wifi.setPowerManager(&g_appContext.power);

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

  I2CBus::init();
  if (!SystemBoot::runI2cScanAndInitDisplay(g_appContext)) {
    SystemBoot::initializeDisplay(g_appContext);
  }
  g_appContext.otaManager.setDisplay(&g_appContext.display);

  g_appContext.power.applyExternalRTCIfPresent();

#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  // S3 PSRAM : Serial activé après NVS pour que le chemin WiFi (wifi_manager) puisse logger sans bloquer
  Serial.begin(SystemConfig::SERIAL_BAUD_RATE);
  delay(100);
#endif

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

  if (!AppTasks::start(g_appContext)) {
    BOOT_LOG("[WARN] Système dégradé: pas de tâches FreeRTOS\n");
  }

  if (wifiConnected) {
    SystemBoot::postConfiguration(g_appContext, g_hostname, otaState);
  }

  SystemBoot::finalizeDisplay(g_appContext);
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  vTaskDelay(pdMS_TO_TICKS(20));
  BOOT_LOG("[BOOT] setup done, loop starts\n");
  BOOT_LOG("[BOOT] init done\n");
  wifi.startDelayedModeInitTask();
#else
  // Notification de démarrage par mail (extrait dans SystemBoot — v13.93, allège le god-file)
  SystemBoot::sendStartupTestMail(g_appContext);
  #endif  // !BOARD_S3 || !BOARD_HAS_PSRAM
}

void loop() {
  power.updateTime();
  unsigned long now = millis();
  
  // S3 PSRAM : init WiFi.mode() après 3 s (évite blocage au boot) ; sans effet sur les autres envs
  wifi.tryDelayedModeInit();

  // Centraliser les opérations WiFi dans le loop Arduino (évite concurrence multi-tâches)
  wifi.checkConnectionStability();
  bool wasWifiConnected = (WiFi.status() == WL_CONNECTED);
  wifi.loop(&oled);
  // Sauvegarder identifiants dès reconnexion réussie (pour réveil light sleep)
  if (!wasWifiConnected && WiFi.status() == WL_CONNECTED) {
    power.saveCurrentWifiCredentials();
  }

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
