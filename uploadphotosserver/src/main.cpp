/*
  Firmware ESP32-CAM unifié — upload photo vers iot.olution.info
  Cibles : msp1, n3pp, ffp3 (build_flags -DTARGET_MSP1 / -DTARGET_N3PP / -DTARGET_FFP3).
  Basé sur RandomNerdTutorials.com/esp32-cam-post-image-photo-server/
*/

#include <Arduino.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include "config.h"
#include "credentials.h"
#include "time.h"
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <HTTPClient.h>
#if defined(USE_HTTPS_ENDPOINTS)
// Upload photo en TLS (opt-in flag de build). Inclus uniquement sous le flag :
// le build par defaut (HTTP) reste inchange. ATTENTION RAM ESP32-CAM, cf.
// docs/HTTPS_MIGRATION.md (validation cible obligatoire avant prod).
#include <WiFiClientSecure.h>
#endif
#include "n3_log.h"
#include "n3_time.h"
#include "n3_wifi.h"
#include "n3_ota.h"
#include "n3_ota_periodic.h"
#include "n3_mail.h"
#include "n3_notify.h"  /* Phase 3 : taxonomie N3Severity/N3NotifMode */
#include "camera_remote.h"
#include "camera_setup.h"
#include "camera_uploader.h"
#include "camera_sync.h"
#include "camera_sleep.h"
#include "camera_mail_events.h"
#include "camera_time.h"
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <esp_chip_info.h>

#if USE_SD
#include "FS.h"
#include "SD_MMC.h"
#endif

#if USE_DEEP_SLEEP
#include <ESP32Time.h>
#include <Preferences.h>
#endif

String serverName = SERVER_NAME;
String serverPath = SERVER_PATH;

String Wifiactif;

#if USE_DEEP_SLEEP
/* Cadence deleguee a shared/n3_common/n3_ota_periodic (T4.2) — meme 2 h. */
static constexpr uint32_t OTA_PERIODIC_INTERVAL_SECONDS = OtaPeriodic::kDefaultIntervalSeconds;
RTC_DATA_ATTR static uint32_t otaElapsedSinceLastCheckSeconds = OTA_PERIODIC_INTERVAL_SECONDS;
RTC_DATA_ATTR static int lastPhotoWindowState = -1;  /* -1: inconnu, 0: nuit, 1: jour */
RTC_DATA_ATTR static uint8_t pendingWindowMailMask = 0;
RTC_DATA_ATTR static int lastRemoteForceWakeupState = 0;
RTC_DATA_ATTR static int lastRemoteResetModeState = 0;
#endif

static bool otaUpdateStartedThisBoot = false;
static char otaRemoteVersion[16] = "";
static char otaFirmwareUrl[160] = "";

/* Phase 0 (arbitrage mails) — alerte « OTA échec » non livrée : avant, un échec
 * d'envoi SMTP dans otaMailEndCallback perdait définitivement l'alerte (le deep
 * sleep efface tout). On mémorise désormais l'alerte en RTC et on la retente aux
 * réveils suivants (WiFi OK), avec un budget borné pour ne pas marteler le TLS. */
static constexpr uint8_t OTA_FAIL_MAIL_MAX_TRIES = 5;
RTC_DATA_ATTR static bool pendingOtaFailMail = false;
RTC_DATA_ATTR static uint8_t pendingOtaFailMailTries = 0;
RTC_DATA_ATTR static char pendingOtaFailMailExtra[192] = "";
static bool remoteMailNotifEnabled = MAIL_NOTIFICATIONS_ENABLED;
/* Phase 3 arbitrage : mode de notification gradue (cle 103, retro-compatible bool).
 * remoteMailNotifEnabled reste le raccourci booleen (mode != None). */
static N3NotifMode remoteNotifMode = MAIL_NOTIFICATIONS_ENABLED ? N3NotifMode::Full : N3NotifMode::None;
/* Phase 3 arbitrage : proxy « serveur OK » de ce reveil = GET config 200 + POST
 * version 2xx. false -> FAILOVER : mails plafonnes a P1/P2 (critique-only). Le
 * serveur ne couvre pas encore les diagnostics CAM (boot/jour-nuit/OTA), donc
 * AUCUNE suppression quand le serveur est OK — regle d'ordonnancement du plan. */
static bool serverExchangeOk = false;
static String remoteMailRecipient = "";
static uint32_t runtimeSleepSeconds = TIME_TO_SLEEP;
static bool forceWakeupActiveThisBoot = false;
static bool resetModeActiveThisBoot = false;

#if USE_DEEP_SLEEP
static void n3EnterRuntimeDeepSleep(const char* reason) {
  N3_LOGI("[SLEEP] %s (runtime=%u s)",
          reason ? reason : "deep sleep",
          static_cast<unsigned int>(runtimeSleepSeconds));
  delay(500);
  n3EnterDeepSleepSeconds(runtimeSleepSeconds);
}
#endif

#if USE_SD
bool sdAvailable = false;  /* true seulement si SD montée au boot */
#endif

#if USE_DEEP_SLEEP
Preferences preferences;
ESP32Time rtc;
#endif

