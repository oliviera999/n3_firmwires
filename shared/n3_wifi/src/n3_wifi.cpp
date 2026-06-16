#include "n3_wifi.h"
#include <cstring>

#define N3_WIFI_CAND_MAX 10
#define N3_WIFI_LASTGOOD_MAGIC 0x4E335747UL

// Dernier AP connecté avec succès, en mémoire RTC : survit au deep sleep,
// permet une reconnexion directe (BSSID+canal) sans scan (~3-5 s économisées).
struct N3WifiLastGood {
  uint32_t magic;
  uint8_t bssid[6];
  uint8_t chan;
  char ssid[33];
};
static RTC_DATA_ATTR N3WifiLastGood s_lastGood;

static void rememberLastGood() {
  const uint8_t* b = WiFi.BSSID();
  int32_t ch = WiFi.channel();
  String ssid = WiFi.SSID();
  if (!b || ch <= 0 || ssid.isEmpty()) {
    s_lastGood.magic = 0;
    return;
  }
  memcpy(s_lastGood.bssid, b, 6);
  s_lastGood.chan = (uint8_t)ch;
  strncpy(s_lastGood.ssid, ssid.c_str(), 32);
  s_lastGood.ssid[32] = '\0';
  s_lastGood.magic = N3_WIFI_LASTGOOD_MAGIC;
}

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

static bool isConfiguredSsid(const char* scanSsid, const N3WifiConfig& config) {
  if (!scanSsid) {
    return false;
  }
  for (size_t i = 0; i < config.networkCount; i++) {
    if (config.networks[i].ssid && strcmp(scanSsid, config.networks[i].ssid) == 0) {
      return true;
    }
  }
  return false;
}

static void logScanResults(int n, const char* label, const N3WifiConfig& config) {
  if (n < 0) {
    Serial.printf("[WiFi] %s: erreur scan (%d)\n", label, n);
    return;
  }
  Serial.printf("[WiFi] %s: %d reseau(x) visible(s)\n", label, n);
  const int showMax = 25;
  const int show = (n > showMax) ? showMax : n;
  for (int j = 0; j < show; j++) {
    char scanSsid[33];
    strncpy(scanSsid, WiFi.SSID(j).c_str(), 32);
    scanSsid[32] = '\0';
    const bool configured = isConfiguredSsid(scanSsid, config);
    Serial.printf("[WiFi]   %2d. \"%s\" RSSI=%4d ch=%2d%s\n",
                  j + 1,
                  scanSsid,
                  WiFi.RSSI(j),
                  WiFi.channel(j),
                  configured ? " *" : "");
  }
  if (n > showMax) {
    Serial.printf("[WiFi]   ... %d autre(s) non affiche(s)\n", n - showMax);
  }
  if (n == 0) {
    Serial.println("[WiFi]   (aucun AP detecte — verifier 2,4 GHz et portee)");
  }
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

  unsigned long timeoutMs = config.timeoutMs > 0 ? config.timeoutMs : 5000;

  // Reconnexion rapide : essai direct sur le dernier AP réussi avant tout scan.
  // En cas d'échec (AP changé/disparu), on invalide le cache et on retombe sur
  // le scan complet ci-dessous.
  if (!config.disableFastReconnect && s_lastGood.magic == N3_WIFI_LASTGOOD_MAGIC) {
    for (size_t i = 0; i < config.networkCount; i++) {
      if (strcmp(s_lastGood.ssid, config.networks[i].ssid) != 0) continue;
      unsigned long fastTimeout = timeoutMs / 2;
      if (fastTimeout < 2000) fastTimeout = (timeoutMs < 2000) ? timeoutMs : 2000;
      Serial.printf("[WiFi] Reconnexion rapide %s ch=%d\n", s_lastGood.ssid, s_lastGood.chan);
      wifiBeginSafe(config.networks[i].ssid, config.networks[i].pass,
                    s_lastGood.chan, s_lastGood.bssid);
      if (waitConnected(fastTimeout)) {
        rememberLastGood();
        if (outWifiactif) *outWifiactif = String(config.networks[i].ssid);
        if (config.onSuccess) config.onSuccess(config.networks[i].ssid);
        Serial.printf("[WiFi] OK(rapide) %s %s RSSI=%d\n", config.networks[i].ssid,
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
      }
      s_lastGood.magic = 0;
      WiFi.disconnect(false, true);
      delay(100);
      Serial.println("[WiFi] Reconnexion rapide echouee — scan complet");
      break;
    }
  }

  delay(config.preScanDelayMs > 0 ? config.preScanDelayMs : 100);

  int n = WiFi.scanNetworks(false, true);
  logScanResults(n, "scan", config);
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

  unsigned long delayBetweenMs = config.delayBetweenMs;

  for (size_t idx = 0; idx < orderCount; idx++) {
    size_t i = order[idx];
    const char* ssid = config.networks[i].ssid;
    const char* pass = config.networks[i].pass;

    if (cand[i].present) {
      Serial.printf("[WiFi] Try %s RSSI=%d ch=%d\n", ssid, cand[i].rssi, cand[i].chan);
      wifiBeginSafe(ssid, pass, cand[i].chan, cand[i].bssid);
    } else {
      Serial.printf("[WiFi] Try (invisible) %s — rescan...\n", ssid);
      delay(300);
      const int n2 = WiFi.scanNetworks(false, true);
      logScanResults(n2, "rescan", config);
      bool found = false;
      int8_t rssi2 = -128;
      uint8_t bssid2[6] = {};
      uint8_t chan2 = 0;
      for (int j = 0; j < n2; j++) {
        if (strcmp(WiFi.SSID(j).c_str(), ssid) != 0) {
          continue;
        }
        const int8_t r = (int8_t)WiFi.RSSI(j);
        if (r > rssi2) {
          rssi2 = r;
          chan2 = (uint8_t)WiFi.channel(j);
          const uint8_t* b = WiFi.BSSID(j);
          if (b) {
            memcpy(bssid2, b, 6);
          }
          found = true;
        }
      }
      if (found) {
        Serial.printf("[WiFi] Rescan: %s visible RSSI=%d ch=%d\n", ssid, rssi2, chan2);
        wifiBeginSafe(ssid, pass, chan2, bssid2);
      } else {
        Serial.printf("[WiFi] Rescan: %s toujours invisible (%d APs)\n", ssid, n2);
        WiFi.begin(ssid, pass);
      }
    }

    const unsigned long tryTimeout = cand[i].present ? timeoutMs : (timeoutMs * 2);
    if (waitConnected(tryTimeout)) {
      rememberLastGood();
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
      if (waitConnected(timeoutMs * 2)) {
        rememberLastGood();
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
