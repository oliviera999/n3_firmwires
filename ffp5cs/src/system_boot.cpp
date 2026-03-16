#include "system_boot.h"
#include "system_sensors.h"
#include <Arduino.h>
#include "ffp5cs_fs.h"
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>  // Baseline heap au boot (stabilité long uptime)
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>

#include "config.h"
#include "nvs_manager.h"
#include "nvs_keys.h"
#include "gpio_parser.h"  // v11.179: resetEdgeDetectionState
#include "pins.h"
#include "i2c_bus.h"
#include "boot_log.h"  // BOOT_LOG : ets_printf (S3 PSRAM) ou Serial.printf (autres)
#include "sd_card.h"
#include "sd_logger.h"
#include <Wire.h>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

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
  BOOT_LOG("[Event] Boot start\n");

#ifndef DISABLE_ASYNC_WEBSERVER
  // Label "spiffs" pour correspondre à la table de partition et à la recherche esp_littlefs (partition "spiffs")
  const char* fsLabel = "spiffs";
  BOOT_LOG("[FS] Mounting " FFP5CS_FS_NAME " (label=%s)...\n", fsLabel);
  uint32_t fsStartTime = millis();
  if (!FFP5CS_FS.begin(false, "/spiffs", 10, fsLabel)) {
    BOOT_LOG("[FS] " FFP5CS_FS_NAME " mount failed - tentative de format\n");
    if (FFP5CS_FS.format()) {
      BOOT_LOG("[FS] Format reussi, tentative de remontage\n");
      if (FFP5CS_FS.begin(false, "/spiffs", 10, fsLabel)) {
        BOOT_LOG("[FS] Remontage apres format reussi\n");
      } else {
        BOOT_LOG("[FS] CRITIQUE: Impossible de monter " FFP5CS_FS_NAME " meme apres format\n");
      }
    } else {
      BOOT_LOG("[FS] CRITIQUE: Format " FFP5CS_FS_NAME " echoue\n");
    }
  } else {
    uint32_t fsDuration = millis() - fsStartTime;
    size_t total = FFP5CS_FS.totalBytes();
    size_t used = FFP5CS_FS.usedBytes();
    BOOT_LOG("[FS] " FFP5CS_FS_NAME " ok: %u/%u bytes (montage: %u ms)\n",
             static_cast<unsigned>(used),
             static_cast<unsigned>(total),
             fsDuration);

    if (!FFP5CS_FS.exists("/index.html")) {
      BOOT_LOG("[FS] Fichier index.html manquant\n");
    }
    if (!FFP5CS_FS.exists("/shared/common.js")) {
      BOOT_LOG("[FS] Fichier common.js manquant\n");
    }
  }
#else
  // Partition spiffs 64 Ko présente (partitions_esp32_wroom_ota_fs_mail.csv) pour la lib ESP Mail Client
  const char* fsLabel = "spiffs";
  BOOT_LOG("[FS] Montage " FFP5CS_FS_NAME " (label=%s) pour ESP Mail...\n", fsLabel);
  if (!FFP5CS_FS.begin(false, "/spiffs", 10, fsLabel)) {
    BOOT_LOG("[FS] " FFP5CS_FS_NAME " mount failed - tentative de format\n");
    if (FFP5CS_FS.format()) {
      BOOT_LOG("[FS] Format reussi, remontage\n");
      if (FFP5CS_FS.begin(false, "/spiffs", 10, fsLabel)) {
        size_t total = FFP5CS_FS.totalBytes();
        size_t used = FFP5CS_FS.usedBytes();
        BOOT_LOG("[FS] " FFP5CS_FS_NAME " ok: %u/%u bytes\n", static_cast<unsigned>(used), static_cast<unsigned>(total));
      }
    }
  } else {
    size_t total = FFP5CS_FS.totalBytes();
    size_t used = FFP5CS_FS.usedBytes();
    BOOT_LOG("[FS] " FFP5CS_FS_NAME " ok: %u/%u bytes\n", static_cast<unsigned>(used), static_cast<unsigned>(total));
  }
