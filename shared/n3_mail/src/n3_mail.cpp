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

bool n3MailBuildNetReportBody(const N3MailNetReportInfo& info, char* outBody, size_t outBodySize) {
  if (!outBody || outBodySize == 0) return false;

  const char* projectName = n3SafeStr(info.projectName, "n3-device");
  const char* sensorName = n3SafeStr(info.sensorName, "unknown");
  const char* firmwareVersion = n3SafeStr(info.firmwareVersion, "unknown");
  const char* localTime = n3SafeStr(info.localTime, "indisponible");
  const char* wifiSsid = n3SafeStr(info.wifiSsid, "(deconnecte)");
  const char* wifiIp = n3SafeStr(info.wifiIp, "(n/a)");
  const N3NetStatsSnapshot& s = info.stats;

  int written = snprintf(
      outBody,
      outBodySize,
      "[%s] Rapport reseau / communication\n"
      "Capteur: %s | firmware: %s\n"
      "Heure locale: %s | uptime: %lu s | bootCount: %lu\n"
      "Periode couverte: %lu s (~%.1f h)\n"
      "\n"
      "--- WiFi ---\n"
      "SSID: %s | IP: %s | RSSI actuel: %d dBm\n"
      "Heap libre: %lu o | heap min: %lu o\n"
      "\n"
      "--- POST post-data (periode) ---\n"
      "Envoi: %lu | OK: %lu | echecs: %lu | proche timeout: %lu\n"
      "Dernier: code_HTTP=%d | duree_totale=%lu ms | RSSI=%d dBm\n"
      "Max: %lu ms | moyenne: %lu ms | timeout client: %lu ms\n"
      "Log serie equivalent: [SERVER][POST] Verdict | duree_totale=... ms\n"
      "\n"
      "--- GET outputs_state (periode) ---\n"
      "Requetes: %lu | OK: %lu | echecs: %lu | serie echecs actuelle: %lu\n"
      "Dernier: code_HTTP=%d | duree_totale=%lu ms | RSSI=%d dBm\n"
      "Max: %lu ms | timeout client: %lu ms\n"
      "Log serie equivalent: [SERVER][GET] Verdict | duree_totale=... ms\n"
      "\n"
      "--- Comparaison ffp5cs (aquaponie) ---\n"
      "ffp5cs log: [HTTP] Verdict | duree_totale=... ms | latence_connect=... ms\n"
      "ffp5cs POST timeout WROOM: ~28000 ms (legacy n3pp/msp: %lu ms)\n"
      "Si ffp5cs voit ~20 s mais n3pp timeout ici: latence reseau/infra, pas echec PHP seul.\n"
      "Si duree_totale POST ici > %lu ms: timeout probable cote legacy (ffp5cs peut encore reussir).\n"
      "Cote serveur ffp3: chercher PostData timing_ms et auth_ms dans cronlog.txt.\n",
      projectName,
      sensorName,
      firmwareVersion,
      localTime,
      static_cast<unsigned long>(info.uptimeSeconds),
      static_cast<unsigned long>(info.bootCount),
      static_cast<unsigned long>(info.reportPeriodSeconds),
      static_cast<double>(info.reportPeriodSeconds) / 3600.0,
      wifiSsid,
      wifiIp,
      info.wifiRssiNow,
      static_cast<unsigned long>(info.freeHeap),
      static_cast<unsigned long>(info.minFreeHeap),
      static_cast<unsigned long>(s.postCount),
      static_cast<unsigned long>(s.postOkCount),
      static_cast<unsigned long>(s.postFailCount),
      static_cast<unsigned long>(s.postNearTimeoutCount),
      s.postLastCode,
      static_cast<unsigned long>(s.postLastDurationMs),
      s.postLastRssi,
      static_cast<unsigned long>(s.postMaxDurationMs),
      static_cast<unsigned long>(s.postAvgDurationMs),
      static_cast<unsigned long>(info.httpTimeoutMs),
      static_cast<unsigned long>(s.getCount),
      static_cast<unsigned long>(s.getOkCount),
      static_cast<unsigned long>(s.getFailCount),
      static_cast<unsigned long>(info.outputsGetFailureStreak),
      s.getLastCode,
      static_cast<unsigned long>(s.getLastDurationMs),
      s.getLastRssi,
      static_cast<unsigned long>(s.getMaxDurationMs),
      static_cast<unsigned long>(info.httpTimeoutMs),
      static_cast<unsigned long>(info.httpTimeoutMs),
      static_cast<unsigned long>(info.httpTimeoutMs));

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
