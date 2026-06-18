#include "pgl_network.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#elif __has_include("secrets.h.example")
#include "secrets.h.example"
#else
#error "Fichier secrets.h manquant. Copier include/secrets.h.example en include/secrets.h"
#endif
#include "pgl_log.h"
#include "n3_data.h"
#include "n3_wifi.h"
#include <ArduinoJson.h>

namespace {
N3WifiNetwork kWifiNetworks[] = {
    {PGL_WIFI_SSID_1, PGL_WIFI_PASS_1},
    {PGL_WIFI_SSID_2, PGL_WIFI_PASS_2},
#if defined(PGL_WIFI_SSID_3) && defined(PGL_WIFI_PASS_3)
    {PGL_WIFI_SSID_3, PGL_WIFI_PASS_3},
#endif
};

void onWifiConnecting() {
  PGL_LOG_V("WiFi: tentative en cours...");
}

void onWifiSuccess(const char* ssid) {
  PGL_LOG_V("WiFi: callback succes ssid=%s", ssid);
}

void onWifiFailure() {
  PGL_LOG("WiFi: echec — aucun reseau disponible");
}

void logWifiStatusDetail(const char* prefix) {
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG_V("%s: deconnecte (status=%s)", prefix, pglWifiStatusName(WiFi.status()));
    return;
  }
  PGL_LOG_V("%s: ssid=%s ip=%s gw=%s mask=%s dns=%s rssi=%d ch=%d mac=%s",
            prefix,
            WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str(),
            WiFi.gatewayIP().toString().c_str(),
            WiFi.subnetMask().toString().c_str(),
            WiFi.dnsIP().toString().c_str(),
            WiFi.RSSI(),
            WiFi.channel(),
            WiFi.macAddress().c_str());
}

const char* httpVerdict(int code) {
  if (code >= 200 && code < 300) return "OK";
  if (code >= 300 && code < 400) return "redirection";
  if (code == 401) return "cle API invalide (verifier PGL_API_KEY secrets.h)";
  if (code == 400) return "payload rejete (events manquant ou invalide)";
  if (code >= 400 && code < 500) return "erreur client";
  if (code == 500) return "erreur serveur (table BDD manquante ?)";
  if (code >= 500) return "erreur serveur";
  if (code == -1) return "WiFi deconnecte";
  if (code <= 0) return "echec reseau/timeout";
  return "inconnu";
}
}  // namespace

const PglServerCommStatus& PglNetwork::getServerStatus() const {
  return serverStatus_;
}

void PglNetwork::recordPostResult(int httpCode) {
  serverStatus_.lastPostHttp = httpCode;
  serverStatus_.lastPostMs = millis();
}

void PglNetwork::recordHeartbeatResult(int httpCode) {
  serverStatus_.lastHeartbeatHttp = httpCode;
  serverStatus_.lastHeartbeatMs = millis();
}

void PglNetwork::begin() {
  WiFi.mode(WIFI_MODE_STA);
  const size_t netCount = sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0]);
  PGL_LOG("WiFi: %u reseau(x) configure(s), timeout=%ums (arriere-plan)",
          static_cast<unsigned int>(netCount), PGL_WIFI_TIMEOUT_MS);
  for (size_t i = 0; i < netCount; ++i) {
    const bool hasPass = kWifiNetworks[i].pass && kWifiNetworks[i].pass[0] != '\0';
    PGL_LOG_V("WiFi: [%u] ssid=\"%s\" mot_de_passe=%s",
              static_cast<unsigned int>(i + 1),
              kWifiNetworks[i].ssid ? kWifiNetworks[i].ssid : "(null)",
              hasPass ? "oui" : "non");
  }
  lastWifiStatus_ = WiFi.status();
}

void PglNetwork::buildWifiConfig(N3WifiConfig& cfg) const {
  cfg = {
      kWifiNetworks,
      sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0]),
      PGL_WIFI_TIMEOUT_MS,
      300,
      800,
      8,
      onWifiConnecting,
      onWifiFailure,
      onWifiSuccess,
      false,
  };
}

void PglNetwork::startBackgroundWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiSessionActive_ = false;
    wifiConnecting_ = false;
    wifiBackoff_ = false;
    return;
  }
  N3WifiConfig cfg;
  buildWifiConfig(cfg);
  n3WifiSessionBegin(wifiSession_, cfg);
  wifiSessionActive_ = true;
  wifiConnecting_ = true;
  wifiBackoff_ = false;
  wifiRetryAfterMs_ = 0;
  PGL_LOG("WiFi: connexion en arriere-plan demarree");
}

