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
#include "n3_http.h"
#include "n3_time.h"
#include "n3_wifi.h"
#include "n3_ota.h"
#include "n3_mail.h"
#include "camera_remote.h"
#include <esp_sleep.h>
#include <esp_system.h>

#if USE_SD
#include <EEPROM.h>
#include "FS.h"
#include "SD_MMC.h"
#endif

#if USE_DEEP_SLEEP
#include <ESP32Time.h>
#include <Preferences.h>
#endif

static const char* ntpServer = NTP_SERVER;
static const long gmtOffset_sec = GMT_OFFSET_SEC;
static const int daylightOffset_sec = DAYLIGHT_OFFSET_SEC;

String serverName = SERVER_NAME;
String serverPath = SERVER_PATH;
const int serverPort = SERVER_PORT;

WiFiClient client;
String Wifiactif;

#if USE_DEEP_SLEEP
static constexpr uint32_t OTA_PERIODIC_INTERVAL_SECONDS = 2UL * 60UL * 60UL;
RTC_DATA_ATTR static uint32_t otaElapsedSinceLastCheckSeconds = OTA_PERIODIC_INTERVAL_SECONDS;
RTC_DATA_ATTR static int lastPhotoWindowState = -1;  /* -1: inconnu, 0: nuit, 1: jour */
RTC_DATA_ATTR static uint8_t pendingWindowMailMask = 0;
RTC_DATA_ATTR static int lastRemoteForceWakeupState = 0;
RTC_DATA_ATTR static int lastRemoteResetModeState = 0;
#endif

static bool otaUpdateStartedThisBoot = false;
static char otaRemoteVersion[16] = "";
static char otaFirmwareUrl[160] = "";
static bool remoteMailNotifEnabled = MAIL_NOTIFICATIONS_ENABLED;
static String remoteMailRecipient = "";
static uint32_t runtimeSleepSeconds = TIME_TO_SLEEP;
static bool forceWakeupActiveThisBoot = false;
static bool resetModeActiveThisBoot = false;

#if USE_SD
int pictureNumber = 0;
bool sdAvailable = false;  /* true seulement si SD montée au boot */
#endif

#if USE_DEEP_SLEEP
Preferences preferences;
ESP32Time rtc;
#endif

bool Wificonnect();
String sendPhoto();
void ledBlink(int onMs, int offMs, int count);
static int parseHttpStatusCode(const String& statusLine);
static const char* currentTargetName();
static const char* wakeupCauseText(esp_sleep_wakeup_cause_t cause);
static const char* resetReasonText(esp_reset_reason_t reason);
static bool getLocalTimeString(char* out, size_t outSize);
static void logMonitoringSnapshot(const char* stage);
static void logStepDuration(const char* step, uint32_t durationMs, uint32_t warnMs);
static bool sendDebugEventMail(const char* subjectEvent, const char* eventName, const char* extraInfo);
static void otaMailStartCallback(const char* currentVersion, const char* remoteVersion, const char* firmwareUrl, void* userData);
static void otaMailEndCallback(bool success, const char* details, void* userData);
static void handlePhotoWindowTransitionMails(bool wifiOk);
static void accumulateOtaPeriodicElapsedFromSleep(uint32_t sleepSeconds);
#if USE_DEEP_SLEEP
static void trySendFirstBootMail(bool wifiOk);
#endif

#if USE_DEEP_SLEEP
void warmupCamera();
void adjustExposure();
void initializeCamera();
#endif

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
bool Wificonnect() {
  /* WIFI_LIST est WifiCredential { ssid, password } ; même layout que N3WifiNetwork { ssid, pass }. */
  const N3WifiNetwork* nets = reinterpret_cast<const N3WifiNetwork*>(WIFI_LIST);
  N3WifiConfig cfg = {};
  cfg.networks = nets;
  cfg.networkCount = WIFI_COUNT;
  cfg.timeoutMs = WIFI_CONNECT_TIMEOUT_MS;
  cfg.delayBetweenMs = WIFI_DELAY_BETWEEN_MS;
  cfg.preScanDelayMs = WIFI_PRE_SCAN_DELAY_MS;
  cfg.scanMax = WIFI_SCAN_MAX;
  cfg.onSuccess = [](const char*) { ledBlink(500, 500, 1); };
  return n3WifiConnect(cfg, &Wifiactif);
}

