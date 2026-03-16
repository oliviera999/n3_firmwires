/*
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp32-cam-post-image-photo-server/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*/

#include <Arduino.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include "credentials.h"
//basic OTA
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "time.h"
#include <cstring>

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

String serverName = SERVER_NAME;
String serverPath = SERVER_PATH;

const int serverPort = 80;

WiFiClient client;

String Wifiactif;

// Constantes WiFi (logique type ffp5cs simplifiée)
#define WIFI_CONNECT_TIMEOUT_MS   5000
#define WIFI_DELAY_BETWEEN_MS     250
#define WIFI_PRE_SCAN_DELAY_MS     300
#define WIFI_SCAN_MAX              10
#define WIFI_RECONNECT_INTERVAL_MS 60000
#define FIRMWARE_VERSION "2.2"

void Wificonnect();
String sendPhoto();

// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

const int timerInterval = 1800000;    // time between each HTTP POST image
unsigned long previousMillis = 0;   // last time image was sent

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  // Connection à un réseau wifi
  Wificonnect(); 

  camera_config_t config;
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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_SXGA;
    config.jpeg_quality = 4;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 4;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }
  
  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  sendPhoto(); 

    ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  static uint32_t lastWifiAttempt = 0;
  if (WiFi.status() != WL_CONNECTED) {
    uint32_t now = millis();
    if (lastWifiAttempt == 0 || (now - lastWifiAttempt) >= WIFI_RECONNECT_INTERVAL_MS) {
      lastWifiAttempt = now;
      Wificonnect();
    }
  }
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= timerInterval) {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  Serial.println(timeHour);
  
  int heureLimite = atoi(timeHour);
  Serial.println(heureLimite);
  Serial.println();
  char timeWeekDay[10];
  strftime(timeWeekDay,10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  if (heureLimite < 20 && heureLimite > 6){
    sendPhoto();}
  else (Serial.println ("pas de photos prises, c'est la nuit"));
    previousMillis = currentMillis;
  }
}

String sendPhoto() {
  String getAll;
  String getBody;

  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }
  
  Serial.println("Connecting to server: " + serverName);

  if (client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Connection successful!");    
    String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--RandomNerdTutorials--\r\n";

    uint32_t imageLen = fb->len;
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;
  
    client.println("POST " + serverPath + " HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
    client.println();
    client.print(head);
  
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0; n<fbLen; n=n+1024) {
      if (n+1024 < fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen%1024>0) {
        size_t remainder = fbLen%1024;
        client.write(fbBuf, remainder);
      }
    }   
    client.print(tail);
    
    esp_camera_fb_return(fb);
    
    int timoutTimer = 30000;
    long startTimer = millis();
    boolean state = false;
    
    while ((startTimer + timoutTimer) > millis()) {
      Serial.print(".");
      delay(100);      
      while (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (getAll.length()==0) { state=true; }
          getAll = "";
        }
        else if (c != '\r') { getAll += String(c); }
        if (state==true) { getBody += String(c); }
        startTimer = millis();
      }
      if (getBody.length()>0) { break; }
    }
    Serial.println();
    client.stop();
    Serial.println(getBody);
  }
  else {
    getBody = "Connection to " + serverName +  " failed.";
    Serial.println(getBody);
  }
  return getBody;

  ArduinoOTA.handle();
}

// Connexion WiFi : scan, tri par RSSI, tentatives avec BSSID/canal (logique ffp5cs simplifiée)
static void wifiBeginSafe(const char* ssid, const char* pass, int32_t chan, const uint8_t* bssid) {
  if (pass && strlen(pass) > 0) {
    WiFi.begin(ssid, pass, chan, bssid);
  } else {
    if (chan > 0 || bssid) WiFi.begin(ssid, nullptr, chan, bssid);
    else WiFi.begin(ssid);
  }
}

