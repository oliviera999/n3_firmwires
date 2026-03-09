/* MeteoStationPrototype (msp1) — Automatismes
 * sendEmailNotification, sommeil, EnregistrementHeureFlash, print_wakeup_reason
 */

#include "msp_automation.h"
#include "msp_config.h"
#include "msp_globals.h"
#include "msp_network.h"
#include <esp_sleep.h>
#include "credentials.h"

#define SMTP_HOST SMTP_HOST_ADDR
#define SMTP_PORT esp_mail_smtp_port_465
#define AUTHOR_EMAIL SMTP_EMAIL
#define AUTHOR_PASSWORD SMTP_PASSWORD

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

//fonction permettant l'enregistrement des variables de temps dans la mémoire flash
void EnregistrementHeureFlash() {
  rtc.getTime("%H:%M:%S %d/%m/%Y");
  seconde = rtc.getTime("%S").toInt();
  minute = rtc.getTime("%M").toInt();
  heure = rtc.getTime("%H").toInt();
  jour = rtc.getTime("%d").toInt();
  mois = rtc.getTime("%m").toInt();
  annee = rtc.getTime("%Y").toInt();
  preferences.begin("rtc", false);
  preferences.putInt("heure", heure);
  preferences.putInt("minute", minute);
  preferences.putInt("seconde", seconde);
  preferences.putInt("jour", jour);
  preferences.putInt("mois", mois);
  preferences.putInt("annee", annee);
  preferences.end();
}

// Fonction pour obtenir la raison du réveil de l'ESP32
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Reveil : signal externe (RTC_IO)"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Reveil : signal externe (RTC_CNTL)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Reveil : timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Reveil : touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Reveil : programme ULP"); break;
    default:
      Serial.printf("Reveil non cause par deep sleep : %d\n", wakeup_reason);
      preferences.begin("rtc", true);           // Ouverture session NVS (lecture seule)
      heure = preferences.getInt("heure", 12);   // Récupération heure sauvegardée (défaut 12h)
      minute = preferences.getInt("minute", 0);
      seconde = preferences.getInt("seconde", 0);
      jour = preferences.getInt("jour", 1);
      mois = preferences.getInt("mois", 1);
      annee = preferences.getInt("annee", 2023);
      preferences.end();                                       // Fermeture de la session NVS
      rtc.setTime(seconde, minute, heure, jour, mois, annee);  // définition RTC de l'heure sans synchro NTP
      break;
  }
}

void sommeil() {
  if (WakeUp == 0) {
    if ((PontDiv < SeuilPontDiv) && enableEmailChecked == "true") {
      emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
      sendEmailNotification();
      datatobdd();
      display.clearDisplay();
      photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
      posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
      delay(100);  // Pause entre chaque balayage
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println(" ");
      display.println("   DODO");
      display.display();
      delay(1000);
      EnregistrementHeureFlash();
      Serial.flush();
      esp_sleep_enable_timer_wakeup(0);
      esp_deep_sleep_start();
    }

    display.clearDisplay();
    datatobdd();
    photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
    posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
    delay(100);  // Pause entre chaque balayage
    display.setTextSize(1);
    display.setCursor(0, 35);
    display.println(" ");
    display.println("   SOMMEIL");
    display.display();
    delay(1000);
    EnregistrementHeureFlash();
    Serial.flush();
    esp_deep_sleep_start();
  }
}
