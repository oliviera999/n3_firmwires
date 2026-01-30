#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include <WiFi.h>

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

// v11.162: WebClient simplifié - HTTP par défaut (plus de TLS pour requêtes courantes)
// Réduit fragmentation mémoire en éliminant le besoin de ~32KB contigu pour TLS
class WebClient {
 public:
  explicit WebClient(const char* apiKey = ApiConfig::API_KEY);
  ~WebClient() = default;

  bool sendMeasurements(const Measurements& m, bool includeReset=false);
  bool sendHeartbeat(const class Diagnostics& diag);
  bool postRaw(const char* payload);
  bool fetchRemoteState(ArduinoJson::JsonDocument& doc);

  // Couche réseau minimale (WiFi + timeout ≤5s, une tentative, pas de retry interne)
  bool tryFetchConfigFromServer(ArduinoJson::JsonDocument& doc);
  bool tryPushStatusToServer(const char* payload);

 private:
  char _apiKey[65];  // API key max 64 chars + null terminator
  WiFiClient _client;  // v11.162: Client HTTP simple (plus de TLS)
  HTTPClient _http;
  unsigned long _lastRequestMs{0};  // Fix v11.29: timestamp dernière requête HTTP
  bool httpRequest(const char* url, const char* payload, char* response, size_t responseSize);
  bool loadFromNVSFallback(ArduinoJson::JsonDocument& doc);  // v11.165: Fallback NVS
}; 