#include "n3pp_automation.h"
#include "n3pp_globals.h"
#include "n3pp_network.h"
#include "n3_sleep.h"
#include "n3_time.h"   // sauvegarde/restauration heure NVS (factorisé)
#include "n3_mail.h"   // envoi SMTP factorisé
#include <WiFi.h>      // Phase 3 : en failover, SMTP seulement si WiFi connecte (§3.4-1)

void HeureSansWifi() {
  n3TimeLoadFromFlash(preferences, rtc);  // NVS "rtc" -> rtc (mêmes clés/défauts qu'avant)
  // Resync des globals firmware depuis le RTC chargé (arrosage/affichage les lisent)
  n3TimeSyncBrokenDown(rtc, seconde, minute, heure, jour, mois, annee);
  if (displayOk) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("Heure depuis flash");
    display.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
    display.display();
  }
  delay(500);
}

//fonction permettant l'enregistrement des variables de temps dans la mémoire flash
void EnregistrementHeureFlash() {
  n3TimeSaveToFlash(rtc, preferences);  // NVS "rtc" (mêmes clés qu'avant)
}

// Mode de notification courant, derive de la config distante enableEmailChecked
// (retro-compatible : "checked" -> Full, "unchecked" -> None).
N3NotifMode n3ppNotifMode() {
  return n3NotifModeFromString(enableEmailChecked.c_str());
}

// Raccourci : au moins un envoi est possible (mode != None). Remplace l'ancien
// test "enableEmailChecked == \"checked\"" sur les sites d'alerte (la severite
// fine est filtree dans sendEmailNotification()).
static bool emailEnabled() {
  return n3ppNotifMode() != N3NotifMode::None;
}

// Budget de mails en failover par episode hors-ligne (anti-congestion §3.4-3) :
// re-arme des qu'un POST repasse OK (voir datatobdd). Evite le martelement TLS
// d'un appareil isole qui accumulerait des alertes a chaque reveil.
static const uint8_t FAILOVER_MAIL_BUDGET = 8;

// Configuration et envoi d'un email d'alerte (SMTP) — delegue a n3_mail.
// La severite est filtree par le mode courant ; le sujet est prefixe "[N3PP][Pn]".
// Phase 0 (arbitrage mails) : retourne true si livraison SMTP confirmee OU si le
// mail est volontairement filtre par le mode (silence choisi = traite) ; false si
// l'envoi a echoue. L'appelant ne latche son flag anti-spam que sur true, sinon
// l'alerte serait consideree "envoyee" et perdue hors ligne.
// Phase 3 (arbitrage mails) : en FAILOVER (POST de ce reveil echoue), anti-
// congestion §3.4 — severite plafonnee a P1/P2, SMTP tente seulement si WiFi
// connecte (sinon l'alerte serveur "appareil silencieux" couvre), budget borne.
bool sendEmailNotification(N3Severity severity) {
  // Mutualisé (T4) : politique failover/budget/format déplacée verbatim dans
  // shared/n3_mail (n3MailNotify). Seule la construction des paramètres reste ici.
  N3MailNotifyParams p{};
  p.projectTag = "N3PP";
  p.mode = n3ppNotifMode();
  p.postOkThisWake = postOkThisWake;
  p.failoverMailsSent = &failoverMailsSent;
  p.failoverMailBudget = FAILOVER_MAIL_BUDGET;
  p.smtp.smtpHost = SMTP_HOST;
  p.smtp.smtpPort = SMTP_PORT;
  p.smtp.authorEmail = AUTHOR_EMAIL;
  p.smtp.authorPassword = AUTHOR_PASSWORD;
  p.smtp.senderName = "OAL";
  p.smtp.recipientName = "OAL";
  p.smtp.recipientEmail = inputMessageMailAd.c_str();
  p.subject = emailSubject;
  p.message = emailMessage.c_str();
  return n3MailNotify(p, severity);
}

// Borne maximale de securite pour la duree d'un arrosage (secondes) : empeche
// qu'une valeur distante 105 farfelue ne bloque la pompe trop longtemps et ne
// fasse expirer le WDT (configure a 30 s, voir platformio.ini).
static const int ARROSAGE_MAX_SECONDS = 20;

// Cooldown minimal entre deux arrosages automatiques (millisecondes RTC).
// Empeche l'arrosage permanent quand le sol reste sec a chaque reveil (3 s).
static const unsigned long ARROSAGE_COOLDOWN_MS = 5UL * 60UL * 1000UL;
RTC_DATA_ATTR static unsigned long s_lastArrosageMillisFromBoot = 0;
RTC_DATA_ATTR static uint32_t s_arrosageCooldownAccumulatorMs = ARROSAGE_COOLDOWN_MS;

