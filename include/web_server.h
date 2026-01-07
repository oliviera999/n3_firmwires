#pragma once

#include <Arduino.h>
#include "system_sensors.h"
#include "system_actuators.h"
#include "config.h"
#include "diagnostics.h"
#include <ESPAsyncWebServer.h>
#include <functional> // Pour std::function

// Forward declarations pour éviter les problèmes d'includes
#ifndef DISABLE_ASYNC_WEBSERVER
class AsyncWebServer;
#endif
struct WebServerContext;

class WebServerManager {
 public:
  WebServerManager(SystemSensors& sensors, SystemActuators& acts);
  WebServerManager(SystemSensors& sensors, SystemActuators& acts, Diagnostics& diag);
  ~WebServerManager();
  bool begin();
  void loop(); // Nouvelle méthode pour traiter les boucles WebSocket
 private:
  void initializeServer(); // Méthode privée pour éviter la duplication
  const char* handleRelayAction(
    const char* relayName,
    std::function<bool()> isRunning,
    std::function<void()> start,
    std::function<void()> stop,
    const char* onResponse,
    const char* offResponse
  );

  SystemSensors& _sensors;
  SystemActuators& _acts;
  Diagnostics* _diag;
  #ifndef DISABLE_ASYNC_WEBSERVER
  AsyncWebServer* _server;
  #endif
  WebServerContext* _ctx;
}; 