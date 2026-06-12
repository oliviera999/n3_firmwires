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
};
}

void PglNetwork::begin() {
  WiFi.mode(WIFI_MODE_STA);
  PGL_LOG_V("WiFi: %u reseau(x) configures, timeout=%ums",
            static_cast<unsigned int>(sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0])),
            PGL_WIFI_TIMEOUT_MS);
}

bool PglNetwork::uploadBatch(const PglStoredEvent* events, size_t count, uint32_t totalCount, uint32_t todayCount) {
  if (!events || count == 0) return true;
  ensureWifi();
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG("Upload: WiFi non connecte (%s)", pglWifiStatusName(WiFi.status()));
    return false;
  }
  PGL_LOG_V("Upload: WiFi OK ssid=%s rssi=%d", WiFi.SSID().c_str(), WiFi.RSSI());

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
  PGL_LOG("POST %s → HTTP %d (lot=%u, total=%lu)",
          PGL_SERVER_POST_URL, code, static_cast<unsigned int>(count),
          static_cast<unsigned long>(totalCount));
  return (code >= 200 && code < 300);
}

bool PglNetwork::sendHeartbeat(uint32_t bootCount) {
#if !PGL_ENABLE_SERVER_HEARTBEAT
  (void)bootCount;
  return false;
#else
  ensureWifi();
  if (WiFi.status() != WL_CONNECTED) {
    PGL_LOG("Heartbeat: WiFi non connecte");
    return false;
  }

  static uint32_t minHeap = UINT32_MAX;
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < minHeap) {
    minHeap = freeHeap;
  }

  const uint32_t uptimeSec = millis() / 1000UL;
  const int rssi = WiFi.RSSI();

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
  PGL_LOG("Heartbeat POST %s → HTTP %d (uptime=%lu reboots=%lu)",
          PGL_SERVER_HEARTBEAT_URL, code,
          static_cast<unsigned long>(uptimeSec),
          static_cast<unsigned long>(bootCount));
  return (code >= 200 && code < 300);
#endif
}

bool PglNetwork::isWifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void PglNetwork::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  PGL_LOG("WiFi: connexion en cours...");
  String connectedSsid;
  N3WifiConfig cfg = {
      kWifiNetworks,
      sizeof(kWifiNetworks) / sizeof(kWifiNetworks[0]),
      PGL_WIFI_TIMEOUT_MS,
      250,
      100,
      8,
      nullptr,
      nullptr,
      nullptr,
  };
  const bool ok = n3WifiConnect(cfg, &connectedSsid);
  if (ok) {
    PGL_LOG("WiFi: connecte ssid=%s ip=%s rssi=%d",
            connectedSsid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    PGL_LOG("WiFi: echec connexion (status=%s)", pglWifiStatusName(WiFi.status()));
  }
}
