/* MeteoStationPrototype (msp1) — Automatismes
 * sendEmailNotification, sommeil, EnregistrementHeureFlash, print_wakeup_reason
 */

#include "msp_automation.h"
#include "msp_config.h"
#include "msp_globals.h"
#include "msp_network.h"
#include "n3_sleep.h"
#include "n3_time.h"   // sauvegarde/restauration heure NVS (factorisé)
#include "n3_mail.h"   // envoi SMTP factorisé
#include "n3_data.h"   // statistiques reseau (n3NetStats*) pour le rapport periodique
#include "credentials.h"
#include <WiFi.h>

#define SMTP_HOST SMTP_HOST_ADDR
#define SMTP_PORT esp_mail_smtp_port_465
#define AUTHOR_EMAIL SMTP_EMAIL
#define AUTHOR_PASSWORD SMTP_PASSWORD

// Anti-spam batterie inter-cycles : RTC_DATA_ATTR pour survivre au deep sleep
// (sinon un mail batterie repart a chaque reveil tant que la tension reste basse).
RTC_DATA_ATTR static bool s_mspBatteryMailSent = false;

// Mode de notification courant, derive de la config distante enableEmailChecked
// (retro-compatible : "checked" -> Full, "unchecked" -> None).
N3NotifMode mspNotifMode() {
  return n3NotifModeFromString(enableEmailChecked.c_str());
}

static bool emailEnabled() {
  return mspNotifMode() != N3NotifMode::None;
}

// Budget de mails en failover par episode hors-ligne (anti-congestion §3.4-3) :
// re-arme des qu'un POST repasse OK (voir datatobdd). Evite le martelement TLS.
static const uint8_t FAILOVER_MAIL_BUDGET = 8;

// Configuration et envoi d'un email d'alerte (SMTP) — delegue a n3_mail.
// La severite est filtree par le mode courant ; le sujet est prefixe "[MSP1][Pn]".
// Phase 0 (arbitrage mails) : retourne true si livraison SMTP confirmee OU si le
// mail est volontairement filtre par le mode (silence choisi = traite) ; false si
// l'envoi a echoue. L'appelant ne latche son flag anti-spam que sur true, sinon
// l'alerte serait consideree "envoyee" et perdue hors ligne.
// Phase 3 (arbitrage mails) : en FAILOVER (POST de ce reveil echoue), anti-
// congestion §3.4 — severite plafonnee a P1/P2, SMTP tente seulement si WiFi
// connecte (sinon l'alerte serveur "appareil silencieux" couvre), budget borne.
bool sendEmailNotification(N3Severity severity) {
  const bool failover = !postOkThisWake;
  N3NotifMode mode = mspNotifMode();
  if (failover) {
    mode = n3NotifModeCapFailover(mode);
  }
  if (!n3NotifModeAllows(mode, severity)) {
    Serial.printf("[MAIL][SKIP] severite %s filtree par le mode de notification%s\n",
                  n3SeverityCode(severity), failover ? " (failover P1/P2)" : "");
    return true;  // silence volontaire : traite selon la politique, pas une perte
  }
  if (failover) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[MAIL][FAILOVER] WiFi absent, envoi non tente (retente au prochain reveil)");
      return false;  // pas de latch : l'alerte sera retentee
    }
    if (failoverMailsSent >= FAILOVER_MAIL_BUDGET) {
      Serial.printf("[MAIL][FAILOVER] budget epuise (%u), envoi non tente\n",
                    (unsigned)FAILOVER_MAIL_BUDGET);
      return false;
    }
    ++failoverMailsSent;
  }

  char subjectBuf[96];
  n3MailFormatSubject(subjectBuf, sizeof(subjectBuf), "MSP1", severity, emailSubject);

  N3MailSmtpConfig cfg{};
  cfg.smtpHost = SMTP_HOST;
  cfg.smtpPort = SMTP_PORT;
  cfg.authorEmail = AUTHOR_EMAIL;
  cfg.authorPassword = AUTHOR_PASSWORD;
  cfg.senderName = "OAL";
  cfg.recipientName = "OAL";
  cfg.recipientEmail = inputMessageMailAd.c_str();

  String err;
  if (!n3MailSendText(cfg, subjectBuf, emailMessage.c_str(), &err)) {
    Serial.print("[MAIL] echec envoi: ");
    Serial.println(err);
    return false;
  }
  return true;
}

