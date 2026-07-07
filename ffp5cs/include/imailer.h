#pragma once
// =============================================================================
// FFP5CS — Interface de messagerie (inversion de dépendance, chantier C4)
// =============================================================================
// `IMailer` abstrait la surface de `Mailer` consommée par `Automatism` et ses
// sous-modules (alertes, refill, feeding, sleep). `Mailer` l'implémente.
//
// But : découpler l'orchestration de la god-class du `Mailer` concret (qui tire
// ESP_Mail_Client / FreeRTOS / secrets), afin de pouvoir injecter un FAUX mailer
// dans les tests natifs et, à terme, tester l'orchestration (alertes, fin de
// nourrissage, mails de veille) sans matériel ni réseau.
//
// Surface MINIMALE : seules les 4 méthodes réellement appelées par le module
// `automatism` figurent ici (vérifié par grep exhaustif des `_mailer.`).
// En-tête volontairement LÉGER (pas de config.h / ESP_Mail_Client) :
//   - `SensorReadings` est seulement déclaré (paramètres par référence) ;
//   - le seul argument par défaut est un littéral (`includeDetailedReport=false`),
//     tous les autres args sont fournis explicitement par les appelants.
// =============================================================================

#include <cstdint>
#include "n3_notify.h"  // N3NotifMode (taxonomie sévérité/mode partagée, sans Arduino)

struct SensorReadings;  // déclaration anticipée (méthodes prennent const SensorReadings&)

class IMailer {
 public:
  virtual ~IMailer() = default;

  // Mode de notification gradué (none/important/partial/full) poussé par le
  // serveur ; le mailer filtre chaque mail par sévérité P1-P4. Impl par défaut
  // no-op : les faux mailers des tests natifs n'ont pas à la surcharger.
  virtual void setNotifMode(N3NotifMode mode) { (void)mode; }

  // Envoi simple (file d'attente). Tous les arguments fournis par les appelants.
  virtual bool send(const char* subject, const char* message,
                    const char* toName, const char* toEmail) = 0;

  // Alerte. `includeDetailedReport` : true = boot/OTA/panic, false = alertes opérationnelles.
  virtual bool sendAlert(const char* subject, const char* message,
                         const char* toEmail, bool includeDetailedReport = false) = 0;

  // Alerte avec accusé de livraison différé (Phase 0 arbitrage mails) : le mailer
  // asynchrone (queue + retries) repositionne `*ackFlag = ackFailValue` si la
  // livraison SMTP échoue DÉFINITIVEMENT (retries épuisés), pour ré-armer le flag
  // anti-spam de l'appelant au lieu de « perdre » l'alerte. `ackFlag` doit pointer
  // vers un stockage durable (membre d'un objet long-vivant), et la queue est
  // traitée dans la MÊME tâche que les orchestrateurs (pas de course).
  // Impl par défaut : délègue à sendAlert() — pour un mailer synchrone (ou un faux
  // mailer de test), le retour immédiat suffit : un échec n'a rien latché, donc
  // il n'y a rien à restaurer.
  virtual bool sendAlertAcked(const char* subject, const char* message,
                              const char* toEmail, bool includeDetailedReport,
                              bool* ackFlag, bool ackFailValue) {
    (void)ackFlag; (void)ackFailValue;
    return sendAlert(subject, message, toEmail, includeDetailedReport);
  }

  // Mails de veille / réveil (incluent un instantané capteurs).
  virtual bool sendSleepMail(const char* reason, uint32_t sleepDurationSeconds,
                             const SensorReadings& readings, const char* toEmail) = 0;
  virtual bool sendWakeMail(const char* reason, uint32_t actualSleepSeconds,
                            const SensorReadings& readings, const char* toEmail) = 0;
};
