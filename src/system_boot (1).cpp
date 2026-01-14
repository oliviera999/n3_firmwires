#include "system_boot.h"

#include <WiFi.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <cstring>

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
#include <ArduinoOTA.h>
#endif

#include "event_log.h"
#include "ota_config.h"
#include "config.h"
#include "nvs_manager.h"
#include "timer_manager.h"

namespace SystemBoot {

// ============================================================================
// HOSTNAME & OTA
// ============================================================================

void setupHostname(char* buffer, size_t bufferSize) {
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(buffer,
           bufferSize,
           "%s-%04X",
           SystemConfig::HOSTNAME_PREFIX,
           static_cast<uint16_t>(chipId & 0xFFFF));
  WiFi.setHostname(buffer);
}

void validatePendingOta(OtaState& state) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();

  Serial.println("\n=== VALIDATION OTA AU DÉMARRAGE ===");
  if (running) Serial.printf("[OTA] Partition en cours: %s (0x%08x)\n", running->label, running->address);
  if (boot) Serial.printf("[OTA] Partition de boot: %s (0x%08x)\n", boot->label, boot->address);

  if (running && boot && running->address != boot->address) {
    Serial.println("[OTA] ⚠️ Partition courante différente de la partition de boot!");
  }

  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    switch (otaState) {
      case ESP_OTA_IMG_NEW:
        Serial.println("[OTA] État: NOUVELLE IMAGE (jamais démarrée)");
        break;
      case ESP_OTA_IMG_PENDING_VERIFY: {
        Serial.println("[OTA] État: IMAGE EN ATTENTE DE VALIDATION");
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("[OTA] ✅ Image validée et rollback annulé");
        state.justUpdated = true;
        String tempVersion;
        g_nvsManager.loadString(NVS_NAMESPACES::SYSTEM, "ota_prevVer", tempVersion, "");
        strncpy(state.previousVersion, tempVersion.c_str(), sizeof(state.previousVersion) - 1);
        state.previousVersion[sizeof(state.previousVersion) - 1] = '\0';
        break;
      }
      case ESP_OTA_IMG_VALID: Serial.println("[OTA] État: IMAGE VALIDÉE"); break;
      case ESP_OTA_IMG_INVALID: Serial.println("[OTA] État: IMAGE INVALIDE"); break;
      case ESP_OTA_IMG_ABORTED: Serial.println("[OTA] État: IMAGE ABANDONNÉE"); break;
      case ESP_OTA_IMG_UNDEFINED: Serial.println("[OTA] État: IMAGE NON DÉFINIE"); break;
      default: Serial.printf("[OTA] État inconnu: %d\n", otaState); break;
    }
  } else {
    Serial.println("[OTA] Impossible de récupérer l'état de la partition");
  }
  Serial.println("==================================\n");
}

void checkForOtaUpdate(AppContext& ctx) {
  Serial.println("\n=== OTA RÉACTIVÉ - NOUVEAU GESTIONNAIRE ROBUSTE ===");
  ctx.otaManager.setCurrentVersion(ProjectConfig::VERSION);
  ctx.otaManager.setCheckInterval(TimingConfig::OTA_CHECK_INTERVAL_MS);

  ctx.otaManager.setStatusCallback([&ctx](const String& message) {
    Serial.printf("[OTA] %s\n", message.c_str());
    if (ctx.display.isPresent()) {
      if (message.indexOf("Téléchargement") >= 0 || message.indexOf("Début") >= 0) {
        ctx.display.showOtaProgressOverlay(0);
      }
    }
  });

  ctx.otaManager.setErrorCallback([&ctx](const String& error) {
    Serial.printf("[OTA] ❌ %s\n", error.c_str());
    if (ctx.display.isPresent()) {
      ctx.display.hideOtaProgressOverlay();
    }
  });

  ctx.otaManager.setProgressCallback([&ctx](int progress) {
    Serial.printf("[OTA] 📊 Progression: %d%%\n", progress);
    if (ctx.display.isPresent()) {
      ctx.display.showOtaProgressOverlay(static_cast<uint8_t>(progress));
    }
  });

  if (ctx.otaManager.checkForUpdate()) {
    Serial.println("[OTA] 🆕 Mise à jour disponible !");
    if (ctx.display.isPresent()) {
      char otaMessage[32];
      snprintf(otaMessage, sizeof(otaMessage), "OTA dispo: %s", ctx.otaManager.getRemoteVersion().c_str());
      ctx.display.showDiagnostic(otaMessage);
    }
    Serial.println("[OTA] 🚀 Déclenchement automatique de la mise à jour...");
    if (ctx.otaManager.performUpdate()) {
      Serial.println("[OTA] ✅ Mise à jour lancée avec succès");
    } else {
      Serial.println("[OTA] ❌ Échec du lancement de la mise à jour");
    }
  } else {
    Serial.println("[OTA] ✅ Aucune mise à jour disponible");
  }
}

