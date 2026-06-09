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
//#include <ArduinoOTA.h>
#include "time.h"
#include <EEPROM.h>            // read and write from flash memory
#include "FS.h"                // SD Card ESP32
#include "SD_MMC.h"            // SD Card ESP32
#include <cstring>

/*
#include <BluetoothSerial.h>
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Bluetooth not available or not enabled. It is only available for the ESP32 chip.
#endif
*/
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

String serverName = SERVER_NAME;
String serverPath = SERVER_PATH;

const int serverPort = 80;

/*
BluetoothSerial Serial;
#define BT_DISCOVER_TIME	10000


static bool btScanAsync = true;
static bool btScanSync = true;
*/

WiFiClient client;

String Wifiactif;

// Constantes WiFi (logique type ffp5cs simplifiée)
#define WIFI_CONNECT_TIMEOUT_MS   5000
#define WIFI_DELAY_BETWEEN_MS     250
#define WIFI_PRE_SCAN_DELAY_MS    300
#define WIFI_SCAN_MAX             10
#define FIRMWARE_VERSION "1.8"

// define the number of bytes you want to access
#define EEPROM_SIZE 1

int pictureNumber = 0;


// CAMERA_MODEL_AI_THINKER
// Déclarations anticipées (définies plus bas)
void adjustExposure();
void blinkWifi();
void blinkPhoto();

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

//const int timerInterval = 600000;    // time between each HTTP POST image
//unsigned long previousMillis = 0;   // last time image was sent

#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */ 
#define TIME_TO_SLEEP 5 /* Time ESP32 will go to sleep (in seconds) - temporaire pour tests */

String sendPhoto() {
  String getAll;
  String getBody;

  // Ajustez les paramètres de la caméra et attendez
    //adjustCameraSettings();

    adjustExposure();


  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }

  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(0) + 1;

  // Path where new picture will be saved in SD Card
  String path = "/picture" + String(pictureNumber) +".jpg";

  fs::FS &fs = SD_MMC; 
  Serial.printf("Picture file name: %s\n", path.c_str());
  
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file in writing mode");
  } 
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
    EEPROM.write(0, pictureNumber);
    EEPROM.commit();
  }
  file.close();
  esp_camera_fb_return(fb); 

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
    for (size_t n=0; n<fbLen; n=n+4096) {
      if (n+4096 < fbLen) {
        client.write(fbBuf, 4096);
        fbBuf += 4096;
      }
      else if (fbLen%4096>0) {
        size_t remainder = fbLen%4096;
        client.write(fbBuf, remainder);
      }
    }   
    client.print(tail);
    
    esp_camera_fb_return(fb);
    
    int timoutTimer = 20000;
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
    ESP.restart();
  }
  return getBody;
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
      blinkWifi();
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
        blinkWifi();
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

void adjustCameraSettings() {
    sensor_t * s = esp_camera_sensor_get();
    if (s) {
       // s->set_exposure_ctrl(s, 1);  // Auto exposure
      s->set_exposure_ctrl(s, 1);  // Désactiver l'exposition automatique
      //s->set_aec_value(s, 1);  // Définir une valeur d'exposition manuelle (0-1200)
        s->set_aec2(s, 1);           // Auto exposure (DSP)
        s->set_ae_level(s, 0);       // Auto exposure level (-2 to 2)
        s->set_gain_ctrl(s, 1);      // Auto gain control
        s->set_awb_gain(s, 1);       // Auto white balance
        s->set_wb_mode(s, 1);        // Auto white balance mode
        
        // Vous pouvez ajuster ces valeurs selon vos besoins
        s->set_brightness(s, 0);     // Brightness (-2 to 2)
        s->set_contrast(s, 0);       // Contrast (-2 to 2)
        s->set_saturation(s, 0);     // Saturation (-2 to 2)
    }

    // Donnez du temps à la caméra pour s'ajuster
    // Vous pouvez augmenter ce délai si nécessaire
    delay(15000);  // Attendez 3 secondes
}

void adjustExposure() {
    sensor_t * s = esp_camera_sensor_get();
    camera_fb_t * fb = esp_camera_fb_get();
    if (fb && s) {
        // Analysez la luminosité de l'image
        long brightness = 0;
        for (int i = 0; i < fb->len; i++) {
            brightness += fb->buf[i];
        }
        brightness /= fb->len;

        // Ajustez en fonction de la luminosité
        if (brightness > 200) {  // Valeur arbitraire, à ajuster
            s->set_aec_value(s, s->status.aec_value - 50);
        } else if (brightness < 50) {
            s->set_aec_value(s, s->status.aec_value + 50);
        }

        esp_camera_fb_return(fb);
    }
}

