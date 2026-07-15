/* N3PhasmesProto (n3pp) — Definitions des variables globales
 * Extrait de main.cpp lors de la modernisation 4.38 pour ramener
 * main.cpp sous la barre des 300 lignes (cf. conventions-firmwares.mdc).
 */

#include "n3pp_globals.h"

// ============================================================
// Variables d'etat issues et pour la BDD
// ============================================================

// Config surchargeable par le serveur : RTC_DATA_ATTR pour conserver la derniere
// valeur appliquee (cles 102/103/104/105/107) a travers le deep sleep quand le
// serveur est injoignable. Sinon chaque reveil hors-ligne repartait du defaut.
RTC_DATA_ATTR int HeureArrosage = 6;
// Defaut "sol sec" : 1500 est dans la plage ADC 12 bits (HumidMoy = moyenne
// brute 0..4095). L'ancien defaut 5000 etait inatteignable -> HumidMoy < SeuilSec
// toujours vrai (sol "sec" en permanence) et l'hysteresis SeuilSec+5% jamais
// atteignable. Clamp applique a l'ecriture de la cle 102 (voir n3pp_network.cpp).
RTC_DATA_ATTR int SeuilSec = 1500;
bool WakeUp = 0;
RTC_DATA_ATTR int FreqWakeUp = N3_DEFAULT_FREQ_WAKE_UP_S;  // Defaut deep sleep (s), surchargeable par GPIO 107.
bool ArrosageManu = 0;
bool resetMode = 0;

float temperatureAir;
float h;

int HumidMoy;
int photocellReading;
bool etatPompe = 0;
bool etatRelais = 0;

int tempsArrosageMill = 1000;
RTC_DATA_ATTR int tempsArrosageSec = 4;  // surchargeable par le serveur (cle 105), conserve en RTC.
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
// Anti-spam alerte "pompe active" : evite un mail Critical + POST a chaque
// reveil/iteration tant que le serveur maintient la pompe ON (latch a etat).
RTC_DATA_ATTR bool emailPompeSent = 0;
RTC_DATA_ATTR bool arrosageFait = 1;

// Compteur de demarrages (RTC RAM).
RTC_DATA_ATTR int bootCount = 0;

// Phase 3 arbitrage mails : succes du POST de donnees de CE reveil (code HTTP 200).
// true  -> le serveur a nos donnees, il est l'emetteur PRIMAIRE des alertes
//          partagees (sol sec, batterie) : l'ESP se tait dessus ;
// false -> FAILOVER : l'ESP emet, borne par l'anti-congestion (P1/P2 only,
//          WiFi requis, budget). Recalcule a chaque reveil (deep sleep) ; en RTC
//          pour rester coherent dans tout le cycle (automatismes puis sommeil).
RTC_DATA_ATTR bool postOkThisWake = false;
// Budget de mails failover par episode hors-ligne (anti-congestion §3.4-3) :
// re-arme des qu'un POST repasse OK. RTC pour persister a travers les reveils.
RTC_DATA_ATTR uint8_t failoverMailsSent = 0;

// NB : PAS de RTC_DATA_ATTR ici. Un String detient un pointeur vers le heap ;
// la RTC RAM ne peut pas preserver le heap, et le constructeur global se
// re-execute a chaque boot -> l'attribut n'apportait rien (valeur remise au
// defaut a chaque reveil) tout en etant un motif use-after-free latent. Ces
// deux valeurs sont de toute facon rafraichies depuis le serveur (cles 100/101).
String inputMessageMailAd = SMTP_DEST;
String enableEmailChecked = "checked";

String emailMessage;

// (Session SMTP globale retirée : envoi mail factorisé dans n3_mail,
//  qui crée une SMTPSession locale le temps de l'envoi.)

// ============================================================
// Batterie et pont diviseur
// ============================================================

int PontDiv;
int avgPontDiv;
float measuredVoltage;
float batteryVoltage;
RTC_DATA_ATTR int SeuilPontDiv = 1700;  // Seuil batterie faible (override GPIO 103), conserve en RTC.
// Interrupteur veille infinie sous seuil batterie (override GPIO 112), conserve
// en RTC comme SeuilPontDiv : la derniere valeur serveur survit au deep sleep
// meme si le serveur devient injoignable. Defaut 1 = comportement historique.
RTC_DATA_ATTR bool VeilleInfinie = 1;
extern const float ADC_MAX_VALUE = 4095.0;
extern const float V_REF = N3_BATTERY_VREF;
extern const float calibration = 0.06;
int samples[NUM_SAMPLES];
int sampleIndex = 0;
int sampleTotal = 0;

// ============================================================
// URLs serveur (production / test)
// ============================================================

#ifdef TEST_MODE
const char* serverNamePostData = N3PP_SERVER_SCHEME "iot.olution.info/n3pp-test/post-data";
const char* serverNameOutput = N3PP_SERVER_SCHEME "iot.olution.info/n3pp-test/api/outputs/state?board=3";
const char* serverNameHeartbeat = N3PP_SERVER_SCHEME "iot.olution.info/n3pp-test/heartbeat";
#else
const char* serverNamePostData = N3PP_SERVER_SCHEME "iot.olution.info/n3pp/post-data";
const char* serverNameOutput = N3PP_SERVER_SCHEME "iot.olution.info/n3pp/api/outputs/state?board=3";
const char* serverNameHeartbeat = N3PP_SERVER_SCHEME "iot.olution.info/n3pp/heartbeat";
#endif

String version = FIRMWARE_VERSION;

String apiKeyValue = API_KEY;
String sensorName = "n3pp";

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