// ============================================================================
// STORAGE
// ============================================================================

void initializeStorage(AppContext& ctx, unsigned long& lastDigestMs, uint32_t& lastDigestSeq) {
  EventLog::begin();
  EventLog::add("Boot start");

  Serial.println("[FS] Mounting LittleFS...");
  uint32_t fsStartTime = millis();
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] ❌ LittleFS mount failed - tentative de format");
    if (LittleFS.format()) {
      Serial.println("[FS] ✅ Format réussi, tentative de remontage");
      if (LittleFS.begin(false)) Serial.println("[FS] ✅ Remontage après format réussi");
      else Serial.println("[FS] ❌ CRITIQUE: Impossible de monter LittleFS même après format");
    } else {
      Serial.println("[FS] ❌ CRITIQUE: Format LittleFS échoué");
    }
  } else {
    Serial.printf("[FS] ✅ LittleFS ok: %u/%u bytes (montage: %u ms)\n",
                  static_cast<unsigned>(LittleFS.usedBytes()),
                  static_cast<unsigned>(LittleFS.totalBytes()),
                  millis() - fsStartTime);
  }

  Serial.println(F("[App] 🚀 Initialisation du gestionnaire NVS centralisé"));
  if (!g_nvsManager.begin()) {
    Serial.println(F("[App] ❌ Erreur initialisation NVS Manager"));
    return;
  }

  g_nvsManager.enableDeferredFlush(true);
  g_nvsManager.setFlushInterval(3000);
  g_nvsManager.schedulePeriodicCleanup();
  g_nvsManager.migrateFromOldSystem();

  Serial.println(F("[App] 📥 Chargement état digest depuis NVS centralisé"));
  g_nvsManager.loadULong(NVS_NAMESPACES::SENSORS, "digest_last_seq", lastDigestSeq, 0);
  g_nvsManager.loadULong(NVS_NAMESPACES::SENSORS, "digest_last_ms", lastDigestMs, 0);
}

// ============================================================================
// SERVICES
// ============================================================================

void initializeServices(AppContext& ctx) {
  // Time
  ctx.power.initTime();
  EventLog::add("Time init");
  ctx.power.setNTPConfig(SystemConfig::NTP_GMT_OFFSET_SEC,
                         SystemConfig::NTP_DAYLIGHT_OFFSET_SEC,
                         SystemConfig::NTP_SERVER);
  ctx.timeDriftMonitor.attachPowerManager(&ctx.power);
  ctx.timeDriftMonitor.begin();
  
  // Display (Preliminary)
  if (ctx.display.isPresent()) ctx.display.hideOtaProgressOverlay();
  ctx.display.begin();
  
  // Peripherals
  if (ctx.display.isPresent()) ctx.display.showDiagnostic("Sensors");
  ctx.sensors.begin();

  if (ctx.display.isPresent()) ctx.display.showDiagnostic("Actuators");
  ctx.actuators.begin();

  if (ctx.display.isPresent()) ctx.display.showDiagnostic("WebSrv");
  ctx.webServer.begin();

  if (ctx.display.isPresent()) ctx.display.showDiagnostic("Diag");
  ctx.diagnostics.begin();

  if (ctx.display.isPresent()) ctx.display.showDiagnostic("Systems");
  ctx.power.initWatchdog();
  ctx.power.initModemSleep();

  TimerManager::init();
}