static bool waitConnected(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

void Wificonnect() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (WiFi.status() == WL_CONNECTED) {
    Wifiactif = WiFi.SSID();
    return;
  }
  WiFi.mode(WIFI_STA);

  delay(WIFI_PRE_SCAN_DELAY_MS);
  int n = WiFi.scanNetworks(false, true);
  if (n > WIFI_SCAN_MAX) n = WIFI_SCAN_MAX;

  struct Cand { int8_t rssi; uint8_t bssid[6]; uint8_t chan; bool present; };
  Cand cand[WIFI_SCAN_MAX];
  for (size_t i = 0; i < WIFI_COUNT && i < WIFI_SCAN_MAX; i++) {
    cand[i].rssi = -128;
    cand[i].chan = 0;
    cand[i].present = false;
    memset(cand[i].bssid, 0, 6);
  }

  for (int j = 0; j < n; j++) {
    char scanSsid[33];
    strncpy(scanSsid, WiFi.SSID(j).c_str(), 32);
    scanSsid[32] = '\0';
    int8_t r = (int8_t)WiFi.RSSI(j);
    for (size_t i = 0; i < WIFI_COUNT && i < WIFI_SCAN_MAX; i++) {
      if (strcmp(scanSsid, WIFI_LIST[i].ssid) == 0 && r > cand[i].rssi) {
        cand[i].rssi = r;
        const uint8_t* b = WiFi.BSSID(j);
        if (b) memcpy(cand[i].bssid, b, 6);
        cand[i].chan = (uint8_t)WiFi.channel(j);
        cand[i].present = true;
      }
    }
  }

  size_t order[WIFI_SCAN_MAX];
  size_t orderCount = 0;
  for (size_t i = 0; i < WIFI_COUNT && i < WIFI_SCAN_MAX; i++) {
    if (cand[i].present) order[orderCount++] = i;
  }
  for (size_t k = 0; k < orderCount; k++) {
    for (size_t j = k + 1; j < orderCount; j++) {
      if (cand[order[j]].rssi > cand[order[k]].rssi) {
        size_t t = order[k]; order[k] = order[j]; order[j] = t;
      }
    }
  }
  for (size_t i = 0; i < WIFI_COUNT && i < WIFI_SCAN_MAX; i++) {
    if (!cand[i].present) order[orderCount++] = i;
  }

  for (size_t idx = 0; idx < orderCount; idx++) {
    size_t i = order[idx];
    const char* ssid = WIFI_LIST[i].ssid;
    const char* pass = WIFI_LIST[i].password;

    if (cand[i].present) {
      Serial.printf("[WiFi] Try %s RSSI=%d ch=%d\n", ssid, cand[i].rssi, cand[i].chan);
      wifiBeginSafe(ssid, pass, cand[i].chan, cand[i].bssid);
    } else {
      Serial.printf("[WiFi] Try (invisible) %s\n", ssid);
      WiFi.begin(ssid, pass);
    }

    if (waitConnected(WIFI_CONNECT_TIMEOUT_MS)) {
      Wifiactif = ssid;
      Serial.printf("[WiFi] OK %s %s RSSI=%d\n", ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
      Serial.print("Réseau wifi: ");
      Serial.println(Wifiactif);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return;
    }

    if (cand[i].present) {
      WiFi.disconnect(false, true);
      delay(250);
      Serial.printf("[WiFi] Retry sans BSSID %s\n", ssid);
      WiFi.begin(ssid, pass);
      if (waitConnected(WIFI_CONNECT_TIMEOUT_MS)) {
        Wifiactif = ssid;
        Serial.printf("[WiFi] OK(2e) %s %s RSSI=%d\n", ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
        Serial.print("Réseau wifi: ");
        Serial.println(Wifiactif);
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        return;
      }
    }

    Serial.printf("[WiFi] Echec %s\n", ssid);
    delay(WIFI_DELAY_BETWEEN_MS);
  }

  Serial.println("[WiFi] Echec connexion - aucun reseau disponible");
}
