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
#include "n3_app.h"   // T6.2 : squelette de cycle deep-sleep a callbacks (n3AppRun)

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

// ============================================================
// T6.2 (chantier shared) : adoption du squelette n3_app (n3AppRun).
//
// Le cycle de reveil, jusqu'ici lineaire dans loop(), est exprime comme des
// callbacks (N3AppConfig), executes par n3AppRun() dans l'ordre canonique
// WAKE -> WIFI -> CLOCK -> REMOTE_CFG -> OTA -> SENSE -> PAYLOAD -> AUTOMATION
// -> REPORTS -> SLEEP. Chaque callback est une FINE ENVELOPPE du code EXISTANT,
// deplace VERBATIM et appele au meme instant relatif : aucun changement
// observable (memes octets POST, memes logs, meme timing, memes conditions).
//
// Mapping n3pp -> slots. Comme msp POSTe APRES son actionneur (le tracker), n3pp
// POSTe (bloc periodique) APRES automatismes() : on rassemble donc automatismes()
// dans le callback SENSE (comme msp met Light_val dans SENSE) et on laisse le
// slot AUTOMATION vide, pour que le POST periodique (PAYLOAD) reste bien apres
// automatismes() sans reordonner.
//   onWake              = nullptr
//   connectWifi (WIFI)  = digitalWrite(RELAIS) + reconnexion WiFi
//                         (HeureSansWifi en echec = dans Wificonnect, piege A6)
//   syncClock (CLOCK)   = configTime (1x/reveil) + affichage heure
//   applyRemoteConfig   = etatRelais=1 + variablestoesp() + front reset (110)
//   isOtaDue/otaCheck   = nullptr (aucun check OTA periodique dans loop() ;
//                         n3OtaUiMaybePeriodicCheck reste dans setup(), inchange)
//   readSensors (SENSE) = alerte pompe (POST latche) + lectureCapteurs/batterie/
//                         affichageOLED + automatismes (arrosage + alertes)
//   buildAndSendPayload = etatRelais=1 + POST periodique + EnregistrementHeureFlash
//   runAutomation       = nullptr (automatismes deja dans SENSE)
//   sendReports (REPORTS)= comptage temps ecoule + OTA/rapport reseau + cooldown
//                         arrosage + heartbeat
//   enterSleep (SLEEP)  = restart (reset distant) / veille infinie batterie /
//                         sommeil() normal
//
// Reset distant : l'ancien ESP.restart() inline devient une sortie anticipee
// (ctx.requestRestart) honoree par enterSleep ; le `return` apres l'avoir pose
// reproduit EXACTEMENT l'abandon du reste par ESP.restart() (s_lastResetModeState
// RTC non mis a jour sur le chemin restart, comme avant).
// Veille batterie mid-cycle : automatismes() (dans SENSE) pose
// n3ppVeilleInfinieRequested ; le callback SENSE le traduit en ctx.requestSleepNow
// (le sequenceur saute a SLEEP, sautant PAYLOAD/AUTOMATION/REPORTS comme avant),
// et enterSleep execute le bloc emergency (POST + ecran DODO + veille GPIO) a
// l'identique.
// ============================================================

static void n3ppCbConnectWifi(N3AppContext&) {
  digitalWrite(RELAIS, 1);

  if (WiFi.status() != WL_CONNECTED) {
    Wificonnect();
  }
}

static void n3ppCbSyncClock(N3AppContext&) {
  static bool ntpConfigured = false;

  // configTime n'est utile qu'une fois par reveil WiFi.
  if (WiFi.status() == WL_CONNECTED && !ntpConfigured) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ntpConfigured = true;
    Serial.println("[TIME] configTime appele (1x/reveil)");
  }

  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
}

static void n3ppCbApplyRemoteConfig(N3AppContext& ctx) {
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
      // T6.2 : ex-ESP.restart() inline -> sortie anticipee (enterSleep restart).
      // Le `return` reproduit l'abandon du reste par ESP.restart() :
      // s_lastResetModeState (RTC) n'est PAS mis a jour sur le chemin restart,
      // exactement comme avant (le reboot sautait la ligne ci-dessous).
      ctx.requestRestart = true;
      return;
    }
  }
  s_lastResetModeState = resetRequested;
}

static void n3ppCbReadSensors(N3AppContext& ctx) {
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

  // Veille infinie batterie (mid-cycle) : sortie anticipee -> enterSleep.
  // automatismes() a pose le drapeau et abandonne la suite (branches arrosage) :
  // on saute directement a SLEEP (PAYLOAD/AUTOMATION/REPORTS non executes), comme
  // l'ancien n3SleepStart() inline le faisait.
  if (n3ppVeilleInfinieRequested) {
    ctx.requestSleepNow = true;
  }
}

static void n3ppCbBuildAndSendPayload(N3AppContext&) {
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
}

static void n3ppCbSendReports(N3AppContext&) {
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
}

static void n3ppCbEnterSleep(N3AppContext& ctx) {
  // Reset distant (sortie anticipee) : restart ici, sans passer par sommeil()
  // (aucun log [SLEEP][TRACE]), comme l'ancien ESP.restart() inline.
  if (ctx.requestRestart) {
    ESP.restart();
    return;
  }

  // Veille infinie batterie (sortie anticipee posee dans automatismes()) : bloc
  // emergency rapatrie VERBATIM depuis automatismes() — memes operations, meme
  // ordre (POST final latche + ecran DODO + veille GPIO uniquement). Rien ne
  // s'intercale entre le point de detection (automatismes) et ici (le sequenceur
  // saute droit a SLEEP), donc datatobdd() envoie les memes octets.
  if (n3ppVeilleInfinieRequested) {
    // Harmonisation A8 : POST final + ecran avant la veille infinie (aligne msp,
    // le serveur recoit l'etat batterie PontDiv bas qui justifie la veille).
    datatobdd();
    if (displayOk) {
      display.clearDisplay();
      delay(100);
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println(" ");
      display.println("   DODO");
      display.display();
    }
    delay(1000);
    EnregistrementHeureFlash();
    N3SleepConfig emergencySleep = { N3_WAKEUP_GPIO, HIGH, 0 };
    n3SleepConfigure(emergencySleep);
    Serial.println("[SLEEP][TRACE] start deep sleep mode=emergency timer=0s (wake GPIO uniquement)");
    n3SleepStart();
    return;
  }

  // Sommeil timer normal (gere seul le cas WakeUp=1 = pas de deep sleep).
  sommeil();
}

// Un callback (fine enveloppe) par etape du cycle ; nullptr = etape inerte.
static const N3AppConfig kN3ppAppConfig = {
    nullptr,                       // onWake
    n3ppCbConnectWifi,             // connectWifi (WIFI)
    n3ppCbSyncClock,               // syncClock (CLOCK)
    n3ppCbApplyRemoteConfig,       // applyRemoteConfig (REMOTE_CFG)
    nullptr,                       // isOtaDue (aucun check OTA periodique dans loop)
    nullptr,                       // otaCheck (OTA)
    n3ppCbReadSensors,             // readSensorsOrCapture (SENSE)
    n3ppCbBuildAndSendPayload,     // buildAndSendPayload (PAYLOAD)
    nullptr,                       // runAutomation (automatismes deja dans SENSE)
    n3ppCbSendReports,             // sendReports (REPORTS)
    n3ppCbEnterSleep               // enterSleep (SLEEP)
};

void loop() {
  N3AppContext ctx = {};
  n3AppRun(kN3ppAppConfig, ctx);
}
