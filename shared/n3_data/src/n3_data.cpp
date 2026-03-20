#include "n3_data.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "n3_hmac.h"

static const uint16_t N3_HTTP_TIMEOUT_MS = 5000;

int n3DataPost(const N3PostConfig& config) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  String body;
  for (size_t i = 0; i < config.fieldCount; i++) {
    if (i > 0) body += '&';
    body += config.fields[i].name;
    body += '=';
    body += config.fields[i].value;
  }

  WiFiClient client;
  HTTPClient http;
  http.begin(client, config.url);
  http.setTimeout(N3_HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  if (config.apiKey) {
    n3HmacSignRequest(http, config.apiKey, body.c_str());
  }

  if (config.onSending) config.onSending();

  int code = http.POST(body);
  http.end();

  if (config.onResult) config.onResult(code);
  return code;
}

String n3DataGet(const char* url, unsigned int* outHttpCode) {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(N3_HTTP_TIMEOUT_MS);
  int code = http.GET();
  String payload = (code > 0) ? http.getString() : "{}";
  http.end();
  if (outHttpCode) *outHttpCode = (unsigned int)code;
  return payload;
}