#endif
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  vTaskDelay(pdMS_TO_TICKS(1));
#endif

  BOOT_LOG("[App] Initialisation du gestionnaire NVS centralise\n");
  if (!g_nvsManager.begin()) {
    BOOT_LOG("[App] Erreur initialisation NVS Manager\n");
    return;
  }

  g_nvsManager.migrateFromOldSystem();
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  vTaskDelay(pdMS_TO_TICKS(1));
#endif

  // v11.179: Réinitialiser l'état de détection de front GPIO au boot
  GPIOParser::resetEdgeDetectionState();
}

void initializeSdCard() {
#if defined(BOARD_S3)
  if (SdCard::init()) {
    BOOT_LOG("[BOOT] SD carte détectée et opérationnelle\n");
    if (SdLogger::begin()) {
      SdLogger::rotateLogs(30);
    }
  } else {
    BOOT_LOG("[BOOT] SD carte absente ou indisponible\n");
  }
#else
  (void)0;
#endif
}

void cancelRollbackIfPending() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) != ESP_OK) return;
  if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    BOOT_LOG("[OTA] Image validee (rollback annule) au debut du boot\n");
  }
}

void validatePendingOta(OtaState& state) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();

  BOOT_LOG("\n=== VALIDATION OTA AU DEMARRAGE ===\n");
  if (running) {
    BOOT_LOG("[OTA] Partition en cours: %s (0x%08x)\n", running->label, running->address);
  }
  if (boot) {
    BOOT_LOG("[OTA] Partition de boot: %s (0x%08x)\n", boot->label, boot->address);
  }

  if (running && boot && running->address != boot->address) {
    BOOT_LOG("[OTA] Partition courante differente de la partition de boot!\n");
  }

  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    switch (otaState) {
      case ESP_OTA_IMG_NEW:
        BOOT_LOG("[OTA] Etat: NOUVELLE IMAGE (jamais demarree)\n");
        break;
      case ESP_OTA_IMG_PENDING_VERIFY:
        BOOT_LOG("[OTA] Etat: IMAGE EN ATTENTE DE VALIDATION\n");
        esp_ota_mark_app_valid_cancel_rollback();
        BOOT_LOG("[OTA] Image validee et rollback annule\n");
        state.justUpdated = true;
        {
          char tempBuf[64];
          g_nvsManager.loadString(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_PREV_VER, tempBuf, sizeof(tempBuf), "");
          strncpy(state.previousVersion, tempBuf, sizeof(state.previousVersion) - 1);
          state.previousVersion[sizeof(state.previousVersion) - 1] = '\0';
        }
        break;
      case ESP_OTA_IMG_VALID:
        BOOT_LOG("[OTA] Etat: IMAGE VALIDEE\n");
        break;
      case ESP_OTA_IMG_INVALID:
        BOOT_LOG("[OTA] Etat: IMAGE INVALIDE\n");
        break;
      case ESP_OTA_IMG_ABORTED:
        BOOT_LOG("[OTA] Etat: IMAGE ABANDONNEE\n");
        break;
      case ESP_OTA_IMG_UNDEFINED:
        BOOT_LOG("[OTA] Etat: IMAGE NON DEFINIE\n");
        break;
      default:
        BOOT_LOG("[OTA] Etat inconnu: %d\n", otaState);
        break;
    }
  } else {
    BOOT_LOG("[OTA] Impossible de recuperer l'etat de la partition\n");
  }

  BOOT_LOG("==================================\n");
}

void initializeTimekeeping(AppContext& ctx) {
  ctx.power.initTime();
  BOOT_LOG("[Event] Time init\n");

  ctx.power.setNTPConfig(SystemConfig::NTP_GMT_OFFSET_SEC,
                         SystemConfig::NTP_DAYLIGHT_OFFSET_SEC,
                         SystemConfig::NTP_SERVER);
}

bool initializeDisplay(AppContext& ctx) {
  if (ctx.display.isPresent()) {
    ctx.display.hideOtaProgressOverlay();
  }

  bool result = ctx.display.begin();
  return result;
}

