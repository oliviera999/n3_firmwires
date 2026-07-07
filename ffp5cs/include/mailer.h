#pragma once
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#if FEATURE_MAIL
#include <ESP_Mail_Client.h>
#else
// Minimal types to satisfy compilation when mail feature is disabled
struct SMTPSession {};
struct Session_Config {};
#endif
#include "secrets.h"
#include "system_sensors.h"
#include "imailer.h"  // C4: interface de messagerie (inversion de dépendance)
#include "n3_notify.h"  // taxonomie sévérité P1-P4 + mode (lib partagée n3_mail)

class PowerManager;

// Structure pour les éléments de la queue de mail (v11.144 - optimisé mémoire)
// Permet l'envoi asynchrone des emails sans bloquer automationTask
struct MailQueueItem {
  char subject[96];    // Réduit de 128 (v11.144)
  char message[512];   // Réduit de 1536 - message tronqué si trop long (v11.144)
  char toEmail[48];    // Réduit de 64 (v11.144)
  bool isAlert;        // true = sendAlert, false = send simple
  bool includeDetailedReport;  // true = boot/OTA/panic (rapport temporel), false = alertes opérationnelles
  uint8_t retryCount;  // Nombre de tentatives déjà effectuées (re-queue sur échec SMTP, max 2)
  // Accusé de livraison différé (Phase 0 arbitrage mails) : si la livraison SMTP
  // échoue définitivement (retries épuisés), *ackFlag est repositionné à
  // ackFailValue pour ré-armer l'anti-spam de l'appelant (alerte retentée au
  // prochain cycle au lieu d'être perdue). nullptr = pas d'accusé. Le pointeur
  // vise un membre long-vivant (Automatism) et la queue est traitée dans la même
  // tâche que les orchestrateurs (aucune concurrence).
  bool* ackFlag;
  bool ackFailValue;
};
// Taille totale: ~660 bytes (vs 1729 avant) - économie ~5 KB avec 2 slots

class Mailer : public IMailer {
 public:
  bool begin();
  
  // Mode de notification gradué (filtrage sévérité P1-P4). Poussé par la couche
  // de config (AutomatismSync) via Automatism après chaque applyConfigFromJson.
  void setNotifMode(N3NotifMode mode) override { _notifMode = mode; }
  N3NotifMode notifMode() const { return _notifMode; }

  // Phase 3 arbitrage : mode failover (anti-congestion §3.4). En failover :
  // sévérité plafonnée à P2 (n3NotifModeCapFailover), enqueue refusé sans WiFi,
  // budget borné par épisode hors-ligne (reset à la sortie du failover).
  void setFailoverActive(bool active) override {
    if (_failoverActive && !active) {
      _failoverMailsSent = 0;  // sortie du failover : ré-arme le budget
    }
    _failoverActive = active;
  }
  bool isFailoverActive() const { return _failoverActive; }

  // Méthodes synchrones (utilisées en interne par mailTask ou avant reboot OTA)
  // `severity` : filtrée par le mode courant + préfixe sujet "[FFP5][Pn]".
  bool sendSync(const char* subject, const char* message, const char* toName = "User", const char* toEmail = EmailConfig::DEFAULT_RECIPIENT, N3Severity severity = N3Severity::Alert);
  bool sendAlertSync(const char* subject, const char* message, const char* toEmail = EmailConfig::DEFAULT_RECIPIENT, bool includeDetailedReport = false);
  bool sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings,
                     const char* toEmail = EmailConfig::DEFAULT_RECIPIENT) override;
  bool sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings,
                    const char* toEmail = EmailConfig::DEFAULT_RECIPIENT) override;
  
  // Méthodes asynchrones (non-bloquantes) - v11.142
  // Ces méthodes ajoutent le mail à une queue et retournent immédiatement
  // includeDetailedReport: true = boot/OTA/panic (rapport temporel détaillé), false = alertes opérationnelles (niveaux, chauffage, etc.)
  bool sendAlert(const char* subject, const char* message, const char* toEmail = EmailConfig::DEFAULT_RECIPIENT, bool includeDetailedReport = false) override;
  // Variante avec accusé de livraison différé : voir IMailer::sendAlertAcked.
  bool sendAlertAcked(const char* subject, const char* message, const char* toEmail,
                      bool includeDetailedReport, bool* ackFlag, bool ackFailValue) override;
  bool send(const char* subject, const char* message, const char* toName = "User", const char* toEmail = EmailConfig::DEFAULT_RECIPIENT) override;
  
  // Initialisation de la queue mail (appelé au boot, sans tâche dédiée)
  bool initMailQueue();
  
  // Traitement séquentiel depuis automationTask
  // Retourne true si un mail a été traité, false si aucun mail en attente
  bool processOneMailSync();
  
  // Vérification si des mails sont en attente
  bool hasPendingMails() const;

  /** Définit le PowerManager pour l'epoch affiché (évite doublon getSafeEpoch). Appelé au boot. */
  void setPowerManager(PowerManager* power);

  // Statistiques
  uint32_t getMailsSent() const { return _mailsSent; }
  uint32_t getMailsFailed() const { return _mailsFailed; }
  uint32_t getQueuedMails() const;
  
 private:
  // Enqueue commun de sendAlert / sendAlertAcked (porte les champs d'accusé de livraison).
  bool enqueueAlert(const char* subject, const char* message, const char* toEmail,
                    bool includeDetailedReport, bool* ackFlag, bool ackFailValue);

  SMTPSession _smtp;
  Session_Config _cfg;
  bool _ready{false};
  
  // Queue mail (traitée séquentiellement depuis automationTask)
  QueueHandle_t _mailQueue{nullptr};

  // Backoff après échec SMTP : un envoi raté (serveur lent/injoignable) peut
  // bloquer automationTask plusieurs secondes ; sans délai, le retry du cycle
  // suivant re-bloque immédiatement. On suspend le traitement de la queue
  // pendant MAIL_RETRY_BACKOFF_MS après un échec.
  static constexpr uint32_t MAIL_RETRY_BACKOFF_MS = 60000;
  unsigned long _nextMailRetryMs{0};

  // Epoch: délégation vers PowerManager (évite duplication logique NVS/fallback)
  PowerManager* _power{nullptr};

  // Mode de notification courant (plafond de sévérité). Défaut Full = tout envoyé
  // (comportement legacy inchangé tant que le serveur/l'utilisateur ne restreint pas).
  N3NotifMode _notifMode{N3NotifMode::Full};

  // Phase 3 arbitrage : failover actif (serveur sans nos données) + budget de
  // mails par épisode hors-ligne (anti-congestion §3.4-3, au-dessus du cap queue).
  static constexpr uint8_t FAILOVER_MAIL_BUDGET = 6;
  bool _failoverActive{false};
  uint8_t _failoverMailsSent{0};

  // Statistiques
  uint32_t _mailsSent{0};
  uint32_t _mailsFailed{0};
}; 