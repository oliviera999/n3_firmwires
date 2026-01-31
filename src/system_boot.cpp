#include "system_boot.h"
#include "system_sensors.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
#include <ArduinoOTA.h>
#endif

#include "config.h"
#include "nvs_manager.h"
#include "nvs_keys.h"

namespace SystemBoot {

void setupHostname(char* buffer, size_t bufferSize) {
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(buffer,
           bufferSize,
           "%s-%04X",
           SystemConfig::HOSTNAME_PREFIX,
           static_cast<uint16_t>(chipId & 0xFFFF));
  WiFi.setHostname(buffer);
}

void initializeStorage(AppContext& ctx) {
  Serial.println("[Event] Boot start");

  const char* fsLabel = "spiffs";  // Label "spiffs" pour compatibilité ESP Mail Client
  Serial.printf("[FS] Mounting LittleFS (label=%s)...\n", fsLabel);
  uint32_t fsStartTime = millis();
  if (!LittleFS.begin(false, "/spiffs", 10, fsLabel)) {
    Serial.println("[FS] ❌ LittleFS mount failed - tentative de format");
    if (LittleFS.format()) {
      Serial.println("[FS] ✅ Format réussi, tentative de remontage");
      if (LittleFS.begin(false, "/spiffs", 10, fsLabel)) {
        Serial.println("[FS] ✅ Remontage après format réussi");
      } else {
        Serial.println("[FS] ❌ CRITIQUE: Impossible de monter LittleFS même après format");
      }
    } else {
      Serial.println("[FS] ❌ CRITIQUE: Format LittleFS échoué");
    }
  } else {
    uint32_t fsDuration = millis() - fsStartTime;
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    Serial.printf("[FS] ✅ LittleFS ok: %u/%u bytes (montage: %u ms)\n",
                  static_cast<unsigned>(used),
                  static_cast<unsigned>(total),
                  fsDuration);

    if (!LittleFS.exists("/index.html")) {
      Serial.println("[FS] ⚠️ Fichier index.html manquant");
    }
    if (!LittleFS.exists("/shared/common.js")) {
      Serial.println("[FS] ⚠️ Fichier common.js manquant");
    }
  }

  Serial.println(F("[App] 🚀 Initialisation du gestionnaire NVS centralisé"));
  if (!g_nvsManager.begin()) {
    Serial.println(F("[App] ❌ Erreur initialisation NVS Manager"));
    return;
  }

  g_nvsManager.migrateFromOldSystem();
}

void validatePendingOta(OtaState& state) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();

  Serial.println("\n=== VALIDATION OTA AU DÉMARRAGE ===");
  if (running) {
    Serial.printf("[OTA] Partition en cours: %s (0x%08x)\n", running->label, running->address);
  }
  if (boot) {
    Serial.printf("[OTA] Partition de boot: %s (0x%08x)\n", boot->label, boot->address);
  }

  if (running && boot && running->address != boot->address) {
    Serial.println("[OTA] ⚠️ Partition courante différente de la partition de boot!");
  }

  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    switch (otaState) {
      case ESP_OTA_IMG_NEW:
        Serial.println("[OTA] État: NOUVELLE IMAGE (jamais démarrée)");
        break;
      case ESP_OTA_IMG_PENDING_VERIFY:
        Serial.println("[OTA] État: IMAGE EN ATTENTE DE VALIDATION");
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("[OTA] ✅ Image validée et rollback annulé");
        state.justUpdated = true;
        {
          char tempBuf[64];
          g_nvsManager.loadString(NVS_NAMESPACES::SYSTEM, "ota_prevVer", tempBuf, sizeof(tempBuf), "");
          strncpy(state.previousVersion, tempBuf, sizeof(state.previousVersion) - 1);
          state.previousVersion[sizeof(state.previousVersion) - 1] = '\0';
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
        Serial.printf("[OTA] État inconnu: %d\n", otaState);
        break;
    }
  } else {
    Serial.println("[OTA] Impossible de récupérer l'état de la partition");
  }

