#ifndef N3_MAIL_H
#define N3_MAIL_H

#include <Arduino.h>
#include <stddef.h>
#include "n3_data.h"
#include "n3_notify.h"  // N3Severity/N3NotifMode (n3MailNotify)

struct N3MailSmtpConfig {
  const char* smtpHost;
  uint16_t smtpPort;
  const char* authorEmail;
  const char* authorPassword;
  const char* senderName;
  const char* recipientName;
  const char* recipientEmail;
};

struct N3MailDebugInfo {
  const char* projectName;
  const char* targetName;
  const char* firmwareVersion;
  const char* eventName;
  const char* localTime;
  const char* wakeupReason;
  const char* resetReason;
  const char* wifiSsid;
  const char* wifiIp;
  int wifiRssi;
  uint32_t uptimeSeconds;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  const char* extraInfo;
};

/** Rapport reseau periodique (aligne comparaison logs ffp5cs). */
struct N3MailNetReportInfo {
  const char* projectName;
  const char* sensorName;
  const char* firmwareVersion;
  const char* localTime;
  const char* wifiSsid;
  const char* wifiIp;
  int wifiRssiNow;
  uint32_t bootCount;
  uint32_t uptimeSeconds;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t reportPeriodSeconds;
  uint32_t httpTimeoutMs;
  uint32_t outputsGetFailureStreak;
  N3NetStatsSnapshot stats;
};

/**
 * Specification d'un message texte a envoyer via une session SMTP DEJA connectee.
 * Permet aux firmwares qui possedent leur propre `SMTPSession` (avec mutex TLS,
 * watchdog, file d'attente, FS custom...) de mutualiser uniquement la construction
 * du `SMTP_Message` + l'appel `MailClient.sendMail`, sans rien deleguer de leur
 * gestion de connexion / garde-fous. Voir `n3MailSendMessageWithSession`.
 *
 * Les drapeaux `set*` sont OPT-IN pour rester 100% retro-compatible avec les
 * appelants existants : `n3MailSendText` les active tous (utf-8 / 7bit / priorite
 * basse, comportement historique n3pp/msp) ; un appelant peut les laisser a false
 * pour produire un message "brut" (parite avec un envoi qui ne touchait pas ces
 * champs, ex. l'ancien Mailer ffp5cs).
 */
struct N3MailMessageSpec {
  const char* senderName;      // repli "n3 IoT" si NULL/vide
  const char* senderEmail;     // requis
  const char* recipientName;   // repli "Destinataire" si NULL/vide
  const char* recipientEmail;  // requis
  const char* subject;         // requis
  const char* body;            // requis
  bool setCharsetUtf8;         // true -> text.charSet = "utf-8"
  bool set7bitEncoding;        // true -> transfer_encoding = enc_7bit
  bool setLowPriority;         // true -> priority = basse
};

bool n3MailBuildDebugBody(const N3MailDebugInfo& info, char* outBody, size_t outBodySize);
bool n3MailBuildNetReportBody(const N3MailNetReportInfo& info, char* outBody, size_t outBodySize);

// Envoi "one-shot" : ouvre une session SMTP locale, connecte, envoie, ferme.
// Comportement historique (utf-8 / 7bit / priorite basse). Inchange.
bool n3MailSendText(const N3MailSmtpConfig& smtpConfig,
                    const char* subject,
                    const char* body,
                    String* outError);

// Forward-declaration : evite de tirer <ESP_Mail_Client.h> dans cet en-tete leger
// (les TU qui n'utilisent que les builders de corps ne paient pas l'include). Le
// .cpp et les appelants qui passent une session incluent le vrai header avant.
class SMTPSession;

/**
 * Construit le `SMTP_Message` decrit par `spec` et l'envoie via une session SMTP
 * DEJA CONNECTEE et possedee par l'appelant (le mutex TLS, les feeds watchdog, la
 * garde heap, l'ouverture/fermeture de session restent a la charge de l'appelant).
 *
 * Ne touche PAS a la connexion (pas de connect/closeSession ici) : c'est le point
 * de mutualisation pour les firmwares a pile SMTP riche (ffp5cs) qui veulent
 * partager la plomberie ESP_Mail_Client sans abandonner leurs garde-fous.
 *
 * Retourne true si `MailClient.sendMail` reussit. En cas d'echec, *outError (si
 * non NULL) contient `smtp.errorReason()`.
 */
bool n3MailSendMessageWithSession(SMTPSession& smtp,
                                  const N3MailMessageSpec& spec,
                                  String* outError);

// ---------------------------------------------------------------------------
// Notification graduee avec failover (mutualisee depuis n3pp/msp — corps
// verbatim identique entre les deux, seule variabilite : le tag projet).
// Politique (Phase 3 arbitrage mails) : hors-ligne (postOkThisWake=false), le
// mode est plafonne a P1/P2 (n3NotifModeCapFailover), l'envoi exige le WiFi et
// consomme un budget borne (*failoverMailsSent, re-arme par le firmware au
// retour en ligne). Retour : true = livre OU silence volontaire (severite
// filtree par le mode) ; false = non livre (a retenter au prochain reveil).
// ---------------------------------------------------------------------------
struct N3MailNotifyParams {
  const char* projectTag;      // prefixe sujet, ex. "N3PP" / "MSP1"
  N3NotifMode mode;            // mode de notification courant du firmware
  bool postOkThisWake;         // false => episode failover
  uint8_t* failoverMailsSent;  // compteur budget failover (RTC, cote firmware)
  uint8_t failoverMailBudget;  // ex. 8
  N3MailSmtpConfig smtp;       // configuration SMTP complete
  const char* subject;         // sujet de base (avant prefixe [TAG][Pn])
  const char* message;         // corps du mail
};

bool n3MailNotify(const N3MailNotifyParams& params, N3Severity severity);

#endif
