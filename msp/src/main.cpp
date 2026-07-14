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
#include <Wire.h>
#include <esp_sleep.h>
#include <cstring>
#include "credentials.h"
#include "n3_ota_ui.h"
#include "n3_display.h"
#include "n3_sleep.h"

// ============================================================
// Définitions des variables globales
// ============================================================

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
const char* serverNameOutput = MSP_SERVER_SCHEME "iot.olution.info/msp1-test/api/outputs/state?board=2";
const char* serverNameHeartbeat = MSP_SERVER_SCHEME "iot.olution.info/msp1-test/heartbeat";
#else
const char* serverNamePostData = MSP_SERVER_SCHEME "iot.olution.info/msp1/post-data";
const char* serverNameOutput = MSP_SERVER_SCHEME "iot.olution.info/msp1/api/outputs/state?board=2";
const char* serverNameHeartbeat = MSP_SERVER_SCHEME "iot.olution.info/msp1/heartbeat";
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
String outputsState;

// Reset distant: edge detection with first-sample seeding to avoid reboot loops
// if server state is already "110=1" at boot. RTC_DATA_ATTR : en deep sleep,
// loop() ne tourne qu'une fois par reveil ; sans persistance a travers le sommeil
// le front montant du reset distant n'etait jamais observe (juste re-amorce).
RTC_DATA_ATTR static bool s_resetEdgeInitialized = false;
RTC_DATA_ATTR static bool s_lastResetModeState = false;
// Calibration LDR distante (cle 114) : detection de front sur changement de
// valeur, persistee en RTC (meme mecanique que le reset 110). La demande de
// calibration (114=1) reste armee jusqu'a une execution reussie — il faut de
// la lumiere, une demande recue de nuit est retentee au reveil suivant.
RTC_DATA_ATTR static bool s_calibEdgeInitialized = false;
RTC_DATA_ATTR static int s_lastCalibCommand = 0;
RTC_DATA_ATTR static bool s_calibPending = false;
// Harnais OTA periodique + ecran OLED delegue a shared/n3_ota_ui (T4.2).
// Le compteur cumule reste possede ici (RTC_DATA_ATTR, survit au deep sleep) ;
// la lib le manipule via le pointeur de la config. Initialise A l'intervalle
// pour declencher un check au tout premier boot.
RTC_DATA_ATTR static uint32_t s_otaElapsedSinceLastCheckSeconds = OtaPeriodic::kDefaultIntervalSeconds;
static N3OtaUiContext s_otaUiContext;

static void initOtaUi() {
  const N3OtaUiConfig otaUiConfig = {
      "MSP OTA",
      "http://iot.olution.info/ota/msp/metadata.json",
      "http://iot.olution.info/ota/msp-test/metadata.json",
#ifdef TEST_MODE
      true,
#else
      false,
#endif
      FIRMWARE_VERSION,
      &display,
      &displayOk,
      OtaPeriodic::kDefaultIntervalSeconds,
      &s_otaElapsedSinceLastCheckSeconds
  };
  n3OtaUiInit(s_otaUiContext, otaUiConfig);
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

  // OTA périodique : vérification au boot seulement si la cadence 2h est atteinte
  initOtaUi();
  Wificonnect();
  Serial.println("[WIFI] Connexion initiale OK");
  n3OtaUiMaybePeriodicCheck(s_otaUiContext, "boot");

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
  // Reprise de la derniere position (RTC RAM) apres un reveil deep sleep ;
  // repli sur le milieu de plage en cold boot (valeur RTC invalide), pour
  // eviter qu'un servo demarre dans une position aleatoire.
  AngleServoGD = (rtcAngleServoGD >= minAngleServoGD && rtcAngleServoGD <= maxAngleServoGD)
                     ? rtcAngleServoGD
                     : (minAngleServoGD + maxAngleServoGD) / 2;
  AngleServoHB = (rtcAngleServoHB >= minAngleServoHB && rtcAngleServoHB <= maxAngleServoHB)
                     ? rtcAngleServoHB
                     : (minAngleServoHB + maxAngleServoHB) / 2;
  servogd.write(AngleServoGD);
  servohb.write(AngleServoHB);
  Serial.printf("[SERVO][INIT] angleGD=%d angleHB=%d (source=%s)\n", AngleServoGD, AngleServoHB,
                (AngleServoGD == rtcAngleServoGD && AngleServoHB == rtcAngleServoHB) ? "rtc" : "repli_milieu");
  mspTrackerLoadCalibration();

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

  // Attente bornee de la synchro SNTP avant le premier POST. Sans cela, un cold
  // boot pouvait dater/signer le POST avec une heure NVS perimee (hors fenetre
  // serveur SIG_VALID_WINDOW) -> rejet 401 et mesure perdue. Sur un reveil timer,
  // l'heure est deja restauree (epoch > seuil) et la boucle sort immediatement.
  if (WiFi.status() == WL_CONNECTED) {
    const unsigned long ntpWaitStart = millis();
    while ((unsigned long)rtc.getEpoch() < 1577836800UL &&
           (millis() - ntpWaitStart) < 5000UL) {
      delay(100);
    }
    Serial.printf("[NTP] epoch=%lu (attente %lums)\n",
                  (unsigned long)rtc.getEpoch(),
                  (unsigned long)(millis() - ntpWaitStart));
  }

  //printLocalTime();

  // Mettre à jour les informations depuis ESP (définitions)
  variablestoesp();
  Serial.println("[SERVER][GET] Variables distantes synchronisees");
  Serial.println("[SERVO][TRACE] Tags actifs: [SERVO][MODE] [SERVO][TARGET] [SERVO][AUTO] [SERVO][APPLY]");

  // Initialiser les servos à des valeurs par défaut
  //servogd.write(AngleServoGD);
  //servohb.write(AngleServoHB);
  ++bootCount;  // Incrémenter le compteur de démarrage

  N3SleepConfig sleepCfg = { N3_WAKEUP_GPIO, HIGH, (unsigned long)FreqWakeUp };
  n3SleepConfigure(sleepCfg);
  print_wakeup_reason();
}

