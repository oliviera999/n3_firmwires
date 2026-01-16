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

// Structure pour les éléments de la queue de mail (v11.144 - optimisé mémoire)
// Permet l'envoi asynchrone des emails sans bloquer automationTask
struct MailQueueItem {
  char subject[96];    // Réduit de 128 (v11.144)
  char message[512];   // Réduit de 1536 - message tronqué si trop long (v11.144)
  char toEmail[48];    // Réduit de 64 (v11.144)
  bool isAlert;        // true = sendAlert, false = send simple
};
// Taille totale: ~660 bytes (vs 1729 avant) - économie ~5 KB avec 2 slots

class Mailer {
 public:
  bool begin();
  
  // Méthodes synchrones (utilisées en interne par mailTask)
  bool sendSync(const char* subject, const char* message, const char* toName = "User", const char* toEmail = EmailConfig::DEFAULT_RECIPIENT);
  bool sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings);
  bool sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings);
  
  // Méthodes asynchrones (non-bloquantes) - v11.142
  // Ces méthodes ajoutent le mail à une queue et retournent immédiatement
  bool sendAlert(const char* subject, const String& message, const char* toEmail = EmailConfig::DEFAULT_RECIPIENT);
  bool send(const char* subject, const char* message, const char* toName = "User", const char* toEmail = EmailConfig::DEFAULT_RECIPIENT);
  
  // Démarrage de la tâche mail (appelé au boot)
  bool startMailTask();
  
  // Statistiques
  uint32_t getMailsSent() const { return _mailsSent; }
  uint32_t getMailsFailed() const { return _mailsFailed; }
  uint32_t getQueuedMails() const;
  
 private:
  SMTPSession _smtp;
  Session_Config _cfg;
  bool _ready{false};
  
  // Tâche mail asynchrone (v11.142)
  static void mailTaskFunction(void* param);
  QueueHandle_t _mailQueue{nullptr};
  TaskHandle_t _mailTaskHandle{nullptr};
  
  // Statistiques
  uint32_t _mailsSent{0};
  uint32_t _mailsFailed{0};
  
  // Implémentation interne de sendAlert (synchrone)
  bool sendAlertSync(const char* subject, const String& message, const char* toEmail);
}; 