bool Wificonnect();
void capturePhoto(bool wifiOk);
void ledBlink(int onMs, int offMs, int count);
static void logMonitoringSnapshot(const char* stage);
static void logStepDuration(const char* step, uint32_t durationMs, uint32_t warnMs);
static bool sendDebugEventMail(const char* subjectEvent, const char* eventName, const char* extraInfo, N3Severity severity);
static void otaMailStartCallback(const char* currentVersion, const char* remoteVersion, const char* firmwareUrl, void* userData);
static void otaMailEndCallback(bool success, const char* details, void* userData);
static void trySendPendingOtaFailMail(bool wifiOk);
static void handlePhotoWindowTransitionMails(bool wifiOk);
static void accumulateOtaPeriodicElapsedFromSleep(uint32_t sleepSeconds);
#if USE_DEEP_SLEEP
static void trySendFirstBootMail(bool wifiOk);
#endif
#if USE_SD
static void initSdIfEnabled();
static void runSyncDrainIfNeeded(bool wifiOk);
#endif
static bool initCameraPipeline();

/* ----- LED ----- */
void ledBlink(int onMs, int offMs, int count) {
  pinMode(LED_GPIO, OUTPUT);
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_GPIO, LOW);
    delay(onMs);
    digitalWrite(LED_GPIO, HIGH);
    delay(offMs);
  }
}

/* ----- WiFi (scan + RSSI + BSSID via n3_wifi) ----- */
/* A9 : le reinterpret_cast WIFI_LIST (WifiCredential) -> N3WifiNetwork suppose un layout identique.
 * On le sécurise à la compilation : toute divergence future de l'une des deux structs échoue le build
 * au lieu de corrompre silencieusement la liste WiFi. */
static_assert(sizeof(WifiCredential) == sizeof(N3WifiNetwork),
              "WifiCredential et N3WifiNetwork doivent avoir la meme taille (cast WIFI_LIST)");
static_assert(offsetof(WifiCredential, ssid) == offsetof(N3WifiNetwork, ssid),
              "Offset ssid incompatible entre WifiCredential et N3WifiNetwork");
static_assert(offsetof(WifiCredential, password) == offsetof(N3WifiNetwork, pass),
              "Offset password/pass incompatible entre WifiCredential et N3WifiNetwork");

/** Réinitialise la radio WiFi après deep sleep / reconnexion rapide RTC (évite WIFI_SCAN_FAILED). */
static void wifiRadioResetForWake() {
  N3_LOGI("[WiFi] Reset radio (disconnect/scanDelete/WIFI_OFF)");
  WiFi.disconnect(true, true);
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(100);
}

bool Wificonnect() {
  /* WIFI_LIST est WifiCredential { ssid, password } ; même layout que N3WifiNetwork { ssid, pass }
     (garanti par les static_assert ci-dessus). */
  const N3WifiNetwork* nets = reinterpret_cast<const N3WifiNetwork*>(WIFI_LIST);
  N3WifiConfig cfg = {};
  cfg.networks = nets;
  cfg.networkCount = WIFI_COUNT;
  cfg.timeoutMs = WIFI_CONNECT_TIMEOUT_MS;
  cfg.delayBetweenMs = WIFI_DELAY_BETWEEN_MS;
  cfg.preScanDelayMs = WIFI_PRE_SCAN_DELAY_MS;
  cfg.scanMax = WIFI_SCAN_MAX;
  cfg.disableFastReconnect = true;  /* CAM : evite BSSID RTC obsolete apres deep sleep */
  cfg.onSuccess = [](const char*) { ledBlink(500, 500, 1); };
  return n3WifiConnect(cfg, &Wifiactif);
}

static void logStepDuration(const char* step, uint32_t durationMs, uint32_t warnMs) {
  N3_LOGI("[MON] %s duree=%lu ms", step ? step : "step", static_cast<unsigned long>(durationMs));
  if (warnMs > 0 && durationMs > warnMs) {
    N3_LOGW("[MON] %s lent (%lu ms > %lu ms)",
            step ? step : "step",
            static_cast<unsigned long>(durationMs),
            static_cast<unsigned long>(warnMs));
  }
}

static void logMonitoringSnapshot(const char* stage) {
  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  const char* wifiState = wifiConnected ? "ok" : "ko";
  const int wifiRssi = wifiConnected ? WiFi.RSSI() : 0;
  String ipStr = wifiConnected ? WiFi.localIP().toString() : String("n/a");
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t minHeap = ESP.getMinFreeHeap();
#if USE_SD
  const char* sdState = sdAvailable ? "ok" : "off";
  N3_LOGI("[MON] stage=%s up=%lu s wifi=%s rssi=%d ip=%s heap=%lu min_heap=%lu sd=%s",
          stage ? stage : "unknown",
          static_cast<unsigned long>(millis() / 1000UL),
          wifiState,
          wifiRssi,
          ipStr.c_str(),
          static_cast<unsigned long>(freeHeap),
          static_cast<unsigned long>(minHeap),
          sdState);
#else
  N3_LOGI("[MON] stage=%s up=%lu s wifi=%s rssi=%d ip=%s heap=%lu min_heap=%lu",
          stage ? stage : "unknown",
          static_cast<unsigned long>(millis() / 1000UL),
          wifiState,
          wifiRssi,
          ipStr.c_str(),
          static_cast<unsigned long>(freeHeap),
          static_cast<unsigned long>(minHeap));
#endif
  if (freeHeap < MONITORING_HEAP_WARN_BYTES) {
    N3_LOGW("[MON] heap faible: %lu < %u bytes",
            static_cast<unsigned long>(freeHeap),
            static_cast<unsigned int>(MONITORING_HEAP_WARN_BYTES));
  }
  if (wifiConnected && wifiRssi < -80) {
    N3_LOGW("[MON] signal WiFi faible: RSSI=%d dBm", wifiRssi);
  }
}

