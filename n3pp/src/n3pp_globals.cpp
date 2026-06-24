/* N3PhasmesProto (n3pp) — Definitions des variables globales
 * Extrait de main.cpp lors de la modernisation 4.38 pour ramener
 * main.cpp sous la barre des 300 lignes (cf. conventions-firmwares.mdc).
 */

#include "n3pp_globals.h"

// ============================================================
// Variables d'etat issues et pour la BDD
// ============================================================

int HeureArrosage = 6;
int SeuilSec = 5000;
bool WakeUp = 0;
int FreqWakeUp = N3_DEFAULT_FREQ_WAKE_UP_S;  // Defaut deep sleep (s), surchargeable par GPIO 107.
bool ArrosageManu = 0;
bool resetMode = 0;

float temperatureAir;
float h;

int HumidMoy;
int photocellReading;
bool etatPompe = 0;
bool etatRelais = 0;

int tempsArrosageMill = 1000;
int tempsArrosageSec = 4;
int tempsArrosage = tempsArrosageSec * tempsArrosageMill;

int Humid1;
int Humid2;
int Humid3;
int Humid4;
int soilValidCount = 0;

// Intervalle entre deux lectures capteurs (en ms).
unsigned long previousMillisDatas = 0;
extern const long intervalDatas = N3_DATA_INTERVAL_MS;

// Indicateurs : email d'alerte deja envoye (anti-spam). RTC_DATA_ATTR pour
// survivre au deep sleep (sinon re-spam a chaque reveil tant que la condition dure).
RTC_DATA_ATTR bool emailHumidSent = 0;
RTC_DATA_ATTR bool emailPontDivSent = 0;
RTC_DATA_ATTR bool arrosageFait = 1;

// Compteur de demarrages (RTC RAM).
RTC_DATA_ATTR int bootCount = 0;
bool WakeUpButton = 0;

RTC_DATA_ATTR String inputMessageMailAd = SMTP_DEST;
RTC_DATA_ATTR String enableEmailChecked = "checked";

String emailMessage;

// (Session SMTP globale retirée : envoi mail factorisé dans n3_mail,
//  qui crée une SMTPSession locale le temps de l'envoi.)

// ============================================================
// Batterie et pont diviseur
// ============================================================

int PontDiv;
int avgPontDiv;
float batt;
float measuredVoltage;
float batteryVoltage;
int SeuilPontDiv = 1700;  // Seuil pour batterie faible (override possible via GPIO 103).
extern const float ADC_MAX_VALUE = 4095.0;
extern const float V_REF = N3_BATTERY_VREF;
extern const float calibration = 0.06;
int samples[NUM_SAMPLES];
int sampleIndex = 0;
int sampleTotal = 0;

// ============================================================
// URLs serveur (production / test)
// ============================================================

// Schema serveur : HTTP par defaut (comportement historique inchange).
// Avec le flag de build USE_HTTPS_ENDPOINTS, bascule en https:// (TLS).
// Voir docs/HTTPS_MIGRATION.md (activation, rollback, validation cible).
#if defined(USE_HTTPS_ENDPOINTS)
  #define N3PP_SERVER_SCHEME "https://"
#else
  #define N3PP_SERVER_SCHEME "http://"
#endif

#ifdef TEST_MODE
const char* serverNamePostData = N3PP_SERVER_SCHEME "iot.olution.info/n3pp-test/n3ppdatas/post-n3pp-data.php";
const char* serverNameOutput = N3PP_SERVER_SCHEME "iot.olution.info/n3pp-test/n3ppcontrol/n3pp-outputs-action.php?action=outputs_state&board=3";
#else
const char* serverNamePostData = N3PP_SERVER_SCHEME "iot.olution.info/n3pp/n3ppdatas/post-n3pp-data.php";
const char* serverNameOutput = N3PP_SERVER_SCHEME "iot.olution.info/n3pp/n3ppcontrol/n3pp-outputs-action.php?action=outputs_state&board=3";
#endif

String version = FIRMWARE_VERSION;

String apiKeyValue = API_KEY;
String sensorName = "n3pp";
String sensorLocation = "T06";

// ============================================================
// Affichage OLED et WiFi
// ============================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool displayOk = false;

const char* ssid = WIFI_SSID1;
const char* password = WIFI_PASS1;
const char* ssid2 = WIFI_SSID2;
const char* password2 = WIFI_PASS2;
const char* ssid3 = WIFI_SSID3;
const char* password3 = WIFI_PASS3;

String Wifiactif;

AsyncWebServer server(80);

WiFiUDP wifiUdp;  // Reserve pour usage NTP / debug ; pas de routes locales actives.

String outputsState;

// ============================================================
// Temps RTC / NTP
// ============================================================

const char* ntpServer = N3_NTP_SERVER;
extern const long gmtOffset_sec = N3_GMT_OFFSET;
extern const int daylightOffset_sec = N3_DAYLIGHT_OFFSET;
ESP32Time rtc;
Preferences preferences;
int seconde;
int minute;
int heure;
int jour;
int mois;
int annee;

// Code de reponse HTTP (requetes GET/POST) - lecture par les modules reseau.
unsigned int httpResponseCode;

// Capteur DHT (temperature / humidite air).
DHT dht(DHTPIN, DHTTYPE);