  Serial.println("==================================\n");
}

void initializeTimekeeping(AppContext& ctx) {
  ctx.power.initTime();
  Serial.println("[Event] Time init");

  ctx.power.setNTPConfig(SystemConfig::NTP_GMT_OFFSET_SEC,
                         SystemConfig::NTP_DAYLIGHT_OFFSET_SEC,
                         SystemConfig::NTP_SERVER);
}

bool initializeDisplay(AppContext& ctx) {
  if (ctx.display.isPresent()) {
    ctx.display.hideOtaProgressOverlay();
  }

  Serial.println(F("[DEBUG] Avant oled.begin()"));
  bool result = ctx.display.begin();
  Serial.printf("[DEBUG] oled.begin() retourné: %s\n", result ? "true" : "false");
  Serial.printf("[DEBUG] oled.isPresent(): %s\n", ctx.display.isPresent() ? "true" : "false");
  return result;
}

void initializePeripherals(AppContext& ctx) {
  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("Sensors");
  }
  ctx.sensors.begin();

  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("Actuators");
  }
  ctx.actuators.begin();

  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("WebSrv");
  }
  ctx.webServer.begin();

  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("Diag");
  }
  ctx.diagnostics.begin();

  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("Systems");
  }
  ctx.power.initWatchdog();
}

void loadConfiguration(AppContext& ctx) {
  Serial.println(F("\n[App] Chargement configuration complète depuis NVS..."));
  ctx.config.loadConfigFromNVS();
  ctx.automatism.begin();
}

void finalizeDisplay(AppContext& ctx) {
  if (!ctx.display.isPresent()) {
    return;
  }

  ctx.display.forceEndSplash();
  ctx.display.clear();
  ctx.display.showDiagnostic("Init ok");
  delay(SystemConfig::FINAL_INIT_DELAY_MS);
}

