#pragma once

#include <Arduino.h>
#include <functional>
#include <ArduinoJson.h>

class AsyncWebServerRequest;
class Automatism;
class Mailer;
class ConfigManager;
class PowerManager;
class WifiManager;
class SystemSensors;
class SystemActuators;
class RealtimeWebSocket;

struct WebServerContext {
  Automatism& automatism;
  Mailer& mailer;
  ConfigManager& config;
  PowerManager& power;
  WifiManager& wifi;
  SystemSensors& sensors;
  SystemActuators& actuators;
  RealtimeWebSocket& realtimeWs;

  uint32_t streamMinHeapBytes{60000};
  uint32_t jsonMinHeapBytes{50000};
  uint32_t emailMinHeapBytes{50000};

  WebServerContext(Automatism& automatism,
                   Mailer& mailer,
                   ConfigManager& config,
                   PowerManager& power,
                   WifiManager& wifi,
                   SystemSensors& sensors,
                   SystemActuators& actuators,
                   RealtimeWebSocket& realtimeWs);

  bool ensureHeap(AsyncWebServerRequest* req,
                  uint32_t minHeap,
                  const __FlashStringHelper* routeName) const;

  void sendJson(AsyncWebServerRequest* req,
                const JsonDocument& doc,
                bool enableCors = true) const;

  bool sendManualActionEmail(const char* action,
                             const char* subject,
                             const char* emailType) const;
};












