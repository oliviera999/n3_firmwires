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
//basic OTA
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "time.h"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

#include "credentials.h"

#define FIRMWARE_VERSION "1.5"

String serverName = "iot.olution.info";
String serverPath = "/ffp3/ffp3gallery/upload.php";
const int serverPort = 80;

WiFiClient client;

// WiFi — credentials externalisées dans credentials.h (copier credentials.h.example)
const char* ssid = WIFI_SSID1;
const char* password = WIFI_PASS1;
const char* ssid2 = WIFI_SSID2;
const char* password2 = WIFI_PASS2;
const char* ssid3 = WIFI_SSID3;
const char* password3 = WIFI_PASS3;
String Wifiactif;

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

const int timerInterval = 600000;    // time between each HTTP POST image
unsigned long previousMillis = 0;   // last time image was sent

void blink();
void blinkWifi();
void blinkPhoto();
void Wificonnect();
String sendPhoto();

void setup() {
  pinMode(33, OUTPUT); // Set the pin as output
  digitalWrite(33, LOW); //Turn on
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  blink();

  WiFi.mode(WIFI_STA);
  Wificonnect(); 
  blink();

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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 7;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 7;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }
  
  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
  blink();
  // Port defaults to 3232
  ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("ffp3");

  // No authentication by default
   ArduinoOTA.setPassword("admin");

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

  sendPhoto(); 

  blink();

}

void loop() {
  blink();
  ArduinoOTA.handle();

  // Connection à un réseau wifi
  if(WiFi.waitForConnectResult()!= WL_CONNECTED ){ 
    Wificonnect();
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= timerInterval) {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    //WebSerial.println("Failed to obtain time");

    return;
  }
  
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.println("Time variables :");

  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);  
  int heureLimite = atoi(timeHour);
  Serial.println(heureLimite);

  char timeWeekDay[10];
  strftime(timeWeekDay,10, "%A", &timeinfo);
  Serial.println(timeWeekDay);

  if (heureLimite < 22 && heureLimite > 6){
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
    blinkPhoto();
  }
  else {
    getBody = "Connection to " + serverName +  " failed.";
    Serial.println(getBody);
  }
  return getBody;

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("Connection Failed! Rebooting...");

      delay(500);
      ESP.restart();
    }
  }

//    ArduinoOTA.handle();
}

//connection à un réseau wifi
void Wificonnect() {
  WiFi.begin(ssid, password);
  for (int i = 0; i < 10; i++) {
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      delay(500);
      Serial.println("Connecting to WiFi..");
    }
    if (WiFi.waitForConnectResult() == WL_CONNECTED) {
      Wifiactif = ssid;
      blinkWifi();
    }
  }
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed! Test de l'autre réseau");
    //  return;
    WiFi.begin(ssid2, password2);
    for (int i = 0; i < 10; i++) {
      if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        delay(500);
        Serial.println("Connecting to WiFi2..");
      }
      if (WiFi.waitForConnectResult() == WL_CONNECTED) {
        Wifiactif = ssid2;
        blinkWifi();
      }
    }
  }

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed! Test du dernier");
    //  return;
    WiFi.begin(ssid3,password3);
    for (int i = 0; i < 10; i++) {
      if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        delay(500);
        Serial.println("Connecting to WiFi3...");
      }
      if (WiFi.waitForConnectResult() == WL_CONNECTED) {
        Wifiactif = ssid3;
      blinkWifi();
      }
    }
  }
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed! Test également échoué");
    return;
  }

  Serial.print("Réseau wifi: ");
  Serial.println(Wifiactif);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
void blink(){
  digitalWrite(33, LOW); //Turn on
  delay (100); //Wait 1 sec
  digitalWrite(33, HIGH); //Turn off
  delay (100); //Wait 1 sec
  digitalWrite(33, LOW); //Turn on
  delay (100); //Wait 1 sec
  digitalWrite(33, HIGH); //Turn off
  delay (100); //Wait 1 sec
}

void blinkWifi(){
  digitalWrite(33, LOW); //Turn on
  delay (500); //Wait 1 sec
  digitalWrite(33, HIGH); //Turn off
  delay (500); //Wait 1 sec
}

void blinkPhoto(){
  digitalWrite(33, LOW); //Turn on
  delay (1500); //Wait 1 sec
  digitalWrite(33, HIGH); //Turn off
  delay (1500); //Wait 1 sec
  digitalWrite(33, LOW); //Turn on
  delay (1500); //Wait 1 sec
  digitalWrite(33, HIGH); //Turn off
  delay (1500); //Wait 1 sec
}



/*
#include <Arduino.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
//basic OTA
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "time.h"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

String serverName = "iot.olution.info";   // REPLACE WITH YOUR Raspberry Pi IP ADDRESS
//String serverName = "example.com";   // OR REPLACE WITH YOUR DOMAIN NAME

String serverPath = "/ffp3/ffp3gallery/upload.php";     // The default serverPath should be upload.php

const int serverPort = 80;

WiFiClient client;

//déclarations des variables pour le Wifi
// identifiant Wifi
//const char* ssid = "raspN3";
//const char* password = "n3LLrasp";
//const char* ssid = "Tenda_1654B8";
//const char* password = "";
const char* ssid = "dlink";
const char* password = "n3LLdlink";
//const char* ssid = "VOTRE_SSID_TEST";
//const char* password = "123456789";
// identifiant Wifi alternatif2
const char* ssid2 = "raspN3";
const char* password2 = "n3LLrasp";
//const char* ssid2 = "dlink";
//const char* password2 = "n3LLdlink";
// identifiant Wifi alternatif
const char* ssid3 = "VOTRE_SSID_3";
const char* password3 = "123456789";
String Wifiactif;

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

const int timerInterval = 600000;    // time between each HTTP POST image
unsigned long previousMillis = 0;   // last time image was sent

void setup() {
  //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 7;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 7;  //0-63 lower number means higher quality
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

  // Connection à un réseau wifi
  if(WiFi.waitForConnectResult()!= WL_CONNECTED ){ 
    Wificonnect();
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

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("Connection Failed! Rebooting...");
      delay(5000);
      ESP.restart();
    }
  }

    ArduinoOTA.handle();
}

//connection à un réseau wifi
void Wificonnect() {
  WiFi.begin(ssid, password);
  for (int i = 0; i < 10; i++) {
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      delay(500);
      Serial.println("Connecting to WiFi..");
    }
    if (WiFi.waitForConnectResult() == WL_CONNECTED) {
      Wifiactif = ssid;
    }
  }
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed! Test de l'autre réseau");
    //  return;
    WiFi.begin(ssid2, password2);
    for (int i = 0; i < 10; i++) {
      if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        delay(500);
        Serial.println("Connecting to WiFi2..");
      }
      if (WiFi.waitForConnectResult() == WL_CONNECTED) {
        Wifiactif = ssid2;
      }
    }
  }

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed! Test du dernier");
    //  return;
    WiFi.begin(ssid3,password3);
    for (int i = 0; i < 10; i++) {
      if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        delay(500);
        Serial.println("Connecting to WiFi3...");
      }
      if (WiFi.waitForConnectResult() == WL_CONNECTED) {
        Wifiactif = ssid3;
      }
    }
  }
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed! Test également échoué");
    //  return;
  }

  Serial.print("Réseau wifi: ");
  Serial.println(Wifiactif);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
*/
