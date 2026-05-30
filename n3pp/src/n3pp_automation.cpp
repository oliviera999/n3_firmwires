#include "n3pp_automation.h"
#include "n3pp_globals.h"
#include "n3pp_network.h"
#include "n3_sleep.h"

void HeureSansWifi() {
  preferences.begin("rtc", true);           // Ouverture session NVS (lecture seule)
  heure = preferences.getInt("heure", 12);  // Récupération heure sauvegardée (défaut 12h)
  minute = preferences.getInt("minute", 0);
  seconde = preferences.getInt("seconde", 0);
  jour = preferences.getInt("jour", 1);
  mois = preferences.getInt("mois", 1);
  annee = preferences.getInt("annee", 2023);
  preferences.end();                                       // Fermeture de la session NVS
  rtc.setTime(seconde, minute, heure, jour, mois, annee);   // Définition RTC sans synchro NTP
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
  rtc.getTime("%H:%M:%S %d/%m/%Y");
  seconde=rtc.getTime("%S").toInt();
  minute=rtc.getTime("%M").toInt();
  heure=rtc.getTime("%H").toInt();
  jour=rtc.getTime("%d").toInt();
  mois=rtc.getTime("%m").toInt();
  annee=rtc.getTime("%Y").toInt();
  preferences.begin("rtc", false);
  preferences.putInt("heure", heure);
  preferences.putInt("minute", minute);
  preferences.putInt("seconde", seconde);
  preferences.putInt("jour", jour);
  preferences.putInt("mois", mois);
  preferences.putInt("annee", annee);
  preferences.end();
}

// Configuration et envoi d'un email d'alerte (SMTP)
void sendEmailNotification() {
  /* Paramètres de session SMTP (serveur, port, identifiants) */
  Session_Config config;

  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;

  /* Message à envoyer */
  SMTP_Message message;

  /* En-têtes du message (expéditeur, destinataire, sujet) */
  message.sender.name = F("OAL");
  message.sender.email = AUTHOR_EMAIL;

  message.subject = emailSubject;

  message.addRecipient(F("OAL"), inputMessageMailAd);

  message.text.content = emailMessage;

  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;

  /* Connexion au serveur SMTP */
  if (!smtp.connect(&config)) {
    MailClient.printf("SMTP erreur connexion, Status: %d, Error: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return;
  }

  /* Variante : connexion sans login, puis authentification séparée
     if (!smtp.connect(&config, false)) return;
     if (!smtp.loginWithPassword(AUTHOR_EMAIL, AUTHOR_PASSWORD)) return;
  */

  if (!smtp.isLoggedIn()) {
    Serial.println("SMTP : pas encore connecte.");
  } else {
    if (smtp.isAuthenticated())
      Serial.println("SMTP : authentification reussie.");
    else
      Serial.println("SMTP : connecte sans auth.");
  }

  /* Envoi de l'email puis fermeture de la session */
  if (!MailClient.sendMail(&smtp, &message))
    MailClient.printf("SMTP erreur envoi, Status: %d, Error: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());

  // Vider le journal des résultats d'envoi (évite accumulation en mémoire)
  smtp.sendingResult.clear();
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

void automatismes() {
  //remplissage de l'aquarium cas si l'aquarium est trop bas et la réserve assez remplie

  //mail si sécheresse trop forte
  if ((HumidMoy < SeuilSec) && enableEmailChecked == "checked" && !emailHumidSent) {
    emailMessage = String("Le sol est sec. L'humidité moyenne est de ") + String(HumidMoy);
    sendEmailNotification();
      Serial.println(emailMessage);
      // SerialBT.println(emailMessage);
      emailHumidSent = true;
    
    //variablestoesp();
    datatobdd();
  }

  // mail si le niveau est revenu à la normale
  if ((HumidMoy > SeuilSec) && enableEmailChecked == "checked" && emailHumidSent) {
    emailMessage = String("L'humidite est remontee. La moyenne est maintenant de ") + String(HumidMoy);
    Serial.println(emailMessage);
    sendEmailNotification();
    emailHumidSent = false;
    datatobdd();
  }

  Serial.print("seuilsec3 : ");
  Serial.println(SeuilSec);
  Serial.print("tempsArrosage3: ");
  Serial.println(tempsArrosage);

  // mail si tension trop basse (batterie)
  if ((PontDiv < SeuilPontDiv)) {
    if (enableEmailChecked == "checked" && !emailPontDivSent) {
      emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
      Serial.println(emailMessage);
      sendEmailNotification();
      emailPontDivSent = true;
    }
    n3SleepStart();
  }

  // Arrosage en cas de secheresse : protege par cooldown pour eviter
  // un arrosage repete a chaque reveil deep sleep si le sol reste sec.
  if (HumidMoy < SeuilSec) {
    if (arrosageAutoCooldownExpired()) {
      arrosage();
      if (enableEmailChecked == "checked") {
        emailMessage = String("Arrosage auto effectue (sol sec, humidite=") +
                       String(HumidMoy) + String(")");
        Serial.println("[ARROSAGE] auto");
        sendEmailNotification();
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

  if ((HeureArrosage == heure) && arrosageFait == 0) {
    arrosage();
    arrosageFait = 1;
    Serial.println("[ARROSAGE] heure programmee effectue");
    Serial.print("arrosageFait=");
    Serial.println(arrosageFait);
    if (enableEmailChecked == "checked") {
      emailMessage = String("Arrosage heure programmee effectue (") +
                     String(heure) + String("h)");
      sendEmailNotification();
    }
    etatPompe = 1;
    datatobdd();
    etatPompe = 0;
  }

  // Arrosage manuel demande depuis l'interface
  if (ArrosageManu == 1) {
    datatobdd();
    Serial.print("[ARROSAGE] manuel demande, ArrosageManu=");
    Serial.println(ArrosageManu);
    arrosage();
    ArrosageManu = 0;
    if (enableEmailChecked == "checked") {
      emailMessage = String("Arrosage manuel effectue");
      sendEmailNotification();
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

    if ((PontDiv < SeuilPontDiv) && enableEmailChecked == "checked") {
      Serial.println(String("[SLEEP][TRACE] branche=emergency_batterie PontDiv=") + String(PontDiv) +
                     " < SeuilPontDiv=" + String(SeuilPontDiv));
      emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
      sendEmailNotification();
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
    }

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
      preferences.begin("rtc", true);           // Ouverture session NVS (lecture seule)
      heure = preferences.getInt("heure", 12);  // Récupération heure sauvegardée (défaut 12h)
      minute = preferences.getInt("minute", 0);
      seconde = preferences.getInt("seconde", 0);
      jour = preferences.getInt("jour", 1);
      mois = preferences.getInt("mois", 1);
      annee = preferences.getInt("annee", 2023);
      preferences.end();                                       // Fermeture de la session NVS
      rtc.setTime(seconde, minute, heure, jour, mois, annee);  // Définition RTC sans synchro NTP
      break;
  }
}