static void checkForOtaUpdateInternal(AppContext& ctx) {
  Serial.println("\n=== OTA RÉACTIVÉ - NOUVEAU GESTIONNAIRE ROBUSTE ===");
  Serial.println("[OTA] ✅ OTA RÉACTIVÉ - Gestionnaire moderne et stable");
  Serial.println("[OTA] ✅ Fonctionnalités: Tâche dédiée, esp_http_client, fallback");
  Serial.println("[OTA] ✅ Sécurité: Watchdog désactivé, validation complète");

  ctx.otaManager.setCurrentVersion(ProjectConfig::VERSION);
  ctx.otaManager.setCheckInterval(TimingConfig::OTA_CHECK_INTERVAL_MS);

  ctx.otaManager.setStatusCallback([&ctx](const char* message) {
    Serial.printf("[OTA] %s\n", message ? message : "(null)");
    if (ctx.display.isPresent() && message) {
      if (strstr(message, "Téléchargement") || strstr(message, "Début")) {
        ctx.display.showOtaProgressOverlay(0);
      }
    }
  });

  ctx.otaManager.setErrorCallback([&ctx](const char* error) {
    Serial.printf("[OTA] ❌ %s\n", error ? error : "(null)");
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

  Serial.println("[OTA] Mode manuel: pas de vérification automatique au boot");
  Serial.printf("[OTA] 📋 Version courante: %s\n", ctx.otaManager.getCurrentVersion());

  Serial.printf("[OTA] 📊 Espace libre sketch: %d bytes\n", ESP.getFreeSketchSpace());
  Serial.printf("[OTA] 📊 Heap libre: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("[OTA] 📊 Version courante: %s\n", ProjectConfig::VERSION);

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) {
    Serial.printf("[OTA] 📊 Partition courante: %s (0x%08x)\n",
                  running->label,
                  running->address);
  }
}

bool connectWifi(AppContext& ctx, const char* hostname) {
  (void)hostname;
  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("WiFi...");
  }

  if (!ctx.wifi.connect(&ctx.display)) {
    Serial.println(F("[App] WiFi non connecté - lancement AP de secours"));
    if (ctx.display.isPresent()) {
      ctx.display.showDiagnostic("AP secours");
    }
    ctx.wifi.startFallbackAP();
    Serial.println("[Event] WiFi connect failed; fallback AP started");
    return false;
  }

  return true;
}

void checkForOtaUpdate(AppContext& ctx) {
  checkForOtaUpdateInternal(ctx);
}

void onWifiReady(AppContext& ctx, const char* hostname, OtaState& state) {
  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("NTP sync");
  }
  ctx.power.syncTimeFromNTP();
  char timeBuf[64];
  ctx.power.getCurrentTimeString(timeBuf, sizeof(timeBuf));
  Serial.printf("[Time] Heure après sync NTP: %s\n", timeBuf);

#if FEATURE_MAIL
  ctx.mailer.begin();
  Serial.println(F("[Boot] Initialisation queue mail séquentielle..."));
  bool mailQueueOk = ctx.mailer.initMailQueue();
  Serial.printf("[Boot] initMailQueue retourne: %s\n", mailQueueOk ? "OK" : "ECHEC");
  if (mailQueueOk) {
    Serial.println("[Event] Mailer ready (sequential processing)");
  } else {
    Serial.println("[Event] Mailer ready (queue init FAILED - fallback sync)");
  }
#else
  Serial.println("[Event] Mailer disabled (FEATURE_MAIL=0)");
#endif

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  checkForOtaUpdateInternal(ctx);
  state.lastCheck = millis();
  Serial.println("[Event] OTA initial check done");
#endif

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
  ArduinoOTA.setPort(SystemConfig::ARDUINO_OTA_PORT);
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA
    .onStart([&ctx, hostname]() {
      Serial.println("[OTA] Début de la mise à jour");
      WiFi.setSleep(false);
      Serial.println("[Event] ArduinoOTA start");
      if (ctx.display.isPresent()) {
        ctx.display.showOtaProgressOverlay(0);
      }

      bool mailNotif = ctx.automatism.isEmailEnabled();
      const char* toEmail = mailNotif ? ctx.automatism.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
      char body[BufferConfig::EMAIL_MAX_SIZE_BYTES];
      snprintf(body, sizeof(body),
               "Début de mise à jour OTA (Interface web ArduinoOTA)\n\n"
               "Détails:\n"
               "- Méthode: Interface web ArduinoOTA\n"
               "- Version courante: %s\n"
               "- Hostname: %s\n"
               "- Hôte: %s:%u",
               ProjectConfig::VERSION,
               hostname ? hostname : "(unknown)",
               hostname ? hostname : "(unknown)",
               SystemConfig::ARDUINO_OTA_PORT);
      ctx.mailer.sendAlert("OTA début - Interface web", body, toEmail);
    })
    .onProgress([&ctx](unsigned int progress, unsigned int total) {
      int percent = (progress * 100) / total;
      Serial.printf("[OTA] 📊 Progression ArduinoOTA: %d%% (%u/%u)\n",
                    percent,
                    progress,
                    total);
      if (ctx.display.isPresent()) {
        ctx.display.showOtaProgressOverlay(static_cast<uint8_t>(percent));
      }
    })
    .onEnd([&ctx]() {
      Serial.println("[OTA] Fin de la mise à jour");
      WiFi.setSleep(false);
      if (ctx.display.isPresent()) {
        ctx.display.hideOtaProgressOverlay();
      }
    })
    .onError([&ctx](ota_error_t error) {
      Serial.printf("[OTA] Erreur %u\n", error);
      if (ctx.display.isPresent()) {
        ctx.display.hideOtaProgressOverlay();
      }
    });
  ArduinoOTA.begin();
#endif
}

