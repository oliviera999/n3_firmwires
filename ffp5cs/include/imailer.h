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

struct SensorReadings;  // déclaration anticipée (méthodes prennent const SensorReadings&)

class IMailer {
 public:
  virtual ~IMailer() = default;

  // Envoi simple (file d'attente). Tous les arguments fournis par les appelants.
  virtual bool send(const char* subject, const char* message,
                    const char* toName, const char* toEmail) = 0;

  // Alerte. `includeDetailedReport` : true = boot/OTA/panic, false = alertes opérationnelles.
  virtual bool sendAlert(const char* subject, const char* message,
                         const char* toEmail, bool includeDetailedReport = false) = 0;

  // Mails de veille / réveil (incluent un instantané capteurs).
  virtual bool sendSleepMail(const char* reason, uint32_t sleepDurationSeconds,
                             const SensorReadings& readings, const char* toEmail) = 0;
  virtual bool sendWakeMail(const char* reason, uint32_t actualSleepSeconds,
                            const SensorReadings& readings, const char* toEmail) = 0;
};
