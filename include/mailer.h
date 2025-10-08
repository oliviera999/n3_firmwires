#pragma once
#include "project_config.h"
#if FEATURE_MAIL
#include <ESP_Mail_Client.h>
#else
// Minimal types to satisfy compilation when mail feature is disabled
struct SMTPSession {};
struct Session_Config {};
#endif
#include "secrets.h"

class Mailer {
 public:
  bool begin();
  bool send(const char* subject, const char* message, const char* toName = "User", const char* toEmail = EmailConfig::DEFAULT_RECIPIENT);
  bool sendAlert(const char* subject, const String& message, const char* toEmail = Config::DEFAULT_MAIL_TO);
  bool sendSleepMail(const char* reason, uint32_t sleepDurationSeconds);
  bool sendWakeMail(const char* reason, uint32_t actualSleepSeconds);
 private:
  SMTPSession _smtp;
  Session_Config _cfg; // conservé pour les reconnexions
  bool _ready{false};
}; 