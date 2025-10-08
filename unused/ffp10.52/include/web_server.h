#pragma once

#include <Arduino.h>
#include "system_sensors.h"
#include "system_actuators.h"
#include "project_config.h"
#include "diagnostics.h"

// Forward declaration pour éviter les problèmes d'includes
#ifndef DISABLE_ASYNC_WEBSERVER
class AsyncWebServer;
#endif

class WebServerManager {
 public:
  WebServerManager(SystemSensors& sensors, SystemActuators& acts);
  WebServerManager(SystemSensors& sensors, SystemActuators& acts, Diagnostics& diag);
  ~WebServerManager();
  bool begin();
  void loop(); // Nouvelle méthode pour traiter les boucles WebSocket
 private:
  SystemSensors& _sensors;
  SystemActuators& _acts;
  Diagnostics* _diag;
  #ifndef DISABLE_ASYNC_WEBSERVER
  AsyncWebServer* _server;
  #endif
}; 