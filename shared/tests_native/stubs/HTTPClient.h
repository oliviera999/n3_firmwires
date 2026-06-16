// Stub HTTPClient minimal pour tests natifs.
//  - n3_hmac inclut <HTTPClient.h> pour n3HmacSignRequest : on capture le dernier
//    header ajouté pour assertion (test_hmac).
//  - n3_data inclut <HTTPClient.h> pour n3DataPost/n3DataGet : on fournit aussi
//    begin/setTimeout/POST/GET/getString/end en no-op pour que la TU compile
//    (test_data ne cible que la logique pure d'encodage, pas le réseau).
#pragma once
#include <Arduino.h>
#include <string>

// WiFiClient est passé à http.begin(client, url) par n3_data.
class WiFiClient {};

class HTTPClient {
 public:
  std::string lastHeaderName;
  std::string lastHeaderValue;

  void addHeader(const char* name, const char* value) {
    lastHeaderName = name ? name : "";
    lastHeaderValue = value ? value : "";
  }

  // No-ops suffisants pour compiler n3_data (non exercés par les tests).
  void begin(WiFiClient& /*client*/, const char* /*url*/) {}
  void setTimeout(int /*ms*/) {}
  int POST(const String& /*body*/) { return 0; }
  int GET() { return 0; }
  String getString() { return String(""); }
  void end() {}
};