// Rapport mail reseau periodique (P4 / Diagnostic) — meme motif que n3pp.
// Le compteur survit au deep sleep (RTC_DATA_ATTR) : on cumule le temps ecoule
// a chaque cycle et on envoie un rapport tous les N3_NETWORK_REPORT_INTERVAL_S.
RTC_DATA_ATTR static uint32_t s_mspNetReportElapsedSeconds = 0;

void mspAccumulateNetReportElapsedFromSleep(int sleepSeconds) {
  if (sleepSeconds <= 0) return;
  if (s_mspNetReportElapsedSeconds >= N3_NETWORK_REPORT_INTERVAL_S) return;
  const uint32_t sleepSec = static_cast<uint32_t>(sleepSeconds);
  const uint32_t remaining = N3_NETWORK_REPORT_INTERVAL_S - s_mspNetReportElapsedSeconds;
  s_mspNetReportElapsedSeconds += (sleepSec >= remaining) ? remaining : sleepSec;
}

void mspMaybeSendNetworkReportEmail() {
  if (s_mspNetReportElapsedSeconds < N3_NETWORK_REPORT_INTERVAL_S) {
    const uint32_t remaining = N3_NETWORK_REPORT_INTERVAL_S - s_mspNetReportElapsedSeconds;
    Serial.printf("[MAIL][NET] rapport ignore, restant=%lu s\n",
                  static_cast<unsigned long>(remaining));
    return;
  }

  // Rapport reseau = diagnostic (P4) : envoye seulement si le mode l'autorise
  // (mode Full / legacy "checked"). Mode none/important/partial -> filtre.
  if (!n3NotifModeAllows(mspNotifMode(), N3Severity::Diagnostic)) {
    Serial.println("[MAIL][NET] rapport diagnostic (P4) filtre par le mode de notification, timer reinitialise");
    s_mspNetReportElapsedSeconds = 0;
    n3NetStatsResetPeriod();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MAIL][NET] WiFi indisponible, rapport reporte");
    return;
  }

  // Phase 3 arbitrage : en FAILOVER (POST de ce reveil echoue), les diagnostics
  // P4 ne valent pas le cout d'une session TLS (§3.4-2). Reporte sans toucher au
  // timer : le rapport partira au premier reveil revenu en ligne.
  if (!postOkThisWake) {
    Serial.println("[MAIL][NET] failover (POST echoue), rapport P4 reporte");
    return;
  }

  char localTime[32];
  snprintf(localTime, sizeof(localTime), "%s", rtc.getTime("%d/%m/%Y %H:%M:%S").c_str());

  char ssidBuf[33];
  snprintf(ssidBuf, sizeof(ssidBuf), "%s", WiFi.SSID().c_str());
  String ipStr = WiFi.localIP().toString();

  N3NetStatsSnapshot stats = {};
  n3NetStatsGetSnapshot(stats);

  N3MailNetReportInfo report = {};
  report.projectName = "msp1";
  report.sensorName = sensorName.c_str();
  report.firmwareVersion = version.c_str();
  report.localTime = localTime;
  report.wifiSsid = ssidBuf;
  report.wifiIp = ipStr.c_str();
  report.wifiRssiNow = WiFi.RSSI();
  report.bootCount = static_cast<uint32_t>(bootCount);
  report.uptimeSeconds = millis() / 1000UL;
  report.freeHeap = ESP.getFreeHeap();
  report.minFreeHeap = ESP.getMinFreeHeap();
  report.reportPeriodSeconds = N3_NETWORK_REPORT_INTERVAL_S;
  report.httpTimeoutMs = N3_HTTP_TIMEOUT_MS;
  report.outputsGetFailureStreak = 0;  // msp ne suit pas ce compteur (specifique n3pp)
  report.stats = stats;

  char body[2048];
  if (!n3MailBuildNetReportBody(report, body, sizeof(body))) {
    Serial.println("[MAIL][NET] Echec generation corps rapport");
    return;
  }

  N3MailSmtpConfig smtpCfg = {};
  smtpCfg.smtpHost = SMTP_HOST;
  smtpCfg.smtpPort = SMTP_PORT;
  smtpCfg.authorEmail = AUTHOR_EMAIL;
  smtpCfg.authorPassword = AUTHOR_PASSWORD;
  smtpCfg.senderName = "MSP1 IoT";
  smtpCfg.recipientName = "OAL";
  smtpCfg.recipientEmail = inputMessageMailAd.c_str();

  char subject[96];
  snprintf(subject, sizeof(subject), "[MSP1][P4] rapport reseau v%s", FIRMWARE_VERSION);

  String smtpError;
  if (n3MailSendText(smtpCfg, subject, body, &smtpError)) {
    Serial.println("[MAIL][NET] Rapport reseau envoye");
    s_mspNetReportElapsedSeconds = 0;
    n3NetStatsResetPeriod();
  } else {
    Serial.printf("[MAIL][NET] Echec envoi: %s\n", smtpError.c_str());
  }
}

