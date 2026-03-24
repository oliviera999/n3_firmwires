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
#include <cstring>
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

// Reset distant: edge detection with first-sample seeding to avoid reboot loops
// if server state is already "110=1" at boot.
static bool s_resetEdgeInitialized = false;
static bool s_lastResetModeState = false;
static constexpr uint32_t OTA_PERIODIC_INTERVAL_SECONDS = 2UL * 60UL * 60UL;
RTC_DATA_ATTR static uint32_t s_otaElapsedSinceLastCheckSeconds = OTA_PERIODIC_INTERVAL_SECONDS;
static char s_otaCurrentVersion[16] = "";
static char s_otaRemoteVersion[16] = "";
static uint8_t s_otaDisplayedPercent = 255;

static void renderOtaScreen(const char* statusLine, uint8_t percent) {
  if (!displayOk) return;

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("N3PP OTA");
  display.setCursor(0, 10);
  display.println(statusLine ? statusLine : "En cours");

  display.setCursor(0, 20);
  display.print(s_otaCurrentVersion[0] ? s_otaCurrentVersion : FIRMWARE_VERSION);
  display.print(" -> ");
  display.println(s_otaRemoteVersion[0] ? s_otaRemoteVersion : "?");

  display.drawRect(0, 36, SCREEN_WIDTH, 10, WHITE);
  const int fillWidth = (percent >= 100) ? (SCREEN_WIDTH - 2) : ((percent * (SCREEN_WIDTH - 2)) / 100);
  if (fillWidth > 0) {
    display.fillRect(1, 37, fillWidth, 8, WHITE);
  }

  display.setCursor(0, 50);
  display.print("Progression: ");
  display.print(percent);
  display.println("%");
  display.display();
}

static void otaDisplayStartCallback(const char* currentVersion,
                                    const char* remoteVersion,
                                    const char* firmwareUrl,
                                    void* userData) {
  (void)firmwareUrl;
  (void)userData;
  snprintf(s_otaCurrentVersion, sizeof(s_otaCurrentVersion), "%s",
           currentVersion ? currentVersion : FIRMWARE_VERSION);
  snprintf(s_otaRemoteVersion, sizeof(s_otaRemoteVersion), "%s",
           remoteVersion ? remoteVersion : "?");
  s_otaDisplayedPercent = 255;
  renderOtaScreen("MAJ disponible", 0);
}

static void otaDisplayEndCallback(bool success, const char* details, void* userData) {
  (void)userData;
  if (success) {
    renderOtaScreen("Succes - reboot", 100);
    return;
  }

  const bool upToDate = (details != nullptr) &&
                        (strstr(details, "deja a jour") != nullptr ||
                         strstr(details, "pas de mise a jour") != nullptr);
  renderOtaScreen(upToDate ? "Deja a jour" : "Echec OTA",
                  upToDate ? 100 : (s_otaDisplayedPercent == 255 ? 0 : s_otaDisplayedPercent));
}

static void otaDisplayProgressCallback(int current, int total, uint8_t percent, void* userData) {
  (void)current;
  (void)total;
  (void)userData;
  if (percent == s_otaDisplayedPercent) return;
  s_otaDisplayedPercent = percent;
  renderOtaScreen("Telechargement", percent);
}

static bool tryOtaBeforeResetForRemoteCommand() {
#ifdef TEST_MODE
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/n3pp-test/metadata.json",
      FIRMWARE_VERSION, -1, nullptr,
      otaDisplayStartCallback, otaDisplayEndCallback,
      nullptr, otaDisplayProgressCallback
  };
#else
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/n3pp/metadata.json",
      FIRMWARE_VERSION, -1, nullptr,
      otaDisplayStartCallback, otaDisplayEndCallback,
      nullptr, otaDisplayProgressCallback
  };
#endif
  return n3OtaCheck(otaConfig);
}

static void maybeRunPeriodicOtaCheck(const char* reason) {
  if (s_otaElapsedSinceLastCheckSeconds < OTA_PERIODIC_INTERVAL_SECONDS) {
    const uint32_t remaining = OTA_PERIODIC_INTERVAL_SECONDS - s_otaElapsedSinceLastCheckSeconds;
    Serial.printf("[OTA] check 2h ignore (%s), restant=%lu s\n",
                  reason ? reason : "n/a",
                  static_cast<unsigned long>(remaining));
    return;
  }

#ifdef TEST_MODE
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/n3pp-test/metadata.json",
      FIRMWARE_VERSION, -1, nullptr,
      otaDisplayStartCallback, otaDisplayEndCallback,
      nullptr, otaDisplayProgressCallback
  };
