/* MeteoStationPrototype (msp1)
 * Station meteo + tracker solaire — salle aeree n3
 * Credentials externalises dans credentials.h
 * OTA HTTP distant via n3_common
 */

#include "msp_config.h"
#include "msp_globals.h"
#include "msp_sensors.h"
#include "msp_display.h"
#include "msp_network.h"
#include "msp_automation.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <esp_sleep.h>
#include "credentials.h"
#include "n3_ota.h"
#include "n3_display.h"
#include "n3_sleep.h"

// ============================================================
// Définitions des variables globales
// ============================================================

// --- Capteurs température sol (DS18B20) ---
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);
float temperatureSol;

// --- Luminosité (filtrage moyenne mobile) ---
int readings1[numReadings];
int readings2[numReadings];
int readings3[numReadings];
int readings4[numReadings];
int readIndex = 0;
int total1 = 0, total2 = 0, total3 = 0, total4 = 0;
int average1 = 0, average2 = 0, average3 = 0, average4 = 0;
int photocellReadingA = 0, photocellReadingB = 0, photocellReadingC = 0, photocellReadingD = 0;
int photocellReadingMoy = 0;

// --- Servos (tracker solaire) ---
Servo servogd;
Servo servohb;
int posLumMax1 = 0, posLumMax2 = 0, posLumMax3 = 0, posLumMax4 = 0;
int AngleServoGD;
int AngleServoHB;

// --- DHT intérieur / extérieur ---
DHT dhtint(DHTPININT, DHTTYPEINT);
DHT dhtext(DHTPINEXT, DHTTYPEEXT);
//variables T et H pour les DHT
float tempAirInt;
float humidAirInt;
float tempAirExt;
float humidAirExt;

// --- Deep sleep ---
bool WakeUp = 0;
int FreqWakeUp = 3;
//bool ArrosageManu = 0;

// --- Batterie / pont diviseur ---
int PontDiv;
int avgPontDiv;
float batt;
float measuredVoltage;
float batteryVoltage;
int SeuilPontDiv = 1700;
int samples[NUM_SAMPLES];
int sampleIndex = 0;
int sampleTotal = 0;

// --- Seuils / états ---
int SeuilSec = 5000;
bool resetMode = 0;
bool etatRelais = 0;
int Oled = 0;

// --- Capteurs analogiques ---
int HumidSol;
int Pluie;
unsigned long previousMillisDatas = 0;

// --- Email ---
bool emailHumidSent = 0;
//bool emailPontDivSent = 0;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR String inputMessageMailAd = SMTP_DEST;
RTC_DATA_ATTR String enableEmailChecked = "checked";
String emailMessage;

/* Session SMTP globale pour l'envoi des emails (ESP Mail Client) */
SMTPSession smtp;

// --- Réseau ---
#ifdef TEST_MODE
const char* serverNamePostData = "http://iot.olution.info/msp1-test/msp1datas/post-msp1-data.php";
const char* serverNameOutput = "http://iot.olution.info/msp1-test/msp1control/msp1-outputs-action.php?action=outputs_state&board=2";
#else
const char* serverNamePostData = "http://iot.olution.info/msp1/msp1datas/post-msp1-data.php";
const char* serverNameOutput = "http://iot.olution.info/msp1/msp1control/msp1-outputs-action.php?action=outputs_state&board=2";
#endif

unsigned int httpResponseCode;
String version = FIRMWARE_VERSION;
String apiKeyValue = API_KEY;
String sensorName = "msp1";
String sensorLocation = "T06";

// --- Affichage OLED ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool displayOk = false;

// --- WiFi ---
const char* ssid = WIFI_SSID1;
const char* password = WIFI_PASS1;
const char* ssid2 = WIFI_SSID2;
const char* password2 = WIFI_PASS2;
const char* ssid3 = WIFI_SSID3;
const char* password3 = WIFI_PASS3;
String Wifiactif;

AsyncWebServer server(80);
WiFiUDP wifiUdp;
String outputsState;

// Reset distant: edge detection with first-sample seeding to avoid reboot loops
// if server state is already "110=1" at boot.
static bool s_resetEdgeInitialized = false;
static bool s_lastResetModeState = false;
static constexpr uint32_t OTA_PERIODIC_INTERVAL_SECONDS = 2UL * 60UL * 60UL;
RTC_DATA_ATTR static uint32_t s_otaElapsedSinceLastCheckSeconds = OTA_PERIODIC_INTERVAL_SECONDS;