static bool sendDebugEventMail(const char* subjectEvent, const char* eventName, const char* extraInfo, N3Severity severity) {
#if MAIL_NOTIFICATIONS_ENABLED && defined(SMTP_HOST_ADDR) && defined(SMTP_PORT_NUM) && defined(SMTP_EMAIL) && defined(SMTP_PASSWORD) && defined(SMTP_DEST)
  /* Phase 3 arbitrage : filtrage par severite (taxonomie n3_notify, cle 103
   * graduee). En FAILOVER (echange serveur KO), plafond P1/P2 : les diagnostics
   * P3/P4 ne valent pas le cout TLS hors ligne — retour false = non envoye,
   * les appelants a etat (pending masks, first-boot) retenteront. */
  const N3NotifMode effectiveMode =
      serverExchangeOk ? remoteNotifMode : n3NotifModeCapFailover(remoteNotifMode);
  if (!n3NotifModeAllows(effectiveMode, severity)) {
    N3_LOGI("[MAIL] severite %s filtree (mode%s)",
            n3SeverityCode(severity), serverExchangeOk ? "" : " failover");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    N3_LOGI("[MAIL] Envoi annule: WiFi indisponible.");
    return false;
  }

  N3MailSmtpConfig smtpCfg = {};
  smtpCfg.smtpHost = SMTP_HOST_ADDR;
  smtpCfg.smtpPort = SMTP_PORT_NUM;
  smtpCfg.authorEmail = SMTP_EMAIL;
  smtpCfg.authorPassword = SMTP_PASSWORD;
  smtpCfg.senderName = "N3 uploadphotosserver";
  smtpCfg.recipientName = "N3";
  smtpCfg.recipientEmail = (remoteMailRecipient.length() > 0) ? remoteMailRecipient.c_str() : SMTP_DEST;

  char subject[MAIL_SUBJECT_MAX_LEN];
  snprintf(subject, sizeof(subject), "[uploadphotosserver][%s][%s] %s",
           currentTargetName(), n3SeverityCode(severity), subjectEvent);

  char localTime[32];
  cameraGetLocalTimeString(localTime, sizeof(localTime));
  String ipStr = WiFi.localIP().toString();
  char ssidBuf[33];
  ssidBuf[0] = '\0';
  if (WiFi.status() == WL_CONNECTED) {
    String wifiSsid = WiFi.SSID();
    snprintf(ssidBuf, sizeof(ssidBuf), "%s", wifiSsid.c_str());
  } else {
    snprintf(ssidBuf, sizeof(ssidBuf), "%s", "(deconnecte)");
  }
  int wifiRssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

  N3MailDebugInfo dbgInfo = {};
  dbgInfo.projectName = "uploadphotosserver";
  dbgInfo.targetName = currentTargetName();
  dbgInfo.firmwareVersion = FIRMWARE_VERSION;
  dbgInfo.eventName = eventName;
  dbgInfo.localTime = localTime;
  dbgInfo.wakeupReason = wakeupCauseText(esp_sleep_get_wakeup_cause());
  dbgInfo.resetReason = resetReasonText(esp_reset_reason());
  dbgInfo.wifiSsid = ssidBuf;
  dbgInfo.wifiIp = ipStr.c_str();
  dbgInfo.wifiRssi = wifiRssi;
  dbgInfo.uptimeSeconds = millis() / 1000UL;
  dbgInfo.freeHeap = ESP.getFreeHeap();
  dbgInfo.minFreeHeap = ESP.getMinFreeHeap();
  dbgInfo.extraInfo = extraInfo;

  char body[MAIL_BODY_MAX_LEN];
  if (!n3MailBuildDebugBody(dbgInfo, body, sizeof(body))) {
    N3_LOGW("[MAIL] Echec generation corps mail.");
    return false;
  }

  String smtpError;
  bool ok = n3MailSendText(smtpCfg, subject, body, &smtpError);
  if (!ok) {
    N3_LOGW("[MAIL] Echec envoi: %s", smtpError.c_str());
  } else {
    N3_LOGI("[MAIL] Mail envoye: %s", subject);
  }
  return ok;
#else
  (void)subjectEvent;
  (void)eventName;
  (void)extraInfo;
  (void)severity;
  N3_LOGI("[MAIL] SMTP non configure dans credentials.h, notification ignoree.");
  return false;
#endif
}

static void otaMailStartCallback(const char* currentVersion,
                                 const char* remoteVersion,
                                 const char* firmwareUrl,
                                 void* userData) {
  (void)userData;
  otaUpdateStartedThisBoot = true;
  snprintf(otaRemoteVersion, sizeof(otaRemoteVersion), "%s", remoteVersion ? remoteVersion : "inconnue");
  snprintf(otaFirmwareUrl, sizeof(otaFirmwareUrl), "%s", firmwareUrl ? firmwareUrl : "inconnue");

  /* Pas de SMTP ici : TLS (ESP Mail Client) + verification sha256 OTA sur la meme
   * pile loopTask (~8 Ko par defaut) provoquent stack canary panic sur ESP32-CAM sans PSRAM. */
  N3_LOGI("[OTA][MAIL] demarrage %s -> %s (notification SMTP reportee, heap=%u)",
          currentVersion ? currentVersion : FIRMWARE_VERSION,
          otaRemoteVersion,
          static_cast<unsigned int>(ESP.getFreeHeap()));
}

