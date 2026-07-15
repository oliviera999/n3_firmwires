#include "n3_data.h"
#include "n3_defaults.h"
#include <WiFi.h>
#include <HTTPClient.h>
#if defined(USE_HTTPS_ENDPOINTS)
// Chemin TLS opt-in (flag de build USE_HTTPS_ENDPOINTS). Inclus UNIQUEMENT sous
// le flag pour ne rien changer au build par defaut (HTTP) ni aux tests natifs,
// qui ne fournissent pas de stub WiFiClientSecure.h.
#include <WiFiClientSecure.h>
#endif
#include "n3_hmac.h"
#include "n3_hmac_canonical.h"  // HMAC canonique sans String (chemin chaud POST)

// Selectionne le type de client TCP selon le flag de build.
//  - USE_HTTPS_ENDPOINTS non defini (DEFAUT) : WiFiClient (HTTP clair, inchange).
//  - USE_HTTPS_ENDPOINTS defini : WiFiClientSecure + setInsecure() (TLS chiffre,
//    sans epinglage de certificat — cf. docs/HTTPS_MIGRATION.md, suivi CA documente).
#if defined(USE_HTTPS_ENDPOINTS)
  #define N3_DATA_TCP_CLIENT_TYPE WiFiClientSecure

  // Epinglage de certificat (pinning du root CA) — OPT-IN, INERTE PAR DEFAUT.
  // Mecanisme strictement additif : on ne change RIEN au comportement existant
  // tant qu'aucun CA n'est explicitement fourni par le firmware.
  //
  // Activation : poser un en-tete "n3_data_ca_cert.h" dans le include path du
  // firmware (ex. poissonglouton/include/n3_data_ca_cert.h, cf. le template
  // shared/n3_data/n3_data_ca_cert.h.example). Cet en-tete doit definir :
  //     static const char N3_DATA_CA_CERT_PEM[] = R"EOF(... PEM ...)EOF";
  // Detecte ici via __has_include : si le fichier est present, on valide le
  // certificat serveur avec setCACert(N3_DATA_CA_CERT_PEM) (protection MITM).
  // Si le fichier est ABSENT (cas de toute la flotte aujourd'hui), on retombe
  // EXACTEMENT sur l'ancien comportement : setInsecure() (TLS sans validation).
  #if defined(__has_include)
  #  if __has_include("n3_data_ca_cert.h")
  #    include "n3_data_ca_cert.h"   // doit definir N3_DATA_CA_CERT_PEM (PEM)
  #    define N3_DATA_HAS_CA_CERT 1
  #  endif
  #endif

  #if defined(N3_DATA_HAS_CA_CERT)
    // CA fourni : on epingle le root CA -> le certificat serveur est valide.
    #define N3_DATA_TCP_CLIENT_PREPARE(clientVar) do { (clientVar).setCACert(N3_DATA_CA_CERT_PEM); } while (0)
  #else
    // Aucun CA fourni (DEFAUT, inchange) : TLS chiffre mais sans validation.
    #define N3_DATA_TCP_CLIENT_PREPARE(clientVar) do { (clientVar).setInsecure(); } while (0)
  #endif
#else
  #define N3_DATA_TCP_CLIENT_TYPE WiFiClient
  #define N3_DATA_TCP_CLIENT_PREPARE(clientVar) do { } while (0)
#endif

