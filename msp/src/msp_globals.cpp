/* MeteoStationPrototype (msp1) — Definitions des variables globales
 * Extrait de main.cpp (preal. T6 lot 0 du chantier core shared, meme
 * modernisation que n3pp_globals.cpp en 4.38) : les declarations extern
 * vivent dans include/msp_globals.h, les definitions ici, verbatim.
 * Restent dans main.cpp : les statics module-locaux (fronts reset/calib,
 * contexte OTA), previousMillisDatas, le serveur web et les constantes NTP
 * (usage strictement local a main.cpp).
 */

#include "msp_globals.h"

#include <Wire.h>
#include "credentials.h"

// --- Capteurs température sol (DS18B20) ---
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);
float temperatureSol;

// --- Luminosité ---
int photocellReadingA = 0, photocellReadingB = 0, photocellReadingC = 0, photocellReadingD = 0;
int photocellReadingMoy = 0;

// --- Servos (tracker solaire) ---
Servo servogd;
Servo servohb;
int posLumMax1 = 0, posLumMax2 = 0, posLumMax3 = 0, posLumMax4 = 0;
int AngleServoGD;
int AngleServoHB;
bool servoModeAuto = true;
bool trackerModeSweep = false;  // défaut : asservissement différentiel (audit tracker 2026-07)
int ldrCalibCommand = 0;        // clé serveur 114 (calibration LDR), 0 = repos
// Derniere position appliquee, persistee en RTC RAM : au reveil deep sleep on
// repart de la position physique reelle au lieu du milieu de plage (-1 = cold
// boot, invalide). Evite l'aller-retour inutile et rend l'asservissement
// differentiel quasi instantane quand le soleil a peu bouge.
RTC_DATA_ATTR int rtcAngleServoGD = -1;
RTC_DATA_ATTR int rtcAngleServoHB = -1;

// --- DHT intérieur / extérieur ---
DHT dhtint(DHTPININT, DHTTYPEINT);
DHT dhtext(DHTPINEXT, DHTTYPEEXT);

// --- BME280 optionnels (v2.73, pattern ffp5cs USE_AIR_SENSOR_AUTO) ---
// Sondés au setup : si détectés sur I2C, ils remplacent les DHT (INT=0x76,
// EXT=0x77 — SDO à VDD sur le second module). Sinon, chemin DHT inchangé.
N3Bme280 bmeInt;
N3Bme280 bmeExt;
float pressionAirInt = NAN;  // hPa (BME280 INT uniquement, NAN sinon)
//variables T et H pour les DHT
float tempAirInt;
float humidAirInt;
float tempAirExt;
float humidAirExt;

// --- Deep sleep ---
bool WakeUp = 0;
int FreqWakeUp = N3_DEFAULT_FREQ_WAKE_UP_S;  // Defaut deep sleep (s), surchargeable par GPIO 107.
// Interrupteur veille infinie sous seuil batterie (override GPIO 112). Defaut 1
// = comportement historique ; si le serveur est injoignable la protection reste
// active (fail-safe batterie).
bool VeilleInfinie = 1;

// --- Batterie / pont diviseur ---
int PontDiv;
int avgPontDiv;
float measuredVoltage;
float batteryVoltage;
int SeuilPontDiv = 1700;
int samples[NUM_SAMPLES];
int sampleIndex = 0;
int sampleTotal = 0;

// --- Seuils / états ---
// Defaut dans la plage ADC 0..4095 (la cle serveur 102 est deja bornee a cette
// plage dans msp_network.cpp) ; aligne sur la valeur corrigee de n3pp (1500).
// msp n'a pas d'action locale sur le sol : SeuilSec est seulement POSTe/affiche.
int SeuilSec = 1500;
bool resetMode = 0;
bool etatRelais = 0;

// --- Capteurs analogiques ---
int HumidSol;
int Pluie;

// --- Email ---
RTC_DATA_ATTR int bootCount = 0;
// Phase 3 arbitrage mails : succes du POST de donnees de CE reveil (HTTP 200).
// true  -> le serveur a nos donnees, il est l'emetteur PRIMAIRE de l'alerte
//          batterie (seule alerte partagee msp) : l'ESP se tait dessus ;
// false -> FAILOVER : l'ESP emet, borne par l'anti-congestion (P1/P2 only,
//          WiFi requis, budget). RTC pour rester coherent sur tout le cycle.
RTC_DATA_ATTR bool postOkThisWake = false;
// Budget de mails failover par episode hors-ligne (§3.4-3), re-arme au POST OK.
RTC_DATA_ATTR uint8_t failoverMailsSent = 0;
RTC_DATA_ATTR String inputMessageMailAd = SMTP_DEST;
RTC_DATA_ATTR String enableEmailChecked = "checked";
String emailMessage;

/* Session SMTP désormais locale à n3_mail (plus de global). */

// --- Réseau ---
#ifdef TEST_MODE
const char* serverNamePostData = MSP_SERVER_SCHEME "iot.olution.info/msp1-test/post-data";
const char* serverNameOutput = MSP_SERVER_SCHEME "iot.olution.info/msp1-test/api/firmware/outputs/state?board=2";
const char* serverNameHeartbeat = MSP_SERVER_SCHEME "iot.olution.info/msp1-test/heartbeat";
#else
const char* serverNamePostData = MSP_SERVER_SCHEME "iot.olution.info/msp1/post-data";
const char* serverNameOutput = MSP_SERVER_SCHEME "iot.olution.info/msp1/api/firmware/outputs/state?board=2";
const char* serverNameHeartbeat = MSP_SERVER_SCHEME "iot.olution.info/msp1/heartbeat";
#endif

unsigned int httpResponseCode;
String version = FIRMWARE_VERSION;
String apiKeyValue = API_KEY;
String sensorName = "msp1";

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

String outputsState;

// --- Temps RTC / NTP ---
ESP32Time rtc;
Preferences preferences;
int seconde;
int minute;
int heure;
int jour;
int mois;
int annee;