static void otaMailEndCallback(bool success, const char* details, void* userData) {
  (void)userData;
  if (!otaUpdateStartedThisBoot) return;

  N3_LOGI("[OTA][MAIL] fin success=%d version_distante=%s details=%s heap=%u",
          success ? 1 : 0,
          otaRemoteVersion,
          details ? details : "n/a",
          static_cast<unsigned int>(ESP.getFreeHeap()));
  /* Echec OTA : ne jamais appeler SMTP ici (TLS + mbedtls OTA sur loopTask ~32 Ko
   * provoquent stack canary panic — cf. v2.51). Memorisation RTC ; envoi differe
   * via trySendPendingOtaFailMail() apres la pile OTA degonflee. */
  if (!success && remoteMailNotifEnabled) {
    snprintf(pendingOtaFailMailExtra, sizeof(pendingOtaFailMailExtra),
             "OTA echec (mail differe).\nVersion distante: %s\nURL: %s\nDetails: %s",
             otaRemoteVersion, otaFirmwareUrl, details ? details : "n/a");
    pendingOtaFailMail = true;
    pendingOtaFailMailTries = 0;
    N3_LOGI("[OTA][MAIL] Alerte memorisee (SMTP differe, evite stack overflow loopTask).");
  }
}

/* Phase 0 (arbitrage mails) : retente l'alerte « OTA échec » mémorisée en RTC.
 * Appelé à chaque réveil après la récupération de la config distante (destinataire
 * et mailNotif à jour). Budget borné : au-delà de OTA_FAIL_MAIL_MAX_TRIES essais
 * avec WiFi, on abandonne explicitement (log) pour ne pas marteler le TLS. */
static void trySendPendingOtaFailMail(bool wifiOk) {
  if (!pendingOtaFailMail) return;
  if (!remoteMailNotifEnabled) {
    N3_LOGI("[OTA][MAIL] Alerte OTA en attente abandonnee: notifications desactivees.");
    pendingOtaFailMail = false;
    return;
  }
  if (!wifiOk) {
    N3_LOGI("[OTA][MAIL] Alerte OTA en attente: WiFi indisponible, report au prochain reveil.");
    return;
  }
  if (pendingOtaFailMailTries >= OTA_FAIL_MAIL_MAX_TRIES) {
    N3_LOGW("[OTA][MAIL] Alerte OTA en attente abandonnee apres %u essais.",
            static_cast<unsigned int>(pendingOtaFailMailTries));
    pendingOtaFailMail = false;
    return;
  }
  ++pendingOtaFailMailTries;
  if (sendDebugEventMail("OTA terminee (echec)", "ota-end-failed", pendingOtaFailMailExtra, N3Severity::Alert)) {
    N3_LOGI("[OTA][MAIL] Alerte OTA en attente livree.");
    pendingOtaFailMail = false;
  } else {
    N3_LOGW("[OTA][MAIL] Alerte OTA en attente: nouvel echec (%u/%u).",
            static_cast<unsigned int>(pendingOtaFailMailTries),
            static_cast<unsigned int>(OTA_FAIL_MAIL_MAX_TRIES));
  }
}

static void handlePhotoWindowTransitionMails(bool wifiOk) {
#if MAIL_NOTIFICATIONS_ENABLED && USE_DEEP_SLEEP
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    N3_LOGI("[MAIL] Transition jour/nuit ignoree: heure NTP indisponible.");
    return;
  }

  bool inWindow = (timeinfo.tm_hour >= HOUR_START && timeinfo.tm_hour < HOUR_END);
  int currentState = inWindow ? 1 : 0;

  if (lastPhotoWindowState < 0) {
    lastPhotoWindowState = currentState;
    N3_LOGI("[MAIL] Etat jour/nuit initialise: %s", inWindow ? "jour" : "nuit");
  } else if (currentState != lastPhotoWindowState) {
    if (inWindow) {
      pendingWindowMailMask |= MAIL_PENDING_MORNING;
      pendingWindowMailMask &= static_cast<uint8_t>(~MAIL_PENDING_EVENING);
      N3_LOGI("[MAIL] Transition detectee: reprise photos (matin).");
    } else {
      pendingWindowMailMask |= MAIL_PENDING_EVENING;
      pendingWindowMailMask &= static_cast<uint8_t>(~MAIL_PENDING_MORNING);
      N3_LOGI("[MAIL] Transition detectee: pause photos (soir).");
    }
    lastPhotoWindowState = currentState;
  }

  if (!wifiOk || WiFi.status() != WL_CONNECTED) {
    N3_LOGI("[MAIL] Transition en attente: WiFi indisponible.");
    return;
  }

  if (!inWindow && (pendingWindowMailMask & MAIL_PENDING_EVENING)) {
    char extra[MAIL_EXTRA_MAX_LEN];
    snprintf(extra, sizeof(extra),
             "Passage en mode nuit detecte: les photos sont suspendues entre %02d:00 et %02d:00.",
             HOUR_END, HOUR_START);
    if (sendDebugEventMail("Mode nuit active", "photo-window-night", extra, N3Severity::Diagnostic)) {
      pendingWindowMailMask &= static_cast<uint8_t>(~MAIL_PENDING_EVENING);
    }
  }

  if (inWindow && (pendingWindowMailMask & MAIL_PENDING_MORNING)) {
    char extra[MAIL_EXTRA_MAX_LEN];
    snprintf(extra, sizeof(extra),
             "Passage en mode jour detecte: la prise de photos reprend (creneau %02d:00-%02d:00).",
             HOUR_START, HOUR_END);
    if (sendDebugEventMail("Mode jour actif", "photo-window-day", extra, N3Severity::Diagnostic)) {
      pendingWindowMailMask &= static_cast<uint8_t>(~MAIL_PENDING_MORNING);
    }
  }
#else
  (void)wifiOk;
#endif
}