void loop() {
  static bool ntpConfigured = false;

  digitalWrite(RELAIS, 1);

  // Pas de server.begin() ici : aucune route locale n'est enregistree, donc
  // pas besoin de relancer le serveur asynchrone a chaque cycle (cf. audit 2.42).

  if (WiFi.status() != WL_CONNECTED) {
    Wificonnect();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WIFI] Connecte");
    } else {
      Serial.println("[WIFI][WARN] Non connecte, cycle en mode degrade");
    }
  }

  // configTime n'est utile qu'une fois par reveil WiFi.
  if (WiFi.status() == WL_CONNECTED && !ntpConfigured) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ntpConfigured = true;
    Serial.println("[TIME] configTime appele (1x/reveil)");
  }

  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));

  variablestoesp();  // Mise a jour des variables depuis la BDD

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
    if (!n3OtaUiCheckNow(s_otaUiContext)) {
      Serial.println("[REMOTE][OTA] Aucune MAJ OTA dispo, reset direct");
      ESP.restart();
    }
  }
  s_lastResetModeState = resetRequested;

  // Calibration LDR distante (cle 114) : 1 = calibrer (front), 2 = gains
  // neutres (front). Le front est detecte sur changement de valeur ; pour
  // relancer une calibration, repasser la cle a 0 puis a 1.
  if (!s_calibEdgeInitialized) {
    s_lastCalibCommand = ldrCalibCommand;
    s_calibEdgeInitialized = true;
    if (ldrCalibCommand != 0) {
      Serial.printf("[SERVO][CALIB] seed cle 114=%d au premier poll (pas d'action)\n", ldrCalibCommand);
    }
  } else if (ldrCalibCommand != s_lastCalibCommand) {
    if (ldrCalibCommand == 1) {
      s_calibPending = true;
      Serial.println("[SERVO][CALIB] demande de calibration recue (cle 114=1)");
    } else if (ldrCalibCommand == 2) {
      mspTrackerResetCalibration();
      s_calibPending = false;
    }
    s_lastCalibCommand = ldrCalibCommand;
  }
  if (s_calibPending && mspTrackerCalibrate()) {
    s_calibPending = false;
  }

  LectureCapteurs();

  batterie();

  affichageOLED();

  Light_val();  // Suivi de la lumiere et tracker solaire

  // Envoi periodique des donnees AVANT le deep sleep (sinon ce bloc n'etait
  // jamais atteint, cf. audit 2.42) : sommeil() peut declencher n3SleepStart()
  // qui ne rend jamais la main.
  unsigned long currentMillisDatas = millis();
  if (currentMillisDatas - previousMillisDatas >= intervalDatas) {
    previousMillisDatas = currentMillisDatas;
    datatobdd();
    EnregistrementHeureFlash();
  }

  // Comptabilise le temps ECOULE pour l'OTA periodique. En deep sleep (WakeUp==0),
  // loop() tourne une fois par reveil puis dort FreqWakeUp s -> on compte FreqWakeUp.
  // En mode eveille (WakeUp==1), sommeil() ne dort pas et loop() re-tourne vite :
  // ajouter FreqWakeUp a chaque tour declenchait le check OTA en rafale. On mesure
  // alors l'ecoule reel via millis().
  static unsigned long s_lastOtaTimerMillis = 0;
  const unsigned long nowOtaTimerMs = millis();
  int elapsedForOta;
  if (WakeUp == 0) {
    elapsedForOta = FreqWakeUp;
  } else {
    elapsedForOta = (s_lastOtaTimerMillis == 0)
                        ? 0
                        : (int)((nowOtaTimerMs - s_lastOtaTimerMillis) / 1000UL);
  }
  s_lastOtaTimerMillis = nowOtaTimerMs;
  n3OtaUiAccumulateElapsed(s_otaUiContext, elapsedForOta);
  mspAccumulateNetReportElapsedFromSleep(elapsedForOta);
  mspMaybeSendNetworkReportEmail();
  sendHeartbeat();
  sommeil();

  // Reset des accumulateurs servo apres le sommeil (utile seulement si WakeUp=1).
  photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
  posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
  // Pas de delay(100) ici : le timing est gere par sommeil() / Light_val() ;
  // l'ancienne pause faisait juste consommer du CPU si WakeUp=1.
}