static int parseHttpStatusCode(const String& statusLine) {
  int code = 0;
  if (sscanf(statusLine.c_str(), "HTTP/%*d.%*d %d", &code) == 1) return code;
  return 0;
}

static const char* currentTargetName() {
#if defined(TARGET_MSP1)
  return "msp1";
#elif defined(TARGET_N3PP)
  return "n3pp";
#elif defined(TARGET_FFP3)
  return "ffp3";
#else
  return "unknown";
#endif
}

static const char* wakeupCauseText(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0: return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1: return "ext1";
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP: return "ulp";
    default: return "non_deep_sleep";
  }
}

static const char* resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN: return "unknown";
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "ext";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "other";
  }
}

static bool getLocalTimeString(char* out, size_t outSize) {
  if (!out || outSize == 0) return false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    snprintf(out, outSize, "NTP indisponible");
    return false;
  }
  strftime(out, outSize, "%Y-%m-%d %H:%M:%S", &timeinfo);
  return true;
}

static void logStepDuration(const char* step, uint32_t durationMs, uint32_t warnMs) {
  Serial.printf("[MON] %s duree=%lu ms\n", step ? step : "step", static_cast<unsigned long>(durationMs));
  if (warnMs > 0 && durationMs > warnMs) {
    Serial.printf("[MON][WARN] %s lent (%lu ms > %lu ms)\n",
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
  Serial.printf("[MON] stage=%s up=%lu s wifi=%s rssi=%d ip=%s heap=%lu min_heap=%lu sd=%s\n",
                stage ? stage : "unknown",
                static_cast<unsigned long>(millis() / 1000UL),
                wifiState,
                wifiRssi,
                ipStr.c_str(),
                static_cast<unsigned long>(freeHeap),
                static_cast<unsigned long>(minHeap),
                sdState);
#else
  Serial.printf("[MON] stage=%s up=%lu s wifi=%s rssi=%d ip=%s heap=%lu min_heap=%lu\n",
                stage ? stage : "unknown",
                static_cast<unsigned long>(millis() / 1000UL),
                wifiState,
                wifiRssi,
                ipStr.c_str(),
                static_cast<unsigned long>(freeHeap),
                static_cast<unsigned long>(minHeap));
#endif
  if (freeHeap < MONITORING_HEAP_WARN_BYTES) {
    Serial.printf("[MON][WARN] heap faible: %lu < %u bytes\n",
                  static_cast<unsigned long>(freeHeap),
                  static_cast<unsigned int>(MONITORING_HEAP_WARN_BYTES));
  }
  if (wifiConnected && wifiRssi < -80) {
    Serial.printf("[MON][WARN] signal WiFi faible: RSSI=%d dBm\n", wifiRssi);
  }
}

static bool sendDebugEventMail(const char* subjectEvent, const char* eventName, const char* extraInfo) {
#if MAIL_NOTIFICATIONS_ENABLED && defined(SMTP_HOST_ADDR) && defined(SMTP_PORT_NUM) && defined(SMTP_EMAIL) && defined(SMTP_PASSWORD) && defined(SMTP_DEST)
  if (!remoteMailNotifEnabled) {
    Serial.println("[MAIL] Notifications desactivees par configuration distante.");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MAIL] Envoi annule: WiFi indisponible.");
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
  snprintf(subject, sizeof(subject), "[uploadphotosserver][%s] %s", currentTargetName(), subjectEvent);

  char localTime[32];
  getLocalTimeString(localTime, sizeof(localTime));
  String ipStr = WiFi.localIP().toString();
  int wifiRssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

  N3MailDebugInfo dbgInfo = {};
  dbgInfo.projectName = "uploadphotosserver";
  dbgInfo.targetName = currentTargetName();
  dbgInfo.firmwareVersion = FIRMWARE_VERSION;
  dbgInfo.eventName = eventName;
  dbgInfo.localTime = localTime;
  dbgInfo.wakeupReason = wakeupCauseText(esp_sleep_get_wakeup_cause());
  dbgInfo.resetReason = resetReasonText(esp_reset_reason());
  dbgInfo.wifiSsid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID().c_str() : "(deconnecte)";
  dbgInfo.wifiIp = ipStr.c_str();
  dbgInfo.wifiRssi = wifiRssi;
  dbgInfo.uptimeSeconds = millis() / 1000UL;
  dbgInfo.freeHeap = ESP.getFreeHeap();
  dbgInfo.minFreeHeap = ESP.getMinFreeHeap();
  dbgInfo.extraInfo = extraInfo;

  char body[MAIL_BODY_MAX_LEN];
  if (!n3MailBuildDebugBody(dbgInfo, body, sizeof(body))) {
    Serial.println("[MAIL] Echec generation corps mail.");
    return false;
  }

  String smtpError;
  bool ok = n3MailSendText(smtpCfg, subject, body, &smtpError);
  if (!ok) {
    Serial.printf("[MAIL] Echec envoi: %s\n", smtpError.c_str());
  } else {
    Serial.printf("[MAIL] Mail envoye: %s\n", subject);
  }
  return ok;
#else
  (void)subjectEvent;
  (void)eventName;
  (void)extraInfo;
  Serial.println("[MAIL] SMTP non configure dans credentials.h, notification ignoree.");
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

  char extra[MAIL_EXTRA_MAX_LEN];
  snprintf(extra, sizeof(extra),
           "OTA demarrage avant telechargement.\nVersion locale: %s\nVersion distante: %s\nURL firmware: %s\nMetadata: %s",
           currentVersion ? currentVersion : FIRMWARE_VERSION, otaRemoteVersion, otaFirmwareUrl, OTA_METADATA_URL);
  sendDebugEventMail("OTA demarrage", "ota-start", extra);
}

static void otaMailEndCallback(bool success, const char* details, void* userData) {
  (void)userData;
  if (!otaUpdateStartedThisBoot) return;

  char extra[MAIL_EXTRA_MAX_LEN];
  snprintf(extra, sizeof(extra),
           "OTA fin de sequence.\nResultat: %s\nVersion distante: %s\nURL firmware: %s\nDetails: %s",
           success ? "succes" : "echec", otaRemoteVersion, otaFirmwareUrl, details ? details : "n/a");
  sendDebugEventMail(success ? "OTA terminee (succes)" : "OTA terminee (echec)",
                     success ? "ota-end-success" : "ota-end-failed",
                     extra);
}

static void handlePhotoWindowTransitionMails(bool wifiOk) {
#if MAIL_NOTIFICATIONS_ENABLED && USE_DEEP_SLEEP
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[MAIL] Transition jour/nuit ignoree: heure NTP indisponible.");
    return;
  }

  bool inWindow = (timeinfo.tm_hour >= HOUR_START && timeinfo.tm_hour < HOUR_END);
  int currentState = inWindow ? 1 : 0;

  if (lastPhotoWindowState < 0) {
    lastPhotoWindowState = currentState;
    Serial.printf("[MAIL] Etat jour/nuit initialise: %s\n", inWindow ? "jour" : "nuit");
  } else if (currentState != lastPhotoWindowState) {
    if (inWindow) {
      pendingWindowMailMask |= MAIL_PENDING_MORNING;
      pendingWindowMailMask &= static_cast<uint8_t>(~MAIL_PENDING_EVENING);
      Serial.println("[MAIL] Transition detectee: reprise photos (matin).");
    } else {
      pendingWindowMailMask |= MAIL_PENDING_EVENING;
      pendingWindowMailMask &= static_cast<uint8_t>(~MAIL_PENDING_MORNING);
      Serial.println("[MAIL] Transition detectee: pause photos (soir).");
    }
    lastPhotoWindowState = currentState;
  }

  if (!wifiOk || WiFi.status() != WL_CONNECTED) {
    Serial.println("[MAIL] Transition en attente: WiFi indisponible.");
    return;
  }

  if (!inWindow && (pendingWindowMailMask & MAIL_PENDING_EVENING)) {
    char extra[MAIL_EXTRA_MAX_LEN];
    snprintf(extra, sizeof(extra),
             "Passage en mode nuit detecte: les photos sont suspendues entre %02d:00 et %02d:00.",
             HOUR_END, HOUR_START);
    if (sendDebugEventMail("Mode nuit active", "photo-window-night", extra)) {
      pendingWindowMailMask &= static_cast<uint8_t>(~MAIL_PENDING_EVENING);
    }
  }

  if (inWindow && (pendingWindowMailMask & MAIL_PENDING_MORNING)) {
    char extra[MAIL_EXTRA_MAX_LEN];
    snprintf(extra, sizeof(extra),
             "Passage en mode jour detecte: la prise de photos reprend (creneau %02d:00-%02d:00).",
             HOUR_START, HOUR_END);
    if (sendDebugEventMail("Mode jour actif", "photo-window-day", extra)) {
      pendingWindowMailMask &= static_cast<uint8_t>(~MAIL_PENDING_MORNING);
    }
  }
#else
  (void)wifiOk;
#endif
}

#if USE_DEEP_SLEEP
/* Mail une seule fois au premier vrai démarrage ; jamais après réveil deep sleep (ESP_RST_DEEPSLEEP). */
static constexpr const char* kFirstBootPrefNs = "upcam";
static constexpr const char* kFirstBootMailKey = "fb_mail";

static void trySendFirstBootMail(bool wifiOk) {
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
    Serial.println("[MAIL] Premier demarrage: ignore (reveil deep sleep).");
    return;
  }
  if (!wifiOk || WiFi.status() != WL_CONNECTED) {
    Serial.println("[MAIL] Premier demarrage: reporte (WiFi indisponible).");
    return;
  }

  if (!preferences.begin(kFirstBootPrefNs, false)) {
    Serial.println("[MAIL] Premier demarrage: Preferences begin a echoue.");
    return;
  }
  const bool alreadySent = preferences.getBool(kFirstBootMailKey, false);
  preferences.end();
  if (alreadySent) return;

  char extra[MAIL_EXTRA_MAX_LEN];
  snprintf(extra, sizeof(extra),
           "Premier demarrage de la camera (hors cycle deep sleep). Raison reset: %s. "
           "Si l'envoi reussit, aucun autre mail \"premier demarrage\" ne sera envoye (NVS %s/%s).",
           resetReasonText(esp_reset_reason()), kFirstBootPrefNs, kFirstBootMailKey);

  if (sendDebugEventMail("Premier demarrage", "first-boot", extra)) {
    if (preferences.begin(kFirstBootPrefNs, false)) {
      preferences.putBool(kFirstBootMailKey, true);
      preferences.end();
      Serial.println("[MAIL] Premier demarrage: envoye, flag NVS enregistre.");
    }
  } else {
    Serial.println("[MAIL] Premier demarrage: echec — nouvel essai si prochain boot n'est pas un reveil deep sleep.");
  }
}
#endif

