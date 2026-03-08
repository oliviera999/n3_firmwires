#include "n3_wifi.h"
#include <cstring>

#define N3_WIFI_CAND_MAX 10

static void wifiBeginSafe(const char* ssid, const char* pass, int32_t chan, const uint8_t* bssid) {
  if (pass && strlen(pass) > 0) {
    WiFi.begin(ssid, pass, chan, bssid);
  } else {
    if (chan > 0 || bssid) WiFi.begin(ssid, nullptr, chan, bssid);
    else WiFi.begin(ssid);
  }
}

static bool waitConnected(uint32_t timeoutMs) {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool n3WifiConnect(const N3WifiConfig& config, String* outWifiactif) {
  if (outWifiactif) outWifiactif->clear();

  if (WiFi.status() == WL_CONNECTED) {
    if (outWifiactif) *outWifiactif = WiFi.SSID();
    return true;
  }

  if (config.onConnecting) config.onConnecting();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  delay(config.preScanDelayMs > 0 ? config.preScanDelayMs : 300);

  int n = WiFi.scanNetworks(false, true);
  int scanMax = (config.scanMax > 0 && config.scanMax <= N3_WIFI_CAND_MAX) ? config.scanMax : N3_WIFI_CAND_MAX;
  if (n > scanMax) n = scanMax;

  struct Cand { int8_t rssi; uint8_t bssid[6]; uint8_t chan; bool present; };
  Cand cand[N3_WIFI_CAND_MAX];
  size_t netCount = config.networkCount;
  if (netCount > (size_t)scanMax) netCount = scanMax;

  for (size_t i = 0; i < netCount; i++) {
    cand[i].rssi = -128;
    cand[i].chan = 0;
    cand[i].present = false;
    memset(cand[i].bssid, 0, 6);
  }

  for (int j = 0; j < n; j++) {
    char scanSsid[33];
    strncpy(scanSsid, WiFi.SSID(j).c_str(), 32);
    scanSsid[32] = '\0';
    int8_t r = (int8_t)WiFi.RSSI(j);
    for (size_t i = 0; i < netCount; i++) {
      if (strcmp(scanSsid, config.networks[i].ssid) == 0 && r > cand[i].rssi) {
        cand[i].rssi = r;
        const uint8_t* b = WiFi.BSSID(j);
        if (b) memcpy(cand[i].bssid, b, 6);
        cand[i].chan = (uint8_t)WiFi.channel(j);
        cand[i].present = true;
      }
    }
  }

  size_t order[N3_WIFI_CAND_MAX];
  size_t orderCount = 0;
  for (size_t i = 0; i < netCount; i++) {
    if (cand[i].present) order[orderCount++] = i;
  }
  for (size_t k = 0; k < orderCount; k++) {
    for (size_t j = k + 1; j < orderCount; j++) {
      if (cand[order[j]].rssi > cand[order[k]].rssi) {
        size_t t = order[k]; order[k] = order[j]; order[j] = t;
      }
    }
  }
  for (size_t i = 0; i < netCount; i++) {
    if (!cand[i].present) order[orderCount++] = i;
  }

  unsigned long timeoutMs = config.timeoutMs > 0 ? config.timeoutMs : 5000;
  unsigned long delayBetweenMs = config.delayBetweenMs;

  for (size_t idx = 0; idx < orderCount; idx++) {
    size_t i = order[idx];
    const char* ssid = config.networks[i].ssid;
    const char* pass = config.networks[i].pass;

    if (cand[i].present) {
      Serial.printf("[WiFi] Try %s RSSI=%d ch=%d\n", ssid, cand[i].rssi, cand[i].chan);
      wifiBeginSafe(ssid, pass, cand[i].chan, cand[i].bssid);
    } else {
      Serial.printf("[WiFi] Try (invisible) %s\n", ssid);
      WiFi.begin(ssid, pass);
    }

    if (waitConnected(timeoutMs)) {
      if (outWifiactif) *outWifiactif = String(ssid);
      if (config.onSuccess) config.onSuccess(ssid);
      Serial.printf("[WiFi] OK %s %s RSSI=%d\n", ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
      return true;
    }

    if (cand[i].present) {
      WiFi.disconnect(false, true);
      delay(250);
      Serial.printf("[WiFi] Retry sans BSSID %s\n", ssid);
      WiFi.begin(ssid, pass);
      if (waitConnected(timeoutMs)) {
        if (outWifiactif) *outWifiactif = String(ssid);
        if (config.onSuccess) config.onSuccess(ssid);
        Serial.printf("[WiFi] OK(2e) %s %s\n", ssid, WiFi.localIP().toString().c_str());
        return true;
      }
    }
    Serial.printf("[WiFi] Echec %s\n", ssid);
    delay(delayBetweenMs);
  }

  Serial.println("[WiFi] Echec connexion - aucun reseau disponible");
  if (config.onFailure) config.onFailure();
  return false;
}
