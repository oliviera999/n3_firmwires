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
// Variables globales : definitions extraites dans msp_globals.cpp
// (preal. T6 lot 0 — main.cpp ne garde que l'etat module-local).
// ============================================================

unsigned long previousMillisDatas = 0;

AsyncWebServer server(80);

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

// --- Temps NTP (constantes locales a main.cpp ; rtc/preferences/calendrier
// dans msp_globals.cpp) ---
const char* ntpServer = MSP_NTP_SERVER;
const long gmtOffset_sec = MSP_GMT_OFFSET_SEC;
const int daylightOffset_sec = MSP_DAYLIGHT_OFFSET_SEC;

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
