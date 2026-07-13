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

// Captures globales pour assertions (n3DataPost instancie son HTTPClient en
// local : seul un état partagé permet au test d'inspecter l'appel).
inline std::string& n3StubLastPostBody() {
  static std::string body;
  return body;
}
inline std::string& n3StubLastBeginUrl() {
  static std::string url;
  return url;
}

class HTTPClient {
 public:
  std::string lastHeaderName;
  std::string lastHeaderValue;

  void addHeader(const char* name, const char* value) {
    lastHeaderName = name ? name : "";
    lastHeaderValue = value ? value : "";
  }

  // No-ops suffisants pour compiler n3_data ; POST/begin capturent leurs
  // arguments pour les tests de contrat (body url-encodé, URL cible).
  void begin(WiFiClient& /*client*/, const char* url) {
    n3StubLastBeginUrl() = url ? url : "";
  }
  void setTimeout(int /*ms*/) {}
  int POST(const String& body) {
    n3StubLastPostBody() = body.c_str();
    return 0;
  }
  int GET() { return 0; }
  String getString() { return String(""); }
  void end() {}
};
