#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include <WiFi.h>

struct Measurements {
  float tempAir{0};
  float humidity{0};  // Harmonisé avec SensorReadings et l'API JSON
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
  // Retour: 0=échec, 1=OK HTTP, 2=OK NVS fallback (v11.193)
  int fetchRemoteState(ArduinoJson::JsonDocument& doc);

  // Couche réseau minimale (WiFi + timeout GET/Heartbeat 5s, POST 7s dérogation, pas de retry interne)
  // Retour: 0 = échec, 1 = OK depuis HTTP, 2 = OK depuis NVS (fallback). v11.193: éviter processFetchedRemoteConfig quand 2.
  int tryFetchConfigFromServer(ArduinoJson::JsonDocument& doc);
  bool tryPushStatusToServer(const char* payload);

  // v11.171: Queue persistante pour POSTs échoués (offline-first)
  bool queueFailedPost(const char* payload);      // Ajoute à la queue NVS
  bool processQueuedPosts();                       // Rejoue les POSTs en attente
  uint8_t getQueuedPostsCount();                   // Nombre de POSTs en attente
  void clearQueuedPosts();                         // Vide la queue

 private:
  char _apiKey[65];  // API key max 64 chars + null terminator
  WiFiClient _client;  // v11.162: Client HTTP simple (plus de TLS)
  HTTPClient _http;
  unsigned long _lastRequestMs{0};  // Fix v11.29: timestamp dernière requête HTTP
  bool httpRequest(const char* url, const char* payload, char* response, size_t responseSize,
                   uint32_t timeoutMs = NetworkConfig::HTTP_TIMEOUT_MS);
  bool loadFromNVSFallback(ArduinoJson::JsonDocument& doc);  // v11.165: Fallback NVS
  
  // v11.171: Constantes queue persistante
  static constexpr uint8_t MAX_QUEUED_POSTS = 3;   // Max 3 POSTs en queue (préserve flash)
  static constexpr size_t MAX_POST_SIZE = 512;     // Taille max payload
}; 