void warmupCamera() {
    for (int i = 0; i < 3; i++) {
        camera_fb_t * fb = esp_camera_fb_get();
        if (fb) {
            esp_camera_fb_return(fb);
        }
        delay(1000);
    }
}

void initializeCamera() {
    sensor_t * s = esp_camera_sensor_get();
    if (s) {
        // Commencez avec des valeurs conservatrices
        s->set_exposure_ctrl(s, 0);  // Désactivez temporairement l'auto-exposition
        s->set_aec_value(s, 300);    // Définissez une valeur d'exposition initiale basse
        s->set_gain_ctrl(s, 0);      // Désactivez temporairement l'auto-gain
        s->set_agc_gain(s, 0);       // Définissez un gain initial bas
        
        delay(1000);  // Attendez que la caméra s'adapte

        // Activez progressivement les contrôles automatiques
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_awb_gain(s, 1);
        //s->set_wb_mode(s, 1);        // Auto white balance mode
        
        delay(10000);  // Donnez du temps pour l'ajustement final
    }
}
/*
void btAdvertisedDeviceFound(BTAdvertisedDevice* pDevice) {
	Serial.printf("Found a device asynchronously: %s\n", pDevice->toString().c_str());
}*/
void setup() {
  pinMode(33, OUTPUT); // Set the pin as output
  digitalWrite(33, LOW); //Turn on
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  blink();

  //Serial.println("Starting SD Card");
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    return;
  }
  
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    return;
  }

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
  config.xclk_freq_hz = 5000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_SXGA;
    config.jpeg_quality = 3;  //0-63 lower number means higher quality
    config.fb_count = 2;
    //config.grab_mode = CAMERA_GRAB_LATEST;

  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 7;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }
  
  // camera init
  esp_err_t err = esp_camera_init(&config);
  sensor_t * s = esp_camera_sensor_get();
  /*if (s) {
      s->set_exposure_ctrl(s, 1);  // Désactiver l'exposition automatique
      //s->set_aec_value(s, 8);  // Définir une valeur d'exposition manuelle (0-1200)
      s->set_aec2(s, 1);  // Auto exposure (DSP)
      s->set_ae_level(s, -2);  // Réduire le niveau d'exposition
      s->set_gain_ctrl(s, 1);  // Auto gain
      s->set_agc_gain(s, 0);  // Réduire le gain
      s->set_brightness(s, 0);  // Réduire la luminosité (-2 à 2)
      s->set_contrast(s, 1);  // Augmenter légèrement le contraste (0 à 2)
      s->set_wb_mode(s, 1);  // Auto White Balance
  }*/
  
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
  delay(1000);


  blink();
  /*// Port defaults to 3232
  ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("n3pp");

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

  ArduinoOTA.begin();*/
/*
  Serial.begin("ESP32test"); //Bluetooth device name
  Serial.println("The device started, now you can pair it with bluetooth!");


  if (btScanAsync) {
    Serial.print("Starting discoverAsync...");
    if (Serial.discoverAsync(btAdvertisedDeviceFound)) {
      Serial.println("Findings will be reported in \"btAdvertisedDeviceFound\"");
      delay(10000);
      Serial.print("Stopping discoverAsync... ");
      Serial.discoverAsyncStop();
      Serial.println("stopped");
    } else {
      Serial.println("Error on discoverAsync f.e. not workin after a \"connect\"");
    }
  }
  
  if (btScanSync) {
    Serial.println("Starting discover...");
    BTScanResults *pResults = Serial.discover(BT_DISCOVER_TIME);
    if (pResults)
      pResults->dump(&Serial);
    else
      Serial.println("Error on BT Scan, no result!");
  }  */

    initializeCamera();
    warmupCamera();

  WiFi.mode(WIFI_STA);
  Wificonnect(); 
  blink();

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //sendPhoto(); 

  blink();

}

void loop() {
  blink();
  //ArduinoOTA.handle();

  // Connection à un réseau wifi
/*  if(WiFi.waitForConnectResult()!= WL_CONNECTED ){ 
    Wificonnect();
  }*/
/*
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= timerInterval) {*/
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    //WebSerial.println("Failed to obtain time");
    //ESP.restart();
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
  //  previousMillis = currentMillis;
  //}

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  Serial.println("Going to sleep now");
  delay(1000);
  Serial.flush(); 
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
}