//fonction permettant l'enregistrement des variables de temps dans la mémoire flash
void EnregistrementHeureFlash() {
  n3TimeSaveToFlash(rtc, preferences);  // NVS "rtc" (mêmes clés qu'avant)
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
      n3TimeLoadFromFlash(preferences, rtc);  // NVS "rtc" -> rtc (mêmes clés/défauts qu'avant)
      // Resync des globals firmware depuis le RTC chargé
      seconde = rtc.getTime("%S").toInt();
      minute = rtc.getTime("%M").toInt();
      heure = rtc.getTime("%H").toInt();
      jour = rtc.getTime("%d").toInt();
      mois = rtc.getTime("%m").toInt();
      annee = rtc.getTime("%Y").toInt();
      break;
  }
}

void sommeil() {
  Serial.println(String("[SLEEP][TRACE] entree WakeUp=") + String(WakeUp ? 1 : 0) +
                 " FreqWakeUp=" + String(FreqWakeUp) +
                 " resetMode=" + String(resetMode ? 1 : 0) +
                 " PontDiv=" + String(PontDiv) +
                 " SeuilPontDiv=" + String(SeuilPontDiv));

  if (WakeUp == 0) {
    if (PontDiv >= SeuilPontDiv) {
      s_mspBatteryMailSent = false;  // batterie revenue au-dessus du seuil : re-arme l'alerte
    }
    // Protection batterie faible DECORELE de l'email : le sommeil protecteur
    // "GPIO uniquement" (grace au fix n3_sleep sur sleepSeconds==0) s'applique
    // meme si les notifications sont sur "none". Seul l'envoi du mail reste
    // conditionne par emailEnabled().
    if (PontDiv < SeuilPontDiv) {
      Serial.println(String("[SLEEP][TRACE] branche=emergency_batterie PontDiv=") + String(PontDiv) +
                     " < SeuilPontDiv=" + String(SeuilPontDiv));
      // Phase 3 arbitrage : batterie calculee par le SERVEUR (PontDiv/SeuilPontDiv
      // au POST, n3_serveur 6.16.0). POST de ce reveil OK -> l'ESP se tait ;
      // sinon failover (P1). La protection sommeil reste INCONDITIONNELLE.
      if (!postOkThisWake && emailEnabled() && !s_mspBatteryMailSent) {
        emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
        // Phase 0 (arbitrage mails) : latch uniquement sur livraison SMTP confirmee —
        // un envoi echoue hors ligne n'est plus considere "fait" (alerte retentee).
        if (sendEmailNotification(N3Severity::Critical)) {
          s_mspBatteryMailSent = true;
        }
      }
      datatobdd();
      if (displayOk) display.clearDisplay();
      photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
      posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
      delay(100);
      if (displayOk) {
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
    photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
    posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
    delay(100);
    if (displayOk) {
      display.setTextSize(1);
      display.setCursor(0, 35);
      display.println(" ");
      display.println("   SOMMEIL");
      display.display();
    }
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
