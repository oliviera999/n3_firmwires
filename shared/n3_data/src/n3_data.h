#pragma once

#include <Arduino.h>

struct N3DataField {
  const char* name;
  String value;
};

struct N3PostConfig {
  const char* url;
  const char* apiKey;           // cle API pour signature HMAC body (header X-Signature). NULL = pas de signature legacy
  const N3DataField* fields;
  size_t fieldCount;
  void (*onSending)();
  void (*onResult)(int httpCode);
  // Auth contrat FFP3 (depuis n3_data 1.1) : si sigSecret est defini et que
  // currentEpochSeconds > 0, on ajoute "timestamp" + "signature" au body.
  // signature = HMAC-SHA256(timestamp, sigSecret).
  // Cote serveur, valide par SignatureValidator (FFP3 / Msp / N3pp).
  const char* sigSecret;         // partage avec l'env serveur API_SIG_SECRET (NULL = pas de HMAC FFP3)
  unsigned long currentEpochSeconds; // 0 = pas d'horodatage dispo (firmware sans NTP)
  // Optionnel : si non-NULL, le body de la reponse HTTP (succes ou erreur) y est copie.
  String* responseBodyOut;       // NULL = comportement legacy (pas de capture)
};

/**
 * Envoie un POST URL-encoded avec les champs fournis.
 *
 * Authentification (par ordre de priorite) :
 *   1. Si `sigSecret` non NULL et `currentEpochSeconds > 0` : ajoute
 *      `timestamp` + `signature` (HMAC-SHA256(timestamp, sigSecret)) au body
 *      et au header `X-Signature`. C'est le contrat FFP3/Msp/N3pp moderne.
 *   2. Si `apiKey` non NULL : ajoute `X-Api-Key` + signature HMAC(body, apiKey)
 *      en header (compat ancien serveur legacy via header).
 *
 * Retourne le code HTTP ou une valeur negative en cas d'erreur reseau.
 */
int n3DataPost(const N3PostConfig& config);

/**
 * GET HTTP simple. Retourne le body de la reponse.
 * outHttpCode recoit le code HTTP si non-NULL.
 * deviceApiKey : si non NULL, envoie le header X-Api-Key (auth serveur galerie / device).
 */
String n3DataGet(const char* url, unsigned int* outHttpCode, const char* deviceApiKey = nullptr);

// ---------------------------------------------------------------------------
// Heartbeat generique (mutualise depuis n3pp/msp, corps verbatim identique).
// Envoie un POST url-encode {api_key, sensor, version, uptime, free, min,
// reboots, rssi} vers l'endpoint heartbeat. Conserve la semantique d'origine :
//  - garde WiFi (skip + log si non connecte, retour -1) ;
//  - plancher de heap `min` en static de fonction (reinitialise a chaque
//    reveil deep-sleep, comme dans les firmwares d'origine) ;
//  - delay(200) apres le POST.
// ---------------------------------------------------------------------------
struct N3HeartbeatConfig {
  const char* url;                    // endpoint heartbeat (serverNameHeartbeat)
  const char* apiKeyHeader;           // header X-Api-Key + signature legacy (API_KEY). NULL = aucun
  const char* apiKeyField;            // champ body "api_key" (apiKeyValue)
  const char* sensor;                 // champ body "sensor" (sensorName)
  const char* version;                // champ body "version" (FIRMWARE_VERSION)
  int bootCount;                      // champ body "reboots"
  const char* sigSecret;              // HMAC FFP3/X-Sig (API_SIG_SECRET). NULL = off
  unsigned long currentEpochSeconds;  // 0 = horloge non fiable (pas de timestamp signe)
};

/** Retourne le code HTTP du POST, ou -1 si WiFi non connecte. */
int n3DataSendHeartbeat(const N3HeartbeatConfig& config);

/** Statistiques reseau HTTP (POST/GET) pour rapports mail et comparaison ffp5cs. */
struct N3NetStatsSnapshot {
  uint32_t postCount;
  uint32_t postOkCount;
  uint32_t postFailCount;
  uint32_t postLastDurationMs;
  uint32_t postMaxDurationMs;
  uint32_t postAvgDurationMs;
  uint32_t postNearTimeoutCount;
  int postLastCode;
  int postLastRssi;
  uint32_t getCount;
  uint32_t getOkCount;
  uint32_t getFailCount;
  uint32_t getLastDurationMs;
  uint32_t getMaxDurationMs;
  int getLastCode;
  int getLastRssi;
};

void n3NetStatsRecordPost(int httpCode, unsigned long durationMs, int rssi);
void n3NetStatsRecordGet(int httpCode, unsigned long durationMs, int rssi);
void n3NetStatsGetSnapshot(N3NetStatsSnapshot& out);
void n3NetStatsResetPeriod();