bool PglNetwork::pollWifi(uint32_t budgetMs) {
  const wl_status_t prevStatus = WiFi.status();
  bool stateChanged = (prevStatus != lastWifiStatus_);

  if (WiFi.status() == WL_CONNECTED) {
    if (wifiConnecting_ || wifiSessionActive_) {
      wifiConnecting_ = false;
      wifiSessionActive_ = false;
      wifiBackoff_ = false;
      stateChanged = true;
      PGL_LOG("WiFi: connecte ssid=%s ip=%s rssi=%d ch=%d",
              WiFi.SSID().c_str(),
              WiFi.localIP().toString().c_str(),
              WiFi.RSSI(),
              WiFi.channel());
      logWifiStatusDetail("WiFi");
    }
    lastWifiStatus_ = WiFi.status();
    return stateChanged;
  }

  if (wifiBackoff_) {
    if (millis() < wifiRetryAfterMs_) {
      lastWifiStatus_ = WiFi.status();
      return stateChanged;
    }
    wifiBackoff_ = false;
    startBackgroundWifi();
    stateChanged = true;
  }

  if (!wifiSessionActive_) {
    lastWifiStatus_ = WiFi.status();
    return stateChanged;
  }

  String connectedSsid;
  const N3WifiPollResult result = n3WifiSessionPoll(wifiSession_, budgetMs, &connectedSsid);
  if (result == N3WifiPollResult::Connected) {
    wifiConnecting_ = false;
    wifiSessionActive_ = false;
    wifiBackoff_ = false;
    stateChanged = true;
    PGL_LOG("WiFi: connecte ssid=%s ip=%s rssi=%d ch=%d",
            connectedSsid.c_str(),
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI(),
            WiFi.channel());
    logWifiStatusDetail("WiFi");
  } else if (result == N3WifiPollResult::Failed) {
    wifiConnecting_ = false;
    wifiSessionActive_ = false;
    wifiBackoff_ = true;
    wifiRetryAfterMs_ = millis() + PGL_WIFI_RETRY_INTERVAL_MS;
    stateChanged = true;
    PGL_LOG("WiFi: echec connexion (status=%s), nouvel essai dans %ums",
            pglWifiStatusName(WiFi.status()),
            static_cast<unsigned int>(PGL_WIFI_RETRY_INTERVAL_MS));
  } else {
    wifiConnecting_ = true;
  }

  if (prevStatus != WiFi.status()) {
    stateChanged = true;
  }
  lastWifiStatus_ = WiFi.status();
  return stateChanged;
}

bool PglNetwork::isWifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool PglNetwork::isWifiOffline() const {
  return WiFi.status() != WL_CONNECTED && !isWifiConnecting();
}

bool PglNetwork::isWifiConnecting() const {
  return (wifiConnecting_ || wifiSessionActive_) && WiFi.status() != WL_CONNECTED;
}

void PglNetwork::tryConnectBeforeUpload() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (!wifiSessionActive_ && !wifiBackoff_) {
    startBackgroundWifi();
  }
  pollWifi(PGL_WIFI_UPLOAD_BUDGET_MS);
}