void arrosage() {
  int tempsArrosageSecSafe = tempsArrosageSec;
  if (tempsArrosageSecSafe < 1) tempsArrosageSecSafe = 1;
  if (tempsArrosageSecSafe > ARROSAGE_MAX_SECONDS) {
    Serial.printf("[ARROSAGE][WARN] tempsArrosageSec=%d clamp a %d s\n",
                  tempsArrosageSecSafe, ARROSAGE_MAX_SECONDS);
    tempsArrosageSecSafe = ARROSAGE_MAX_SECONDS;
  }
  const unsigned long tempsArrosageMs = (unsigned long)tempsArrosageSecSafe * 1000UL;

  digitalWrite(POMPE, 1);
  Serial.printf("[ARROSAGE] en cours (%d s)\n", tempsArrosageSecSafe);
  // Decoupe le delay en morceaux d'1 s pour pouvoir alimenter le WDT si besoin.
  for (int i = 0; i < tempsArrosageSecSafe; ++i) {
    delay(1000);
  }
  digitalWrite(POMPE, 0);
  Serial.println("[ARROSAGE] termine");
  s_arrosageCooldownAccumulatorMs = 0;
  s_lastArrosageMillisFromBoot = millis();
  delay(500);
}

// Indique si un nouvel arrosage automatique declenche par "sol sec" est autorise.
bool arrosageAutoCooldownExpired() {
  return s_arrosageCooldownAccumulatorMs >= ARROSAGE_COOLDOWN_MS;
}

// Ajoute le temps de sommeil (sleepSeconds) au compteur de cooldown pour
// pouvoir l'utiliser malgre le reset RAM (RTC_DATA_ATTR conserve la valeur).
void arrosageAutoAccumulateCooldown(int sleepSeconds) {
  if (sleepSeconds <= 0) return;
  if (s_arrosageCooldownAccumulatorMs >= ARROSAGE_COOLDOWN_MS) return;
  const uint32_t add = (uint32_t)sleepSeconds * 1000U;
  if (add > ARROSAGE_COOLDOWN_MS - s_arrosageCooldownAccumulatorMs) {
    s_arrosageCooldownAccumulatorMs = ARROSAGE_COOLDOWN_MS;
  } else {
    s_arrosageCooldownAccumulatorMs += add;
  }
}

// Hysteresis autour de SeuilSec : le retour "a la normale" exige de depasser
// SeuilSec + 5 % — evite les paires de mails et arrosages en rafale quand la
// mesure oscille autour du seuil entre deux reveils.
static int seuilRetourNormal() {
  return SeuilSec + (SeuilSec / 20);
}

