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
  unsigned long timeoutMs = 5000;
  size_t orderCount = 0;
  size_t orderIdx = 0;
  size_t order[N3_WIFI_SESSION_CAND_MAX];
  N3WifiCand cand[N3_WIFI_SESSION_CAND_MAX];
  size_t netCount = 0;
  bool retryNoBssid = false;
  bool onConnectingCalled = false;
  bool fastReconnectTried = false;
  char currentSsid[33] = {};
  char connectedSsid[33] = {};
  char invisibleRescanSsid[33] = {};
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

/** Connexion WiFi bloquante : scan, tri par RSSI, essai avec BSSID/canal puis retry sans BSSID. */
bool n3WifiConnect(const N3WifiConfig& config, String* outWifiactif);

#endif