void initializeTimekeeping(AppContext& ctx) {
  ctx.power.initTime();
  EventLog::add("Time init");
  ctx.power.setNTPConfig(SystemConfig::NTP_GMT_OFFSET_SEC,
                         SystemConfig::NTP_DAYLIGHT_OFFSET_SEC,
                         SystemConfig::NTP_SERVER);
  ctx.timeDriftMonitor.attachPowerManager(&ctx.power);
  ctx.timeDriftMonitor.begin();
}

bool initializeDisplay(AppContext& ctx) {
  if (ctx.display.isPresent()) ctx.display.hideOtaProgressOverlay();
  return ctx.display.begin();
}

void initializePeripherals(AppContext& ctx) {
  if (ctx.display.isPresent()) ctx.display.showDiagnostic("Sensors");
  ctx.sensors.begin();

  if (ctx.display.isPresent()) ctx.display.showDiagnostic("Actuators");
  ctx.actuators.begin();

  if (ctx.display.isPresent()) ctx.display.showDiagnostic("WebSrv");
  ctx.webServer.begin();

  if (ctx.display.isPresent()) ctx.display.showDiagnostic("Diag");
  ctx.diagnostics.begin();

  if (ctx.display.isPresent()) ctx.display.showDiagnostic("Systems");
  ctx.power.initWatchdog();
  ctx.power.initModemSleep();

  TimerManager::init();
}

void finalizeDisplay(AppContext& ctx) {
  if (ctx.display.isPresent()) {
    ctx.display.hideOtaProgressOverlay();
  }
}

// ============================================================================
// NETWORK
// ============================================================================

bool connectNetwork(AppContext& ctx, const char* hostname) {
  if (ctx.display.isPresent()) ctx.display.showDiagnostic("WiFi...");

  if (!ctx.wifi.connect(&ctx.display)) {
    Serial.println(F("[App] WiFi non connecté - lancement AP de secours"));
    if (ctx.display.isPresent()) ctx.display.showDiagnostic("AP secours");
    ctx.wifi.startFallbackAP();
    EventLog::add("WiFi connect failed; fallback AP started");
    return false;
  }
  return true;
}

bool connectWifi(AppContext& ctx, const char* hostname) {
  return connectNetwork(ctx, hostname);
}

void onNetworkReady(AppContext& ctx, const char* hostname, OtaState& state) {
  if (ctx.display.isPresent()) ctx.display.showDiagnostic("NTP sync");
  ctx.power.syncTimeFromNTP();
  Serial.printf("[Time] Heure après sync NTP: %s\n", ctx.power.getCurrentTimeString().c_str());

#if FEATURE_MAIL
  ctx.mailer.begin();
  EventLog::add("Mailer ready");
#endif

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  checkForOtaUpdate(ctx);
  state.lastCheck = millis();
  EventLog::add("OTA initial check done");
#endif

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
  ArduinoOTA.setPort(SystemConfig::ARDUINO_OTA_PORT);
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([&ctx, hostname]() {
      Serial.println("[OTA] Début de la mise à jour");
      WiFi.setSleep(false);
      EventLog::add("ArduinoOTA start");
      if (ctx.display.isPresent()) ctx.display.showOtaProgressOverlay(0);
      
      bool mailNotif = ctx.automatism.isEmailEnabled();
      const char* toEmail = mailNotif ? ctx.automatism.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
      String body = String("Début de mise à jour OTA (Interface web ArduinoOTA)\nHostname: ") + hostname;
      ctx.mailer.sendAlert("OTA début - Interface web", body, toEmail);
    })
    .onProgress([&ctx](unsigned int progress, unsigned int total) {
      int percent = (progress * 100) / total;
      if (ctx.display.isPresent()) ctx.display.showOtaProgressOverlay(static_cast<uint8_t>(percent));
    })
    .onEnd([&ctx]() {
      Serial.println("[OTA] Fin de la mise à jour");
      WiFi.setSleep(false);
      if (ctx.display.isPresent()) ctx.display.hideOtaProgressOverlay();
    })
    .onError([&ctx](ota_error_t error) {
      Serial.printf("[OTA] Erreur %u\n", error);
      if (ctx.display.isPresent()) ctx.display.hideOtaProgressOverlay();
    });
  ArduinoOTA.begin();
#endif

  // Fetch remote state
  StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
  ctx.automatism.fetchRemoteState(tmp);
}