void runI2cScanAndLog(char* outBuf, size_t outSize) {
  if (!outBuf || outSize == 0) {
    return;
  }
  outBuf[0] = '\0';
  I2CBusGuard guard;
  if (!guard) {
    BOOT_LOG("[BOOT] Scan I2C: mutex non acquis\n");
    return;
  }
  // Adresses 7 bits 0x08..0x77 (0x00-0x07 et 0x78-0x7F réservés / 10-bit)
  const uint8_t first = 0x08;
  const uint8_t last = 0x77;
  uint8_t count = 0;
  char* p = outBuf;
  size_t remaining = outSize;
  for (uint8_t addr = first; addr <= last && remaining > 4; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      int n = snprintf(p, remaining, "%s0x%02X", count ? ", " : "", addr);
      if (n > 0 && (size_t)n < remaining) {
        p += n;
        remaining -= (size_t)n;
        count++;
      }
    }
  }
  BOOT_LOG("[BOOT] Scan I2C (SDA=%d, SCL=%d): %s\n",
            Pins::I2C_SDA, Pins::I2C_SCL,
            count ? outBuf : "aucun peripherique");
  if (count == 0 && remaining >= 24) {
    snprintf(outBuf, outSize, "aucun périphérique");
  }
}

bool runI2cScanAndInitDisplay(AppContext& ctx) {
  static char i2cScanBuf[128];
  i2cScanBuf[0] = '\0';
  bool oledFound = false;
  {
    I2CBusGuard guard;
    if (!guard) {
      BOOT_LOG("[BOOT] Scan I2C: mutex non acquis\n");
      return false;
    }
    delay(500);
    const uint8_t first = 0x08;
    const uint8_t last = 0x77;
    uint8_t count = 0;
    char* p = i2cScanBuf;
    size_t remaining = sizeof(i2cScanBuf);
    for (uint8_t addr = first; addr <= last && remaining > 4; addr++) {
      Wire.beginTransmission(addr);
      uint8_t err = Wire.endTransmission();
      if (err == 0) {
        if (addr == Pins::OLED_ADDR) oledFound = true;
        int n = snprintf(p, remaining, "%s0x%02X", count ? ", " : "", addr);
        if (n > 0 && (size_t)n < remaining) {
          p += n;
          remaining -= (size_t)n;
          count++;
        }
      }
    }
    BOOT_LOG("[BOOT] Scan I2C (SDA=%d, SCL=%d): %s\n",
             Pins::I2C_SDA, Pins::I2C_SCL,
             count ? i2cScanBuf : "aucun peripherique");
    if (count == 0 && remaining >= 24) {
      snprintf(i2cScanBuf, sizeof(i2cScanBuf), "aucun périphérique");
    }
    if (oledFound) {
      ctx.display.beginHoldingMutex();
    }
  }
  if (ctx.display.isPresent()) {
    ctx.display.flush();
    return true;
  }
  return false;
}

void initializePeripherals(AppContext& ctx) {
  // Scan I2C au boot (Wire déjà initialisé par l'OLED) pour log et diagnostic
  static char i2cScanBuf[128];
  runI2cScanAndLog(i2cScanBuf, sizeof(i2cScanBuf));
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
  BOOT_LOG("\n[App] Chargement configuration complete depuis NVS...\n");
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
  BOOT_LOG("\n=== OTA REACTIVE - NOUVEAU GESTIONNAIRE ROBUSTE ===\n");
  BOOT_LOG("[OTA] OTA REACTIVE - Gestionnaire moderne et stable\n");
  BOOT_LOG("[OTA] Fonctionnalites: Tache dediee, esp_http_client, fallback\n");
  BOOT_LOG("[OTA] Securite: Watchdog desactive, validation complete\n");

  ctx.otaManager.setCurrentVersion(ProjectConfig::VERSION);
  ctx.otaManager.setCheckInterval(TimingConfig::OTA_CHECK_INTERVAL_MS);

  ctx.otaManager.setStatusCallback([&ctx](const char* message) {
    BOOT_LOG("[OTA] %s\n", message ? message : "(null)");
    if (ctx.display.isPresent() && message) {
      if (strstr(message, "Téléchargement") || strstr(message, "Début")) {
        ctx.display.showOtaProgressOverlay(0);
      }
    }
  });

  ctx.otaManager.setErrorCallback([&ctx](const char* error) {
    BOOT_LOG("[OTA] ERR %s\n", error ? error : "(null)");
    if (ctx.display.isPresent()) {
      ctx.display.hideOtaProgressOverlay();
    }
  });

  ctx.otaManager.setProgressCallback([&ctx](int progress) {
    BOOT_LOG("[OTA] Progression: %d%%\n", progress);
    if (ctx.display.isPresent()) {
      ctx.display.showOtaProgressOverlay(static_cast<uint8_t>(progress));
    }
  });

  BOOT_LOG("[OTA] Verification au boot et toutes les 2h gerees par netTask (prod)\n");
  BOOT_LOG("[OTA] Version courante: %s\n", ctx.otaManager.getCurrentVersion());

  BOOT_LOG("[OTA] Espace libre sketch: %d bytes\n", ESP.getFreeSketchSpace());
  BOOT_LOG("[OTA] Heap libre: %d bytes\n", ESP.getFreeHeap());
  BOOT_LOG("[OTA] Plus grand bloc libre: %u bytes (baseline boot)\n",
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
  BOOT_LOG("[OTA] Version courante: %s\n", ProjectConfig::VERSION);

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) {
    BOOT_LOG("[OTA] Partition courante: %s (0x%08x)\n",
             running->label,
             running->address);
  }
}