PglUploadResult PglNetwork::uploadBatch(const PglStoredEvent* events, size_t count,
                                        uint32_t totalCount, uint32_t todayCount) {
  PglUploadResult result;
  if (!events || count == 0) {
    result.ok = true;
    return result;
  }

  tryConnectBeforeUpload();
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG("Serveur POST: annule — WiFi %s", pglWifiStatusName(WiFi.status()));
    recordPostResult(-1);
    return result;
  }
  logWifiStatusDetail("Serveur POST");

  PGL_LOG("Serveur POST: envoi vers %s", PGL_SERVER_POST_URL);
  PGL_LOG("Serveur POST: lot=%u evt | total=%lu today=%lu | sensor=%s location=%s v=%s",
          static_cast<unsigned int>(count),
          static_cast<unsigned long>(totalCount),
          static_cast<unsigned long>(todayCount),
          PGL_SENSOR_NAME,
          PGL_SENSOR_LOCATION,
          PGL_FIRMWARE_VERSION);

  String eventsPayload;
  eventsPayload.reserve(count * 32);
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) eventsPayload += ',';
    eventsPayload += String(events[i].epoch);
    eventsPayload += ':';
    eventsPayload += String(events[i].countDelta);
    eventsPayload += ':';
    eventsPayload += String(events[i].sensorMode);
    eventsPayload += ':';
    eventsPayload += String(events[i].tandemValidated);
    eventsPayload += ':';
    eventsPayload += String(events[i].batteryMilliVolt);
    eventsPayload += ':';
    eventsPayload += String(events[i].rssi);
    eventsPayload += ':';
    eventsPayload += String(events[i].eventId);
  }

  PGL_LOG_V("Serveur POST: payload events=%s", eventsPayload.c_str());

  String responseBody;

  N3DataField fields[] = {
      {"api_key", String(PGL_API_KEY)},
      {"sensor", String(PGL_SENSOR_NAME)},
      {"location", String(PGL_SENSOR_LOCATION)},
      {"version", String(PGL_FIRMWARE_VERSION)},
      {"total_count", String(totalCount)},
      {"today_count", String(todayCount)},
      {"batch_count", String(static_cast<unsigned int>(count))},
      {"events", eventsPayload},
  };

  N3PostConfig cfg = {
      PGL_SERVER_POST_URL,
      PGL_API_KEY,
      fields,
      sizeof(fields) / sizeof(fields[0]),
      nullptr,
      nullptr,
      nullptr,
      0,
      &responseBody,
  };
  const int code = n3DataPost(cfg);
  recordPostResult(code);
  result.ok = (code >= 200 && code < 300);

  PGL_LOG("Serveur POST: HTTP %d — %s (lot=%u evt, total=%lu)",
          code,
          httpVerdict(code),
          static_cast<unsigned int>(count),
          static_cast<unsigned long>(totalCount));

  if (result.ok && responseBody.length() > 0) {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, responseBody);
    if (!err && doc["last_acked_event_id"].is<uint32_t>()) {
      result.lastAckedEventId = doc["last_acked_event_id"].as<uint32_t>();
      PGL_LOG_V("Serveur POST: ack last_event_id=%lu",
                static_cast<unsigned long>(result.lastAckedEventId));
    } else if (err) {
      PGL_LOG_V("Serveur POST: parse JSON ack echoue (%s)", err.c_str());
    }
  }

  return result;
}

bool PglNetwork::sendHeartbeat(uint32_t bootCount) {
#if !PGL_ENABLE_SERVER_HEARTBEAT
  (void)bootCount;
  return false;
#else
  tryConnectBeforeUpload();
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG("Serveur HB: annule — WiFi %s", pglWifiStatusName(WiFi.status()));
    recordHeartbeatResult(-1);
    return false;
  }
  logWifiStatusDetail("Serveur HB");

  static uint32_t minHeap = UINT32_MAX;
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < minHeap) {
    minHeap = freeHeap;
  }

  const uint32_t uptimeSec = millis() / 1000UL;
  const int rssi = WiFi.RSSI();

  PGL_LOG("Serveur HB: envoi vers %s", PGL_SERVER_HEARTBEAT_URL);
  PGL_LOG_V("Serveur HB: uptime=%lus free=%lu min=%lu reboots=%lu rssi=%d",
            static_cast<unsigned long>(uptimeSec),
            static_cast<unsigned long>(freeHeap),
            static_cast<unsigned long>(minHeap == UINT32_MAX ? freeHeap : minHeap),
            static_cast<unsigned long>(bootCount),
            rssi);

  N3DataField fields[] = {
      {"api_key", String(PGL_API_KEY)},
      {"sensor", String(PGL_SENSOR_NAME)},
      {"version", String(PGL_FIRMWARE_VERSION)},
      {"uptime", String(uptimeSec)},
      {"free", String(freeHeap)},
      {"min", String(minHeap == UINT32_MAX ? freeHeap : minHeap)},
      {"reboots", String(bootCount)},
      {"rssi", String(rssi)},
  };

  N3PostConfig cfg = {
      PGL_SERVER_HEARTBEAT_URL,
      PGL_API_KEY,
      fields,
      sizeof(fields) / sizeof(fields[0]),
      nullptr,
      nullptr,
      nullptr,
      0,
      nullptr,
  };
  const int code = n3DataPost(cfg);
  recordHeartbeatResult(code);
  const bool ok = (code >= 200 && code < 300);
  PGL_LOG("Serveur HB: HTTP %d — %s (uptime=%lus reboots=%lu)",
          code,
          httpVerdict(code),
          static_cast<unsigned long>(uptimeSec),
          static_cast<unsigned long>(bootCount));
  return ok;
#endif
}
