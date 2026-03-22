#include "n3_mail.h"

#include <ESP_Mail_Client.h>
#include <cstdio>

static const char* n3SafeStr(const char* value, const char* fallback) {
  if (value && value[0] != '\0') return value;
  return fallback;
}

bool n3MailBuildDebugBody(const N3MailDebugInfo& info, char* outBody, size_t outBodySize) {
  if (!outBody || outBodySize == 0) return false;

  const char* projectName = n3SafeStr(info.projectName, "n3-device");
  const char* targetName = n3SafeStr(info.targetName, "unknown");
  const char* firmwareVersion = n3SafeStr(info.firmwareVersion, "unknown");
  const char* eventName = n3SafeStr(info.eventName, "event");
  const char* localTime = n3SafeStr(info.localTime, "indisponible");
  const char* wakeupReason = n3SafeStr(info.wakeupReason, "inconnu");
  const char* resetReason = n3SafeStr(info.resetReason, "inconnu");
  const char* wifiSsid = n3SafeStr(info.wifiSsid, "(deconnecte)");
  const char* wifiIp = n3SafeStr(info.wifiIp, "(n/a)");
  const char* extraInfo = n3SafeStr(info.extraInfo, "Aucune information complementaire.");

  int written = snprintf(
      outBody,
      outBodySize,
      "[%s] Notification %s\n"
      "Cible: %s\n"
      "Version firmware: %s\n"
      "Heure locale: %s\n"
      "Uptime: %lu s\n"
      "Wakeup: %s\n"
      "Reset: %s\n"
      "WiFi SSID: %s\n"
      "IP: %s\n"
      "RSSI: %d dBm\n"
      "Heap libre: %lu octets\n"
      "Heap min: %lu octets\n"
      "\n"
      "Details:\n"
      "%s\n",
      projectName,
      eventName,
      targetName,
      firmwareVersion,
      localTime,
      static_cast<unsigned long>(info.uptimeSeconds),
      wakeupReason,
      resetReason,
      wifiSsid,
      wifiIp,
      info.wifiRssi,
      static_cast<unsigned long>(info.freeHeap),
      static_cast<unsigned long>(info.minFreeHeap),
      extraInfo);

  return (written > 0) && (static_cast<size_t>(written) < outBodySize);
}

bool n3MailSendText(const N3MailSmtpConfig& smtpConfig,
                    const char* subject,
                    const char* body,
                    String* outError) {
  if (!smtpConfig.smtpHost || !smtpConfig.authorEmail || !smtpConfig.authorPassword ||
      !smtpConfig.recipientEmail || !subject || !body) {
    if (outError) *outError = "Configuration SMTP ou contenu mail invalide.";
    return false;
  }

  SMTPSession smtp;
  Session_Config sessionConfig;
  sessionConfig.server.host_name = smtpConfig.smtpHost;
  sessionConfig.server.port = smtpConfig.smtpPort;
  sessionConfig.login.email = smtpConfig.authorEmail;
  sessionConfig.login.password = smtpConfig.authorPassword;

  SMTP_Message message;
  message.sender.name = n3SafeStr(smtpConfig.senderName, "n3 IoT");
  message.sender.email = smtpConfig.authorEmail;
  message.subject = subject;
  message.addRecipient(n3SafeStr(smtpConfig.recipientName, "Destinataire"), smtpConfig.recipientEmail);
  message.text.content = body;
  message.text.charSet = "utf-8";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;

  if (!smtp.connect(&sessionConfig)) {
    if (outError) {
      *outError = "SMTP connect echec: ";
      *outError += smtp.errorReason().c_str();
    }
    return false;
  }

  bool sendOk = MailClient.sendMail(&smtp, &message);
  if (!sendOk && outError) {
    *outError = "SMTP envoi echec: ";
    *outError += smtp.errorReason().c_str();
  }
  smtp.sendingResult.clear();
  return sendOk;
}
