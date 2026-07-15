/* N3PhasmesProto (n3pp)
 * Serre / aquaponie — salle aeree n3
 * Credentials externalises dans credentials.h
 * OTA HTTP distant via n3_common
 * Globals : n3pp_globals.cpp (extraits depuis main.cpp en v4.38)
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
#include "n3_ota_ui.h"
#include "n3_display.h"
#include "n3_sleep.h"

// ============================================================
// OTA periodique (toutes les 2 h, cumul RTC du deep sleep)
// + reset distant : front montant sur GPIO 110, OTA prioritaire.
// ============================================================

// RTC_DATA_ATTR : en mode deep sleep loop() ne tourne qu'une fois par reveil.
// Sans persistance a travers le sommeil, le 1er poll ne faisait que re-amorcer
// l'etat et un front montant du reset distant (110) n'etait JAMAIS observe.
RTC_DATA_ATTR static bool s_resetEdgeInitialized = false;
RTC_DATA_ATTR static bool s_lastResetModeState = false;
// Harnais OTA periodique + ecran OLED delegue a shared/n3_ota_ui (T4.2).
// Le compteur cumule reste possede ici (RTC_DATA_ATTR, survit au deep sleep) ;
// la lib le manipule via le pointeur de la config. Initialise A l'intervalle
// pour declencher un check au tout premier boot.
RTC_DATA_ATTR static uint32_t s_otaElapsedSinceLastCheckSeconds = OtaPeriodic::kDefaultIntervalSeconds;
static N3OtaUiContext s_otaUiContext;

static void initOtaUi() {
  const N3OtaUiConfig otaUiConfig = {
      "N3PP OTA",
      "http://iot.olution.info/ota/n3pp/metadata.json",
      "http://iot.olution.info/ota/n3pp-test/metadata.json",
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

// ============================================================
// setup() / loop()
// ============================================================

void setup() {
  // Brown-out detector desactive pendant le boot (pic de courant a l'init WiFi),
  // puis RE-ACTIVE en fin de setup() : sur un noeud sur batterie, le laisser
  // desactive en fonctionnement exposait les ecritures flash/NVS/OTA a une tension
  // basse (risque de corruption). On sauvegarde la valeur d'origine pour la restaurer.
  // NB: a valider sur cible ; si des resets brown-out apparaissent au demarrage
  // WiFi, retirer la restauration en fin de setup().
  const uint32_t savedBrownOutReg = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(POMPE, OUTPUT);
  pinMode(RELAIS, OUTPUT);
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

  // OTA periodique : verification au boot uniquement si la cadence 2h est atteinte.
  initOtaUi();
  Wificonnect();
  n3OtaUiMaybePeriodicCheck(s_otaUiContext, "boot");

  print_wakeup_reason();

  pinMode(humidite1, INPUT);
  pinMode(humidite2, INPUT);
  pinMode(humidite3, INPUT);
  pinMode(humidite4, INPUT);
  pinMode(pontdiv, INPUT);
  pinMode(LUMINOSITE, INPUT);

  dht.begin();
  Serial.println("[DHT] Initialisation OK");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Attente bornee de la synchro SNTP avant le premier POST : sans cela un cold
  // boot pouvait dater/signer le POST avec une heure NVS perimee (hors fenetre
  // serveur SIG_VALID_WINDOW) -> rejet et mesure perdue. Sur reveil timer l'heure
  // est deja restauree (epoch > seuil) : la boucle sort immediatement.
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

  variablestoesp();
  etatPompe = 0;
  etatRelais = 1;
  Serial.printf("[REMOTE] resetMode(setup apres sync)=%d\n", resetMode ? 1 : 0);

  ++bootCount;
  Serial.println("[BOOT] Compteur demarrages: " + String(bootCount));

  N3SleepConfig sleepCfg = { N3_WAKEUP_GPIO, HIGH, (unsigned long)FreqWakeUp };
  n3SleepConfigure(sleepCfg);

  // Re-active le brown-out detector pour proteger les ecritures flash/NVS/OTA
  // pendant le fonctionnement (voir note en debut de setup()).
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, savedBrownOutReg);
}

void loop() {
  static bool ntpConfigured = false;

  digitalWrite(RELAIS, 1);

  if (WiFi.status() != WL_CONNECTED) {
    Wificonnect();
  }

  // configTime n'est utile qu'une fois par reveil WiFi.
  if (WiFi.status() == WL_CONNECTED && !ntpConfigured) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ntpConfigured = true;
    Serial.println("[TIME] configTime appele (1x/reveil)");
  }

  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));

  etatRelais = 1;
  Serial.println("[SERVER][GET] Poll configuration distante");
  variablestoesp();

  // Reset mode distant (GPIO 110) : front montant, OTA prioritaire.
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

  // Alerte si la pompe est active (etat envoye au serveur). Latch emailPompeSent :
  // un seul mail Critical + POST sur la transition vers "pompe active" ; sinon,
  // pompe maintenue ON par le serveur -> flood de mails/POST a chaque reveil.
  if (digitalRead(POMPE) == 1) {
    etatPompe = 1;
    if (!emailPompeSent) {
      Serial.println("[SERVER][POST] Envoi immediat (pompe active)");
      // POST immediat conserve : c'est la donnee (etatPompe=1) dont le serveur
      // derive lui-meme l'alerte « arrosage continu » (n3_serveur 6.18.0).
      datatobdd();
      // Phase 3 arbitrage : si ce POST a reussi, le serveur est l'emetteur
      // primaire de l'alerte -> latch sans mail local ; sinon failover P1.
      if (postOkThisWake) {
        emailPompeSent = true;
      } else {
        emailMessage = String("ATTENTION, arrosage continu en cours !");
        // Phase 0 : latch uniquement sur livraison confirmee (retente sinon).
        if (sendEmailNotification(N3Severity::Critical)) {
          emailPompeSent = true;
        }
      }
    }
  } else {
    emailPompeSent = false;  // pompe relachee : re-arme l'alerte
  }

  lectureCapteurs();
  batterie();

  affichageOLED();
  automatismes();

  etatRelais = 1;

  // Envoi periodique AVANT le deep sleep (sommeil() peut ne pas rendre la main).
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
    Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  }

  // Comptabilise le temps ECOULE pour le cooldown arrosage, l'OTA et le rapport reseau.
  // En mode deep sleep (WakeUp==0), loop() ne tourne qu'une fois par reveil puis dort
  // FreqWakeUp secondes : on comptabilise donc FreqWakeUp. En mode eveille (WakeUp==1),
  // sommeil() ne dort pas et loop() re-tourne toutes les quelques secondes : ajouter
  // FreqWakeUp a chaque tour faisait exploser les compteurs (OTA/rapport en rafale,
  // cooldown arrosage neutralise). On mesure alors l'ecoule reel via millis().
  static unsigned long s_lastTimerMillis = 0;
  const unsigned long nowTimerMs = millis();
  int elapsedForTimers;
  if (WakeUp == 0) {
    elapsedForTimers = FreqWakeUp;  // deep sleep imminent de FreqWakeUp s
  } else {
    elapsedForTimers = (s_lastTimerMillis == 0)
                           ? 0
                           : (int)((nowTimerMs - s_lastTimerMillis) / 1000UL);
  }
  s_lastTimerMillis = nowTimerMs;
  n3OtaUiAccumulateElapsed(s_otaUiContext, elapsedForTimers);
  n3ppAccumulateNetReportElapsedFromSleep(elapsedForTimers);
  n3ppMaybeSendNetworkReportEmail();
  arrosageAutoAccumulateCooldown(elapsedForTimers);
  sendHeartbeat();
  sommeil();
}
