// Stub HTTPClient minimal pour tests natifs (n3_hmac inclut <HTTPClient.h>
// pour n3HmacSignRequest). Capture le dernier header ajouté pour assertion.
#pragma once
#include <string>

class HTTPClient {
 public:
  std::string lastHeaderName;
  std::string lastHeaderValue;
  void addHeader(const char* name, const char* value) {
    lastHeaderName = name ? name : "";
    lastHeaderValue = value ? value : "";
  }
};