void postConfiguration(AppContext& ctx, const char* hostname, OtaState& state) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  Serial.println("[App] Chargement variables distantes: géré par netTask (déport TLS)");

  Serial.println("[App] Vérification des flags OTA...");
  Serial.printf("[App] state.justUpdated: %s\n", state.justUpdated ? "true" : "false");
  Serial.printf("[App] ctx.config.getOtaUpdateFlag(): %s\n",
                ctx.config.getOtaUpdateFlag() ? "true" : "false");
  Serial.printf("[App] ctx.automatism.isEmailEnabled(): %s\n",
                ctx.automatism.isEmailEnabled() ? "true" : "false");

  if (state.justUpdated && ctx.automatism.isEmailEnabled()) {
    Serial.println("[App] Envoi email pour mise à jour OTA serveur distant...");
    char body[BufferConfig::EMAIL_MAX_SIZE_BYTES];
    size_t bodyLen = snprintf(body, sizeof(body),
        "Mise à jour OTA effectuée avec succès.\n\n"
        "Détails de la mise à jour:\n"
        "- Méthode: Serveur distant automatique\n"
        "- Ancienne version: %s\n"
        "- Nouvelle version: %s\n"
        "- Hostname: %s\n"
        "- Compilé le: %s %s\n"
        "- Redémarrage automatique effectué",
        state.previousVersion[0] != '\0' ? state.previousVersion : "",
        ProjectConfig::VERSION,
        hostname,
        __DATE__, __TIME__);
    if (bodyLen >= sizeof(body)) {
      body[sizeof(body) - 4] = '.';
      body[sizeof(body) - 3] = '.';
      body[sizeof(body) - 2] = '.';
      body[sizeof(body) - 1] = '\0';
    }
    char subj[128];
    snprintf(subj, sizeof(subj), "OTA mise à jour - Serveur distant [%s]", hostname);
    bool emailSent = ctx.mailer.sendAlert(subj, body, ctx.automatism.getEmailAddress());
    Serial.printf("[App] Email serveur distant %s\n", emailSent ? "envoyé" : "échoué");
    state.justUpdated = false;
    g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_PREV_VER);
    state.previousVersion[0] = '\0';
  }

#if FEATURE_OTA && FEATURE_OTA != 0
  if (ctx.config.getOtaUpdateFlag()) {
    bool mailNotif = ctx.automatism.isEmailEnabled();
    const char* mail = mailNotif ? ctx.automatism.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;

    if (mailNotif) {
      Serial.println("[App] Envoi email pour mise à jour OTA interface web...");
    } else {
      Serial.println("[App] Email non activé dans les variables distantes, utilisation de l'adresse par défaut...");
    }

    const char* safeHost = (hostname && hostname[0] != '\0') ? hostname : "(unknown)";
    char body[BufferConfig::EMAIL_MAX_SIZE_BYTES];
    size_t bodyLen = snprintf(body, sizeof(body),
        "Mise à jour OTA effectuée avec succès.\n\n"
        "Détails de la mise à jour:\n"
        "- Méthode: Interface web ElegantOTA\n"
        "- Nouvelle version: %s\n"
        "- Hostname: %s\n"
        "- Compilé le: %s %s\n"
        "- Redémarrage automatique effectué",
        ProjectConfig::VERSION,
        safeHost,
        __DATE__, __TIME__);
    if (bodyLen >= sizeof(body)) {
      body[sizeof(body) - 4] = '.';
      body[sizeof(body) - 3] = '.';
      body[sizeof(body) - 2] = '.';
      body[sizeof(body) - 1] = '\0';
    }
    char subj[128];
    snprintf(subj, sizeof(subj), "OTA mise à jour - Interface web [%s]", safeHost);
    bool emailSent = ctx.mailer.sendAlert(subj, body, mail);
    Serial.printf("[App] Email interface web %s\n", emailSent ? "envoyé" : "échoué");
    ctx.config.setOtaUpdateFlag(false);
  }
#endif

  SensorReadings rs = ctx.sensors.read();
  ctx.automatism.sendFullUpdate(rs, "resetMode=0");
}

}  // namespace SystemBoot
