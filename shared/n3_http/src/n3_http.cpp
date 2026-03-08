#include "n3_http.h"
#include <HTTPClient.h>
#include <WiFi.h>

String n3HttpGet(const char* url, int* outResponseCode) {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  String payload = (code > 0) ? http.getString() : "{}";
  http.end();
  if (outResponseCode) *outResponseCode = code;
  return payload;
}

int n3HttpPost(const char* url, const char* contentType, const String& body) {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  if (contentType && contentType[0]) {
    http.addHeader("Content-Type", contentType);
  }
  int code = http.POST(body);
  http.end();
  return code;
}