bool connectWifi(AppContext& ctx, const char* hostname) {
  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("WiFi...");
  }

  if (!ctx.wifi.connect(&ctx.display, hostname)) {
    BOOT_LOG("[App] WiFi non connecte - AP secours\n");
    if (ctx.display.isPresent()) {
      ctx.display.showDiagnostic("AP secours");
    }
    ctx.wifi.startFallbackAP();
    BOOT_LOG("[Event] fallback AP started\n");
    return false;
  }

  // Sauvegarder identifiants dès la première connexion réussie (réveil light sleep)
  ctx.power.saveCurrentWifiCredentials();
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
  BOOT_LOG("[Time] Heure apres sync NTP: %s\n", timeBuf);

#if FEATURE_MAIL
  ctx.mailer.begin();
  BOOT_LOG("[Boot] Initialisation queue mail sequentielle...\n");
  bool mailQueueOk = ctx.mailer.initMailQueue();
  BOOT_LOG("[Boot] initMailQueue retourne: %s\n", mailQueueOk ? "OK" : "ECHEC");
  if (mailQueueOk) {
    BOOT_LOG("[Event] Mailer ready (sequential processing)\n");
  } else {
    BOOT_LOG("[Event] Mailer ready (queue init FAILED - fallback sync)\n");
  }
#else
  BOOT_LOG("[Event] Mailer disabled (FEATURE_MAIL=0)\n");
#endif

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
  checkForOtaUpdateInternal(ctx);
  state.lastCheck = millis();
  BOOT_LOG("[Event] OTA initial check done\n");
#endif

}

