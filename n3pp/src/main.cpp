/* N3PhasmesProto (n3pp)
 * Serre / aquaponie — salle aeree n3
 * Credentials externalises dans credentials.h
 * OTA HTTP distant via n3_common
 */

#include "n3pp_globals.h"
#include "n3pp_sensors.h"
#include "n3pp_display.h"
#include "n3pp_network.h"
#include "n3pp_automation.h"

#include <esp_sleep.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "n3_ota.h"
#include "n3_display.h"
#include "n3_sleep.h"

//variable et témoin issues et pour la bdd
int HeureArrosage = 6;
int SeuilSec = 5000;
//int SeuilPontDiv = 1100;
bool WakeUp = 0;
int FreqWakeUp = 3;  // Durée du sommeil en secondes (valeur par défaut 3 s)
bool ArrosageManu = 0;
bool resetMode = 0;

float temperatureAir;
float h;

int HumidMoy;
int photocellReading;
/*float Rsensor;
#define VIN 3.3 // V power voltage
#define R 10000 //ohm resistance value
int lux; //Lux value*/
bool etatPompe = 0;
bool etatRelais = 0;

int tempsArrosageMill = 1000;
int tempsArrosageSec = 4;
int tempsArrosage = tempsArrosageSec * tempsArrosageMill;

int Humid1;
int Humid2;
int Humid3;
int Humid4;

 // int PontDiv; 

// Intervalle entre deux lectures capteurs (en ms)
unsigned long previousMillisDatas = 0;
extern const long intervalDatas = N3_DATA_INTERVAL_MS;

// Indicateur : email d'alerte déjà envoyé ou non (évite spam)
bool emailHumidSent = 0;
bool emailPontDivSent = 0;
RTC_DATA_ATTR bool arrosageFait = 1;

//wakeUp touch
RTC_DATA_ATTR int bootCount = 0;
//touch_pad_t touchPin;
bool WakeUpButton = 0;

RTC_DATA_ATTR String inputMessageMailAd = SMTP_DEST;
RTC_DATA_ATTR String enableEmailChecked = "checked";

String emailMessage;

/* Session SMTP globale pour l'envoi des emails (ESP Mail Client) */
SMTPSession smtp;

int PontDiv;        //valeur
int avgPontDiv;
float batt;
float measuredVoltage;
float batteryVoltage;
int SeuilPontDiv = 1700;  // Seuil pour considérer la batterie faible
extern const float ADC_MAX_VALUE = 4095.0;
extern const float V_REF = N3_BATTERY_VREF;
extern const float calibration = 0.06;
// Tableau pour stocker les échantillons
int samples[NUM_SAMPLES];
int sampleIndex = 0;
int sampleTotal = 0;

// Définition des URLs serveur (base de données olution / iot.olution.info)
#ifdef TEST_MODE
const char* serverNamePostData = "http://iot.olution.info/n3pp-test/n3ppdatas/post-n3pp-data.php";
const char* serverNameOutput = "http://iot.olution.info/n3pp-test/n3ppcontrol/n3pp-outputs-action.php?action=outputs_state&board=3";
#else
const char* serverNamePostData = "http://iot.olution.info/n3pp/n3ppdatas/post-n3pp-data.php";
const char* serverNameOutput = "http://iot.olution.info/n3pp/n3ppcontrol/n3pp-outputs-action.php?action=outputs_state&board=3";
#endif

String version = FIRMWARE_VERSION;

String apiKeyValue = API_KEY;
String sensorName = "n3pp";
String sensorLocation = "T06";

// Affichage SSD1306 connecté en I2C (broches SDA, SCL)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool displayOk = false;  // false si OLED absente ou I2C en erreur

const char* ssid = WIFI_SSID1;
const char* password = WIFI_PASS1;
const char* ssid2 = WIFI_SSID2;
const char* password2 = WIFI_PASS2;
const char* ssid3 = WIFI_SSID3;
const char* password3 = WIFI_PASS3;

String Wifiactif;

// Serveur web asynchrone sur le port 80 (interface locale)
AsyncWebServer server(80);

//temps NTP
WiFiUDP wifiUdp;
// NTP : ajuster offset/fuseau dans gmtOffset_sec et daylightOffset_sec selon la localisation

String outputsState;  // Variable pour gérer l'état des sorties à distance

//variables en lien avec le temps rtc et ntp
const char* ntpServer = N3_NTP_SERVER;
extern const long gmtOffset_sec = N3_GMT_OFFSET;
extern const int daylightOffset_sec = N3_DAYLIGHT_OFFSET;
ESP32Time rtc;                           //initialisation du contrôle RTC via la bibliothèque ESP32time
Preferences preferences;                 //initialisation du stockage dse l'heure dans la mémoire flash
int seconde;
int minute;
int heure;
int jour;
int mois;
int annee;

// Code de réponse HTTP (requêtes GET/POST)
unsigned int httpResponseCode;

DHT dht(DHTPIN, DHTTYPE);

/*
//fonction temps NTP
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Impossible d'obtenir l'heure NTP");
    return;
  }
  Serial.print("temps NTP ");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  heure = timeinfo.tm_hour;
  minute = timeinfo.tm_min;
  seconde = timeinfo.tm_sec;
  jour = timeinfo.tm_mday;
  mois = timeinfo.tm_mon + 1;
  annee = timeinfo.tm_year + 1900;

  Serial.print("Heure locale ");
  Serial.print(heure);
  Serial.print(":");
  Serial.print(minute);
  Serial.print(":");
  Serial.print(seconde);
  Serial.print("  ");
  Serial.print(jour);
  Serial.print("-");
  Serial.print(mois);
  Serial.print("-");
  Serial.println(annee);
}*/