static void accumulateOtaPeriodicElapsedFromSleep(uint32_t sleepSeconds) {
#if USE_DEEP_SLEEP
  if (sleepSeconds == 0) return;
  if (otaElapsedSinceLastCheckSeconds >= OTA_PERIODIC_INTERVAL_SECONDS) return;

  const uint32_t remaining = OTA_PERIODIC_INTERVAL_SECONDS - otaElapsedSinceLastCheckSeconds;
  otaElapsedSinceLastCheckSeconds += (sleepSeconds >= remaining) ? remaining : sleepSeconds;
#else
  (void)sleepSeconds;
#endif
}

#if USE_DEEP_SLEEP
void adjustExposure() {
  sensor_t* s = esp_camera_sensor_get();
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb && s) {
    long brightness = 0;
    for (size_t i = 0; i < fb->len; i++) brightness += fb->buf[i];
    brightness /= (fb->len ? (long)fb->len : 1);
    if (brightness > 200)
      s->set_aec_value(s, s->status.aec_value - 50);
    else if (brightness < 50)
      s->set_aec_value(s, s->status.aec_value + 50);
    esp_camera_fb_return(fb);
  }
}

void warmupCamera() {
  for (int i = 0; i < 3; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(1000);
  }
}

void initializeCamera() {
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_exposure_ctrl(s, 0);
    s->set_aec_value(s, 300);
    s->set_gain_ctrl(s, 0);
    s->set_agc_gain(s, 0);
    delay(1000);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_awb_gain(s, 1);
    delay(10000);
  }
}
#endif

