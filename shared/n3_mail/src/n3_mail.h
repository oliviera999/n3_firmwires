#ifndef N3_MAIL_H
#define N3_MAIL_H

#include <Arduino.h>
#include <stddef.h>
#include "n3_data.h"

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

bool n3MailBuildDebugBody(const N3MailDebugInfo& info, char* outBody, size_t outBodySize);
bool n3MailBuildNetReportBody(const N3MailNetReportInfo& info, char* outBody, size_t outBodySize);
bool n3MailSendText(const N3MailSmtpConfig& smtpConfig,
                    const char* subject,
                    const char* body,
                    String* outError);

#endif
