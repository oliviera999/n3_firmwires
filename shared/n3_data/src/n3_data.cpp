#include "n3_data.h"
#include "n3_defaults.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "n3_hmac.h"

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

  // Contrat FFP3 moderne : ajouter timestamp + signature(HMAC(timestamp, sigSecret))
  // au body si sigSecret est fourni et que le firmware a une heure valide.
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

  WiFiClient client;
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

  if (config.onSending) config.onSending();

  int code = http.POST(body);
  http.end();

  if (config.onResult) config.onResult(code);
  return code;
}

String n3DataGet(const char* url, unsigned int* outHttpCode, const char* deviceApiKey) {
  if (WiFi.status() != WL_CONNECTED) {
    if (outHttpCode) *outHttpCode = 0;
    return String("{}");
  }
  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(N3_HTTP_TIMEOUT_MS);
  if (deviceApiKey != nullptr && deviceApiKey[0] != '\0') {
    http.addHeader("X-Api-Key", deviceApiKey);
  }
  int code = http.GET();
  String payload = (code > 0) ? http.getString() : String("{}");
  http.end();
  if (outHttpCode) *outHttpCode = (unsigned int)code;
  return payload;
}