/* ----- sendPhoto : capture, optionnel SD, envoi HTTP, puis esp_camera_fb_return ----- */
String sendPhoto() {
  const uint32_t sendPhotoStartMs = millis();
  String getAll, getBody;
  logMonitoringSnapshot("sendPhoto:start");

#if USE_DEEP_SLEEP
  adjustExposure();
#endif

  const uint32_t captureStartMs = millis();
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM][ERROR] Echec capture camera");
    logMonitoringSnapshot("sendPhoto:capture_ko");
    delay(1000);
    ESP.restart();
  }
  logStepDuration("capture_camera", millis() - captureStartMs, 1200);
  Serial.printf("[MON] capture taille=%u bytes\n", static_cast<unsigned int>(fb->len));

#if USE_SD
  const uint32_t sdWriteStartMs = millis();
  if (sdAvailable) {
    EEPROM.begin(EEPROM_SIZE);
    uint32_t persistedCount = 0;
    EEPROM.get(0, persistedCount);
    persistedCount++;
    pictureNumber = static_cast<int>(persistedCount);
    String path = "/picture" + String(pictureNumber) + ".jpg";
    fs::FS& fs = SD_MMC;
    File file = fs.open(path.c_str(), FILE_WRITE);
    if (file) {
      size_t written = file.write(fb->buf, fb->len);
      file.close();
      if (written == fb->len) {
        EEPROM.put(0, persistedCount);
        EEPROM.commit();
      } else {
        Serial.println("[SD][WARN] Ecriture incomplete, desactivation SD");
        sdAvailable = false;
      }
    } else {
      Serial.println("[SD][WARN] Ouverture fichier impossible, desactivation SD");
      sdAvailable = false;
    }
  }
  logStepDuration("ecriture_sd", millis() - sdWriteStartMs, 1500);
  /* Ne pas appeler esp_camera_fb_return(fb) ici : on envoie encore HTTP avec fb->buf */
