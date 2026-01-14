#include "bootstrap_services.h"

#include <Arduino.h>

#include "event_log.h"
#include "config.h"
#include "timer_manager.h"

namespace BootstrapServices {

void initializeTimekeeping(AppContext& ctx) {
  ctx.power.initTime();
  EventLog::add("Time init");

  ctx.power.setNTPConfig(SystemConfig::NTP_GMT_OFFSET_SEC,
                         SystemConfig::NTP_DAYLIGHT_OFFSET_SEC,
                         SystemConfig::NTP_SERVER);

  ctx.timeDriftMonitor.attachPowerManager(&ctx.power);
  ctx.timeDriftMonitor.begin();
  EventLog::add("Time drift monitor init");
}

bool initializeDisplay(AppContext& ctx) {
  if (ctx.display.isPresent()) {
    ctx.display.hideOtaProgressOverlay();
  }

  Serial.println(F("[DEBUG] Avant oled.begin()"));
  bool result = ctx.display.begin();
  Serial.printf("[DEBUG] oled.begin() retourné: %s\n", result ? "true" : "false");
  Serial.printf("[DEBUG] oled.isPresent(): %s\n", ctx.display.isPresent() ? "true" : "false");
  return result;
}

void initializePeripherals(AppContext& ctx) {
  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("Sensors");
  }
  ctx.sensors.begin();

  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("Actuators");
  }
  ctx.actuators.begin();

  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("WebSrv");
  }
  ctx.webServer.begin();

  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("Diag");
  }
  ctx.diagnostics.begin();

  if (ctx.display.isPresent()) {
    ctx.display.showDiagnostic("Systems");
  }
  ctx.power.initWatchdog();
  ctx.power.initModemSleep();

  TimerManager::init();
}

void loadConfiguration(AppContext& ctx) {
  Serial.println(F("\n[App] Chargement configuration complète depuis NVS..."));
  ctx.config.loadConfigFromNVS();
  ctx.automatism.begin();
}

void finalizeDisplay(AppContext& ctx) {
  if (!ctx.display.isPresent()) {
    return;
  }

  ctx.display.forceEndSplash();
  ctx.display.clear();
  ctx.display.showDiagnostic("Init ok");
  delay(SystemConfig::FINAL_INIT_DELAY_MS);
}

}  // namespace BootstrapServices