void automatismes() {
  //remplissage de l'aquarium cas si l'aquarium est trop bas et la réserve assez remplie

  //mail si sécheresse trop forte (uniquement si au moins un capteur sol valide)
  // Phase 3 arbitrage : "sol sec" est desormais calcule par le SERVEUR sur les
  // donnees du POST (HumidMoy/SeuilSec, n3_serveur 6.16.0). Si le POST de ce
  // reveil a reussi, l'ESP se tait (fin des doublons) ; sinon failover local.
  if (!postOkThisWake && (soilValidCount > 0) && (HumidMoy < SeuilSec) && emailEnabled() && !emailHumidSent) {
    emailMessage = String("Le sol est sec. L'humidité moyenne est de ") + String(HumidMoy);
    // Phase 0 : latch uniquement sur livraison confirmee (sinon retente au prochain reveil).
    if (sendEmailNotification(N3Severity::Alert)) {
      emailHumidSent = true;
    }
    Serial.println(emailMessage);
    // SerialBT.println(emailMessage);

    //variablestoesp();
    datatobdd();
  }

  // mail si le niveau est revenu à la normale (hysteresis : SeuilSec + 5 %)
  if ((soilValidCount > 0) && (HumidMoy > seuilRetourNormal()) && emailEnabled() && emailHumidSent) {
    if (postOkThisWake) {
      // Serveur primaire : il notifie lui-meme le retour a la normale. On re-arme
      // le latch failover en silence pour ne pas bloquer un futur episode hors ligne.
      emailHumidSent = false;
    } else {
      emailMessage = String("L'humidite est remontee. La moyenne est maintenant de ") + String(HumidMoy);
      Serial.println(emailMessage);
      // Phase 0 : ne de-latcher qu'apres livraison confirmee du mail de fin.
      // (En failover, l'Info P3 est filtree -> sendEmailNotification renvoie true
      // = traite selon la politique, le latch est re-arme sans mail.)
      if (sendEmailNotification(N3Severity::Info)) {
        emailHumidSent = false;
      }
      datatobdd();
    }
  }

  Serial.print("seuilsec3 : ");
  Serial.println(SeuilSec);
  Serial.print("tempsArrosage3: ");
  Serial.println(tempsArrosage);

  // Protection batterie faible : sommeil protecteur "GPIO uniquement" (aucun
  // reveil timer) pour cesser de vider la batterie au lieu de se reveiller toutes
  // les FreqWakeUp secondes. DECORELE de l'email : la protection s'applique meme
  // si les notifications sont sur "none" (seul l'envoi du mail reste conditionnel).
  if ((PontDiv < SeuilPontDiv)) {
    // Phase 3 arbitrage : batterie calculee par le SERVEUR (PontDiv/SeuilPontDiv
    // au POST). POST de ce reveil OK -> l'ESP se tait ; sinon failover (P1).
    // La protection sommeil ci-dessous reste INCONDITIONNELLE (decorelee du mail).
    if (!postOkThisWake && emailEnabled() && !emailPontDivSent) {
      emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
      Serial.println(emailMessage);
      // Phase 0 : latch uniquement sur livraison confirmee.
      if (sendEmailNotification(N3Severity::Critical)) {
        emailPontDivSent = true;
      }
    }
    // Interrupteur serveur (cle 112) : la mise en veille infinie sous le seuil
    // peut etre desactivee depuis l'interface de controle. Desactivee -> on ne
    // s'endort PAS en mode urgence ici ; le sommeil timer normal (sommeil())
    // reprend la main en fin de cycle. L'alerte batterie ci-dessus reste active.
    if (VeilleInfinie) {
      // Harmonisation A8 (lot 0 T6, chantier shared) : POST final + ecran avant
      // la veille infinie — comportement repris du bloc emergency de sommeil()
      // (supprime : code mort, ce bloc-ci s'endort toujours en premier) et
      // aligne sur msp qui POSTe avant la veille d'urgence. Le serveur recoit
      // ainsi l'etat batterie (PontDiv bas) qui justifie la veille.
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
    } else {
      Serial.println("[SLEEP][TRACE] veille infinie DESACTIVEE (cle 112=0), batterie basse -> sommeil timer normal");
    }
  } else {
    // Batterie revenue au-dessus du seuil : re-arme l'alerte (anti-spam a etat).
    emailPontDivSent = false;
  }

  // Anti-inondation : au plus UN arrosage par cycle automatismes(). Les trois
  // branches ci-dessous (sol sec auto, heure programmee, manuel) n'avaient aucune
  // exclusion mutuelle -> a l'heure programmee avec sol sec + cooldown expire, la
  // branche "sol sec" arrosait (et remettait le cooldown a 0), puis l'heure
  // programmee arrosait de nouveau : double dose + POST en double. Precedence =
  // ordre du code : sol sec auto > heure programmee > manuel. Quand une branche
  // ulterieure est bloquee, son etat (arrosageFait=0, ArrosageManu=1) n'est PAS
  // consomme -> son intention est reportee au cycle suivant (le cooldown empeche
  // alors la re-declenchement de la branche "sol sec").
  bool arrosageEffectueCeCycle = false;

  // Arrosage en cas de secheresse : protege par cooldown pour eviter
  // un arrosage repete a chaque reveil deep sleep si le sol reste sec.
  // Bloque si aucun capteur sol valide (capteurs debranches lus "tres sec").
  if (soilValidCount == 0) {
    Serial.println("[ARROSAGE][SKIP] aucun capteur sol valide, arrosage auto bloque");
  } else if (HumidMoy < SeuilSec) {
    if (arrosageAutoCooldownExpired()) {
      arrosage();
      arrosageEffectueCeCycle = true;
      // Phase 3 arbitrage : confirmation derivee cote serveur (transition etatPompe
      // au POST) quand l'echange est sain ; en failover l'Info P3 est filtree.
      if (!postOkThisWake && emailEnabled()) {
        emailMessage = String("Arrosage auto effectue (sol sec, humidite=") +
                       String(HumidMoy) + String(")");
        Serial.println("[ARROSAGE] auto");
        sendEmailNotification(N3Severity::Info);
      }
      datatobdd();
    } else {
      Serial.printf("[ARROSAGE][SKIP] cooldown actif (%lu/%lu ms cumules)\n",
                    (unsigned long)s_arrosageCooldownAccumulatorMs,
                    (unsigned long)ARROSAGE_COOLDOWN_MS);
    }
  }

  rtc.getTime("%H:%M:%S %d/%m/%Y");
  heure = rtc.getTime("%H").toInt();

  Serial.print("heure : ");
  Serial.println(heure);

  //arrosage auto
  if (HeureArrosage != heure) {
    arrosageFait = 0;
    Serial.println("arrosage pas à l'heure");
  }

  if ((HeureArrosage == heure) && arrosageFait == 0 && !arrosageEffectueCeCycle) {
    arrosage();
    arrosageEffectueCeCycle = true;
    arrosageFait = 1;
    Serial.println("[ARROSAGE] heure programmee effectue");
    Serial.print("arrosageFait=");
    Serial.println(arrosageFait);
    // Phase 3 arbitrage : confirmation derivee cote serveur (transition etatPompe).
    if (!postOkThisWake && emailEnabled()) {
      emailMessage = String("Arrosage heure programmee effectue (") +
                     String(heure) + String("h)");
      sendEmailNotification(N3Severity::Info);
    }
    etatPompe = 1;
    datatobdd();
    etatPompe = 0;
  }

  // Arrosage manuel demande depuis l'interface
  if (ArrosageManu == 1 && !arrosageEffectueCeCycle) {
    datatobdd();
    Serial.print("[ARROSAGE] manuel demande, ArrosageManu=");
    Serial.println(ArrosageManu);
    arrosage();
    arrosageEffectueCeCycle = true;
    ArrosageManu = 0;
    // Phase 3 arbitrage : confirmation derivee cote serveur (transition etatPompe).
    if (!postOkThisWake && emailEnabled()) {
      emailMessage = String("Arrosage manuel effectue");
      sendEmailNotification(N3Severity::Info);
    }
    etatPompe = 1;
    datatobdd();
    etatPompe = 0;
  }
}