#if USE_DEEP_SLEEP
/* Mail une seule fois au premier vrai démarrage, après la 1re capture réussie (pas avant :
 * SMTP + framebuffer caméra en DRAM satureraient la heap). Jamais après réveil deep sleep. */
static constexpr const char* kFirstBootPrefNs = "upcam";
static constexpr const char* kFirstBootMailKey = "fb_mail";

static void trySendFirstBootMail(bool wifiOk) {
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
    N3_LOGI("[MAIL] Premier demarrage: ignore (reveil deep sleep).");
    return;
  }
  if (!wifiOk || WiFi.status() != WL_CONNECTED) {
    N3_LOGI("[MAIL] Premier demarrage: reporte (WiFi indisponible).");
    return;
  }

  if (!preferences.begin(kFirstBootPrefNs, false)) {
    N3_LOGW("[MAIL] Premier demarrage: Preferences begin a echoue.");
    return;
  }
  const bool alreadySent = preferences.getBool(kFirstBootMailKey, false);
  preferences.end();
  if (alreadySent) return;

  char extra[MAIL_EXTRA_MAX_LEN];
  snprintf(extra, sizeof(extra),
           "Premier demarrage de la camera (mail envoye apres 1re capture reussie). "
           "Raison reset: %s. Si l'envoi reussit, aucun autre mail \"premier demarrage\" "
           "(NVS %s/%s).",
           resetReasonText(esp_reset_reason()), kFirstBootPrefNs, kFirstBootMailKey);

  if (sendDebugEventMail("Premier demarrage", "first-boot", extra, N3Severity::Info)) {
    if (preferences.begin(kFirstBootPrefNs, false)) {
      preferences.putBool(kFirstBootMailKey, true);
      preferences.end();
      N3_LOGI("[MAIL] Premier demarrage: envoye, flag NVS enregistre.");
    }
  } else {
    N3_LOGW("[MAIL] Premier demarrage: echec — nouvel essai si prochain boot n'est pas un reveil deep sleep.");
  }
}
#endif

static void accumulateOtaPeriodicElapsedFromSleep(uint32_t sleepSeconds) {
#if USE_DEEP_SLEEP
  /* Cumul saturant delegue a la logique pure partagee (T4.2, semantique inchangee). */
  otaElapsedSinceLastCheckSeconds = OtaPeriodic::accumulate(
      otaElapsedSinceLastCheckSeconds, OTA_PERIODIC_INTERVAL_SECONDS, sleepSeconds);
#else
  (void)sleepSeconds;
#endif
}

/* Horodatage de capture local "Y-m-d_H-i-s". Retourne false (et out vide) si l'horloge n'est pas
 * fiable (jamais synchronisée NTP / restaurée flash, année < 2020). Non bloquant : lit l'heure
 * système courante (entretenue par la RTC à travers le deep sleep + persistance NVS). */
static bool captureStampNow(char* out, size_t outSize) {
  if (!out || outSize == 0) return false;
  out[0] = '\0';
  const time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year + 1900 < 2020) {
    N3_LOGW("[TIME] horodatage capture refuse (horloge non fiable)");
    return false;
  }
  return strftime(out, outSize, "%Y-%m-%d_%H-%M-%S", &timeinfo) > 0;
}

/* ----- capturePhoto : capture, stockage SD (file d'attente), upload direct si pas de SD -----
 *
 * Renforcement offline : la photo est d'abord PERSISTÉE sur la carte SD (numérotée). L'envoi
 * réseau n'a plus lieu ici quand la SD est disponible — c'est cameraSyncDrain() qui pousse le
 * backlog (cette photo incluse) au serveur. Si la SD est indisponible mais le WiFi présent, on
 * réalise un upload direct en mémoire (pas de file d'attente possible). Sans WiFi ni SD, l'appelant
 * n'invoque pas cette fonction. */
void capturePhoto(bool wifiOk) {
  const uint32_t capturePhotoStartMs = millis();
  logMonitoringSnapshot("capturePhoto:start");

  /* Exposition : pilotée par l'AEC MATÉRIEL de l'OV2640 (réactivé dans initializeCamera + warm-up).
   * Plus d'adjustExposure logiciel (mesure JPEG inopérante + fuite framebuffer : M1/B1, audit 2026-07-05). */

  const uint32_t captureStartMs = millis();
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    N3_LOGE("[CAM] Echec capture camera");
    logMonitoringSnapshot("capturePhoto:capture_ko");
#if USE_DEEP_SLEEP
    n3EnterRuntimeDeepSleep("Echec capture camera");
#else
    delay(1000);
    ESP.restart();
#endif
  }
  logStepDuration("capture_camera", millis() - captureStartMs, 1200);
  N3_LOGI("[MON] capture taille=%u bytes", static_cast<unsigned int>(fb->len));

  /* Horodatage de capture (porté par la file SD puis envoyé au serveur). "" si horloge inconnue. */
  char stamp[24];
  const bool stampOk = captureStampNow(stamp, sizeof(stamp));