#else
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/n3pp/metadata.json",
      FIRMWARE_VERSION, -1, nullptr,
      otaDisplayStartCallback, otaDisplayEndCallback,
      nullptr, otaDisplayProgressCallback
  };
#endif

  Serial.printf("[OTA] check 2h declenche (%s)\n", reason ? reason : "n/a");
  n3OtaCheck(otaConfig);
  s_otaElapsedSinceLastCheckSeconds = 0;
}

static void accumulateOtaPeriodicElapsedFromSleep(int sleepSeconds) {
  if (sleepSeconds <= 0) return;
  if (s_otaElapsedSinceLastCheckSeconds >= OTA_PERIODIC_INTERVAL_SECONDS) return;

  const uint32_t sleepSec = static_cast<uint32_t>(sleepSeconds);
  const uint32_t remaining = OTA_PERIODIC_INTERVAL_SECONDS - s_otaElapsedSinceLastCheckSeconds;
  s_otaElapsedSinceLastCheckSeconds += (sleepSec >= remaining) ? remaining : sleepSec;
}

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
  // Démarrage minimal : brown-out, pins critiques, série, OTA partition, WiFi
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(POMPE, OUTPUT);   //lumière
  pinMode(RELAIS, OUTPUT);  //lumière
  digitalWrite(RELAIS, 1);

  Serial.begin(115200);
  delay(500);

  n3OtaSyncBootPartition();
  WiFi.mode(WIFI_MODE_STA);

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

  // OTA périodique : vérification au boot seulement si la cadence 2h est atteinte
  Wificonnect();
  maybeRunPeriodicOtaCheck("boot");

  // Après OTA : affichage, capteurs, NTP, etc.
  print_wakeup_reason();

  pinMode(humidite1, INPUT);
  pinMode(humidite2, INPUT);
  pinMode(humidite3, INPUT);
  pinMode(humidite4, INPUT);
  pinMode(pontdiv, INPUT);
  pinMode(LUMINOSITE, INPUT);

  dht.begin();
  Serial.println("[DHT] Initialisation OK");

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
  etatPompe = 0;
  etatRelais = 1;
  Serial.printf("[REMOTE] resetMode(setup apres sync)=%d\n", resetMode ? 1 : 0);
  //datatobdd();

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("[BOOT] Compteur demarrages: " + String(bootCount));



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
  Serial.println("[TIME] Synchronisation cycle");
  //printLocalTime();

  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));  // Affichage heure RTC au format indiqué

  etatRelais = 1;
  //récupération des infos de la bdd
  Serial.println("[SERVER][GET] Poll configuration distante");
  variablestoesp();

  // Reset mode distant (GPIO 110): OTA first if available, then restart fallback.
  bool resetRequested = (resetMode == 1);
  if (!s_resetEdgeInitialized) {
    s_lastResetModeState = resetRequested;
    s_resetEdgeInitialized = true;
    if (resetRequested) {
      Serial.println("[REMOTE] Reset distant seed=1 au premier poll (pas de reboot immediat)");
    }
  } else if (resetRequested && !s_lastResetModeState) {
    Serial.println("[REMOTE] Reset distant demande (front montant)");
    if (!tryOtaBeforeResetForRemoteCommand()) {
      Serial.println("[REMOTE][OTA] Aucune MAJ OTA dispo, reset direct");
      ESP.restart();
    }
  }
  s_lastResetModeState = resetRequested;

  //contrôle de l'état actif de la pompe ou pas
  if (digitalRead(POMPE) == 1) {
    etatPompe = 1;
    Serial.println("[SERVER][POST] Envoi immediat (pompe active)");
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
    Serial.println("[SERVER][POST] Envoi diagnostic premier tour");
    datatobdd();
  }

  affichageOLED();

  automatismes();

  etatRelais = 1;

  accumulateOtaPeriodicElapsedFromSleep(FreqWakeUp);
  sommeil();

  //envoi régulier des datas mesurées sur iot.olution.info
  unsigned long currentMillisDatas = millis();
  if (currentMillisDatas - previousMillisDatas >= intervalDatas) {
    previousMillisDatas = currentMillisDatas;
    Serial.println("[SERVER][POST] Envoi periodique capteurs");
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