void sommeil() {
  Serial.println(String("[SLEEP][TRACE] entree WakeUp=") + String(WakeUp ? 1 : 0) +
                 " FreqWakeUp=" + String(FreqWakeUp) +
                 " resetMode=" + String(resetMode ? 1 : 0) +
                 " PontDiv=" + String(PontDiv) +
                 " SeuilPontDiv=" + String(SeuilPontDiv));
  if (WakeUp == 0) {

    if (PontDiv < SeuilPontDiv && !VeilleInfinie) {
      Serial.println(String("[SLEEP][TRACE] veille infinie DESACTIVEE (cle 112=0) PontDiv=") + String(PontDiv) +
                     " < SeuilPontDiv=" + String(SeuilPontDiv) + " -> sommeil timer normal");
    }
    // Harmonisation A8 (lot 0 T6) : le bloc "emergency_batterie" duplique ici
    // etait du CODE MORT — automatismes() (appele avant dans le meme reveil,
    // memes PontDiv/SeuilPontDiv/VeilleInfinie, aucune sortie anticipee) part
    // deja en veille infinie sous la meme condition. Son contenu utile (POST
    // final + ecran DODO) a ete rapatrie dans automatismes(). Site unique
    // d'evaluation batterie = automatismes(), comme le re-armement du latch.

    Serial.println(String("[SLEEP][TRACE] branche=regular WakeUp=0 timer=") + String(FreqWakeUp) + "s");
    if (displayOk) display.clearDisplay();
    datatobdd();
    Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) + " Seconds");
    if (displayOk) {
      display.setTextSize(1);
      display.setCursor(0, 35);
      display.println(" ");
      display.println("   Entree en sommeil");
      display.display();
    }
    Serial.println("Going to sleep now");
    delay(1000);
    EnregistrementHeureFlash();
    N3SleepConfig regularSleep = { N3_WAKEUP_GPIO, HIGH, (unsigned long)FreqWakeUp };
    n3SleepConfigure(regularSleep);
    Serial.printf("[SLEEP] Timer configure a %d s\n", FreqWakeUp);
    Serial.println(String("[SLEEP][TRACE] start deep sleep mode=regular timer=") + String(FreqWakeUp) + "s");
    n3SleepStart();
  } else {
    Serial.println(String("[SLEEP][TRACE] skip deep sleep: WakeUp=1 (commande serveur), FreqWakeUp=") +
                   String(FreqWakeUp) + "s");
  }
}

// Fonction pour obtenir la raison du réveil de l'ESP32
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup caused by ULP program"); break;
    default:
      Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
      // Restauration via n3_time (clé epoch unique, avec migration anciennes clés).
      n3TimeLoadFromFlash(preferences, rtc);
      n3TimeSyncBrokenDown(rtc, seconde, minute, heure, jour, mois, annee);
      break;
  }
}