static bool tryOtaBeforeResetForRemoteCommand() {
#ifdef TEST_MODE
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/msp-test/metadata.json",
      FIRMWARE_VERSION, -1, nullptr
  };
#else
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/msp/metadata.json",
      FIRMWARE_VERSION, -1, nullptr
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
      "http://iot.olution.info/ota/msp-test/metadata.json",
      FIRMWARE_VERSION, -1, nullptr
  };
#else
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/msp/metadata.json",
      FIRMWARE_VERSION, -1, nullptr
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

// --- Temps RTC / NTP ---
const char* ntpServer = MSP_NTP_SERVER;
const long gmtOffset_sec = MSP_GMT_OFFSET_SEC;
const int daylightOffset_sec = MSP_DAYLIGHT_OFFSET_SEC;
ESP32Time rtc;
Preferences preferences;
int seconde;
int minute;
int heure;
int jour;
int mois;
int annee;

// ============================================================
// setup() et loop()
// ============================================================

void setup() {
  // Démarrage minimal : relais, série, OTA partition, WiFi
  pinMode(RELAIS, OUTPUT);
  digitalWrite(RELAIS, 1);

  Serial.begin(115200);

  n3OtaSyncBootPartition();
  WiFi.mode(WIFI_MODE_STA);

  // OTA périodique : vérification au boot seulement si la cadence 2h est atteinte
  Wificonnect();
  Serial.println("[WIFI] Connexion initiale OK");
  maybeRunPeriodicOtaCheck("boot");

  // Après OTA : affichage, capteurs, NTP, etc.
  displayOk = n3DisplayInit(display);
  if (displayOk) {
    delay(600);
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(" Demarrage");
    display.println(" ");
    display.println("  msp1");
    display.print("  v:");
    display.println(version);
    display.display();
  }

  pinMode(HumiditeSol, INPUT);
  pinMode(27, INPUT);
  pinMode(pontdiv, INPUT);
  pinMode(LUMINOSITEa, INPUT);
  pinMode(LUMINOSITEb, INPUT);
  pinMode(LUMINOSITEc, INPUT);
  pinMode(LUMINOSITEd, INPUT);
  pinMode(SERVOGD, OUTPUT);
  pinMode(SERVOHB, OUTPUT);

  servogd.attach(SERVOGD);
  servohb.attach(SERVOHB);

  pinMode(DHTPININT, INPUT);
  pinMode(DHTPINEXT, INPUT);
  dhtint.begin();
  dhtext.begin();
  Serial.println("[DHT] Initialisation OK");

  sensors.begin();
  sensors.setResolution(10);
  Serial.println("[DS18B20] Initialisation OK");

  for (int i = 0; i < NUM_SAMPLES; i++) {
    samples[i] = 0;
  }
  Serial.println("[LUM] Initialisation capteurs OK");

  print_wakeup_reason();

  // Configuration et synchronisation temporelles
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("[NTP] Configuration terminee");

  //printLocalTime();

  // Mettre à jour les informations depuis ESP (définitions)
  variablestoesp();
  Serial.println("[SERVER][GET] Variables distantes synchronisees");

  // Initialiser les servos à des valeurs par défaut
  //servogd.write(AngleServoGD);
  //servohb.write(AngleServoHB);
  ++bootCount;  // Incrémenter le compteur de démarrage

  N3SleepConfig sleepCfg = { N3_WAKEUP_GPIO, HIGH, (unsigned long)FreqWakeUp };
  n3SleepConfigure(sleepCfg);
  print_wakeup_reason();
}

void loop() {
  // Mise à jour du temps NTP
  //printLocalTime();
  digitalWrite(RELAIS, 1);  // Activer le relais

  // Initialisation du serveur Web
  server.begin();

  if (WiFi.status() != WL_CONNECTED) {  // Vérification de la connexion WiFi
    Wificonnect();
  }

  // Reconnexion si nécessaire
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }

  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));  // Affichage heure RTC au format indiqué

  //printLocalTime();

  variablestoesp();  // Mise à jour des variables depuis la BDD

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

  LectureCapteurs();

  batterie();

  affichageOLED();

  Light_val();  // Suivi de la lumière

  accumulateOtaPeriodicElapsedFromSleep(FreqWakeUp);
  sommeil();

  // Envoi régulier des données mesurées
  unsigned long currentMillisDatas = millis();
  if (currentMillisDatas - previousMillisDatas >= intervalDatas) {
    previousMillisDatas = currentMillisDatas;
    datatobdd();
    EnregistrementHeureFlash();
  }

  photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
  posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
  delay(100);  // Pause entre chaque balayage
}
