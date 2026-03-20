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
RTC_DATA_ATTR static int otaBootCount = 0;
#endif

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
  String getAll, getBody;

#if USE_DEEP_SLEEP
  adjustExposure();
#endif

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Echec capture camera");
    delay(1000);
    ESP.restart();
  }

#if USE_SD
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
        Serial.println("SD : ecriture incomplete, desactivation SD.");
        sdAvailable = false;
      }
    } else {
      Serial.println("SD : ouverture fichier impossible, desactivation SD.");
      sdAvailable = false;
    }
  }
  /* Ne pas appeler esp_camera_fb_return(fb) ici : on envoie encore HTTP avec fb->buf */
#endif

  bool connected = false;
  for (int attempt = 1; attempt <= UPLOAD_CONNECT_RETRIES; attempt++) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi deconnecte, tentative de reconnexion...");
      Wificonnect();
    }
    Serial.printf("Connexion au serveur (%d/%d) : %s\n", attempt, UPLOAD_CONNECT_RETRIES, serverName.c_str());
    if (client.connect(serverName.c_str(), serverPort)) {
      connected = true;
      break;
    }
    delay(UPLOAD_RETRY_DELAY_MS);
  }
  if (!connected) {
    getBody = "Connexion a " + serverName + " echouee apres retries.";
    Serial.println(getBody);
    esp_camera_fb_return(fb);
    return getBody;
  }

  Serial.println("Connexion serveur OK.");
  String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"esp32-cam-" + String(FIRMWARE_VERSION) + ".jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--RandomNerdTutorials--\r\n";
  uint32_t imageLen = fb->len;
  uint32_t extraLen = head.length() + tail.length();
  uint32_t totalLen = imageLen + extraLen;

  client.println("POST " + serverPath + " HTTP/1.1");
  client.println("Host: " + serverName);
  client.println("Content-Length: " + String(totalLen));
  client.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
  client.println("X-Api-Key: " + String(API_KEY));
  client.println();
  client.print(head);

  const size_t chunk = UPLOAD_CHUNK_SIZE;
  size_t sent = 0;
  while (sent < fb->len) {
    size_t toSend = (fb->len - sent) > chunk ? chunk : (fb->len - sent);
    client.write(fb->buf + sent, toSend);
    sent += toSend;
  }
  client.print(tail);

  /* Libérer le framebuffer après envoi complet (évite use-after-free) */
  esp_camera_fb_return(fb);

  int timeoutTimer = HTTP_RESPONSE_TIMEOUT_MS;
  long startTimer = millis();
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
  Serial.println();
  client.stop();
  if (statusLine.length() > 0) {
    Serial.println("Reponse: " + statusLine);
    int statusCode = parseHttpStatusCode(statusLine);
    if (statusCode != 200) {
      Serial.printf("Upload non confirme, code HTTP=%d\n", statusCode);
    }
  }
  Serial.println(getBody);
  ledBlink(1500, 1500, 2);
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
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LOW);
  ledBlink(100, 100, 2);

#if USE_DEEP_SLEEP
  n3PrintWakeupReason(preferences, rtc);
#endif

  WiFi.mode(WIFI_STA);
  bool wifiOk = Wificonnect();
  ledBlink(100, 100, 1);

#if USE_SD
  if (!SD_MMC.begin()) {
    Serial.println("Carte SD : montage echoue, continuation sans SD.");
    sdAvailable = false;
  } else if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("Aucune carte SD detectee, continuation sans SD.");
    sdAvailable = false;
  } else {
    sdAvailable = true;
    Serial.println("Carte SD OK.");
  }
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

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

#if USE_DEEP_SLEEP
  initializeCamera();
  warmupCamera();
  delay(1000);
#endif

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
#endif

  /* OTA distant : verification tous les N boots (toutes cibles en deep sleep) */
#if defined(TARGET_MSP1)
  otaBootCount++;
  if (otaBootCount % OTA_CHECK_EVERY_N_BOOTS == 0) {
    N3OtaConfig otaCfg = { OTA_METADATA_URL, FIRMWARE_VERSION, -1, "msp1" };
    n3OtaCheck(otaCfg);
  }
#elif defined(TARGET_N3PP)
  otaBootCount++;
  if (otaBootCount % OTA_CHECK_EVERY_N_BOOTS == 0) {
    N3OtaConfig otaCfg = { OTA_METADATA_URL, FIRMWARE_VERSION, -1, "n3pp" };
    n3OtaCheck(otaCfg);
  }
#elif defined(TARGET_FFP3)
  otaBootCount++;
  if (otaBootCount % OTA_CHECK_EVERY_N_BOOTS == 0) {
    N3OtaConfig otaCfg = { OTA_METADATA_URL, FIRMWARE_VERSION, -1, "ffp3" };
    n3OtaCheck(otaCfg);
  }
#endif

  /* Une photo en setup si dans le créneau, puis deep sleep (toutes cibles) */
  if (inPhotoWindow()) {
    if (!wifiOk && !sdAvailable) {
      Serial.println("Photo ignoree: pas de WiFi et pas de SD disponible.");
    } else if (!wifiOk) {
      Serial.println("WiFi indisponible: tentative de sauvegarde locale SD + upload si reconnexion.");
      sendPhoto();
    } else {
    sendPhoto();
    }
  }

  ledBlink(100, 100, 1);
}

void loop() {
#if USE_DEEP_SLEEP
      Serial.println("Entree en deep sleep.");
  delay(1000);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
#endif
}
