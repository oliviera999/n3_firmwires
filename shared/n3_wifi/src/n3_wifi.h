#ifndef N3_WIFI_H
#define N3_WIFI_H

#include <Arduino.h>
#include <WiFi.h>

struct N3WifiNetwork {
  const char* ssid;
  const char* pass;
};

struct N3WifiConfig {
  const N3WifiNetwork* networks;
  size_t networkCount;
  unsigned long timeoutMs;
  unsigned long delayBetweenMs;
  unsigned long preScanDelayMs;
  int scanMax;
  void (*onConnecting)();
  void (*onFailure)();
  void (*onSuccess)(const char* ssid);
  // Champ ajouté en fin de struct : les init agrégées existantes le laissent à
  // false (reconnexion rapide active par défaut).
  bool disableFastReconnect;
};

enum class N3WifiPollResult : uint8_t {
  InProgress = 0,
  Connected,
  Failed,
};

/** Cause d'echec de session (n3WifiSessionPoll -> Failed). */
enum N3WifiFailureReason : uint8_t {
  N3_WIFI_FAIL_NONE = 0,
  N3_WIFI_FAIL_SCAN_START = 1,
  N3_WIFI_FAIL_SCAN = 2,
  N3_WIFI_FAIL_NO_AP = 3,
  N3_WIFI_FAIL_ALL_NETS = 4,
  N3_WIFI_FAIL_RESCAN = 5,
  N3_WIFI_FAIL_TIMEOUT = 6,
};

#define N3_WIFI_SESSION_CAND_MAX 10

struct N3WifiCand {
  int8_t rssi;
  uint8_t bssid[6];
  uint8_t chan;
  bool present;
};

/** Session de connexion WiFi incrémentale (non bloquante). */
struct N3WifiSession {
  const N3WifiConfig* config = nullptr;
  uint8_t phase = 0;
  unsigned long phaseDeadline = 0;
  unsigned long connectDeadline = 0;
  unsigned long connectStartedMs = 0;
  unsigned long timeoutMs = 5000;
  size_t orderCount = 0;
  size_t orderIdx = 0;
  size_t order[N3_WIFI_SESSION_CAND_MAX];
  N3WifiCand cand[N3_WIFI_SESSION_CAND_MAX];
  size_t netCount = 0;
  bool retryNoBssid = false;
  bool pendingNoBssidRetry = false;
  bool onConnectingCalled = false;
  bool fastReconnectTried = false;
  char currentSsid[33] = {};
  char connectedSsid[33] = {};
  char invisibleRescanSsid[33] = {};
  uint8_t failureReason = 0;
};

/** Démarre ou redémarre une session de connexion. */
void n3WifiSessionBegin(N3WifiSession& session, const N3WifiConfig& config);

/** Réinitialise la session (idle). */
void n3WifiSessionReset(N3WifiSession& session);

/**
 * Avance la session dans la limite de budgetMs.
 * outSsid est renseigné en cas de Connected.
 */
N3WifiPollResult n3WifiSessionPoll(N3WifiSession& session, uint32_t budgetMs, String* outSsid);

/** Libelle court de la phase interne n3_wifi (session.phase). */
const char* n3WifiSessionPhaseName(uint8_t phase);

/** Libelle court de la cause d'echec session (session.failureReason). */
const char* n3WifiSessionFailureReasonName(uint8_t reason);

/** Connexion WiFi bloquante : scan, tri par RSSI, essai avec BSSID/canal puis retry sans BSSID. */
bool n3WifiConnect(const N3WifiConfig& config, String* outWifiactif);

#endif