void postConfiguration(AppContext& ctx, const char* hostname, OtaState& state) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  BOOT_LOG("[App] Chargement variables distantes: gere par netTask (deport TLS)\n");

  BOOT_LOG("[App] Verification des flags OTA...\n");
  BOOT_LOG("[App] state.justUpdated: %s\n", state.justUpdated ? "true" : "false");
  BOOT_LOG("[App] ctx.config.getOtaUpdateFlag(): %s\n",
           ctx.config.getOtaUpdateFlag() ? "true" : "false");
  BOOT_LOG("[App] ctx.automatism.isEmailEnabled(): %s\n",
           ctx.automatism.isEmailEnabled() ? "true" : "false");

  if (state.justUpdated && ctx.automatism.isEmailEnabled()) {
    const char* safeHost = (hostname && hostname[0] != '\0') ? hostname : "(unknown)";
    const bool fromRemote = (state.previousVersion[0] != '\0');
    if (fromRemote) {
      BOOT_LOG("[App] Envoi email pour mise a jour OTA serveur distant...\n");
    } else {
      BOOT_LOG("[App] Envoi email pour mise a jour OTA (interface web)...\n");
    }
    char body[BufferConfig::EMAIL_MAX_SIZE_BYTES];
    size_t bodyLen;
    if (fromRemote) {
      bodyLen = snprintf(body, sizeof(body),
          "Mise à jour OTA effectuée avec succès.\n\n"
          "Détails de la mise à jour:\n"
          "- Méthode: Serveur distant (HTTP OTA)\n"
          "- Ancienne version: %s\n"
          "- Nouvelle version: %s\n"
          "- Hostname: %s\n"
          "- Environnement: %s\n"
          "- Compilé le: %s %s\n"
          "- Redémarrage automatique effectué",
          state.previousVersion,
          ProjectConfig::VERSION,
          safeHost,
          Utils::getProfileName(),
          __DATE__, __TIME__);
    } else {
      bodyLen = snprintf(body, sizeof(body),
          "Mise à jour OTA effectuée avec succès.\n\n"
          "Détails de la mise à jour:\n"
          "- Méthode: Interface web\n"
          "- Nouvelle version: %s\n"
          "- Hostname: %s\n"
          "- Environnement: %s\n"
          "- Compilé le: %s %s\n"
          "- Redémarrage automatique effectué",
          ProjectConfig::VERSION,
          safeHost,
          Utils::getProfileName(),
          __DATE__, __TIME__);
    }
    if (bodyLen >= sizeof(body)) {
      body[sizeof(body) - 4] = '.';
      body[sizeof(body) - 3] = '.';
      body[sizeof(body) - 2] = '.';
      body[sizeof(body) - 1] = '\0';
    }
    char subj[128];
    snprintf(subj, sizeof(subj), "OTA mise à jour - %s [%s]",
             fromRemote ? "Serveur distant" : "Interface web", safeHost);
    bool emailQueued = ctx.mailer.sendAlert(subj, body, ctx.automatism.getEmailAddress(), true);
    BOOT_LOG("[App] Email OTA %s\n", emailQueued ? "ajoute a la queue" : "echoue (ajout queue)");
    state.justUpdated = false;
    g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_PREV_VER);
    state.previousVersion[0] = '\0';
    // Éviter double email : si on a envoyé le mail justUpdated, ne pas envoyer aussi celui du flag
    ctx.config.setOtaUpdateFlag(false);
  }

#if FEATURE_OTA && FEATURE_OTA != 0
  if (ctx.config.getOtaUpdateFlag()) {
    if (ctx.automatism.isEmailEnabled()) {
      const char* mail = ctx.automatism.getEmailAddress();
      const char* safeHost = (hostname && hostname[0] != '\0') ? hostname : "(unknown)";
      char otaMethodBuf[32];
      g_nvsManager.loadString(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_LAST_METHOD,
                              otaMethodBuf, sizeof(otaMethodBuf), "");
      const char* otaMethod = (otaMethodBuf[0] != '\0') ? otaMethodBuf : "Interface web";
      char body[BufferConfig::EMAIL_MAX_SIZE_BYTES];
      size_t bodyLen = snprintf(body, sizeof(body),
          "Mise à jour OTA effectuée avec succès.\n\n"
          "Détails de la mise à jour:\n"
          "- Méthode: %s\n"
          "- Nouvelle version: %s\n"
          "- Hostname: %s\n"
          "- Environnement: %s\n"
          "- Compilé le: %s %s\n"
          "- Redémarrage automatique effectué",
          otaMethod,
          ProjectConfig::VERSION,
          safeHost,
          Utils::getProfileName(),
          __DATE__, __TIME__);
      if (bodyLen >= sizeof(body)) {
        body[sizeof(body) - 4] = '.';
        body[sizeof(body) - 3] = '.';
        body[sizeof(body) - 2] = '.';
        body[sizeof(body) - 1] = '\0';
      }
      char subj[128];
      snprintf(subj, sizeof(subj), "OTA mise à jour - Interface web [%s]", safeHost);
      bool emailQueued = ctx.mailer.sendAlert(subj, body, mail, true);
      BOOT_LOG("[App] Email interface web %s\n", emailQueued ? "ajoute a la queue" : "echoue (ajout queue)");
    }
    ctx.config.setOtaUpdateFlag(false);
  }
#endif

  SensorReadings rs = ctx.sensors.read();
  ctx.automatism.sendFullUpdate(rs, "resetMode=0");
}

}  // namespace SystemBoot
