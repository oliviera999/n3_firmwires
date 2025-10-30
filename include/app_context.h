#pragma once

#include "wifi_manager.h"
#include "display_view.h"
#include "power.h"
#include "mailer.h"
#include "web_client.h"
#include "config_manager.h"
#include "ota_manager.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "diagnostics.h"
#include "web_server.h"
#include "automatism.h"
#include "time_drift_monitor.h"

struct AppContext {
  WifiManager& wifi;
  DisplayView& display;
  PowerManager& power;
  Mailer& mailer;
  WebClient& webClient;
  ConfigManager& config;
  OTAManager& otaManager;
  SystemSensors& sensors;
  SystemActuators& actuators;
  Diagnostics& diagnostics;
  WebServerManager& webServer;
  Automatism& automatism;
  TimeDriftMonitor& timeDriftMonitor;
};