#if USE_SD
  const uint32_t sdWriteStartMs = millis();
  if (sdAvailable) {
    /* A6 : on réserve le numéro sans l'engager ; pic_count n'est committé qu'après écriture SD
       confirmée (un échec ne brûle plus de numéro fantôme). */
    const uint32_t pictureNumber = cameraSyncPeekNextPictureNumber();
    String path = cameraSyncBuildSdPath(pictureNumber, stampOk ? stamp : nullptr);
    fs::FS& fs = SD_MMC;
    File file = fs.open(path.c_str(), FILE_WRITE);
    if (file) {
      size_t written = file.write(fb->buf, fb->len);
      file.close();
      if (written == fb->len) {
        cameraSyncCommitWrittenCount(pictureNumber);  // persistance confirmée -> on engage le numéro
        N3_LOGI("[SD] Sauvegarde locale %s (%u bytes) — en file d'attente",
                path.c_str(), static_cast<unsigned int>(written));
      } else {
        N3_LOGW("[SD] Ecriture incomplete, desactivation SD (numero non engage)");
        sdAvailable = false;
      }
    } else {
      N3_LOGW("[SD] Ouverture fichier impossible, desactivation SD (numero non engage)");
      sdAvailable = false;
    }
  }
  logStepDuration("ecriture_sd", millis() - sdWriteStartMs, 1500);
#endif

  /* Sans carte SD, pas de file d'attente : upload direct si le WiFi est là (sinon photo perdue). */
  if (!sdAvailable && wifiOk) {
    const uint32_t uploadStartMs = millis();
    /* A6/A7 : numéro réservé sans engagement ; confirmé (pic_count + curseur) uniquement si l'upload
       réussit, sinon aucun numéro brûlé et `pending` ne gonfle pas. */
    const uint32_t directSeq = cameraSyncPeekNextPictureNumber();
    char seqStr[12];
    snprintf(seqStr, sizeof(seqStr), "%lu", static_cast<unsigned long>(directSeq));
    char filename[64];
    snprintf(filename, sizeof(filename), "esp32-cam-%s-v%s.jpg", currentTargetName(), FIRMWARE_VERSION);
    char uploadUrl[128];
    snprintf(uploadUrl, sizeof(uploadUrl), "%s%s%s", SERVER_SCHEME, serverName.c_str(), serverPath.c_str());
    CameraUploadParams up = {};
    up.url = uploadUrl;
    up.apiKey = API_KEY;
    up.sigSecret = CAM_DEVICE_SIG_SECRET;
    up.syncSession = "";
    up.capturedAt = stampOk ? stamp : "";
    up.captureSeq = seqStr;
    up.reconnect = Wificonnect;
    const int code = cameraUploadJpegBuffer(up, fb->buf, fb->len, String(filename));
    logStepDuration("upload_http", millis() - uploadStartMs, 5000);
    N3_LOGI("[CAPTURE] Upload direct (sans SD) HTTP=%d seq=%lu",
            code, static_cast<unsigned long>(directSeq));
    if (code == 200 || code == 202) {
      cameraSyncMarkDirectUploadConfirmed(directSeq);  // A7 : avance pic_count + curseur ensemble
      ledBlink(1500, 1500, 2);
    }
  }

  /* Libérer le framebuffer après usage (évite use-after-free) */
  esp_camera_fb_return(fb);

#if USE_DEEP_SLEEP
  trySendFirstBootMail(wifiOk);
#endif

  logMonitoringSnapshot("capturePhoto:end");
  logStepDuration("capturePhoto_total", millis() - capturePhotoStartMs, 12000);
}

#if USE_SD
static void initSdIfEnabled() {
  const uint32_t sdInitStartMs = millis();
  if (!SD_MMC.begin()) {
    N3_LOGW("[SD] Montage echoue, continuation sans SD");
    sdAvailable = false;
  } else if (SD_MMC.cardType() == CARD_NONE) {
    N3_LOGW("[SD] Aucune carte detectee, continuation sans SD");
    sdAvailable = false;
  } else {
    sdAvailable = true;
    N3_LOGI("[SD] Carte SD OK");
  }
  logStepDuration("init_sd", millis() - sdInitStartMs, 1000);
  logMonitoringSnapshot("setup:post_sd");
}

static void runSyncDrainIfNeeded(bool wifiOk) {
  if (!wifiOk || !sdAvailable || cameraSyncPendingCount() == 0) {
    return;
  }
  CameraSyncConfig sc = {};
  const String syncUploadUrl = String(SERVER_SCHEME) + serverName + serverPath;
  sc.uploadUrl = syncUploadUrl.c_str();
  sc.startUrl = SYNC_START_URL;
  sc.finishUrl = SYNC_FINISH_URL;
  sc.apiKey = API_KEY;
  sc.sigSecret = CAM_DEVICE_SIG_SECRET;
  sc.board = REMOTE_BOARD_ID;
  sc.targetName = currentTargetName();
  sc.firmwareVersion = FIRMWARE_VERSION;
  sc.maxUploadsPerWake = SYNC_MAX_UPLOADS_PER_WAKE;
  sc.fullDrainThreshold = SYNC_FULL_DRAIN_THRESHOLD;
  sc.reconnect = Wificonnect;
  const CameraSyncResult sr = cameraSyncDrain(sc);
  if (sr.ran) {
    N3_LOGI("[SYNC] Bilan reveil: envoyees=%u echecs=%u backlog_initial=%u restant=%u",
            static_cast<unsigned int>(sr.sent), static_cast<unsigned int>(sr.failed),
            static_cast<unsigned int>(sr.pending), static_cast<unsigned int>(cameraSyncPendingCount()));
  }
}
#endif

static bool initCameraPipeline() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAM_XCLK_HZ;
  config.pixel_format = PIXFORMAT_JPEG;

  n3LogCameraSccbDiagnostics();

  const uint32_t camInitStartMs = millis();
  char camModeLabel[24] = {};
  const esp_err_t err = n3CameraInitWithFallback(&config, camModeLabel, sizeof camModeLabel);
  if (err != ESP_OK) {
    N3_LOGE("[CAM] Init camera impossible apres repli DRAM/PSRAM (0x%x)",
            static_cast<unsigned>(err));
#if USE_DEEP_SLEEP
    n3EnterRuntimeDeepSleep("Init camera impossible");
#else
    delay(1000);
    ESP.restart();
#endif
    return false;
  }
  N3_LOGI("[CAM] mode actif: %s", camModeLabel);
  logStepDuration("init_camera", millis() - camInitStartMs, 2500);
  logMonitoringSnapshot("setup:post_camera_init");

