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

/*
 * Fonction (commentée) : afficher le touchpad qui a réveillé l'ESP32 du deep sleep.
 void print_wakeup_touchpad(){
  touchPin = esp_sleep_get_touchpad_wakeup_status();

  switch(touchPin)
  {
    case 0  : Serial.println("Touch detecte sur GPIO 4"); break;
    case 1  : Serial.println("Touch detecte sur GPIO 0"); break;
    case 2  : Serial.println("Touch detecte sur GPIO 2"); break;
    case 3  : Serial.println("Touch detecte sur GPIO 15"); break;
    case 4  : Serial.println("Touch detecte sur GPIO 13"); break;
    case 5  : Serial.println("Touch detecte sur GPIO 12"); break;
    case 6  : Serial.println("Touch detecte sur GPIO 14"); break;
    case 7  : Serial.println("Touch detecte sur GPIO 27"); break;
    case 8  : Serial.println("Touch detecte sur GPIO 33"); break;
    case 9  : Serial.println("Touch detecte sur GPIO 32"); break;
    default : Serial.println("Reveil non cause par touchpad"); break;
  }
}*/

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

void arrosage() {
  digitalWrite(POMPE, 1);
  Serial.println("arrosage en cours");
  delay(tempsArrosage);
  digitalWrite(POMPE, 0);
  Serial.println("arrosage terminé");
  delay(1000);
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
    String emailMessage = String("L'humidité est remonté. La moyenne est maintenant de ") + String(HumidMoy);
    sendEmailNotification();
      Serial.println(emailMessage);
      // SerialBT.println(emailMessage);
      emailHumidSent = false;
    //variablestoesp();
    datatobdd();
  }


  Serial.print("seuilsec3 : ");
  Serial.println(SeuilSec);
  Serial.print("tempsArrosage3: ");
  Serial.println(tempsArrosage);
  //mail si tension trop basse

  if ((PontDiv < SeuilPontDiv)) {
    if (enableEmailChecked == "checked" && !emailPontDivSent) {
      String emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
    sendEmailNotification();
      Serial.println(emailMessage);
      // SerialBT.println(emailMessage);
        emailPontDivSent = true;
      }
    n3SleepStart();
  }

  //arrosage en cas de sécheresse
  if ((HumidMoy < SeuilSec)) {
    arrosage();
    if (enableEmailChecked == "checked") {
      String emailMessage = String("arrosage auto effectué ");
      Serial.println("arrosage auto");
      sendEmailNotification();
    }
    //variablestoesp();
    datatobdd();
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
    Serial.println("arrosage heure auto effectué");
    Serial.print("bouffe soir 2 :");
    Serial.println(arrosageFait);
    if (enableEmailChecked == "checked") {
      String emailMessage = String("arrosage heure auto effectué ");
      //bouffeSoirTemoin = 1;
      sendEmailNotification();
    }
    //variablestoesp();
    etatPompe = 1;
    datatobdd();
    etatPompe = 0;
  }

  //bouffe manuelle
  if (ArrosageManu == 1) {
    datatobdd();
    Serial.println(ArrosageManu);
    arrosage();
    ArrosageManu = 0;
    //arrosageFait = 0;
    if (enableEmailChecked == "checked") {
      String emailMessage = String("arrosage manu effectué ");
      //bouffeSoirTemoin = 1;
      sendEmailNotification();
    }
    //variablestoesp();
    etatPompe = 1;
    datatobdd();
    etatPompe = 0;
  }
}

void sommeil() {
  //Go to sleep now
  Serial.println("WakeUp : ");
  Serial.print(WakeUp);
  if (WakeUp == 0) {

    if ((PontDiv < SeuilPontDiv) && enableEmailChecked == "checked") {
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
      n3SleepStart();
    }

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
    n3SleepStart();
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