#endif

  bool connected = false;
  const uint32_t connectStartMs = millis();
  for (int attempt = 1; attempt <= UPLOAD_CONNECT_RETRIES; attempt++) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI][WARN] Deconnecte, tentative de reconnexion");
      Wificonnect();
    }
    Serial.printf("[SERVER][CONNECT] Tentative %d/%d host=%s:%d\n",
                  attempt,
                  UPLOAD_CONNECT_RETRIES,
                  serverName.c_str(),
                  serverPort);
    if (client.connect(serverName.c_str(), serverPort)) {
      connected = true;
      break;
    }
    delay(UPLOAD_RETRY_DELAY_MS);
  }
  if (!connected) {
    logStepDuration("connexion_http", millis() - connectStartMs, 4000);
    getBody = "[SERVER][CONNECT][ERROR] Connexion a " + serverName + " echouee apres retries.";
    Serial.println(getBody);
    esp_camera_fb_return(fb);
    logMonitoringSnapshot("sendPhoto:connect_ko");
    logStepDuration("sendPhoto_total", millis() - sendPhotoStartMs, 12000);
    return getBody;
  }
  logStepDuration("connexion_http", millis() - connectStartMs, 4000);

  Serial.println("[SERVER][CONNECT] Connexion serveur OK");
  String photoFilename = "esp32-cam-" + String(currentTargetName()) + "-v" + String(FIRMWARE_VERSION) + ".jpg";
  String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"" + photoFilename + "\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--RandomNerdTutorials--\r\n";
  uint32_t imageLen = fb->len;
  uint32_t extraLen = head.length() + tail.length();
  uint32_t totalLen = imageLen + extraLen;

  client.println("POST " + serverPath + " HTTP/1.1");
  client.println("Host: " + serverName);
  client.println("Content-Length: " + String(totalLen));
  client.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
  client.println("X-Api-Key: " + String(API_KEY));
  Serial.printf("[SERVER][POST] %s payload=%u bytes\n",
                serverPath.c_str(),
                static_cast<unsigned int>(imageLen));
  client.println();
  client.print(head);

  const size_t chunk = UPLOAD_CHUNK_SIZE;
  const uint32_t uploadStartMs = millis();
  size_t sent = 0;
  while (sent < fb->len) {
    size_t toSend = (fb->len - sent) > chunk ? chunk : (fb->len - sent);
    client.write(fb->buf + sent, toSend);
    sent += toSend;
  }
  client.print(tail);
  logStepDuration("upload_http", millis() - uploadStartMs, 5000);
  Serial.printf("[MON] upload envoye=%u bytes payload=%u bytes\n",
                static_cast<unsigned int>(sent),
                static_cast<unsigned int>(imageLen));

  /* Libérer le framebuffer après envoi complet (évite use-after-free) */
  esp_camera_fb_return(fb);

  int timeoutTimer = HTTP_RESPONSE_TIMEOUT_MS;
  uint32_t startTimer = millis();
  const uint32_t responseStartMs = startTimer;
  boolean state = false;
  String statusLine;
  while ((startTimer + timeoutTimer) > millis()) {
    Serial.print(".");
    delay(100);
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        if (getAll.length() > 0 && statusLine.length() == 0) statusLine = getAll;
        if (getAll.length() == 0) state = true;
        getAll = "";
      } else if (c != '\r') getAll += String(c);
      if (state) getBody += String(c);
      startTimer = millis();
    }
    if (getBody.length() > 0) break;
  }
  logStepDuration("attente_reponse_http", millis() - responseStartMs, HTTP_RESPONSE_TIMEOUT_MS);
  Serial.println();
  client.stop();
  if (statusLine.length() > 0) {
    Serial.println("[SERVER][RESP] " + statusLine);
    int statusCode = parseHttpStatusCode(statusLine);
    if (statusCode != 200) {
      Serial.printf("[SERVER][RESP][WARN] Upload non confirme, HTTP=%d\n", statusCode);
    }
  }
  Serial.println("[SERVER][BODY] " + getBody);
  Serial.printf("[MON] reponse_len=%u chars\n", static_cast<unsigned int>(getBody.length()));
  ledBlink(1500, 1500, 2);
  logMonitoringSnapshot("sendPhoto:end");
  logStepDuration("sendPhoto_total", millis() - sendPhotoStartMs, 12000);
  return getBody;
}

