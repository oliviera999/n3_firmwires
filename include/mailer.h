#pragma once
#include "config.h"
#if FEATURE_MAIL
#include <ESP_Mail_Client.h>
#else
// Minimal types to satisfy compilation when mail feature is disabled
struct SMTPSession {};
struct Session_Config {};
#endif
#include "secrets.h"
#include "system_sensors.h"

class Mailer {
 public:
  bool begin();
  bool send(const char* subject, const char* message, const char* toName = "User", const char* toEmail = EmailConfig::DEFAULT_RECIPIENT);
  bool sendAlert(const char* subject, const String& message, const char* toEmail = EmailConfig::DEFAULT_RECIPIENT);
  bool sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings);
  bool sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings);
 private:
  SMTPSession _smtp;
  Session_Config _cfg; // conservé pour les reconnexions
  bool _ready{false};
}; 