// ============================================================================
// CONFIG & FINALIZATION
// ============================================================================

void loadConfiguration(AppContext& ctx) {
  Serial.println(F("\n[App] Chargement configuration complète depuis NVS..."));
  ctx.config.loadConfigFromNVS();
  ctx.automatism.begin();
}

void postConfiguration(AppContext& ctx, const char* hostname, OtaState& state) {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("[App] Chargement immédiat des variables distantes...");
  StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> tmp;
  if (ctx.automatism.fetchRemoteState(tmp)) {
    Serial.println("[App] Variables distantes chargées avec succès");
  } else {
    Serial.println("[App] Échec chargement variables distantes - utilisation du cache");
  }

  if (state.justUpdated && ctx.automatism.isEmailEnabled()) {
    Serial.println("[App] Envoi email pour mise à jour OTA serveur distant...");
    String body = String("Mise à jour OTA effectuée avec succès.\n\n") +
                  "Ancienne version: " + state.previousVersion + "\n" +
                  "Nouvelle version: " + String(ProjectConfig::VERSION) + "\n" +
                  "Hostname: " + hostname;
    
    String subj = "OTA mise à jour - Serveur distant ["; subj += hostname; subj += "]";
    bool emailSent = ctx.mailer.sendAlert(subj.c_str(), body.c_str(), ctx.automatism.getEmailAddress());
    Serial.printf("[App] Email serveur distant %s\n", emailSent ? "envoyé" : "échoué");
    
    state.justUpdated = false;
    g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, "ota_prevVer");
    state.previousVersion[0] = '\0';
  }

#if FEATURE_OTA && FEATURE_OTA != 0
  if (ctx.config.getOtaUpdateFlag()) {
    bool mailNotif = ctx.automatism.isEmailEnabled();
    const char* mail = mailNotif ? ctx.automatism.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
    
    String body = String("Mise à jour OTA effectuée avec succès (Interface Web).\n") + 
                  "Version: " + String(ProjectConfig::VERSION);
    
    const char* safeHost = (hostname && hostname[0] != '\0') ? hostname : "(unknown)";
    String subj = "OTA mise à jour - Interface web ["; subj += safeHost; subj += "]";
    
    ctx.mailer.sendAlert(subj.c_str(), body.c_str(), mail);
    ctx.config.setOtaUpdateFlag(false);
  }
#endif

  SensorReadings rs = ctx.sensors.read();
  ctx.automatism.sendFullUpdate(rs, "resetMode=0");
}

void onWifiReady(AppContext& ctx, const char* hostname, OtaState& state) {
  onNetworkReady(ctx, hostname, state);
}

void finalizeBoot(AppContext& ctx) {
  if (ctx.display.isPresent()) {
    ctx.display.forceEndSplash();
    ctx.display.clear();
    ctx.display.showDiagnostic("Init ok");
    delay(SystemConfig::FINAL_INIT_DELAY_MS);
  }
}

} // namespace SystemBoot