/* ----- Créneau horaire 6h–22h ----- */
static bool inPhotoWindow() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return true; /* Échec NTP : autoriser la photo par défaut (comportement fail-open) */
  char hourBuf[3];
  strftime(hourBuf, sizeof(hourBuf), "%H", &timeinfo);
  int h = atoi(hourBuf);
  return (h >= HOUR_START && h < HOUR_END);
}

void setup() {
  const uint32_t setupStartMs = millis();
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(200);
  Serial.printf("[BOOT] uploadphotosserver env=%s version=%s\n", currentTargetName(), FIRMWARE_VERSION);
  Serial.printf("[BOOT] reset=%s wakeup=%s\n",
                resetReasonText(esp_reset_reason()),
                wakeupCauseText(esp_sleep_get_wakeup_cause()));
  logMonitoringSnapshot("setup:start");
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LOW);
  ledBlink(100, 100, 2);

#if USE_DEEP_SLEEP
  n3PrintWakeupReason(preferences, rtc);
#endif

  const uint32_t wifiStartMs = millis();
  WiFi.mode(WIFI_STA);
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

  if (wifiOk && WiFi.status() == WL_CONNECTED) {
    CameraRemoteConfig remoteCfg = {};
    unsigned int remoteHttpCode = 0;
    if (cameraRemoteFetchConfig(remoteCfg, &remoteHttpCode)) {
      if (remoteCfg.mail.length() > 0) {
        remoteMailRecipient = remoteCfg.mail;
      }
      remoteMailNotifEnabled = remoteCfg.mailNotif;
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

      Serial.printf("[REMOTE] cfg ok mailNotif=%d forceWake=%d sleep=%lu resetMode=%d\n",
                    remoteMailNotifEnabled ? 1 : 0,
                    forceWakeupActiveThisBoot ? 1 : 0,
                    static_cast<unsigned long>(runtimeSleepSeconds),
                    resetModeActiveThisBoot ? 1 : 0);
    } else {
      Serial.printf("[REMOTE] cfg indisponible (HTTP=%u), valeurs locales conservees.\n", remoteHttpCode);
    }

    int versionPostCode = cameraRemotePostFirmwareVersion(currentTargetName());
    Serial.printf("[REMOTE] post version HTTP=%d\n", versionPostCode);
  }
  logStepDuration("connexion_wifi", millis() - wifiStartMs, WIFI_CONNECT_TIMEOUT_MS + 1500);
  logMonitoringSnapshot("setup:post_wifi");
  ledBlink(100, 100, 1);

#if USE_SD
  const uint32_t sdInitStartMs = millis();
  if (!SD_MMC.begin()) {
    Serial.println("[SD][WARN] Montage echoue, continuation sans SD");
    sdAvailable = false;
  } else if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("[SD][WARN] Aucune carte detectee, continuation sans SD");
    sdAvailable = false;
  } else {
    sdAvailable = true;
    Serial.println("[SD] Carte SD OK");
  }
  logStepDuration("init_sd", millis() - sdInitStartMs, 1000);
  logMonitoringSnapshot("setup:post_sd");
