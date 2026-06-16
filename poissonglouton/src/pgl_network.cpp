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

void logWifiScanListPgl() {
  delay(200);
  const int n = WiFi.scanNetworks(false, true);
  PGL_LOG("WiFi scan: %d reseau(x) visible(s)", n);
  if (n < 0) {
    PGL_LOG("WiFi scan: erreur %d", n);
    return;
  }
  const size_t netCount = sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0]);
  const int showMax = 25;
  const int show = (n > showMax) ? showMax : n;
  for (int j = 0; j < show; j++) {
    const char* ssid = WiFi.SSID(j).c_str();
    bool configured = false;
    for (size_t i = 0; i < netCount; i++) {
      if (kWifiNetworks[i].ssid && strcmp(ssid, kWifiNetworks[i].ssid) == 0) {
        configured = true;
        break;
      }
    }
    PGL_LOG("  [%d] \"%s\" RSSI=%d ch=%d%s",
            j + 1,
            ssid,
            WiFi.RSSI(j),
            WiFi.channel(j),
            configured ? " (configure)" : "");
  }
  if (n > showMax) {
    PGL_LOG("  ... %d autre(s) non affiche(s)", n - showMax);
  }
  if (n == 0) {
    PGL_LOG("  (aucun AP detecte — ESP32 = 2,4 GHz uniquement)");
  }
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
  PGL_LOG("WiFi: %u reseau(x) configure(s), timeout=%ums",
          static_cast<unsigned int>(netCount), PGL_WIFI_TIMEOUT_MS);
  for (size_t i = 0; i < netCount; ++i) {
    const bool hasPass = kWifiNetworks[i].pass && kWifiNetworks[i].pass[0] != '\0';
    PGL_LOG_V("WiFi: [%u] ssid=\"%s\" mot_de_passe=%s",
              static_cast<unsigned int>(i + 1),
              kWifiNetworks[i].ssid ? kWifiNetworks[i].ssid : "(null)",
              hasPass ? "oui" : "non");
  }
}

void PglNetwork::connectWifi() {
  PGL_LOG("WiFi: liste des reseaux disponibles...");
  logWifiScanListPgl();
  ensureWifi();
}

bool PglNetwork::uploadBatch(const PglStoredEvent* events, size_t count, uint32_t totalCount, uint32_t todayCount) {
  if (!events || count == 0) return true;
  ensureWifi();
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG("Serveur POST: annule — WiFi %s", pglWifiStatusName(WiFi.status()));
    recordPostResult(-1);
    return false;
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
  eventsPayload.reserve(count * 24);
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
  }

  PGL_LOG_V("Serveur POST: payload events=%s", eventsPayload.c_str());

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
  };
  const int code = n3DataPost(cfg);
  recordPostResult(code);
  const bool ok = (code >= 200 && code < 300);
  PGL_LOG("Serveur POST: HTTP %d — %s (lot=%u evt, total=%lu)",
          code,
          httpVerdict(code),
          static_cast<unsigned int>(count),
          static_cast<unsigned long>(totalCount));
  return ok;
}

bool PglNetwork::sendHeartbeat(uint32_t bootCount) {
#if !PGL_ENABLE_SERVER_HEARTBEAT
  (void)bootCount;
  return false;
#else
  ensureWifi();
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

bool PglNetwork::isWifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void PglNetwork::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    PGL_LOG_V("WiFi: deja connecte ssid=%s rssi=%d", WiFi.SSID().c_str(), WiFi.RSSI());
    return;
  }
  PGL_LOG("WiFi: connexion en cours...");
  String connectedSsid;
  N3WifiConfig cfg = {
      kWifiNetworks,
      sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0]),
      PGL_WIFI_TIMEOUT_MS,
      300,
      800,
      8,
      onWifiConnecting,
      onWifiFailure,
      onWifiSuccess,
  };
  const bool ok = n3WifiConnect(cfg, &connectedSsid);
  if (ok) {
    PGL_LOG("WiFi: connecte ssid=%s ip=%s rssi=%d ch=%d",
            connectedSsid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI(),
            WiFi.channel());
    logWifiStatusDetail("WiFi");
  } else {
    PGL_LOG("WiFi: echec connexion (status=%s)", pglWifiStatusName(WiFi.status()));
  }
}
