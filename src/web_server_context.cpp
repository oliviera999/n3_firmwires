#include "web_server_context.h"

#ifndef DISABLE_ASYNC_WEBSERVER

#include <ESPAsyncWebServer.h>

#include "automatism.h"
#include "config_manager.h"
#include "mailer.h"
#include "power.h"
#include "realtime_websocket.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "wifi_manager.h"

WebServerContext::WebServerContext(Automatism& automatism,
                                   Mailer& mailer,
                                   ConfigManager& config,
                                   PowerManager& power,
                                   WifiManager& wifi,
                                   SystemSensors& sensors,
                                   SystemActuators& actuators,
                                   RealtimeWebSocket& realtimeWs)
    : automatism(automatism),
      mailer(mailer),
      config(config),
      power(power),
      wifi(wifi),
      sensors(sensors),
      actuators(actuators),
      realtimeWs(realtimeWs) {}

bool WebServerContext::ensureHeap(AsyncWebServerRequest* req,
                                  uint32_t minHeap,
                                  const __FlashStringHelper* routeName) const {
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap >= minHeap) {
    return true;
  }

  if (req) {
    String message;
    message.reserve(96);
    message += F("Service temporairement indisponible - mémoire faible (heap=");
    message += freeHeap;
    message += F(" bytes)");
    req->send(503, "text/plain", message);
  }

  Serial.printf_P(PSTR("[Web] ⚠️ Mémoire insuffisante pour %s (%u < %u bytes)\n"),
                  routeName ? (const char*)routeName : "route",
                  freeHeap,
                  minHeap);
  return false;
}

void WebServerContext::sendJson(AsyncWebServerRequest* req,
                                const JsonDocument& doc,
                                bool enableCors) const {
  if (!req) {
    return;
  }

  AsyncResponseStream* response = req->beginResponseStream("application/json");
  if (!response) {
    Serial.println(F("[Web] ❌ Échec création réponse JSON (heap insuffisant)"));
    req->send(500, "text/plain", "Memory error");
    return;
  }

  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  if (enableCors) {
    response->addHeader("Access-Control-Allow-Origin", "*");
  }
  serializeJson(doc, *response);
  req->send(response);
}

bool WebServerContext::sendManualActionEmail(const char* action,
                                             const char* subject,
                                             const char* emailType) const {
  Serial.printf("[Web] 📧 === ENVOI EMAIL %s ===\n", emailType);

  if (!automatism.isEmailEnabled()) {
    Serial.println("[Web] 📧 ⚠️ Email désactivé - pas d'envoi");
    return false;
  }

  const char* emailAddr = automatism.getEmailAddress();
  if (!emailAddr || emailAddr[0] == '\0' || strcmp(emailAddr, "Non configuré") == 0) {
    Serial.printf("[Web] 📧 ❌ Adresse email invalide: '%s' - pas d'envoi\n",
                  emailAddr ? emailAddr : "(null)");
    return false;
  }

  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < emailMinHeapBytes) {
    Serial.printf("[Web] 📧 ❌ Heap insuffisant pour email (%u < %u bytes) - pas d'envoi\n",
                  freeHeap,
                  emailMinHeapBytes);
    return false;
  }

  constexpr size_t MESSAGE_BUFFER_SIZE = 256;
  char messageBuffer[MESSAGE_BUFFER_SIZE];
  size_t messageLen = automatism.createFeedingMessage(messageBuffer,
                                                      sizeof(messageBuffer),
                                                      action,
                                                      automatism.getFeedBigDur(),
                                                      automatism.getFeedSmallDur());

  Serial.printf("[Web] 📧 Configuration: enabled=%s, addr='%s', msgLen=%u\n",
                automatism.isEmailEnabled() ? "TRUE" : "FALSE",
                emailAddr ? emailAddr : "(null)",
                static_cast<unsigned>(messageLen));

  String message(messageBuffer);
  constexpr int MAX_RETRIES = 2;
  for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
    Serial.printf("[Web] 📧 Tentative d'envoi %d/%d...\n", attempt, MAX_RETRIES);
    if (mailer.sendAlert(subject, message.c_str(), emailAddr)) {
      Serial.printf("[Web] 📧 ✅ Email %s envoyé avec succès (tentative %d)\n",
                    emailType,
                    attempt);
      automatism.triggerMailBlink();
      return true;
    }

    Serial.printf("[Web] 📧 ❌ Échec tentative %d/%d pour %s\n",
                  attempt,
                  MAX_RETRIES,
                  emailType);
    if (attempt < MAX_RETRIES) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  Serial.printf("[Web] 📧 ❌ Échec définitif après %d tentatives pour %s\n",
                MAX_RETRIES,
                emailType);
  return false;
}

#else
// Stubs si DISABLE_ASYNC_WEBSERVER est défini
// Note: Les signatures doivent correspondre au header même si AsyncWebServer n'est pas disponible
// On utilise des forward declarations pour éviter les erreurs de compilation
struct AsyncWebServerRequest {};
void WebServerContext::sendJson(AsyncWebServerRequest* req, const JsonDocument& doc, bool enableCors) const {}
bool WebServerContext::sendManualActionEmail(const char* action, const char* subject, const char* emailType) const { return false; }
bool WebServerContext::ensureHeap(AsyncWebServerRequest* req, uint32_t minHeap, const __FlashStringHelper* routeName) const { return false; }
#endif


