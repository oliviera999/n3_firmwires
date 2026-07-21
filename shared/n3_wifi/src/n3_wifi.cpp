#include "n3_wifi.h"

#include <cstring>

#include "n3_wifi_select.h"     // noyau pur mutualise : selection RSSI/BSSID + ordre d'essai
#include "n3_wifi_reconnect.h"  // noyau pur mutualise : record dernier AP + fast reconnect

#define N3_WIFI_CAND_MAX N3_WIFI_SESSION_CAND_MAX
#define N3_WIFI_LASTGOOD_MAGIC 0x4E335747UL

namespace {

enum N3WifiPhase : uint8_t {
  kIdle = 0,
  kFastReconnect,
  kPreScanWait,
  kScanAsync,
  kTryConnect,
  kWaitConnect,
  kRetryNoBssid,
  kWaitRetryConnect,
  kInvisibleRescanWait,
  kInvisibleRescan,
  kDelayBetween,
  kConnected,
  kFailed,
};

// Record du dernier AP en RTC (survit au deep sleep). Type + logique de
// validation/construction mutualises dans shared/n3_wifi_reconnect ; le magic
// reste propre a n3_wifi (record RTC compatible a travers les OTA).
static RTC_DATA_ATTR N3WifiReconnect::LastGoodAp s_lastGood;

void rememberLastGood() {
  N3WifiReconnect::store(s_lastGood, N3_WIFI_LASTGOOD_MAGIC, WiFi.SSID().c_str(), WiFi.BSSID(),
                         WiFi.channel());
}

void wifiBeginSafe(const char* ssid, const char* pass, int32_t chan, const uint8_t* bssid) {
  if (pass && strlen(pass) > 0) {
    WiFi.begin(ssid, pass, chan, bssid);
  } else if (chan > 0 || bssid) {
    WiFi.begin(ssid, nullptr, chan, bssid);
  } else {
    WiFi.begin(ssid);
  }
}

bool isConfiguredSsid(const char* scanSsid, const N3WifiConfig& config) {
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

void logScanResults(int n, const char* label, const N3WifiConfig& config) {
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

void setupWifiSta() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
}

void callOnConnectingOnce(N3WifiSession& session) {
  if (session.onConnectingCalled || !session.config || !session.config->onConnecting) {
    return;
  }
  session.onConnectingCalled = true;
  session.config->onConnecting();
}

void finishConnected(N3WifiSession& session, const char* ssid) {
  rememberLastGood();
  strncpy(session.connectedSsid, ssid, 32);
  session.connectedSsid[32] = '\0';
  session.phase = kConnected;
  if (session.config && session.config->onSuccess) {
    session.config->onSuccess(ssid);
  }
  Serial.printf("[WiFi] OK %s %s RSSI=%d\n", ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void finishFailed(N3WifiSession& session, uint8_t reason) {
  session.phase = kFailed;
  session.failureReason = reason;
  Serial.printf("[WiFi] Echec connexion — %s\n", n3WifiSessionFailureReasonName(reason));
  if (session.config && session.config->onFailure) {
    session.config->onFailure();
  }
}

void buildOrderFromScan(N3WifiSession& session, int n) {
  const N3WifiConfig& config = *session.config;
  int scanMax = (config.scanMax > 0 && config.scanMax <= N3_WIFI_CAND_MAX) ? config.scanMax
                                                                           : N3_WIFI_CAND_MAX;
  if (n > scanMax) {
    n = scanMax;
  }

  // Mapping des resultats de scan Arduino -> entrees agnostiques du noyau pur.
  // Les SSID sont copies dans des buffers stables (N3WifiSelect ne les copie pas).
  static char scanSsids[N3_WIFI_CAND_MAX][33];
  N3WifiSelect::ScanEntry entries[N3_WIFI_CAND_MAX];
  const size_t scanCount = (n > 0) ? static_cast<size_t>(n) : 0;
  for (size_t j = 0; j < scanCount; j++) {
    strncpy(scanSsids[j], WiFi.SSID(j).c_str(), 32);
    scanSsids[j][32] = '\0';
    entries[j].ssid = scanSsids[j];
    entries[j].rssi = static_cast<int8_t>(WiFi.RSSI(j));
    const uint8_t* b = WiFi.BSSID(j);
    if (b) {
      memcpy(entries[j].bssid, b, N3WifiSelect::kBssidLen);
    } else {
      memset(entries[j].bssid, 0, N3WifiSelect::kBssidLen);
    }
    entries[j].channel = static_cast<uint8_t>(WiFi.channel(j));
  }

  // SSID configures -> tableau de pointeurs (stables : possedes par config).
  const char* ssidPtrs[N3_WIFI_CAND_MAX];
  size_t cfgCount = config.networkCount;
  if (cfgCount > N3_WIFI_CAND_MAX) {
    cfgCount = N3_WIFI_CAND_MAX;
  }
  for (size_t i = 0; i < cfgCount; i++) {
    ssidPtrs[i] = config.networks[i].ssid;
  }

  // Noyau pur mutualise (shared/n3_wifi_select) : meilleure candidate RSSI +
  // BSSID/canal par credential, puis ordre d'essai (visibles tries RSSI
  // decroissant, egalites -> index d'origine, caches en fin). Comportement
  // identique a l'ancienne boucle locale (verifie par test_wifi_select).
  N3WifiSelect::Candidate cand[N3_WIFI_CAND_MAX];
  size_t order[N3_WIFI_CAND_MAX];
  const size_t netCount = N3WifiSelect::buildOrder(ssidPtrs, cfgCount, entries, scanCount, cand,
                                                   order, static_cast<size_t>(scanMax));

  // Recopie dans l'etat de session (N3WifiCand : memes champs, chan == channel).
  session.netCount = netCount;
  for (size_t i = 0; i < netCount; i++) {
    session.cand[i].rssi = cand[i].rssi;
    session.cand[i].chan = cand[i].channel;
    session.cand[i].present = cand[i].present;
    memcpy(session.cand[i].bssid, cand[i].bssid, N3WifiSelect::kBssidLen);
    session.order[i] = order[i];
  }
  session.orderCount = netCount;
  session.orderIdx = 0;
}

void startFastReconnect(N3WifiSession& session) {
  const N3WifiConfig& config = *session.config;

  // Decision mutualisee (shared/n3_wifi_reconnect) : le dernier AP RTC est-il
  // encore un reseau configure ? Si oui, index du credential a re-associer.
  const char* ssidPtrs[N3_WIFI_CAND_MAX];
  size_t n = config.networkCount;
  if (n > N3_WIFI_CAND_MAX) {
    n = N3_WIFI_CAND_MAX;
  }
  for (size_t i = 0; i < n; i++) {
    ssidPtrs[i] = config.networks[i].ssid;
  }
  const int idx = N3WifiReconnect::matchIndex(s_lastGood, N3_WIFI_LASTGOOD_MAGIC, ssidPtrs, n);

  if (idx >= 0) {
    const size_t i = static_cast<size_t>(idx);
    const unsigned long fastTimeout = N3WifiReconnect::fastTimeoutMs(session.timeoutMs);
    Serial.printf("[WiFi] Reconnexion rapide %s ch=%d\n", s_lastGood.ssid, s_lastGood.channel);
    wifiBeginSafe(config.networks[i].ssid,
                  config.networks[i].pass,
                  s_lastGood.channel,
                  s_lastGood.bssid);
    strncpy(session.currentSsid, config.networks[i].ssid, 32);
    session.currentSsid[32] = '\0';
    session.connectDeadline = millis() + fastTimeout;
    session.connectStartedMs = millis();
    session.retryNoBssid = false;
    session.phase = kFastReconnect;
    return;
  }
  session.phase = kPreScanWait;
  session.phaseDeadline = millis() + (config.preScanDelayMs > 0 ? config.preScanDelayMs : 100);
}

void beginTryCurrentNetwork(N3WifiSession& session);

/** Repli bloquant si le scan async echoue (ESP32-CAM / deep sleep). Retourne true si etat session mis a jour. */
bool trySyncScanFallback(N3WifiSession& session) {
  Serial.println("[WiFi] Repli scan synchrone...");
  WiFi.scanDelete();
  delay(150);
  const int n = WiFi.scanNetworks(false, true);
  if (n == WIFI_SCAN_FAILED) {
    Serial.println("[WiFi][WARN] scan synchrone echoue");
    return false;
  }
  logScanResults(n, "scan-sync", *session.config);
  buildOrderFromScan(session, n);
  if (session.orderCount == 0) {
    finishFailed(session, N3_WIFI_FAIL_NO_AP);
    return true;
  }
  beginTryCurrentNetwork(session);
  return true;
}

void startScanAsync(N3WifiSession& session) {
  const int rc = WiFi.scanNetworks(true, true);
  if (rc == WIFI_SCAN_FAILED) {
    Serial.println("[WiFi][WARN] demarrage scan async echoue");
    if (trySyncScanFallback(session)) {
      return;
    }
    finishFailed(session, N3_WIFI_FAIL_SCAN_START);
    return;
  }
  session.phase = kScanAsync;
}

void beginTryCurrentNetwork(N3WifiSession& session) {
  if (session.orderIdx >= session.orderCount) {
    finishFailed(session, N3_WIFI_FAIL_ALL_NETS);
    return;
  }

  const size_t i = session.order[session.orderIdx];
  const N3WifiConfig& config = *session.config;
  const char* ssid = config.networks[i].ssid;
  const char* pass = config.networks[i].pass;
  if (!ssid || ssid[0] == '\0') {
    session.orderIdx++;
    if (session.orderIdx >= session.orderCount) {
      finishFailed(session, N3_WIFI_FAIL_ALL_NETS);
      return;
    }
    beginTryCurrentNetwork(session);
    return;
  }
  strncpy(session.currentSsid, ssid, 32);
  session.currentSsid[32] = '\0';
  session.retryNoBssid = false;

  if (session.cand[i].present) {
    Serial.printf("[WiFi] Try %s RSSI=%d ch=%d\n", ssid, session.cand[i].rssi, session.cand[i].chan);
    wifiBeginSafe(ssid, pass, session.cand[i].chan, session.cand[i].bssid);
    session.connectDeadline = millis() + session.timeoutMs;
    session.connectStartedMs = millis();
    session.phase = kWaitConnect;
    return;
  }

  Serial.printf("[WiFi] Try (invisible) %s — rescan...\n", ssid);
  strncpy(session.invisibleRescanSsid, ssid, 32);
  session.invisibleRescanSsid[32] = '\0';
  session.phase = kInvisibleRescanWait;
  session.phaseDeadline = millis() + 300;
}

void advanceAfterConnectFailure(N3WifiSession& session) {
  const size_t i = session.order[session.orderIdx];
  if (session.cand[i].present && !session.retryNoBssid) {
    WiFi.disconnect(true, true);
    session.retryNoBssid = true;
    session.pendingNoBssidRetry = true;
    session.phase = kDelayBetween;
    session.phaseDeadline = millis() + 500;
    Serial.printf("[WiFi] Retry sans BSSID %s (pause 500ms)\n", session.currentSsid);
    return;
  }

  Serial.printf("[WiFi] Echec %s\n", session.currentSsid);
  session.retryNoBssid = false;
  session.pendingNoBssidRetry = false;
  session.orderIdx++;
  if (session.orderIdx >= session.orderCount) {
    finishFailed(session, N3_WIFI_FAIL_ALL_NETS);
    return;
  }
  const unsigned long delayBetweenMs =
      session.config->delayBetweenMs > 0 ? session.config->delayBetweenMs : 0;
  if (delayBetweenMs > 0) {
    session.phase = kDelayBetween;
    session.phaseDeadline = millis() + delayBetweenMs;
  } else {
    beginTryCurrentNetwork(session);
  }
}

bool processInvisibleRescanResults(N3WifiSession& session, int n2) {
  logScanResults(n2, "rescan", *session.config);
  const size_t i = session.order[session.orderIdx];
  const char* pass = session.config->networks[i].pass;
  bool found = false;
  int8_t rssi2 = -128;
  uint8_t bssid2[6] = {};
  uint8_t chan2 = 0;

  for (int j = 0; j < n2; j++) {
    if (strcmp(WiFi.SSID(j).c_str(), session.invisibleRescanSsid) != 0) {
      continue;
    }
    const int8_t r = static_cast<int8_t>(WiFi.RSSI(j));
    if (r > rssi2) {
      rssi2 = r;
      chan2 = static_cast<uint8_t>(WiFi.channel(j));
      const uint8_t* b = WiFi.BSSID(j);
      if (b) {
        memcpy(bssid2, b, 6);
      }
      found = true;
    }
  }

  if (found) {
    Serial.printf("[WiFi] Rescan: %s visible RSSI=%d ch=%d\n",
                  session.invisibleRescanSsid,
                  rssi2,
                  chan2);
    wifiBeginSafe(session.invisibleRescanSsid, pass, chan2, bssid2);
    session.cand[i].present = true;
    session.connectDeadline = millis() + session.timeoutMs;
    session.connectStartedMs = millis();
    session.phase = kWaitConnect;
    return true;
  }

  Serial.printf("[WiFi] Rescan: %s toujours invisible (%d APs)\n", session.invisibleRescanSsid, n2);
  WiFi.begin(session.invisibleRescanSsid, pass);
  session.connectDeadline = millis() + session.timeoutMs * 2;
  session.connectStartedMs = millis();
  session.phase = kWaitConnect;
  return true;
}

bool pollPhase(N3WifiSession& session) {
  if (WiFi.status() == WL_CONNECTED && session.phase != kConnected && session.phase != kFailed &&
      session.phase != kIdle) {
    finishConnected(session, WiFi.SSID().c_str());
    return true;
  }

  switch (session.phase) {
    case kIdle:
      return true;

    case kFastReconnect:
      if (millis() >= session.connectDeadline) {
        s_lastGood.magic = 0;
        WiFi.disconnect(false, true);
        Serial.println("[WiFi] Reconnexion rapide echouee — scan complet");
        session.fastReconnectTried = true;
        session.phase = kPreScanWait;
        session.phaseDeadline =
            millis() + (session.config->preScanDelayMs > 0 ? session.config->preScanDelayMs : 100);
      }
      return false;

    case kPreScanWait:
      if (millis() >= session.phaseDeadline) {
        startScanAsync(session);
      }
      return false;

    case kScanAsync: {
      const int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) {
        return false;
      }
      if (n == WIFI_SCAN_FAILED) {
        Serial.println("[WiFi][WARN] scan async echoue");
        if (trySyncScanFallback(session)) {
          return false;
        }
        finishFailed(session, N3_WIFI_FAIL_SCAN);
        return true;
      }
      logScanResults(n, "scan", *session.config);
      buildOrderFromScan(session, n);
      if (session.orderCount == 0) {
        finishFailed(session, N3_WIFI_FAIL_NO_AP);
        return true;
      }
      beginTryCurrentNetwork(session);
      return false;
    }

    // kWaitConnect et kWaitRetryConnect partagent la meme logique d'attente.
    case kWaitConnect:
    case kWaitRetryConnect:
      if (millis() >= session.connectDeadline) {
        advanceAfterConnectFailure(session);
      } else if (WiFi.status() == WL_CONNECT_FAILED &&
                 (millis() - session.connectStartedMs) > 2000) {
        advanceAfterConnectFailure(session);
      }
      return false;

    case kInvisibleRescanWait:
      if (millis() >= session.phaseDeadline) {
        const int rc = WiFi.scanNetworks(true, true);
        if (rc == WIFI_SCAN_FAILED) {
          finishFailed(session, N3_WIFI_FAIL_RESCAN);
          return true;
        }
        session.phase = kInvisibleRescan;
      }
      return false;

    case kInvisibleRescan: {
      const int n2 = WiFi.scanComplete();
      if (n2 == WIFI_SCAN_RUNNING) {
        return false;
      }
      if (n2 == WIFI_SCAN_FAILED) {
        finishFailed(session, N3_WIFI_FAIL_RESCAN);
        return true;
      }
      processInvisibleRescanResults(session, n2);
      return false;
    }

    case kDelayBetween:
      if (millis() >= session.phaseDeadline) {
        if (session.pendingNoBssidRetry) {
          session.pendingNoBssidRetry = false;
          const size_t idx = session.order[session.orderIdx];
          const char* pass = session.config->networks[idx].pass;
          WiFi.begin(session.currentSsid, pass);
          session.connectDeadline = millis() + session.timeoutMs * 2;
          session.connectStartedMs = millis();
          session.phase = kWaitRetryConnect;
        } else {
          beginTryCurrentNetwork(session);
        }
      }
      return false;

    case kConnected:
    case kFailed:
      return true;

    default:
      finishFailed(session, N3_WIFI_FAIL_TIMEOUT);
      return true;
  }
}

}  // namespace

void n3WifiSessionReset(N3WifiSession& session) {
  session.config = nullptr;
  session.phase = kIdle;
  session.phaseDeadline = 0;
  session.connectDeadline = 0;
  session.connectStartedMs = 0;
  session.timeoutMs = 5000;
  session.orderCount = 0;
  session.orderIdx = 0;
  memset(session.order, 0, sizeof(session.order));
  memset(session.cand, 0, sizeof(session.cand));
  session.netCount = 0;
  session.retryNoBssid = false;
  session.pendingNoBssidRetry = false;
  session.onConnectingCalled = false;
  session.fastReconnectTried = false;
  session.currentSsid[0] = '\0';
  session.connectedSsid[0] = '\0';
  session.invisibleRescanSsid[0] = '\0';
  session.failureReason = N3_WIFI_FAIL_NONE;
}

void n3WifiSessionBegin(N3WifiSession& session, const N3WifiConfig& config) {
  n3WifiSessionReset(session);
  session.config = &config;
  session.timeoutMs = config.timeoutMs > 0 ? config.timeoutMs : 5000;

  if (WiFi.status() == WL_CONNECTED) {
    strncpy(session.connectedSsid, WiFi.SSID().c_str(), 32);
    session.connectedSsid[32] = '\0';
    session.phase = kConnected;
    return;
  }

  setupWifiSta();
  callOnConnectingOnce(session);

  if (!config.disableFastReconnect &&
      N3WifiReconnect::valid(s_lastGood, N3_WIFI_LASTGOOD_MAGIC)) {
    startFastReconnect(session);
    return;
  }

  session.phase = kPreScanWait;
  session.phaseDeadline = millis() + (config.preScanDelayMs > 0 ? config.preScanDelayMs : 100);
}

N3WifiPollResult n3WifiSessionPoll(N3WifiSession& session, uint32_t budgetMs, String* outSsid) {
  if (!session.config) {
    return N3WifiPollResult::Failed;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (session.phase != kConnected) {
      finishConnected(session, WiFi.SSID().c_str());
    }
    if (outSsid) {
      *outSsid = session.connectedSsid;
    }
    return N3WifiPollResult::Connected;
  }

  if (session.phase == kConnected) {
    if (outSsid) {
      *outSsid = session.connectedSsid;
    }
    return N3WifiPollResult::Connected;
  }

  if (session.phase == kFailed) {
    return N3WifiPollResult::Failed;
  }

  if (session.phase == kIdle) {
    n3WifiSessionBegin(session, *session.config);
  }

  const unsigned long start = millis();
  while ((millis() - start) < budgetMs) {
    const bool done = pollPhase(session);
    if (session.phase == kConnected) {
      if (outSsid) {
        *outSsid = session.connectedSsid;
      }
      return N3WifiPollResult::Connected;
    }
    if (session.phase == kFailed) {
      return N3WifiPollResult::Failed;
    }
    if (done) {
      break;
    }
    delay(10);
  }

  return N3WifiPollResult::InProgress;
}

const char* n3WifiSessionPhaseName(uint8_t phase) {
  // Valeurs alignees sur N3WifiPhase (n3_wifi.cpp, namespace interne).
  switch (phase) {
    case 0: return "idle";
    case 1: return "fast";
    case 2: return "pre-scan";
    case 3: return "scan";
    case 4: return "try";
    case 5: return "connect";
    case 6: return "retry";
    case 7: return "wait-retry";
    case 8: return "rescan-w";
    case 9: return "rescan";
    case 10: return "pause";
    case 11: return "ok";
    case 12: return "fail";
    default: return "?";
  }
}

const char* n3WifiSessionFailureReasonName(uint8_t reason) {
  switch (reason) {
    case N3_WIFI_FAIL_NONE: return "none";
    case N3_WIFI_FAIL_SCAN_START: return "scan_start";
    case N3_WIFI_FAIL_SCAN: return "scan";
    case N3_WIFI_FAIL_NO_AP: return "no_ap";
    case N3_WIFI_FAIL_ALL_NETS: return "all_nets";
    case N3_WIFI_FAIL_RESCAN: return "rescan";
    case N3_WIFI_FAIL_TIMEOUT: return "timeout";
    default: return "?";
  }
}

bool n3WifiConnect(const N3WifiConfig& config, String* outWifiactif) {
  if (outWifiactif) {
    outWifiactif->clear();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (outWifiactif) {
      *outWifiactif = WiFi.SSID();
    }
    return true;
  }

  N3WifiSession session;
  n3WifiSessionBegin(session, config);
  while (true) {
    String ssid;
    const N3WifiPollResult result = n3WifiSessionPoll(session, 100, outWifiactif ? outWifiactif : &ssid);
    if (result == N3WifiPollResult::Connected) {
      return true;
    }
    if (result == N3WifiPollResult::Failed) {
      return false;
    }
    delay(10);
  }
}