void setup() {
  // Démarrage du moniteur série
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(POMPE, OUTPUT);   //lumière
  pinMode(RELAIS, OUTPUT);  //lumière
  digitalWrite(RELAIS, 1);

  Serial.begin(115200);
  delay(500);

  n3OtaSyncBootPartition();

  WiFi.mode(WIFI_MODE_STA);

  print_wakeup_reason();

  displayOk = n3DisplayInit(display);
  delay(600);
  if (displayOk) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(" Demarrage");
    display.println(" ");
    display.println("  n3pp");
    display.print("  v:");
    display.println(version);
    display.display();
  }

  // définition de l'état initial des pins
  pinMode(humidite1, INPUT);
  pinMode(humidite2, INPUT);
  pinMode(humidite3, INPUT);
  pinMode(humidite4, INPUT);
  pinMode(pontdiv, INPUT);
  //pinMode(contact, INPUT);
  pinMode(LUMINOSITE, INPUT);  //lumière


  //  attachInterrupt(LUMIERE, isr, RISING);

  //démarrage du DHT
  dht.begin();
  Serial.println("dht begin");

  /*
  //démarrage de la fonction OTA autohébergé
  AsyncElegantOTA.begin(&server);    // Start ElegantOTA*/
  /*  
  //démarrage du serveur autohébergé
  server.begin();

  //display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  //démarrage OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  //delay(500);
  display.clearDisplay();
  display.setTextColor(WHITE);
*/
  //affichage version
  //Serial.print("version : ");
  //Serial.println(version);
  /*
  //OTA basique
    // Port defaults to 3232
    ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
   ArduinoOTA.setHostname("n3pp");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

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
      if (error == OTA_AUTH_ERROR); 
      Serial.println("Auth Failed");
      if (error == OTA_BEGIN_ERROR);
      Serial.println("Begin Failed");
      if (error == OTA_CONNECT_ERROR); 
      Serial.println("Connect Failed");
      if (error == OTA_RECEIVE_ERROR); 
      Serial.println("Receive Failed");
      if (error == OTA_END_ERROR); 
      Serial.println("End Failed");
    });

  //serveur OTA 
  ArduinoOTA.begin();*/

  Wificonnect();

  // OTA distant : verification MAJ a chaque boot/reveil (prod vs test selon build)
#ifdef TEST_MODE
  N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/n3pp-test/metadata.json",
      version.c_str(), -1, nullptr
  };
#else
  N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/n3pp/metadata.json",
      version.c_str(), -1, nullptr
  };
#endif
  n3OtaCheck(otaConfig);

  /* //init and get the time
  if(WiFi.status()== WL_CONNECTED ){ 
    Serial.println("start NTP");
    timeClient.begin();                                       //
    timeClient.update();                                      //
    //timeClient.forceUpdate();                                 //
    Serial.println(timeClient.getFormattedTime()); // hh mm ss
    Serial.println(timeClient.getFormattedDate()); // dd mm yyyy*/
  /*    if ( rtc.lostPower() ){
      Serial.println("RTC lost power, let's set the time!");
      adjustRTC();
    }
      // Set RTC time to NTP time
  //rtc.adjust(DateTime(timeClient.getEpochTime()));
  RTCdate();
  rtcmodule();
  RTCdate();
  printLocalTime();
  }*/
  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  //printLocalTime();

  variablestoesp();
  resetMode = 0;
  etatPompe = 0;
  etatRelais = 1;
  Serial.print("RESET SETUP : ");
  Serial.println(resetMode);
  //datatobdd();

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));



  N3SleepConfig sleepCfg = { N3_WAKEUP_GPIO, HIGH, (unsigned long)FreqWakeUp };
  n3SleepConfigure(sleepCfg);
}

void loop() {
  static bool firstLoop = true;

  // Update NTP time
  //printLocalTime();

  //print_wakeup_reason();
  digitalWrite(RELAIS, 1);

  //démarrage du serveur autohébergé
  server.begin();

  // Connexion à un des réseaux WiFi configurés (n3_wifi)
  if (WiFi.status() != WL_CONNECTED) {
    Wificonnect();
  }

  // Si WiFi connecté : initialisation et récupération de l'heure NTP
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
  Serial.println("temps 2 : ");
  //printLocalTime();

  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));  // Affichage heure RTC au format indiqué

  etatRelais = 1;
  //récupération des infos de la bdd
  variablestoesp();

  //contrôle de l'état actif de la pompe ou pas
  if (digitalRead(POMPE) == 1) {
    etatPompe = 1;
    datatobdd();
    if (enableEmailChecked == "checked") {
      String emailMessage = String("ATTENTION, arrosage continu en cours !!!");
      //bouffeSoirTemoin = 1;
      sendEmailNotification();
    }
  }

  lectureCapteurs();

  batterie();

  // Premier tour : envoi POST de diagnostic pour voir immédiatement le code HTTP (200/401/500)
  if (firstLoop) {
    firstLoop = false;
    datatobdd();
  }

  affichageOLED();

  automatismes();

  etatRelais = 1;

  sommeil();

  //envoi régulier des datas mesurées sur iot.olution.info
  unsigned long currentMillisDatas = millis();
  if (currentMillisDatas - previousMillisDatas >= intervalDatas) {
    previousMillisDatas = currentMillisDatas;
    datatobdd();
    EnregistrementHeureFlash();
    if (WiFi.status() != WL_CONNECTED) {
      HeureSansWifi();
      if (displayOk) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
        display.display();
      }
      delay(1000);
    }

    Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));  // Affichage heure RTC au format indiqué

  }
}
