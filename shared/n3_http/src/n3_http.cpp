#include "n3_http.h"
#include "n3_defaults.h"
#include <HTTPClient.h>
#include <WiFi.h>

// Timeout par defaut harmonise avec n3_data (cf. regle "5 s max").
// Avant v1.1 : aucun setTimeout() configure cote n3_http -> risque de blocage
// indetermine si le serveur ou le DNS ne repond pas.
String n3HttpGet(const char* url, int* outResponseCode) {
  if (WiFi.status() != WL_CONNECTED) {
    if (outResponseCode) *outResponseCode = -1;
    return String("{}");
  }
  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(N3_HTTP_TIMEOUT_MS);
  int code = http.GET();
  String payload = (code > 0) ? http.getString() : String("{}");
  http.end();
  if (outResponseCode) *outResponseCode = code;
  return payload;
}

int n3HttpPost(const char* url, const char* contentType, const String& body) {
  if (WiFi.status() != WL_CONNECTED) {
    return -1;
  }
  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(N3_HTTP_TIMEOUT_MS);
  if (contentType && contentType[0]) {
    http.addHeader("Content-Type", contentType);
  }
  int code = http.POST(body);
  http.end();
  return code;
}