#endif

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

  /* Qualité caméra identique pour toutes les cibles (alignée msp1) */
  if (psramFound()) {
    config.frame_size = FRAMESIZE_SXGA;
    config.jpeg_quality = 4;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 4;
    config.fb_count = 1;
  }

  const uint32_t camInitStartMs = millis();
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM][ERROR] Init camera echouee, err=0x%x\n", err);
    delay(1000);
    ESP.restart();
  }
  logStepDuration("init_camera", millis() - camInitStartMs, 2500);
  logMonitoringSnapshot("setup:post_camera_init");

#if USE_DEEP_SLEEP
  const uint32_t warmupStartMs = millis();
  initializeCamera();
  warmupCamera();
  delay(1000);
  logStepDuration("warmup_camera", millis() - warmupStartMs, 15000);
#endif

  const uint32_t ntpStartMs = millis();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

#if USE_DEEP_SLEEP
  {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      rtc.setTime(timeinfo.tm_sec, timeinfo.tm_min, timeinfo.tm_hour,
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      n3TimeSaveToFlash(rtc, preferences);
    }
  }
  logStepDuration("sync_ntp", millis() - ntpStartMs, 3000);
  logMonitoringSnapshot("setup:post_ntp");
#endif

#if USE_DEEP_SLEEP
  trySendFirstBootMail(wifiOk);
#endif

  /* OTA distant : logs explicites + verification toutes les 2 heures de cycles */
#if USE_DEEP_SLEEP
  otaUpdateStartedThisBoot = false;
  const uint32_t remainingBeforeCheck =
      (otaElapsedSinceLastCheckSeconds >= OTA_PERIODIC_INTERVAL_SECONDS)
          ? 0
          : (OTA_PERIODIC_INTERVAL_SECONDS - otaElapsedSinceLastCheckSeconds);
  Serial.printf("[OTA] cible=%s version_local=%s elapsed=%lu/%lu s metadata=%s\n",
                currentTargetName(),
                FIRMWARE_VERSION,
                static_cast<unsigned long>(otaElapsedSinceLastCheckSeconds),
                static_cast<unsigned long>(OTA_PERIODIC_INTERVAL_SECONDS),
                OTA_METADATA_URL);
  if (remainingBeforeCheck == 0) {
    Serial.println("[OTA] verification 2h declenchee");
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
    Serial.println("[OTA] verification 2h terminee");
  } else {
    Serial.printf("[OTA] verification 2h sautee, restante=%lu s\n",
                  static_cast<unsigned long>(remainingBeforeCheck));
  }
#endif

  handlePhotoWindowTransitionMails(wifiOk);
  logMonitoringSnapshot("setup:post_window_mail");

  /* Une photo en setup si dans le créneau, ou forceWakeUp distant one-shot. */
  bool shouldCapture = inPhotoWindow() || forceWakeupActiveThisBoot;
  if (forceWakeupActiveThisBoot) {
    Serial.println("[REMOTE] forceWakeUp actif: capture autorisee hors creneau.");
  }
  if (shouldCapture) {
    if (!wifiOk && !sdAvailable) {
      Serial.println("[CAPTURE][WARN] Photo ignoree: pas de WiFi et pas de SD disponible");
    } else if (!wifiOk) {
      Serial.println("[CAPTURE][WARN] WiFi indisponible: sauvegarde SD locale + upload si reconnexion");
      sendPhoto();
    } else {
    sendPhoto();
    }
  } else {
    Serial.println("[MON] en dehors du creneau photo, upload saute.");
  }

  if (resetModeActiveThisBoot) {
    Serial.println("[REMOTE] resetMode actif: redemarrage immediat.");
    delay(200);
    ESP.restart();
  }

  ledBlink(100, 100, 1);
  logMonitoringSnapshot("setup:end");
  logStepDuration("setup_total", millis() - setupStartMs, 30000);
}

void loop() {
#if USE_DEEP_SLEEP
  Serial.printf("[LOOP] uploadphotosserver env=%s version=%s\n", currentTargetName(), FIRMWARE_VERSION);
  logMonitoringSnapshot("loop:before_sleep");
  accumulateOtaPeriodicElapsedFromSleep(runtimeSleepSeconds);
  Serial.printf("[OTA] cumul avant reveil: %lu/%lu s\n",
                static_cast<unsigned long>(otaElapsedSinceLastCheckSeconds),
                static_cast<unsigned long>(OTA_PERIODIC_INTERVAL_SECONDS));
  Serial.printf("[SLEEP] Entree en deep sleep (%u s)\n", static_cast<unsigned int>(runtimeSleepSeconds));
  delay(1000);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)runtimeSleepSeconds * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
#endif
}