#if USE_DEEP_SLEEP
  const uint32_t warmupStartMs = millis();
  initializeCamera();
  warmupCamera();
  delay(1000);
  logStepDuration("warmup_camera", millis() - warmupStartMs, 15000);
#endif
  return true;
}

void setup() {
  const uint32_t setupStartMs = millis();
  /* Sortie immédiate sur UART0 (ROM) : utile si HardwareSerial ne va pas sur le bon port matériel. */
  esp_rom_printf("\r\n\r\n[N3][ROM] boot env=%s ver=%s 115200 8N1 UART0 RX=GPIO3 TX=GPIO1\r\n",
                 currentTargetName(), FIRMWARE_VERSION);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  /* Broches explicites UART0 AI Thinker / programmateur 6 broches (évite ambiguïtés framework). */
  Serial.begin(115200, SERIAL_8N1, 3, 1);
  Serial.setDebugOutput(CAM_DIAG_DEBUG ? true : false);
  delay(200);
#if SERIAL_BOOT_PAUSE_MS > 0
  /* Volontairement NON migre vers n3_log : notice d'amorce en CRLF explicite, emise
     avant que le moniteur PC ne soit ouvert (le prefixe horodate/uptime et le \n seul
     de n3_log casseraient ce message d'attente brut). */
  Serial.printf("[BOOT] SERIAL_BOOT_PAUSE_MS=%u — ouvrez le moniteur maintenant\r\n",
                static_cast<unsigned>(SERIAL_BOOT_PAUSE_MS));
  delay(SERIAL_BOOT_PAUSE_MS);
#endif
  N3_LOGI("[BOOT] uploadphotosserver env=%s version=%s", currentTargetName(), FIRMWARE_VERSION);
  N3_LOGI("[BOOT] reset=%s wakeup=%s",
          resetReasonText(esp_reset_reason()),
          wakeupCauseText(esp_sleep_get_wakeup_cause()));
  n3LogHardwareDiagnostics();
  logMonitoringSnapshot("setup:start");
  /* Aligne otadata sur la partition en cours (comme n3pp/msp) — evite OTA « Verify Bin Header Failed »
   * apres flash USB sur un slot different du slot de boot. */
  n3OtaSyncBootPartition();
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LOW);
  ledBlink(100, 100, 2);

#if USE_DEEP_SLEEP
  // Réveil timer CAM = horloge perdue au deep sleep -> recharger l'epoch NVS
  // (contrat T1.3 : loadNvsOnTimerWake=true, comportement historique iso).
  n3PrintWakeupReason(preferences, rtc, /*loadNvsOnTimerWake=*/true);
#endif

  const uint32_t wifiStartMs = millis();
  wifiRadioResetForWake();
  bool wifiOk = Wificonnect();
  #ifdef SMTP_DEST
  remoteMailRecipient = SMTP_DEST;
  #else
  remoteMailRecipient = "";
  #endif
  remoteMailNotifEnabled = MAIL_NOTIFICATIONS_ENABLED;
  runtimeSleepSeconds = TIME_TO_SLEEP;
  forceWakeupActiveThisBoot = false;
  resetModeActiveThisBoot = false;

#if USE_DEEP_SLEEP
  /* Horloge fiable AVANT POST version / HMAC (cle 103, X-Sig-*). Sans NTP, l'epoch NVS
   * peut etre hors fenetre SIG_VALID_WINDOW (300 s) -> HTTP 401 « Signature invalide ». */
  {
    const uint32_t ntpStartMs = millis();
    n3CamSyncClock(preferences, rtc, wifiOk);
    logStepDuration("sync_ntp", millis() - ntpStartMs, NTP_SYNC_TIMEOUT_MS + 500);
    logMonitoringSnapshot("setup:post_ntp");
  }
