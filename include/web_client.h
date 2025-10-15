#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "project_config.h"
#include <WiFiClientSecure.h>

struct Measurements {
  float tempAir{0};
  float humid{0};
  float tempWater{0};
  uint16_t wlPota{0};
  uint16_t wlAqua{0};
  uint16_t wlTank{0};
  int diffMaree{0};
  uint16_t luminosite{0};
  bool pumpAqua{false};
  bool pumpTank{false};
  bool heater{false};
  bool light{false};
};

class WebClient {
 public:
  explicit WebClient(const char* apiKey = Config::API_KEY);

  bool sendMeasurements(const Measurements& m, bool includeReset=false);
  bool sendHeartbeat(const class Diagnostics& diag);
  bool postRaw(const String& payload);
  bool postRaw(const String& payload, bool includeSkeleton);
  bool fetchRemoteState(ArduinoJson::JsonDocument& doc);

 private:
  String _apiKey;
  WiFiClientSecure _client;
  HTTPClient _http;
  unsigned long _lastRequestMs{0};  // Fix v11.29: timestamp dernière requête HTTP
  bool httpRequest(const String& url, const String& payload, String& response);
}; 