static String n3UrlEncode(const String& input) {
  String encoded;
  encoded.reserve(input.length() * 3);
  static const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < input.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(input[i]);
    const bool safe =
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

int n3DataPost(const N3PostConfig& config) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  String body;
  for (size_t i = 0; i < config.fieldCount; i++) {
    if (i > 0) body += '&';
    body += n3UrlEncode(config.fields[i].name ? String(config.fields[i].name) : String(""));
    body += '=';
    body += n3UrlEncode(config.fields[i].value);
  }

  // Contrat FFP3 (legacy) : ajouter timestamp + signature(HMAC(timestamp, sigSecret))
  // au body si sigSecret est fourni et que le firmware a une heure valide.
  //
  // ⚠️ SECURITE (finding S6) : cette signature legacy ne HMAC que `tsBuf` (l'epoch),
  // PAS le corps de la requete. Elle N'AUTHENTIFIE DONC PAS le body : une paire
  // capturee `timestamp=&signature=` est rejouable avec un corps arbitraire tant
  // que l'horodatage reste dans la fenetre acceptee. On la conserve UNIQUEMENT pour
  // la retro-compatibilite avec le serveur qui l'attend encore. Les en-tetes
  // X-Sig-* ci-dessous (couvrant tout le corps) sont la vraie protection ; ce champ
  // legacy doit etre retire une fois que le serveur impose X-Sig-*.
  bool hmacFffp3Active = false;
  if (config.sigSecret != nullptr && config.sigSecret[0] != '\0' && config.currentEpochSeconds > 0) {
    char tsBuf[16];
    snprintf(tsBuf, sizeof(tsBuf), "%lu", config.currentEpochSeconds);
    char sigHex[65];
    if (n3HmacSha256(config.sigSecret, tsBuf, sigHex, sizeof(sigHex))) {
      if (body.length() > 0) body += '&';
      body += "timestamp=";
      body += tsBuf;
      body += "&signature=";
      body += sigHex;
      hmacFffp3Active = true;
    }
  }

  N3_DATA_TCP_CLIENT_TYPE client;
  N3_DATA_TCP_CLIENT_PREPARE(client);
  HTTPClient http;
  http.begin(client, config.url);
  http.setTimeout(N3_HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // En-tete X-Signature compatibilite ascendante : signature HMAC du body avec apiKey.
  // Si HMAC FFP3 actif, on peut quand meme garder le header legacy : le serveur
  // l'ignorera car validateAuth() priorise timestamp+signature dans le body.
  if (config.apiKey) {
    http.addHeader("X-Api-Key", config.apiKey);
    n3HmacSignRequest(http, config.apiKey, body.c_str());
  }
  (void)hmacFffp3Active;

  // Body-signing ADDITIF (couvre TOUT le corps, contrairement a la signature
  // legacy qui ne signe que le timestamp). En-tetes X-Sig-* signant
  //   HMAC-SHA256(timestamp + "\n" + nonce + "\n" + body, sigSecret).
  // Retro-compatible : un serveur a jour valide via isValidForBody (integrite du
  // corps + fenetre temps) ; un serveur ancien ignore ces en-tetes et retombe sur
  // le timestamp/signature deja presents dans le body. Le nonce (unique/requete)
  // prepare une future deduplication anti-rejeu cote serveur.
  if (config.sigSecret != nullptr && config.sigSecret[0] != '\0' && config.currentEpochSeconds > 0) {
    static uint32_t s_bodySigCounter = 0;
    char tsBuf2[16];
    snprintf(tsBuf2, sizeof(tsBuf2), "%lu", config.currentEpochSeconds);
    char nonceBuf[32];
    snprintf(nonceBuf, sizeof(nonceBuf), "%lu-%lu",
             config.currentEpochSeconds, (unsigned long)(++s_bodySigCounter));
    // HMAC canonique ts+"\n"+nonce+"\n"+body via la brique partagée (sans String,
    // mbedtls incrémental) — supprime l'allocation heap du chemin chaud POST.
    char bodySigHex[65];
    if (n3hmac::computeHmacHex(config.sigSecret, tsBuf2, nonceBuf, body.c_str(),
                               bodySigHex, sizeof(bodySigHex))) {
      http.addHeader("X-Sig-Timestamp", tsBuf2);
      http.addHeader("X-Sig-Nonce", nonceBuf);
      http.addHeader("X-Sig-Hmac", bodySigHex);
    }
  }

  if (config.onSending) config.onSending();

  const unsigned long postStartMs = millis();
  int code = http.POST(body);
  const unsigned long durationMs = millis() - postStartMs;
  String responseBody;
  // Lire le body : toujours si l'appelant veut le récupérer, sinon seulement sur erreur
  if (config.responseBodyOut != nullptr || code < 200 || code >= 300) {
    responseBody = http.getString();
    if (responseBody.length() > 200) {
      responseBody = responseBody.substring(0, 200) + "...";
    }
    if (config.responseBodyOut != nullptr) {
      *config.responseBodyOut = responseBody;
    }
  }
  http.end();

  const int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
  const char* verdict = (code >= 200 && code < 400)
                            ? "acceptee par le serveur (code 2xx/3xx)"
                            : (code >= 400 && code < 500)
                                  ? "rejet client 4xx"
                                  : (code >= 500)
                                        ? "erreur serveur 5xx"
                                        : (code <= 0)
                                              ? "echec reseau ou timeout (code <= 0)"
                                              : "non classee";
  Serial.printf(
      "[SERVER][POST] Verdict: %s | code_HTTP=%d | duree_totale=%lu ms | timeout=%d ms | RSSI=%d dBm\n",
      verdict, code, durationMs, N3_HTTP_TIMEOUT_MS, rssi);
  if (durationMs >= (unsigned long)N3_HTTP_TIMEOUT_MS - 500UL) {
    Serial.printf("[SERVER][POST][WARN] POST proche ou au-dela du timeout (%lu ms / %d ms)\n",
                  durationMs, N3_HTTP_TIMEOUT_MS);
  }
  if (responseBody.length() > 0) {
    Serial.printf("[SERVER][POST] Corps reponse: %s\n", responseBody.c_str());
  }

  n3NetStatsRecordPost(code, durationMs, rssi);

  if (config.onResult) config.onResult(code);
  return code;
}

String n3DataGet(const char* url, unsigned int* outHttpCode, const char* deviceApiKey) {
  if (WiFi.status() != WL_CONNECTED) {
    if (outHttpCode) *outHttpCode = 0;
    return String("{}");
  }
  N3_DATA_TCP_CLIENT_TYPE client;
  N3_DATA_TCP_CLIENT_PREPARE(client);
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(N3_HTTP_TIMEOUT_MS);
  if (deviceApiKey != nullptr && deviceApiKey[0] != '\0') {
    http.addHeader("X-Api-Key", deviceApiKey);
  }
  const unsigned long getStartMs = millis();
  int code = http.GET();
  const unsigned long durationMs = millis() - getStartMs;
  String payload = (code > 0) ? http.getString() : String("{}");
  http.end();

  const int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
  const char* verdict = (code >= 200 && code < 300)
                            ? "acceptee (code 2xx)"
                            : (code >= 300 && code < 400)
                                  ? "redirection 3xx (suivre URL ou utiliser chemin canonique)"
                                  : (code >= 400 && code < 500)
                                  ? "rejet client 4xx"
                                  : (code >= 500)
                                        ? "erreur serveur 5xx"
                                        : (code <= 0)
                                              ? "echec reseau ou timeout"
                                              : "non classee";
  Serial.printf(
      "[SERVER][GET] Verdict: %s | code_HTTP=%d | duree_totale=%lu ms | timeout=%d ms | RSSI=%d dBm\n",
      verdict, code, durationMs, N3_HTTP_TIMEOUT_MS, rssi);
  n3NetStatsRecordGet(code, durationMs, rssi);

  if (outHttpCode) *outHttpCode = (unsigned int)code;
  return payload;
}

// ---------------------------------------------------------------------------
// Heartbeat generique — mutualise depuis n3pp/msp (corps verbatim identique
// entre les deux firmwares, verifie par diff avant extraction). Toute la
// variabilite passe par N3HeartbeatConfig ; l'etat local (plancher heap `min`)
// reste un static de fonction, reinitialise a chaque reveil deep-sleep comme
// dans les firmwares d'origine.
// ---------------------------------------------------------------------------
int n3DataSendHeartbeat(const N3HeartbeatConfig& config) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SERVER][HB][SKIP] WiFi non connecte");
    return -1;
  }

  static uint32_t minHeap = UINT32_MAX;
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < minHeap) {
    minHeap = freeHeap;
  }

  const uint32_t uptimeSec = millis() / 1000UL;
  const int rssi = WiFi.RSSI();

  N3DataField fields[] = {
    {"api_key",  String(config.apiKeyField ? config.apiKeyField : "")},
    {"sensor",   String(config.sensor ? config.sensor : "")},
    {"version",  String(config.version ? config.version : "")},
    {"uptime",   String(uptimeSec)},
    {"free",     String(freeHeap)},
    {"min",      String(minHeap == UINT32_MAX ? freeHeap : minHeap)},
    {"reboots",  String(config.bootCount)},
    {"rssi",     String(rssi)},
  };

  N3PostConfig cfg = {};
  cfg.url = config.url;
  cfg.apiKey = config.apiKeyHeader;
  cfg.fields = fields;
  cfg.fieldCount = sizeof(fields) / sizeof(fields[0]);
  cfg.sigSecret = config.sigSecret;
  cfg.currentEpochSeconds = config.currentEpochSeconds;
  cfg.onResult = [](int code) {
    if (code == 200) {
      Serial.println("[SERVER][HB] OK");
    } else {
      Serial.printf("[SERVER][HB] erreur HTTP %d\n", code);
    }
  };

  Serial.printf("[SERVER][HB] Envoi vers %s\n", config.url);
  const int code = n3DataPost(cfg);
  delay(200);
  return code;
}