#endif

  if (wifiOk && WiFi.status() == WL_CONNECTED) {
    CameraRemoteConfig remoteCfg = {};
    unsigned int remoteHttpCode = 0;
    if (cameraRemoteFetchConfig(remoteCfg, &remoteHttpCode)) {
      if (remoteCfg.mail.length() > 0) {
        remoteMailRecipient = remoteCfg.mail;
      }
      remoteMailNotifEnabled = remoteCfg.mailNotif;
      remoteNotifMode = remoteCfg.notifMode;  /* Phase 3 : mode gradue (cle 103) */
      runtimeSleepSeconds = remoteCfg.sleepTimeSeconds;

#if USE_DEEP_SLEEP
      if (remoteCfg.forceWakeUp) {
        forceWakeupActiveThisBoot = (lastRemoteForceWakeupState == 0);
        lastRemoteForceWakeupState = 1;
      } else {
        forceWakeupActiveThisBoot = false;
        lastRemoteForceWakeupState = 0;
      }

      if (remoteCfg.resetMode) {
        resetModeActiveThisBoot = (lastRemoteResetModeState == 0);
        lastRemoteResetModeState = 1;
      } else {
        resetModeActiveThisBoot = false;
        lastRemoteResetModeState = 0;
      }
#else
      forceWakeupActiveThisBoot = remoteCfg.forceWakeUp;
      resetModeActiveThisBoot = remoteCfg.resetMode;
#endif

      N3_LOGI("[REMOTE] cfg ok mailNotif=%d forceWake=%d sleep=%lu resetMode=%d",
              remoteMailNotifEnabled ? 1 : 0,
              forceWakeupActiveThisBoot ? 1 : 0,
              static_cast<unsigned long>(runtimeSleepSeconds),
              resetModeActiveThisBoot ? 1 : 0);
    } else {
      N3_LOGI("[REMOTE] cfg indisponible (HTTP=%u), valeurs locales conservees.", remoteHttpCode);
    }

    int versionPostCode = cameraRemotePostFirmwareVersion(currentTargetName());
    N3_LOGI("[REMOTE] post version HTTP=%d", versionPostCode);
    /* Phase 3 arbitrage : proxy « serveur OK » = GET config OK + POST version 2xx. */
    serverExchangeOk = (remoteHttpCode == 200) && versionPostCode >= 200 && versionPostCode < 300;
    N3_LOGI("[REMOTE] echange serveur %s (failover mails %s)",
            serverExchangeOk ? "OK" : "KO",
            serverExchangeOk ? "inactif" : "actif: P1/P2 only");
  }
  logStepDuration("connexion_wifi", millis() - wifiStartMs, WIFI_CONNECT_TIMEOUT_MS + 1500);
  logMonitoringSnapshot("setup:post_wifi");
  ledBlink(100, 100, 1);

  /* OTA distant : logs explicites + verification toutes les 2 heures de cycles */
#if USE_DEEP_SLEEP
  otaUpdateStartedThisBoot = false;
  const uint32_t remainingBeforeCheck = OtaPeriodic::remainingSeconds(
      otaElapsedSinceLastCheckSeconds, OTA_PERIODIC_INTERVAL_SECONDS);
  N3_LOGI("[OTA] cible=%s version_local=%s elapsed=%lu/%lu s metadata=%s",
          currentTargetName(),
          FIRMWARE_VERSION,
          static_cast<unsigned long>(otaElapsedSinceLastCheckSeconds),
          static_cast<unsigned long>(OTA_PERIODIC_INTERVAL_SECONDS),
          OTA_METADATA_URL);
  if (remainingBeforeCheck == 0) {
    N3_LOGI("[OTA] verification 2h declenchee");
    N3OtaConfig otaCfg = {
      OTA_METADATA_URL,
      FIRMWARE_VERSION,
      -1,
      currentTargetName(),
      otaMailStartCallback,
      otaMailEndCallback,
      nullptr
    };
    n3OtaCheck(otaCfg);
    otaElapsedSinceLastCheckSeconds = 0;
    N3_LOGI("[OTA] verification 2h terminee");
  } else {
    N3_LOGI("[OTA] verification 2h sautee, restante=%lu s",
            static_cast<unsigned long>(remainingBeforeCheck));
  }
#endif

  handlePhotoWindowTransitionMails(wifiOk);
  trySendPendingOtaFailMail(wifiOk);
  logMonitoringSnapshot("setup:post_window_mail");

  const bool inWindow = inPhotoWindow() || forceWakeupActiveThisBoot;
  if (forceWakeupActiveThisBoot) {
    N3_LOGI("[REMOTE] forceWakeUp actif: capture autorisee hors creneau.");
  }

#if USE_SD
  initSdIfEnabled();
#endif

  const bool canPersist = wifiOk || sdAvailable;
  const bool needsCapture = inWindow && canPersist;

  if (needsCapture) {
    initCameraPipeline();
    if (!wifiOk) {
      N3_LOGI("[CAPTURE] WiFi indisponible: sauvegarde SD locale, upload differe au prochain reveil connecte.");
    }
    capturePhoto(wifiOk);
  } else if (inWindow && !canPersist) {
    N3_LOGW("[CAPTURE] Photo ignoree: pas de WiFi et pas de SD disponible");
  } else if (!inWindow) {
    N3_LOGI("[MON] hors creneau photo, camera non initialisee");
  }

#if USE_SD
  runSyncDrainIfNeeded(wifiOk);
#endif

  if (resetModeActiveThisBoot) {
    N3_LOGI("[REMOTE] resetMode actif: redemarrage immediat.");
    delay(200);
    ESP.restart();
  }

  ledBlink(100, 100, 1);
  logMonitoringSnapshot("setup:end");
  logStepDuration("setup_total", millis() - setupStartMs, 30000);
}

void loop() {
#if USE_DEEP_SLEEP
  N3_LOGI("[LOOP] uploadphotosserver env=%s version=%s", currentTargetName(), FIRMWARE_VERSION);
  logMonitoringSnapshot("loop:before_sleep");
  /* A9 : la cadence OTA 2h tient compte du temps ÉVEILLÉ de ce réveil (millis()) EN PLUS du sommeil
     à venir, pour éliminer la légère dérive (chaque réveil ajoute ~10-30 s auparavant non comptés). */
  const uint32_t awakeSeconds = static_cast<uint32_t>(millis() / 1000UL);
  accumulateOtaPeriodicElapsedFromSleep(awakeSeconds + runtimeSleepSeconds);
  N3_LOGI("[OTA] cumul avant reveil: %lu/%lu s",
          static_cast<unsigned long>(otaElapsedSinceLastCheckSeconds),
          static_cast<unsigned long>(OTA_PERIODIC_INTERVAL_SECONDS));
  N3_LOGI("[SLEEP] Entree en deep sleep (%u s)", static_cast<unsigned int>(runtimeSleepSeconds));
  delay(1000);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)runtimeSleepSeconds * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
#endif
}
